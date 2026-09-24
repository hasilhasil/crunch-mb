#include "Parameters.h"

// ============================================================
// Parameters.cpp — 62 参数布局（TECH_SPEC §5.1 / §5.2 精确规格）
// ============================================================

namespace Param
{
namespace
{
    // ---------- 读数格式（TECH_SPEC §5 显示格式列） ----------
    juce::String fmtDb2  (float v, int) { return juce::String (v, 2); }                              // dB 2 位小数
    juce::String fmtHz   (float v, int) { return v >= 10.0f ? juce::String ((int) std::round (v))
                                                            : juce::String (v, 1); }                 // Hz 整数或 1 位小数
    juce::String fmtMs   (float v, int) { return v >= 1.0f ? juce::String ((int) std::round (v))
                                                           : juce::String (v, 1); }                  // ≥1 取整，<1 保留 1 位
    juce::String fmtRatio(float v, int) { return juce::String (v, 2); }                              // : 1，2 位小数
    juce::String fmtPct  (float v, int) { return juce::String ((int) std::round (v)); }              // % 整数

    using FloatAttr = juce::AudioParameterFloatAttributes;

    juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::ParameterID pid (const char* id)        { return { juce::String (id), 1 }; }
    juce::ParameterID pid (const juce::String& id){ return { id, 1 }; }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&] (juce::ParameterID id, const juce::String& name,
                         juce::NormalisableRange<float> range, float def,
                         const char* unit, juce::String (*fmt) (float, int))
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            id, name, range, def,
            FloatAttr().withLabel (unit).withStringFromValueFunction (fmt)));
    };
    auto addChoice = [&] (juce::ParameterID id, const juce::String& name,
                          const juce::StringArray& choices, int defIndex)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, choices, defIndex));
    };
    auto addBool = [&] (juce::ParameterID id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (id, name, def));
    };

    // ================= 全局参数（10 个，TECH_SPEC §5.1） =================
    addBool   (pid (ID::enabled), "Enabled", true);

    addChoice (pid (ID::phaseMode), "Phase Mode",
               { "Natural Phase", "Minimal Phase", "Linear Phase" },
               (int) PhaseMode::Natural);

    addChoice (pid (ID::linearQuality), "Linear Quality",
               { "Low", "Medium", "High" },
               (int) LinearQuality::Medium);

    addChoice (pid (ID::xoverSlope), "Crossover Slope",
               { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" },
               (int) Slope::Slope24);

    addFloat  (pid (ID::xover1), "Crossover 1",  skewed (40.0f,   2500.0f,  1.0f,  200.0f),  200.0f, "Hz", fmtHz);
    addFloat  (pid (ID::xover2), "Crossover 2",  skewed (80.0f,   5000.0f,  1.0f, 1000.0f), 1000.0f, "Hz", fmtHz);
    addFloat  (pid (ID::xover3), "Crossover 3",  skewed (160.0f, 10000.0f,  1.0f, 6000.0f), 6000.0f, "Hz", fmtHz);

    addFloat  (pid (ID::inputGain),  "Input Gain",  { -36.0f, 36.0f, 0.1f }, 0.0f, "dB", fmtDb2);
    addFloat  (pid (ID::outputGain), "Output Gain", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB", fmtDb2);

    addChoice (pid (ID::lookahead), "Lookahead",
               { "Off", "1 ms", "3 ms" },
               (int) Lookahead::Off);

    // ================= 每频段参数（13 × 4 = 52，TECH_SPEC §5.2） =================
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto bn = bandName (i);

        addFloat  (pid (bandThreshold (i)), bn + " Threshold", { -60.0f, 0.0f, 0.1f },   -20.0f,  "dB",  fmtDb2);
        addFloat  (pid (bandRange     (i)), bn + " Range",     { -30.0f, 30.0f, 0.1f },   -6.0f,  "dB",  fmtDb2);
        addFloat  (pid (bandRatio     (i)), bn + " Ratio",     { 1.0f, 20.0f, 0.1f },      4.0f,  ": 1", fmtRatio);
        addFloat  (pid (bandAttack    (i)), bn + " Attack",    skewed (0.1f, 200.0f,  0.1f,  10.0f),  10.0f,  "ms", fmtMs);
        addFloat  (pid (bandRelease   (i)), bn + " Release",   skewed (10.0f, 1000.0f, 0.5f, 100.0f), 100.0f, "ms", fmtMs);
        addFloat  (pid (bandMakeup    (i)), bn + " Makeup",    { -36.0f, 36.0f, 0.1f },    0.0f,  "dB",  fmtDb2);
        addFloat  (pid (bandMix       (i)), bn + " Mix",       { 0.0f, 100.0f, 0.1f },   100.0f,  "%",   fmtPct);

        addChoice (pid (bandKnee     (i)), bn + " Knee",     { "Hard", "Soft" }, (int) KneeMode::Soft);
        addChoice (pid (bandDetector (i)), bn + " Detector", { "Peak", "RMS"  }, (int) Detector::Peak);

        addBool   (pid (bandSolo  (i)), bn + " Solo",  false);
        addBool   (pid (bandMute  (i)), bn + " Mute",  false);
        addBool   (pid (bandExt   (i)), bn + " Ext",   false);
        addBool   (pid (bandDelta (i)), bn + " Delta", false);
    }

    return layout;
}

} // namespace Param
