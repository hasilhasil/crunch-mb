#pragma once

// ============================================================
// IPluginBackend — Crunch 系列插件统一后端接口
//
// 来源：`crunch-api-interface.md` v1.0 §2（Crunch 系列统一后端接口规范）。
// 本文件是该规范的 C++ 落地，供 Crunch MB / EQ / Compressor 共用。
//
// 设计约定（规范 §1）：
//   · Facade 模式——UI 与外部调用方统一经 facade 访问后端，不直接碰 APVTS
//   · 所有物理量用实际值（dB / Hz / ms / %），内部处理 normalized 转换
//   · 值型拒绝——业务拒绝通过返回值字段表达，参数越界时钳制
//   · 线程安全分层见各方法注释（RT_SAFE = 可从音频线程调用）
// ============================================================

#include <JuceHeader.h>
#include <vector>
#include <array>

// ---------- 数据类型 ----------

struct ParamInfo {
    juce::String id;          // 参数 ID（如 "threshold" / "band0Freq"）
    juce::String displayName; // 显示名（如 "Threshold" / "Low Frequency"）
    juce::String unit;        // 单位（"dB" / "Hz" / "ms" / "%" / ""）
    float minValue;
    float maxValue;
    float defaultValue;
    bool  isChoice;           // true = 枚举选择
    juce::StringArray choices; // isChoice 时有效
    bool  isAutomatable;      // 是否可自动化
    enum Category { Global, Band, Display } category;
    int   bandIndex;          // Band 类别时有效（0-based），否则 -1
};

struct ParamValue {
    juce::String id;
    float value;              // 实际值（非 normalized）
    bool  isValid;
};

struct SetResult {
    bool  applied;
    float actualValue;        // 钳制后的实际生效值
    juce::String reason;      // applied=false 时的原因
};

struct MeteringSnapshot {
    float inputPeakDb;
    float outputPeakDb;
    // Crunch MB: 4 段 GR；Crunch Compressor: 1 段 GR（[0] 有效）；Crunch EQ: 不适用（全 0）
    std::array<float, 4> gainReductionDb;
    juce::uint64 timestampMs;
};

struct HistoryData {
    std::vector<float> inputDb;           // 输入电平历史
    std::vector<float> outputDb;          // 输出电平历史
    std::vector<float> gainReductionDb;   // GR 历史（仅 Crunch Compressor 提供；Crunch MB 不记录，为空）
    unsigned counter;                     // 环形计数器（用于对齐）
    int    pointsWritten;
};

struct MultibandHistoryData {
    std::vector<float> inputDb;
    std::vector<float> outputDb;
    // 注：不记录 GR 历史（实时 GR 见 MeteringSnapshot）。
    unsigned counter;
    int    pointsWritten;
};

struct CurveData {
    std::vector<float> xValues;  // 频率(Hz) 或 输入电平(dB)
    std::vector<float> yValues;  // 幅度(dB) 或 输出电平(dB)
};

struct StateSnapshot {
    juce::ValueTree state;       // APVTS 状态树（含全部参数）
    juce::String   pluginName;
    juce::String   pluginVersion;
    int            parameterCount;
};

// ---------- 接口基类 ----------

class IPluginBackend
{
public:
    virtual ~IPluginBackend() = default;

    // ===== 参数面 =====

    // 列出全部参数元信息
    virtual std::vector<ParamInfo> listParams() = 0;

    // 读取单个参数当前值（实际值）
    virtual ParamValue getParam (const juce::String& id) = 0;

    // 批量读取
    virtual std::vector<ParamValue> getParams (const std::vector<juce::String>& ids) = 0;

    // 设置参数值（实际值）。越界时钳制，返回实际生效值。
    virtual SetResult setParam (const juce::String& id, float value) = 0;

    // ===== 状态面 =====

    // 获取完整状态快照（可用于序列化 / 远程同步）
    virtual StateSnapshot getStateSnapshot() = 0;

    // 加载完整状态（幂等：重复加载同一状态 = no-op）
    virtual bool loadState (const juce::ValueTree& state) = 0;

    // ===== 实时数据面（RT_SAFE 方法可从音频线程调用）=====

    // 拉取当前 metering 快照（峰值 / GR）
    virtual MeteringSnapshot getMeteringSnapshot() = 0;

    // 拉取电平历史（最多 points 个点，降采样由调用方指定）
    virtual HistoryData readHistory (int points) = 0;

    // ===== 分析面 =====

    // 获取传输曲线（dB in → dB out）。bandIndex 用于多段插件，单段插件传 0。
    virtual CurveData getTransferCurve (int bandIndex, float minDb, float maxDb, int points) = 0;

    // 获取频率响应曲线（Hz → dB）。EQ 返回合成响应；MB 返回指定段的交叉分频响应。
    virtual CurveData getFrequencyResponse (int bandIndex, float minFreq, float maxFreq, int points) = 0;

    // ===== 插件面 =====

    // 插件级旁路
    virtual void setBypass (bool bypassed) = 0;
    virtual bool isBypassed() = 0;

    // 插件元信息
    virtual juce::String getPluginName() = 0;
    virtual juce::String getPluginVersion() = 0;
};
