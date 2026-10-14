#pragma once

// ============================================================
// IMBBackend — Crunch MB 专属扩展（继承 IPluginBackend）
//
// 来源：`crunch-api-interface.md` v1.0 §3。
// ============================================================

#include "IPluginBackend.h"

struct MBGroupSetResult {
    bool  applied;
    std::array<float, 4> actualValues; // 4 段实际生效值（钳制后）
};

class IMBBackend : public IPluginBackend
{
public:
    // ===== 多段扩展 =====

    // 拉取多段电平历史（in/out；GR 不入历史，实时 GR 经 getMeteringSnapshot）
    virtual MultibandHistoryData readMultibandHistory (int points) = 0;

    // 统一控制：对 4 段指定参数施加线性恒定偏移
    // paramName: "threshold" / "attack" / "release" / "makeup" / "mix"
    // delta: 偏移量（dB / ms / %，与参数单位一致）
    // 注意：此处 delta 指参数偏移量，与频段 Delta 监听参数（band{i}Delta）无关
    virtual MBGroupSetResult setBandGroupDelta (const juce::String& paramName, float delta) = 0;

    // 设置交叉点频率（自动执行最小带宽钳制：每段 ≥ 1 八度）
    virtual SetResult setCrossover (int crossoverIndex, float freqHz) = 0;

    // 获取 4 段的当前 GR
    virtual std::array<float, 4> getBandGainReduction() = 0;

    // 获取最新频谱帧（双层 Pre/Post，512 点对数频率）
    virtual bool getLatestSpectrum (std::array<float, 512>& pre,
                                     std::array<float, 512>& post,
                                     std::vector<float>& freqs) = 0;

    // 获取指定段的传输曲线
    // （覆盖基类 getTransferCurve，bandIndex = 0..3 对应 L/LM/HM/H）
};
