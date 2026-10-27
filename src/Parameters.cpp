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
    juce::String fmtPctPrecise(float v,int) { return juce::String(v,1); }

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

    // ================= 全局参数（12 个，TECH_SPEC §5.1 + 后续扩充） =================
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

    // 2026-10-14：过采样（Pro-MB 对标）。高比率 + 快 attack 时抑制增益调制混叠。
    addChoice (pid (ID::oversampling), "Oversampling",
               { "Off", "2x", "4x" },
               (int) Oversampling::Off);

    // Phase：全局 Mix 0-200%（Pro-MB 对标）。
    // 100% = 纯处理声；<100% = 与干声混合（减弱效果）；>100% = 并行压缩提响。
    addFloat  (pid (ID::globalMix), "Mix", { 0.0f, 200.0f, 0.1f }, 100.0f, "%", fmtPctPrecise);

    // Auto-Gain（Future MB 对标，TECH_SPEC §5.1 / RESEARCH_UI P3）。
    // **默认关闭**：关闭时插件行为与本次改动之前完全一致；
    // 打开后测处理前后的 ITU-R BS.1770 K 加权响度并补偿，
    // 使"调参不改变输出音量"（Future MB 的亮点功能）。
    addBool   (pid (ID::autoGain), "Auto-Gain", false);

    // Monitor（Future MB 对标，RESEARCH_UI 交互清单第 9 条）：
    // 试听内部信号——不用戴耳机接跳线就能听"压缩机到底在做什么"。
    //   Output  = 正常输出（**默认**，行为与不设此参数时逐样本一致）
    //   Input   = 干声（输入增益后、分频前）
    //   Band 1-4 = 该段自己的信号（分频后、求和前）
    addChoice (pid (ID::monitorMode), "Monitor",
               { "Output", "Input", "Band 1", "Band 2", "Band 3", "Band 4" },
               (int) Monitor::Output);

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

        // 2026-10-14：Knee 从 Hard/Soft 二态改为连续 0–48 dB（Pro-MB 对标）。
        addFloat  (pid (bandKnee     (i)), bn + " Knee",     { 0.0f, 48.0f, 0.1f }, 6.0f, "dB", fmtDb2);
        addChoice (pid (bandDetector (i)), bn + " Detector", { "Peak", "RMS"  }, (int) Detector::Peak);
        // Phase C-6：动态模式 Compress/Expand（Pro-MB 分段按钮对标）
        addChoice (pid (bandDynamics (i)), bn + " Dynamics", { "Compress", "Expand" }, (int) Dynamics::Compress);

        // 2026-10-14：侧链滤波（Pro-MB Free 模式对标）+ 立体声连续链接
        addChoice (pid (bandSCFilter  (i)), bn + " Side Chain Filtering", { "Band", "Free" }, (int) SCFilter::Band);
        addFloat  (pid (bandSCLow     (i)), bn + " Side Chain Low Frequency",  skewed (20.0f, 2000.0f, 1.0f, 100.0f),  100.0f, "Hz", fmtHz);
        addFloat  (pid (bandSCHigh    (i)), bn + " Side Chain High Frequency", skewed (500.0f, 20000.0f, 1.0f, 5000.0f), 5000.0f, "Hz", fmtHz);
        addFloat  (pid (bandStereoLink(i)), bn + " Stereo Link", { 0.0f, 100.0f, 1.0f }, 100.0f, "%", fmtPct);

        addBool   (pid (bandSolo  (i)), bn + " Solo",  false);
        addBool   (pid (bandMute  (i)), bn + " Mute",  false);
        addBool   (pid (bandExt   (i)), bn + " Ext",   false);
        addBool   (pid (bandDelta (i)), bn + " Delta", false);
    }

    return layout;
}

} // namespace Param
