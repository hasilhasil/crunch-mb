#pragma once

#include <JuceHeader.h>

#include "MBTheme.h"
#include "Anim.h"

class CrunchMBAudioProcessor;

// ============================================================
// AnalyzerPanel — 频谱分析仪设置面板（Pro-MB "Spectrum analyzer" 对标）
//
// Phase D 交互语义（Pro-MB 的签名式交互）：
//   · **悬停** Analyzer 按钮 → 面板打开（非 sticky）
//   · **单击** Analyzer 按钮 → 面板变 **sticky**（不自动消失）
//   · 再点一次 / 点面板外 → 收起
//   · 非 sticky 面板在鼠标离开按钮和面板后自动收起
//
// 为什么自绘而不用 juce::PopupMenu：
//   原生弹出菜单是操作系统外观，且没有 hover-open / sticky 语义。
//   Pro-MB 的面板是插件内自绘的深色圆角块。
//
// 内容（Pro-MB Analyzer 面板对标）：
//   Pre spectrum / Post spectrum（两层开关）
//   Resolution: Low 1024 / Medium 2048 / High 4096 / Maximum 8192
//   Speed:      Fast 10ms / Medium 20ms / Slow 60ms
//   Tilt:       0 / 1.5 / 3.0 / 4.5 dB per oct
//   Freeze
// ============================================================
class AnalyzerPanel : public juce::Component
{
public:
    /** 设置变化回调（宿主用来 applyAnalyzerSettings）。 */
    std::function<void()> onSettingsChanged;

    AnalyzerPanel()
    {
        // R33：bounds 是全屏（DisplayView 传 getLocalBounds()）。默认的
        // setInterceptsMouseClicks(true) 会让这个全屏子组件吞掉所有鼠标事件，
        // DisplayView 的 mouseDown/mouseMove 收不到 → 插件完全无法交互。
        // 命中统一由 DisplayView 分发（同 LinkedDrawer/BandPopover）。
        setInterceptsMouseClicks (false, false);
    }
    ~AnalyzerPanel() override = default;

    void paint (juce::Graphics&) override;

    // ---- 状态 ----
    bool isOpen()   const noexcept { return open_; }
    bool isSticky() const noexcept { return sticky_; }
    float anim()    const noexcept { return anim_; }

    /** 悬停按钮：非 sticky 时打开。 */
    void hoverButton (bool hovering);
    /** 点击按钮：toggle sticky 开合。返回 true 表示状态变了。 */
    bool clickButton();
    /** 点面板外：收起（sticky 也收）。 */
    void clickOutside();

    /** 每帧推进动画。返回 true 表示仍在动画中。 */
    bool advance (double nowMs);
    void snapToCurrent() { anim_=animTarget_=open_?1.0f:0.0f;tween_.snapTo(anim_); }

    void setHostBounds (juce::Rectangle<int> hostBounds);
    void setTheme (MBTheme::Theme t) { pal_ = MBTheme::palette (t); repaint(); }

    // ---- 当前设置（宿主读取）----
    bool  preOn()   const noexcept { return preOn_; }
    bool  postOn()  const noexcept { return postOn_; }
    int   fftSize() const noexcept { return fftSize_; }
    int   speed()   const noexcept { return speed_; }
    float tilt()    const noexcept { return tilt_; }
    bool  frozen()  const noexcept { return frozen_; }

    /** 测试钩子：直接设 Pre/Post 开关。 */
    void setPreOnForTest (bool v)  { preOn_ = v; }
    void setPostOnForTest (bool v) { postOn_ = v; }
    /** 测试钩子：直接设 Resolution/Speed/Tilt/Freeze。 */
    void setFftSizeForTest (int v)   { fftSize_ = v; }
    void setSpeedForTest (int v)     { speed_ = v; }
    void setTiltForTest (float v)    { tilt_ = v; }
    void setFrozenForTest (bool v)   { frozen_ = v; }

    // ---- 命中测试 ----
    /** 面板自身矩形（用于"点面板外收起"判定与绘制）。 */
    juce::Rectangle<float> panelRect() const;
    juce::Rectangle<float> optionRect(int row) const;
    /** 命中某一行；rowOut: 0=Pre 1=Post 2..5=Resolution 6..8=Speed 9..12=Tilt 13=Freeze */
    bool hitTestRow (juce::Point<float> p, int& rowOut) const;

    /** 测试钩子 */
    bool  isStickyForTest() const { return sticky_; }
    /** 供 DisplayView 分发行点击（clickRow 为 private）。 */
    void clickRowPublic (int row) { clickRow (row); }

private:
    void layoutRows();
    void clickRow (int row);

    bool  open_ = false;
    bool  sticky_ = false;
    float anim_ = 0.0f;
    float animTarget_ = 0.0f;
    Tween tween_;
    juce::Rectangle<int> hostBounds_;

    bool  preOn_ = true, postOn_ = true;
    int   fftSize_ = 2048;
    int   speed_ = 1;
    float tilt_ = 0.0f;
    bool  frozen_ = false;

    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);
};
