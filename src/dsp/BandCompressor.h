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

// Phase C-6：动态模式（Pro-MB Compress/Expand 分段按钮对标）
//   Compress → 朝阈值方向压（经典）
//   Expand   → 反向，扩大动态范围（修复过缩音轨 / 门限扩展式清晰化）
enum class DynamicsMode { Compress = 0, Expand = 1 };

class BandCompressor
{
public:
    void prepare (double sampleRate);
    void reset();

    void setParameters (float thresholdDb, float ratio, float attackMs, float releaseMs,
                        float rangeDb, float kneeDb, bool useRmsDetector) noexcept;

    /** Phase C-6：动态模式（Compress / Expand）。 */
    void setDynamicsMode (DynamicsMode m) noexcept { dynamicsMode_ = m; }

    // 立体声联动处理（就地乘平滑增益）
    void processSample (float& left, float& right) noexcept;

    // 更新检测器/平滑状态并返回线性增益（Mix 数学需要干/湿分离时使用）
    void processGain (float left, float right, float& gainL, float& gainR) noexcept;

    // 立体声链接 0..1（2026-10-14）。1 = 两声被较响者驱动（原行为）；0 = 各自独立。
    void setStereoLink (float link01) noexcept { stereoLink_ = juce::jlimit (0.0f, 1.0f, link01); }

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
    float kneeWidthDb_ = 6.0f;      // 连续 0–48 dB；0 = 硬膝（2026-10-14）
    DynamicsMode dynamicsMode_ = DynamicsMode::Compress;   // Phase C-6
    bool  useRms_      = false;

    float attackCoeff_  = 0.0f;   // exp(-1/(tau*fs)) 形式，63%@tau
    float releaseCoeff_ = 0.0f;
    float rmsCoeff_     = 0.0f;

    float smoothedGainDb_ = 0.0f;
    float smoothedGainDbR_ = 0.0f;     // 右声道包络（2026-10-14 立体声链接）
    float stereoLink_     = 1.0f;      // 1 = 完全链接（较响者驱动）
    float lastGain_       = 1.0f;
    float rmsEnvelope_    = 0.0f;
    float rmsEnvelopeR_   = 0.0f;
    float lastDetectorDb_ = -100.0f;
    float lastDetectorDbR_ = -100.0f;
};
