#pragma once

#include <JuceHeader.h>

#include "MBTheme.h"
#include "Anim.h"

class CrunchMBAudioProcessor;

// ============================================================
// OutputPanel — 输出选项面板（Pro-MB Output 面板对标）
//
// Phase D-4：把底栏的 OUT 读数从纯文本升级为**可点按钮**，
// 并用与 AnalyzerPanel / LinkedDrawer 完全相同的 sticky 状态机：
//   悬停打开（非 sticky）→ 鼠标移开自动收起 → 点击变 sticky 钉住
//   → 再点或点面板外收起
//
// 内容（Pro-MB Output 面板对标）：
//   Input Gain    ±36 dB（全局参数 inputGain）
//   Output Gain   ±36 dB（现有全局参数 outputGain）
//   Mix           0–200%（全局干湿比）
//   Global Bypass 开关
//   Auto-Gain     开关（Future MB 对标：ITU-R BS.1770 响度自动补偿）
//   Monitor       试听目标（Future MB 对标：Output/Input/Band1-4/Gain Reduction）
//
// 注意：Mix 0-200% 是**新增**的全局参数（原来是每段独立 Mix 0-100%），
// 需要动 DSP —— 见实现里的 TODO 标记。
// ============================================================
class OutputPanel : public juce::Component
{
public:
    std::function<void()> onSettingsChanged;

    OutputPanel() = default;
    ~OutputPanel() override = default;

    void paint (juce::Graphics&) override;

    // ---- 状态 ----
    bool  isOpen()   const noexcept { return open_; }
    bool  isSticky() const noexcept { return sticky_; }
    float anim()     const noexcept { return anim_; }

    void hoverButton (bool hovering);
    bool clickButton();
    void clickOutside();
    bool advance (double nowMs);

    void setHostBounds (juce::Rectangle<int> hostBounds);
    void setTheme (MBTheme::Theme t) { pal_ = MBTheme::palette (t); repaint(); }

    // ---- 当前设置 ----
    float inputGainDb()  const noexcept { return inputGainDb_; }
    float outputGainDb() const noexcept { return outputGainDb_; }
    float globalMixPct() const noexcept { return globalMixPct_; }
    bool  globalBypass() const noexcept { return globalBypass_; }
    /** Auto-Gain（Future MB 对标，默认关）。 */
    bool  autoGain() const noexcept { return autoGain_; }
    /** Monitor（Future MB 对标）：当前试听目标索引（0=Output）。 */
    int   monitorIndex() const noexcept { return monitorIdx_; }

    /** 测试钩子 */
    void  setInputGainForTest (float v)  { inputGainDb_ = v; }
    void  setOutputGainForTest (float v)  { outputGainDb_ = v; }
    void  setGlobalMixForTest (float v)   { globalMixPct_ = v; }
    void  setGlobalBypassForTest (bool v) { globalBypass_ = v; }
    void  setAutoGainForTest (bool v)     { autoGain_ = v; }
    void  setMonitorForTest (int idx)     { monitorIdx_ = juce::jlimit (0, 6, idx); }

    // ---- 命中测试 ----
    juce::Rectangle<float> panelRect() const;
    /** rowOut: 0=InputGain 1=OutputGain 2=GlobalMix 3=GlobalBypass 4=AutoGain 5=Monitor */
    bool hitTestRow (juce::Point<float> p, int& rowOut) const;
    void clickRow (int row);

private:
    bool  open_ = false;
    bool  sticky_ = false;
    float anim_ = 0.0f;
    float animTarget_ = 0.0f;
    Tween tween_;
    juce::Rectangle<int> hostBounds_;

    float inputGainDb_  = 0.0f;
    float outputGainDb_ = 0.0f;
    float globalMixPct_ = 100.0f;
    bool  globalBypass_ = false;
    bool  autoGain_     = false;   // Future MB 对标：默认关
    int   monitorIdx_   = 0;       // Future MB 对标：0=Output（默认，行为不变）

    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);
};
