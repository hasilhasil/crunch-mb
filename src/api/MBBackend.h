#pragma once

// ============================================================
// MBBackend — Crunch MB 的 IMBBackend facade 实现
//
// TECH_SPEC §8：UI 与外部调用方统一经 facade 访问后端。
// 本类是唯一的后端入口，把 APVTS / DSP / 频谱引擎封装成
// `crunch-api-interface.md` 规定的接口面。
//
// ⚠️ 循环包含：PluginProcessor.h 也 include 本头（为持有 backend_ 成员）。
//    这里只前向声明 CrunchMBAudioProcessor，不 include ——  .cpp 里再 include。
//
// 线程纪律：
//   · getMeteringSnapshot / readHistory / readMultibandHistory /
//     getBandGainReduction —— RT_SAFE（只读 atomic）
//   · 其余方法 —— 消息线程（含 APVTS 写、FIR 评估、序列化）
// ============================================================

#include "IMBBackend.h"

#include <unordered_map>

class CrunchMBAudioProcessor;

class MBBackend : public IMBBackend
{
public:
    explicit MBBackend (CrunchMBAudioProcessor& processor) noexcept
        : proc_ (processor) {}

    // ---------- IPluginBackend：参数面 ----------
    std::vector<ParamInfo> listParams() override;
    ParamValue getParam (const juce::String& id) override;
    std::vector<ParamValue> getParams (const std::vector<juce::String>& ids) override;
    SetResult  setParam (const juce::String& id, float value) override;

    // ---------- IPluginBackend：状态面 ----------
    StateSnapshot getStateSnapshot() override;
    bool loadState (const juce::ValueTree& state) override;

    // ---------- IPluginBackend：实时数据面（RT_SAFE）----------
    MeteringSnapshot getMeteringSnapshot() override;
    HistoryData readHistory (int points) override;

    // ---------- IPluginBackend：分析面 ----------
    CurveData getTransferCurve (int bandIndex, float minDb, float maxDb, int points) override;
    CurveData getFrequencyResponse (int bandIndex, float minFreq, float maxFreq, int points) override;

    // ---------- IPluginBackend：插件面 ----------
    void setBypass (bool bypassed) override;
    bool isBypassed() override;
    juce::String getPluginName() override;
    juce::String getPluginVersion() override;

    // ---------- IMBBackend：多段扩展 ----------
    MultibandHistoryData readMultibandHistory (int points) override;
    MBGroupSetResult setBandGroupDelta (const juce::String& paramName, float delta) override;
    SetResult setCrossover (int crossoverIndex, float freqHz) override;
    void setCrossovers (float x1, float x2, float x3);
    std::array<float, 4> getBandGainReduction() override;
    bool getLatestSpectrum (std::array<float, 512>& pre,
                            std::array<float, 512>& post,
                            std::vector<float>& freqs) override;

    /**
     * Phase：按 ID 取**实际值**的 O(1) 热路径版本（UI 每帧读参数用）。
     *
     * 为什么不用 getParam()：getParam() 走 apvts.getParameter(id)，那是 O(n)
     * 的线性字符串比较（84 个参数）。UI 在 timerCallback 里每帧读十几个参数，
     * 60fps 下就是每秒上千次线性扫描——不可接受。
     *
     * 这里在首次调用时建 id → 参数的哈希表，之后每次查表 O(1)。
     * 找不到时回落到 getParam()（仍正确，只是慢）。
     */
    float getParamFast (const juce::String& id) const;
    void buildParamCache();

    /**
     * Phase（R19）：按 **normalized**（0..1）读取参数。
     *
     * 为什么必须有这个：此前 facade 只有 getParamFast（实际值），UI 层需要
     * 归一化值时只能自己实现——BandPopover 实现了（正确），DisplayView 也
     * 实现了一个（直接 return getParamFast，是错的，R19 修掉并删除）。
     * **同一个换算在两处各写一遍，必然一处漂移**，这与当年 paramMap_ 在
     * facade 和 UI 各存一份是同一个错误（PLAN Phase 6 的立论）。
     * 现在统一由 facade 提供，UI 不再自己算。
     */
    float getParamNorm (const juce::String& id) const;
    /** getParamNorm 的慢路径（未建表/未命中时用）。 */
    float normalizedOf (const juce::String& id) const;

    /**
     * Phase：按 **normalized**（0..1）写入参数。
     *
     * 与 setParam 的分工：setParam 入参是实际值（规范语义），本函数入参是
     * normalized。UI 里有 39 处历史调用点原本就按 normalized 语义
     * （如 lookahead 的 next*0.5f、Solo/Mute 的 0/1），
     * 直接换成 setParam 会把这些值当成 dB/Hz/% 解释 —— 那是真实的行为回归
     * （T21.4/T24.1/T27.3/T40.9/T40.13/T40.18 九个断言因此失败）。
     *
     * 因此：实际值语义走 setParam，normalized 语义走本函数，两者都经 facade，
     * 都不直触 APVTS。
     */
    void setParamNormFast (const juce::String& id, float norm01);

private:
    CrunchMBAudioProcessor& proc_;

    // Phase：id → 参数的 O(1) 查表（buildParamCache 后有效）
    std::unordered_map<juce::String, juce::AudioProcessorParameter*> paramCache_;
    bool paramCacheBuilt_ = false;

    /** 从 APVTS 参数推导 ParamInfo（唯一真源，避免手写表与注册表漂移）。 */
    static ParamInfo describeParam (juce::AudioProcessorParameter* p);

    /** 统一控制五握柄的参数名 → 每段参数 ID。 */
    static juce::String groupParamId (const juce::String& paramName, int band);
};
