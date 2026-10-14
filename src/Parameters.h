#pragma once

#include <JuceHeader.h>

// ============================================================
// Param — 62 参数 ID / 枚举 / 钳制 / 布局工厂（TECH_SPEC §5）
// ============================================================

namespace Param
{
    constexpr int kNumBands = 4;
    constexpr int kLowBand = 0, kLowMidBand = 1, kHighMidBand = 2, kHighBand = 3;

    enum class PhaseMode     { Natural = 0, Minimal = 1, Linear = 2 };
    enum class Detector      { Peak = 0, RMS = 1 };
    enum class SCFilter      { Band = 0, Free = 1 };          // 2026-10-14 侧链滤波模式
    enum class Dynamics      { Compress = 0, Expand = 1 };    // Phase C-6
    enum class Slope         { Slope12 = 0, Slope24 = 1, Slope36 = 2, Slope48 = 3 };
    enum class Lookahead     { Off = 0, Ms1 = 1, Ms3 = 2 };
    enum class LinearQuality { Low = 0, Medium = 1, High = 2 };
    enum class Oversampling  { Off = 0, X2 = 1, X4 = 2 };          // 2026-10-14

    inline int   slopeIndexToDb (int idx) { return 12 * (juce::jlimit (0, 3, idx) + 1); }
    inline int   oversamplingIndexToFactor (int idx)               // 2026-10-14
    {
        switch (juce::jlimit (0, 2, idx))
        {
            case 1:  return 2;
            case 2:  return 4;
            default: break;
        }
        return 1;
    }
    inline float lookaheadIndexToMs (int idx)
    {
        switch (juce::jlimit (0, 2, idx))
        {
            case 1:  return 1.0f;
            case 2:  return 3.0f;
            default: break;
        }
        return 0.0f;
    }

    // ---------- 全局参数 ID（camelCase，TECH_SPEC §5.3） ----------
    namespace ID
    {
        inline constexpr const char* enabled       = "enabled";
        inline constexpr const char* phaseMode     = "phaseMode";
        inline constexpr const char* linearQuality = "linearQuality";
        inline constexpr const char* xoverSlope    = "xoverSlope";
        inline constexpr const char* xover1        = "xover1";
        inline constexpr const char* xover2        = "xover2";
        inline constexpr const char* xover3        = "xover3";
        inline constexpr const char* inputGain     = "inputGain";
        inline constexpr const char* outputGain    = "outputGain";
        inline constexpr const char* lookahead     = "lookahead";
        inline constexpr const char* oversampling  = "oversampling";   // 2026-10-14
        inline constexpr const char* globalMix     = "globalMix";      // Phase：全局干湿比 0-200%
    }

    // ---------- 频段参数 ID（"band" + i + PascalCase） ----------
    inline juce::String bandThreshold (int i) { return "band" + juce::String (i) + "Threshold"; }
    inline juce::String bandRange     (int i) { return "band" + juce::String (i) + "Range"; }
    inline juce::String bandRatio     (int i) { return "band" + juce::String (i) + "Ratio"; }
    inline juce::String bandAttack    (int i) { return "band" + juce::String (i) + "Attack"; }
    inline juce::String bandRelease   (int i) { return "band" + juce::String (i) + "Release"; }
    inline juce::String bandMakeup    (int i) { return "band" + juce::String (i) + "Makeup"; }
    inline juce::String bandMix       (int i) { return "band" + juce::String (i) + "Mix"; }
    inline juce::String bandKnee      (int i) { return "band" + juce::String (i) + "Knee"; }
    inline juce::String bandDetector  (int i) { return "band" + juce::String (i) + "Detector"; }
    inline juce::String bandDynamics  (int i) { return "band" + juce::String (i) + "Dynamics"; }   // Phase C-6
    inline juce::String bandSCFilter  (int i) { return "band" + juce::String (i) + "SCFilter"; }   // 2026-10-14
    inline juce::String bandSCLow     (int i) { return "band" + juce::String (i) + "SCLow"; }
    inline juce::String bandSCHigh    (int i) { return "band" + juce::String (i) + "SCHigh"; }
    inline juce::String bandStereoLink(int i) { return "band" + juce::String (i) + "StereoLink"; }
    inline juce::String bandSolo      (int i) { return "band" + juce::String (i) + "Solo"; }
    inline juce::String bandMute      (int i) { return "band" + juce::String (i) + "Mute"; }
    inline juce::String bandExt       (int i) { return "band" + juce::String (i) + "Ext"; }
    inline juce::String bandDelta     (int i) { return "band" + juce::String (i) + "Delta"; }

    // 宿主显示名映射（TECH_SPEC §5.3）
    inline const juce::String& bandName (int i)
    {
        static const juce::String names[4] = { "Low", "Low Mid", "High Mid", "High" };
        return names[juce::jlimit (0, 3, i)];
    }

    // ---------- 最小带宽钳制（DESIGN §3.2 / TECH_SPEC §3.8） ----------
    // 每分区 ≥ 1 个八度（频率比 ≥ 2:1）。按文档顺序就地钳制。
    inline void clampCrossovers (float& xover1, float& xover2, float& xover3)
    {
        xover1 = juce::jlimit (40.0f,                        xover2 * 0.5f, xover1);
        xover2 = juce::jlimit (juce::jmax (xover1 * 2.0f,  80.0f),  xover3 * 0.5f, xover2);
        xover3 = juce::jlimit (juce::jmax (xover2 * 2.0f, 160.0f), 10000.0f,      xover3);
    }

    // ---------- 布局工厂（62 参数） ----------
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace Param
