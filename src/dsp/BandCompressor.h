#pragma once

#include <JuceHeader.h>

// ============================================================
// BandCompressor — 单段压缩器（自 Compressor.h 演化）
// 去掉 lookahead / auto gain / color（多带由 MultibandDSP 编排）；
// 新增 Range 增益变化钳制（`band{i}Range`，Pro-MB 同功能）。
// 链路：立体声联动检测（Peak/RMS）→ 软/硬拐点增益计算 →
//       Range 钳制 → attack/release 平滑（dB 域）→ 应用。
// 音频线程热路径零分配零锁（TECH_SPEC §4）。
// ============================================================

class BandCompressor
{
public:
    void prepare (double sampleRate);
    void reset();

    void setParameters (float thresholdDb, float ratio, float attackMs, float releaseMs,
                        float rangeDb, bool softKnee, bool useRmsDetector) noexcept;

    // 立体声联动处理（就地乘平滑增益）
    void processSample (float& left, float& right) noexcept;

    // 更新检测器/平滑状态并返回线性增益（Mix 数学需要干/湿分离时使用）
    float processGain (float left, float right) noexcept;

    // 静态传输曲线（含 Range 钳制、不含平滑）——UI 传输曲线 inset 用（TECH_SPEC §6.2）
    float getStaticGainDb (float levelDb) const noexcept;

    float getSmoothedGainDb()  const noexcept { return smoothedGainDb_; }
    float getLastGainLinear()  const noexcept { return lastGain_; }
    float getLastDetectorDb()  const noexcept { return lastDetectorDb_; }

private:
    float computeTargetGainDb (float levelDb) const noexcept;

    double sampleRate_ = 48000.0;

    float thresholdDb_ = -20.0f;
    float ratio_       = 4.0f;
    float rangeDb_     = -30.0f;
    float kneeWidthDb_ = 6.0f;
    bool  useRms_      = false;

    float attackCoeff_  = 0.0f;   // exp(-1/(tau*fs)) 形式，63%@tau
    float releaseCoeff_ = 0.0f;
    float rmsCoeff_     = 0.0f;

    float smoothedGainDb_ = 0.0f;
    float lastGain_       = 1.0f;
    float rmsEnvelope_    = 0.0f;
    float lastDetectorDb_ = -100.0f;
};
