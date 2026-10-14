#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>
#include <unordered_map>

#include "MBTheme.h"
#include "Anim.h"
#include "Knob.h"
#include "../Parameters.h"

class CrunchMBAudioProcessor;

// ============================================================
// BandPopover — 频段 Popover（UI_SPEC §6，Pro-MB 样式；从 DisplayView 拆出）
//
// 职责：
//   · 点击分区 → 该段选中 + 浮层弹出；点空白/✕/Esc → 收起
//   · 5 旋钮行：THRESHOLD / RANGE / ATTACK / RELEASE / MIX（各带范围标注）
//   · 滑杆行：RATIO(1..20) / KNEE(0..48dB) / EXT(INT↔SIDECHAIN)
//   · EXPERT 折叠区：DETECTOR(PEAK/RMS) / LOOKAHEAD(全局占位)
//   · 顶栏：⚡BYP 宏 + 段名 + 频率范围 + ✕
//
// 互斥（O6 方案1）：与 LINKED 抽屉不能同时打开，由 DisplayView 仲裁。
//
// 几何依赖：水平位置随分段居中（DisplayView 经 setBandEdges 传入该段像素边界），
//   缩放因子由 hostBounds（920×620 设计稿基准）推导，不依赖 DisplayView 内部。
// ============================================================
class BandPopover : public juce::Component
{
public:
    static constexpr int kPopKnobs = 5;

    explicit BandPopover (CrunchMBAudioProcessor& processor);
    ~BandPopover() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- 状态 ----
    bool  isOpen() const noexcept { return open_; }
    float anim()  const noexcept { return anim_; }
    int   band()  const noexcept { return band_; }
    void  setOpen (bool open);
    void  setBand (int band);
    /** 传入该段的像素左右边界（用于水平居中）。 */
    void setBandEdges (float leftX, float rightX);

    /** 每帧推进动画。返回 true 表示仍在动画中。 */
    bool advance (double nowMs);
    void snapToCurrent();
    void setHostBounds (juce::Rectangle<int> hostBounds);
    void setTheme (MBTheme::Theme t);
    /** 供绘制 hover 态使用（DisplayView 在 mouseMove 时更新）。 */
    void setLastMousePos (juce::Point<float> p) { lastMousePos_ = p; }

    // ---- 命中测试 ----
    bool hitTestClose  (juce::Point<float> p) const;
    bool hitTestPanel  (juce::Point<float> p) const;
    bool hitTestSlider (juce::Point<float> p, int& kOut) const;
    juce::Rectangle<float> expertRect() const;
    /** Phase C-5：EXPERT 展开内容的矩形（右缘标签左侧）。 */
    juce::Rectangle<float> expertContentRect() const;

    /** Phase C-6：Dynamics 分段按钮（Compress/Expand，Pro-MB segmented 对标）。 */
    juce::Rectangle<float> dynamicsSegRect() const;
    bool hitTestDynamicsSeg (juce::Point<float> p) const;
    /** 返回 true 表示状态翻转了。 */
    bool clickDynamicsSeg();
    bool dynamicsIsExpand() const noexcept { return dynamicsExpand_; }
    bool isExpertOpen() const noexcept { return expertOpen_; }
    void toggleExpert() { expertOpen_ = ! expertOpen_; repaint(); }

    /** Phase C-5：EXPERT 展开内容的两行可点区域。 */
    enum class ExpertRow { None = 0, Detector, Lookahead };
    /** 返回命中的行；None 表示没命中。 */
    ExpertRow hitTestExpertRow (juce::Point<float> p) const;
    /** 点击某行：Detector 翻转 Peak/RMS，Lookahead 循环 Off→1ms→3ms。 */
    void clickExpertRow (ExpertRow row);

    // ---- 交互（由 DisplayView 的鼠标处理转发）----
    void beginSliderDrag (int k, float mouseX);
    void dragSlider (int k, float mouseX);
    void endSliderDrag();
    int  sliderDragIndex() const noexcept { return sliderDrag_; }

    /** 光标闪烁推进。 */
    void tickEditCursors (juce::uint32 nowMs);
    /** Phase C-2：推进所有子旋钮的四态渐变（由宿主 60fps 定时器调用）。 */
    void advanceKnobStates (double nowMs);

    /** 同步参数 → 控件（切段/外部改动后调用）。 */
    void syncFromParams();

    /** 供 DisplayView 读取的滑杆值（绘制 hover 态等）。 */
    double sliderValue (int k) const { return sliders_[(size_t) k]; }

    // ---- 测试钩子 ----
    /** Phase C-6 测试钩子：Dynamics 分段按钮矩形。 */
    juce::Rectangle<float> dynamicsSegRectForTest() const { return dynamicsSegRect(); }
    double popoverValueForTest (int k) const { return values_[(size_t) k]; }
    juce::Rectangle<float> popoverRectForTest() const { return popoverRect(); }
    /** 测试钩子：直接施加一次滑杆值（applySlider 为 private）。 */
    void applySliderForTest (int k, double v) { applySlider (k, v); }
    /** 测试钩子：直接施加一次旋钮值（applyPopoverKnob 为 private）。 */
    void applyKnobForTest (int k, double v) { applyPopoverKnob (k, v); }
    /** 测试钩子：旋钮矩形（T40 用）。 */
    juce::Rectangle<float> popSliderRectForTest (int k) const { return popSliderRect (k); }
    /** 测试钩子：EXPERT 区域（T40 用）。 */
    juce::Rectangle<float> popExpertRectForTest() const { return expertRect(); }
    /** Phase C-5 测试钩子：EXPERT 展开内容矩形。 */
    juce::Rectangle<float> popExpertContentRectForTest() const { return expertContentRect(); }
    /** 测试钩子：EXPERT 是否展开（T40 用）。 */
    bool isPopoverExpertOpenForTest() const { return expertOpen_; }

private:
    juce::Rectangle<float> popoverRect() const;
    juce::Rectangle<float> popKnobRect (int k) const;
    juce::Rectangle<float> popSliderRect (int k) const;

    void ensureKnobs();
    void layoutKnobs (bool force);
    void applyPopoverKnob (int k, double v);
    void applySlider (int k, double v);

    float getParamNorm (const juce::String& id) const;
    float getParamPhysical (const juce::String& id) const;
    float paramRangeMinOf (const juce::String& id) const;
    float paramRangeMaxOf (const juce::String& id) const;
    void  setParamNorm (const juce::String& id, float norm) const;
    void  setParamPhysical (const juce::String& id, float physical) const;
    void  buildParamCache();
    static juce::String fmtHzShort (float hz);

    CrunchMBAudioProcessor& proc_;
    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);

    bool  open_ = false;
    float anim_ = 0.0f;
    float animTarget_ = 0.0f;
    Tween tween_;

    int   band_ = -1;
    float bandLeftX_ = 0.0f, bandRightX_ = 0.0f;
    juce::Rectangle<int> hostBounds_;
    juce::Point<float> lastMousePos_;

    bool  expertOpen_ = false;
    int   sliderDrag_ = -1;
    float sliderDragX0_ = 0.0f;
    double sliderDragV0_ = 0.0;

    std::array<std::unique_ptr<Knob>, kPopKnobs> knobs_;
    bool knobsReady_ = false;

    std::array<double, kPopKnobs> values_ {};             // 物理值
    std::array<float,  kPopKnobs> normValues_ {};         // 归一化 0..1
    std::array<double, 5> sliders_ { 4.0, 1.0, 0.0, 0.0, 0.0 };  // Ratio/Knee/Ext/Detector/(Lookahead)
    bool dynamicsExpand_ = false;      // Phase C-6：false=Compress true=Expand

    std::unordered_map<juce::String, juce::AudioProcessorParameter*> paramMap_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPopover)
};
