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
    /** Monitor（Future MB 对标）：试听哪个内部信号。Output = 正常输出。 */
    enum class Monitor       { Output = 0, Input = 1, Band1 = 2, Band2 = 3,
                               Band3 = 4, Band4 = 5, GainReduction = 6 };

    inline int   slopeIndexToDb (int idx) { return 12 * (juce::jlimit (0, 3, idx) + 1); }

    /**
     * Phase（R19c）：Choice 参数的**实际值 → 选项索引**。
     *
     * 为什么必须收在这里：此前这个换算有三份——
     *   ① PluginProcessor 的匿名命名空间 helper `choiceIndex(atomic<float>*)`
     *   ② DisplayView 里的 `roundToInt (getParamFast (id))`
     *   ③ （曾经）`getParamNorm(...) * (N-1)`，即 R9 修掉的四处
     * ①②目前是对的，但"同一逻辑两份"正是 R19b 刚立为教训的模式：
     * 这次两份都对，下次改一处忘另一处就对不齐了。
     * 现在统一由此提供，且**只接受实际值**（Choice 的实际值就是索引）。
     */
    inline int choiceIndexFromValue (float actualValue)
    {
        return juce::roundToInt (actualValue);
    }
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
        inline constexpr const char* autoGain      = "autoGain";       // Future MB 对标：响度自动补偿（默认关）
        inline constexpr const char* monitorMode   = "monitorMode";    // Future MB 对标：内部信号试听（默认 Output）
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

    /** R32：全部参数 id（含每段的 13 个）。用于给 APVTS 注册监听器——
     *  原先只注册 8 个结构参数，导致 parameterChanged 对其余参数永不触发，
     *  UI 的"参数变化唤醒"收不到通知。 */
    inline std::vector<juce::String> allIds()
    {
        std::vector<juce::String> ids {
            ID::enabled, ID::phaseMode, ID::linearQuality, ID::xoverSlope,
            ID::xover1, ID::xover2, ID::xover3, ID::inputGain, ID::outputGain,
            ID::lookahead, ID::oversampling, ID::globalMix,
            ID::autoGain, ID::monitorMode
        };
        for (int i = 0; i < kNumBands; ++i)
        {
            ids.push_back (bandThreshold (i));
            ids.push_back (bandRange     (i));
            ids.push_back (bandRatio     (i));
            ids.push_back (bandAttack    (i));
            ids.push_back (bandRelease   (i));
            ids.push_back (bandMakeup    (i));
            ids.push_back (bandMix       (i));
            ids.push_back (bandKnee      (i));
            ids.push_back (bandDetector  (i));
            ids.push_back (bandDynamics  (i));
            ids.push_back (bandSCFilter  (i));
            ids.push_back (bandSCLow     (i));
            ids.push_back (bandSCHigh    (i));
            ids.push_back (bandStereoLink(i));
            ids.push_back (bandSolo      (i));
            ids.push_back (bandMute      (i));
            ids.push_back (bandExt       (i));
            ids.push_back (bandDelta     (i));
        }
        return ids;
    }

    // 宿主显示名映射（TECH_SPEC §5.3）
    inline const juce::String& bandName (int i)
    {
        static const juce::String names[4] = { "Low", "Low Mid", "High Mid", "High" };
        return names[juce::jlimit (0, 3, i)];
    }

    // ---------- 最小带宽钳制（DESIGN §3.2 / TECH_SPEC §3.8） ----------
    // 每分区 ≥ 1 个八度（频率比 ≥ 2:1）。先收敛各界的合法区间，避免逆序入参触发
    // juce::jlimit 的 lowerLimit <= upperLimit 断言或破坏 2:1 不变量。
    inline void clampCrossovers (float& xover1, float& xover2, float& xover3)
    {
        const float u1 = juce::jlimit (40.0f, 2500.0f, juce::jmin (xover2 * 0.5f, xover3 * 0.25f));
        xover1 = juce::jlimit (40.0f, u1, xover1);

        const float l2 = juce::jlimit (80.0f, 5000.0f, xover1 * 2.0f);
        const float u2 = juce::jmax   (l2, juce::jmin (5000.0f, xover3 * 0.5f));
        xover2 = juce::jlimit (l2, u2, xover2);

        const float l3 = juce::jlimit (160.0f, 10000.0f, xover2 * 2.0f);
        xover3 = juce::jlimit (l3, 10000.0f, xover3);
    }

    // ---------- 布局工厂（62 参数） ----------
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace Param
