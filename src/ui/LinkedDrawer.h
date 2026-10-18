#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>

#include "MBTheme.h"
#include "Anim.h"
#include "Knob.h"
#include "../Parameters.h"

class CrunchMBAudioProcessor;

// ============================================================
// LinkedDrawer — LINKED CONTROL 抽屉（UI_PLAN_V2 §3.2，从 DisplayView 拆出）
//
// 职责（DESIGN D4 / UI_SPEC §5.2）：
//   · 底部梯形按钮（点开/收起，hover/click sticky 语义由宿主决定）
//   · 5 个握柄旋钮：Threshold / Attack / Release / Makeup（Δ 偏移语义）
//     + Mix（绝对值语义，v1.6 例外）
//   · 面板玻璃底 + accent 描边，随 linkedAnim_ 滑入/滑出
//
// 语义（D4）：4 段完全独立 + 5 组统一控制，按住拖动 = **线性恒定偏移**（非比例）。
//   Δ 语义：knob 值本身是"偏移量"，改动时四段同名参数同量平移
//   Mix 例外：绝对值，四段同设为该值
//
// 线程纪律：所有 setParam* 都走 APVTS setValueNotifyingHost（消息线程），
//   音频线程只读 atomic（TECH_SPEC §4）。
// ============================================================
class LinkedDrawer : public juce::Component
{
public:
    static constexpr int kLinkedKnobs  = 5;      // Threshold/Attack/Release/Makeup/Mix
    static constexpr int kLinkedMixIndex = 4;    // Mix 例外：绝对值语义

    explicit LinkedDrawer (CrunchMBAudioProcessor& processor);
    ~LinkedDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- 状态 ----
    bool  isOpen() const noexcept { return open_; }
    bool  isSticky() const noexcept { return sticky_; }
    float anim()  const noexcept { return anim_; }
    void  setOpen (bool open);                     // 触发开/关（驱动动画）
    void  toggle();

    /**
     * Phase D-2：Pro-MB sticky 语义（与 AnalyzerPanel 同一套状态机）。
     *   hoverButton(true)  -> 非 sticky 时打开
     *   hoverButton(false) -> 非 sticky 时自动收起；sticky 时不动
     *   clickButton()      -> 变 sticky 并保持打开；已 sticky 则收起并解除
     *   clickOutside()     -> 收起并解除 sticky
     */
    void hoverButton (bool hovering);
    bool clickButton();
    void clickOutside();

    /** 每帧推进动画。返回 true 表示仍在动画中（需要继续 repaint）。 */
    bool advance (double nowMs);

    /** 立即吸附到当前状态（无动画）。用于宿主构造/主题切换/尺寸变化。 */
    void snapToCurrent();

    /** 宿主窗口尺寸变化时调用（按钮/面板位置依赖宿主 bounds）。 */
    void setHostBounds (juce::Rectangle<int> hostBounds);

    /** 主题切换。 */
    void setTheme (MBTheme::Theme t);

    // ---- 命中测试（宿主鼠标分发用）----
    bool hitTestButton (juce::Point<float> p) const;
    bool hitTestPanel  (juce::Point<float> p) const;

    /** 同步旋钮显隐（动画结束时隐藏子旋钮）。 */
    void syncKnobVisibility();

    /** 光标闪烁推进（转发给所有子旋钮）。 */
    void tickEditCursors (juce::uint32 nowMs);
    /** Phase C-2：推进所有子旋钮的四态渐变（由宿主 60fps 定时器调用）。 */
    void advanceKnobStates (double nowMs);

    /** 测试钩子：直接施加一次偏移（applyOffset 为 private）。 */
    void applyOffsetForTest (int k, double v) { applyOffset (k, v); }

    /** 测试钩子 */
    juce::Rectangle<int> knobBoundsForTest (int k) const
    {
        return (k >= 0 && k < kLinkedKnobs && knobs_[(size_t) k] != nullptr)
                 ? knobs_[(size_t) k]->getBounds() : juce::Rectangle<int>();
    }

    /** R34（N1）测试钩子：子组件（5 个 Knob）是否可被点击。
     *  必须为 true —— 否则抽屉旋钮点不动（见 .cpp 里 setInterceptsMouseClicks 注释）。 */
    bool acceptsChildClicksForTest() const
    {
        bool onThis = false, onChildren = false;
        getInterceptsMouseClicks (onThis, onChildren);
        return onChildren;
    }

private:
    // ---- 面板几何（依赖宿主 bounds）----
    juce::Rectangle<float> panelRect() const;
    juce::Rectangle<float> buttonRect() const;

    void ensureKnobs();
    void layoutKnobs (bool force);
    void applyOffset (int k, double newValue);

    // ---- 参数读写（走 APVTS）----
    float getParamPhysical (const juce::String& id) const;
    void  setParamPhysical (const juce::String& id, float physical) const;

    CrunchMBAudioProcessor& proc_;
    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);

    bool  open_ = false;
    bool  sticky_ = false;   // Phase D-2：sticky 面板不随鼠标移开而收起
    float anim_ = 0.0f;
    float animTarget_ = 0.0f;
    Tween tween_;

    juce::Rectangle<int> hostBounds_;

    std::array<std::unique_ptr<Knob>, kLinkedKnobs> knobs_;
    std::array<double, kLinkedKnobs - 1> offset_ {};   // 4 个偏移旋钮的当前 Δ

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkedDrawer)
};
