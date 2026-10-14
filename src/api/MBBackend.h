#pragma once

// ============================================================
// MBBackend — Crunch MB 的 IMBBackend facade 实现
//
// TECH_SPEC §8：UI 与外部调用方统一经 facade 访问后端。
// 本类是唯一的后端入口，把 APVTS / DSP / 频谱引擎封装成
// `crunch-api-interface.md` 规定的接口面。
//
// 线程纪律：
//   · getMeteringSnapshot / readHistory / readMultibandHistory /
//     getBandGainReduction —— RT_SAFE（只读 atomic）
//   · 其余方法 —— 消息线程（含 APVTS 写、FIR 评估、序列化）
// ============================================================

#include "IMBBackend.h"
#include "../PluginProcessor.h"

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
    std::array<float, 4> getBandGainReduction() override;
    bool getLatestSpectrum (std::array<float, 512>& pre,
                            std::array<float, 512>& post,
                            std::vector<float>& freqs) override;

private:
    CrunchMBAudioProcessor& proc_;

    /** 从 APVTS 参数推导 ParamInfo（唯一真源，避免手写表与注册表漂移）。 */
    static ParamInfo describeParam (juce::AudioProcessorParameter* p);

    /** 统一控制五握柄的参数名 → 每段参数 ID。 */
    static juce::String groupParamId (const juce::String& paramName, int band);
};
