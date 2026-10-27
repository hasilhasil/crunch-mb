#pragma once

#include <JuceHeader.h>

#include "MBTheme.h"
#include "Anim.h"
#include "Knob.h"
#include <array>

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

    OutputPanel();
    ~OutputPanel() override;
    std::function<void(int,bool)> onGesture;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- 状态 ----
    bool  isOpen()   const noexcept { return open_; }
    bool  isSticky() const noexcept { return sticky_; }
    float anim()     const noexcept { return anim_; }

    void hoverButton (bool hovering);
    bool clickButton();
    void clickOutside();
    bool advance (double nowMs);
    void snapToCurrent() { anim_=animTarget_=open_?1.0f:0.0f;tween_.snapTo(anim_);layoutControls(); }

    void setHostBounds (juce::Rectangle<int> hostBounds);
    void setTheme (MBTheme::Theme t);
    void applyUiSnapshot (const UiPack::UiSnapshot& snap);

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
    void  setMonitorForTest (int idx)     { monitorIdx_ = juce::jlimit (0, 5, idx); }

    /** R35（N4）：把 6 行副本回读成真实参数值。
     *  以前只在"打开面板那一刻"同步一次，于是宿主自动化改 Auto-Gain / Monitor、
     *  或 Alt+拖 OUT 改 Output Gain 之后，面板显示与真实状态相反——
     *  再点一下该行就会写出一个和用户预期相反的值。 */
    void syncFromBackend (float inputGainDb, float outputGainDb, float mixPct,
                          bool anyBypassed, bool autoGainOn, int monitorIdxIn)
    {
        const int mon = juce::jlimit (0, 5, monitorIdxIn);
        const bool changed = (std::abs (inputGainDb_  - inputGainDb)  > 1.0e-4f)
                          || (std::abs (outputGainDb_ - outputGainDb) > 1.0e-4f)
                          || (std::abs (globalMixPct_ - mixPct)       > 1.0e-4f)
                          || (globalBypass_ != anyBypassed)
                          || (autoGain_ != autoGainOn)
                          || (monitorIdx_ != mon);
        if (! changed)
            return;
        inputGainDb_  = inputGainDb;
        outputGainDb_ = outputGainDb;
        globalMixPct_ = mixPct;
        globalBypass_ = anyBypassed;
        autoGain_     = autoGainOn;
        monitorIdx_   = mon;
        syncControls();
        repaint();
    }

    // ---- 命中测试 ----
    juce::Rectangle<float> panelRect() const;
    /** rowOut: 0=InputGain 1=OutputGain 2=GlobalMix 3=GlobalBypass 4=AutoGain 5=Monitor */
    bool hitTestRow (juce::Point<float> p, int& rowOut) const;
    void clickRow (int row);
    juce::Rectangle<float> toggleRect(int index) const;
    Knob& numericControlForTest(int index) { return knobs_[(size_t)index]; }
    void selectMonitorForTest(int idx) { monitorBox_.setSelectedId(juce::jlimit(0, 5, idx) + 1, juce::sendNotificationSync); }

private:
    void syncControls();
    void layoutControls();
    struct PanelLookAndFeel : juce::LookAndFeel_V4
    {
        float scale=1;
        juce::Font getComboBoxFont(juce::ComboBox&) override { return MBTheme::font(MBTheme::uiMedium(),14*scale); }
        juce::Font getPopupMenuFont() override { return MBTheme::font(MBTheme::uiRegular(),15*scale); }
    } menuLookAndFeel_;
    std::array<Knob,3> knobs_;
    juce::ComboBox monitorBox_;
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
    UiPack::ThemeTokens  themeTokens_  = UiPack::buildThemePack  (UiPack::ThemeId::Classic);
    UiPack::LayoutTokens layoutTokens_ = UiPack::buildLayoutPack (UiPack::LayoutId::Standard);
    const UiPack::LocalePack* locale_  = &UiPack::buildLocalePack (UiPack::LocaleId::EnUS);
    juce::String tr (const char* key) const
    {
        return (locale_ != nullptr) ? locale_->get (key)
                                    : UiPack::buildLocalePack (UiPack::LocaleId::EnUS).get (key);
    }
};
