#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>
#include <unordered_map>

#include "MBTheme.h"
#include "Anim.h"
#include "Knob.h"
#include "ValueField.h"
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
    // R36（B5）：6 个旋钮。Pro-MB 的分段面板里是
    //   Range / Threshold / Attack / Ratio / Knee / Release / **Level**，
    // 而我们此前把 **Mix**（全局 0–200% 语义）放进了带内、把等价于 Level 的
    // Makeup(−36..+36 dB) 藏在面板外。按用户决定：**保留 Mix**，同时把
    // OUTPUT（= Makeup，双极 ±dB）补成与其它旋钮同级的第 6 个旋钮。
    static constexpr int kPopKnobs = 6;

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
    /**
     * R36（B6）：该段的**真实频率范围**（Hz）。
     *
     * 事故：面板左上角那段 "LOW 20 - 200" 里，原先塞的是 setBandEdges 的
     * **像素坐标**，被 fmtHzShort() 当成 Hz 打印；而且 setBandEdges 只在
     * openPopoverForBand() 里调用一次 → 拖动分频点后数字永远不更新。
     * 现在由 DisplayView 每帧/每次分频变更时把 Hz 推下来。
     */
    void setBandFreqRange (float hzLow, float hzHigh);
    /** 测试钩子：当前显示的频率文本（"20 - 200"）。 */
    juce::String bandFreqTextForTest() const { return freqText_; }

    /** 每帧推进动画。返回 true 表示仍在动画中。 */
    bool advance (double nowMs);
    void snapToCurrent();
    void setHostBounds (juce::Rectangle<int> hostBounds);
    void setTheme (MBTheme::Theme t);
    /** 供绘制 hover 态使用（DisplayView 在 mouseMove 时更新）。 */
    void setLastMousePos (juce::Point<float> p) { lastMousePos_ = p; }

    // ---- 命中测试 ----
    bool hitTestClose  (juce::Point<float> p) const;
    juce::Rectangle<float> closeRect() const;
    juce::Rectangle<float> popSliderRect (int k) const;
    juce::Rectangle<float> popSliderTrackRect (int k) const;
    bool hitTestPanel  (juce::Point<float> p) const;
    bool hitTestSlider (juce::Point<float> p, int& kOut) const;
    juce::Rectangle<float> expertRect() const;
    /** Phase C-5：EXPERT 展开内容的矩形（右缘标签左侧）。 */
    juce::Rectangle<float> expertContentRect() const;

    /** Phase C-6：Dynamics 分段按钮（Compress/Expand，Pro-MB segmented 对标）。 */
    juce::Rectangle<float> dynamicsSegRect() const;
    bool hitTestDynamicsSeg (juce::Point<float> p) const;
    /** 返回 true 表示状态翻转了。 */
    bool clickDynamicsSeg(bool expand);
    bool dynamicsIsExpand() const noexcept { return dynamicsExpand_; }
    bool isExpertOpen() const noexcept { return expertOpen_; }
    void toggleExpert()
    {
        expertOpen_ = ! expertOpen_;
        layoutKnobs(true);
        // R35（C-b）：EXPERT 内容现在由 ExpertLayer 子组件绘制，要单独重绘
        if (expertLayer_ != nullptr) expertLayer_->repaint();
        repaint();
    }

    /** Phase C-5：EXPERT 展开内容的两行可点区域。 */
    enum class ExpertRow { None = 0, Detector, Lookahead, Filtering, SCLow, SCHigh };
    /** 返回命中的行；None 表示没命中。 */
    ExpertRow hitTestExpertRow (juce::Point<float> p) const;
    /** 点击某行：Detector 翻转 Peak/RMS，Lookahead 循环 Off→1ms→3ms。 */
    void clickExpertRow (ExpertRow row);
    bool selectExpertAt(juce::Point<float> p);
    juce::Rectangle<float> expertOptionRect(ExpertRow row,int option) const;
    void selectExpertOption(ExpertRow row,int option);
    ValueField& sidechainFieldForTest(bool high) { return high?scHigh_:scLow_; }

    // ---- 交互（由 DisplayView 的鼠标处理转发）----
    void beginSliderDrag (int k, float mouseX);
    void dragSlider (int k, float mouseX);
    void endSliderDrag();
    /**
     * Phase（R19）：滑块键盘/滚轮步进。
     *   k = 0 Ratio(1..20) / 1 Knee(0..48dB) / 2 Ext / 3 Detector / 4 Lookahead(三档)
     *   notches > 0 增、< 0 减；fine = Shift 细调
     * 返回 true 表示该滑块消费了这次输入。
     */
    bool stepSlider (int k, int notches, bool fine);
    int  sliderDragIndex() const noexcept { return sliderDrag_; }

    /** 光标闪烁推进。 */
    void tickEditCursors (juce::uint32 nowMs);
    /** Phase C-2：推进所有子旋钮的四态渐变（由宿主 60fps 定时器调用）。 */
    void advanceKnobStates (double nowMs);
    /** R36：任一旋钮的四态渐变是否还在进行（宿主的空闲降频必须读它）。 */
    bool isKnobAnimating() const
    {
        for (auto& kb : knobs_)
            if (kb != nullptr && kb->isAnimating())
                return true;
        return false;
    }

    /** 同步参数 → 控件（切段/外部改动后调用）。 */
    void syncFromParams();

    /** R35（N4）：按 parameterID 精确刷新（宿主自动化 / 其它控件改参数）。
     *  返回 true 表示该 id 属于本面板并已处理。 */
    bool syncParam (const juce::String& paramId);

    /** 供 DisplayView 读取的滑杆值（绘制 hover 态等）。 */
    /**
     * Phase（R21）：k 钳到 0..4 后再索引。
     * 此前 sliderValue() 无钳制而 sliderValueForTest() 有——同一个读取的
     * 两个入口语义不一致（R19b 的同型问题：两份实现必然一份漏）。
     * 现在统一钳制；越界时返回端点值而非 UB。
     */
    double sliderValue (int k) const { return sliders_[(size_t) juce::jlimit (0, 4, k)]; }

    // ---- 测试钩子 ----
    /** Phase C-6 测试钩子：Dynamics 分段按钮矩形。 */
    juce::Rectangle<float> dynamicsSegRectForTest() const { return dynamicsSegRect(); }
    /** 测试钩子：第 k 个旋钮的当前值（k 钳到 0..kPopKnobs-1）。 */
    double popoverValueForTest (int k) const
    {
        return values_[(size_t) juce::jlimit (0, kPopKnobs - 1, k)];
    }
    juce::Rectangle<float> popoverRectForTest() const { return popoverRect(); }
    /** 测试钩子：直接施加一次滑杆值（applySlider 为 private）。 */
    void applySliderForTest (int k, double v) { applySlider (k, v); }
    /** R19：滑块键盘/滚轮步进测试钩子 */
    bool   stepSliderForTest (int k, int notches, bool fine) { return stepSlider (k, notches, fine); }
    double sliderValueForTest (int k) const { return sliders_[(size_t) juce::jlimit (0, 4, k)]; }
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

    // ---- R34 测试钩子 ----
    /** N1：子组件（5 个 Knob）是否可被点击。必须为 true，否则旋钮点不动。 */
    bool acceptsChildClicksForTest() const
    {
        bool onThis = false, onChildren = false;
        getInterceptsMouseClicks (onThis, onChildren);
        return onChildren;
    }
    /** A：第 k 个旋钮的实际 bounds（验证不随动画滑动）。 */
    juce::Rectangle<int> popKnobBoundsForTest (int k) const
    {
        const int i = juce::jlimit (0, kPopKnobs - 1, k);
        const auto* kb = knobs_[(size_t) i].get();
        return kb != nullptr ? kb->getBounds() : juce::Rectangle<int>();
    }
    /** A：第 k 个旋钮的**目标槽位**（与动画无关，由 popKnobRect 决定）。 */
    juce::Rectangle<float> popKnobSlotForTest (int k) const
    {
        return popKnobRect (juce::jlimit (0, kPopKnobs - 1, k));
    }
    /** R35（C-b）测试钩子：EXPERT 覆盖层是否为最上层子组件。 */
    bool expertLayerIsTopmost() const
    {
        if (expertLayer_ == nullptr || getNumChildComponents() == 0)
            return false;
        return getChildComponent (getNumChildComponents() - 1) == expertLayer_.get();
    }

    /** A：第 k 个旋钮的透明度（跟随 anim_ 淡入）。 */
    float popKnobAlphaForTest (int k) const
    {
        const auto* kb = knobs_[(size_t) juce::jlimit (0, kPopKnobs - 1, k)].get();
        return kb != nullptr ? kb->getAlpha() : 0.0f;
    }

private:
    juce::Rectangle<float> popoverRect() const;
    juce::Rectangle<float> popKnobRect (int k) const;

    /** R35（C-b）：EXPERT 展开内容的**覆盖层**。
     *  JUCE 里子组件永远画在父组件 paint() 之后，而 EXPERT 框原先是在
     *  BandPopover::paint() 里画的 → 被 5 个 Knob 子组件盖住右半部分
     *  （旋钮 3/4 正好压在 SC HIGH-PASS / SC LOW-PASS 的数值列上）。
     *  改成独立子组件、并在旋钮之后 addAndMakeVisible，就合成在旋钮之上；
     *  它不接收鼠标（命中仍由 DisplayView 经 hitTestExpertRow 分发）。 */
    struct ExpertLayer : public juce::Component
    {
        BandPopover& owner;
        explicit ExpertLayer (BandPopover& o) : owner (o) { setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics& g) override { owner.paintExpertLayer (g); }
    };
    std::unique_ptr<ExpertLayer> expertLayer_;
    ValueField scLow_,scHigh_;
    void paintExpertLayer (juce::Graphics& g);

    void ensureKnobs();
    void layoutKnobs (bool force);
    void applyPopoverKnob (int k, double v);
    void applySlider (int k, double v);
    /** Phase：侧链频率循环（Free 模式的高通/低通频率档位）。 */
    void layoutExpertFields();

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
    /** R36（B6）：该段频率范围（Hz）与预格式化的顶部文本。 */
    float bandHzLow_ = 0.0f, bandHzHigh_ = 0.0f;
    juce::String freqText_;
    juce::Rectangle<int> hostBounds_;
    juce::Point<float> lastMousePos_;

    bool  expertOpen_ = false;
    int   sliderDrag_ = -1;
    float sliderDragX0_ = 0.0f;
    double sliderDragNorm0_ = 0.0;

    std::array<std::unique_ptr<Knob>, kPopKnobs> knobs_;
    /** R35（D）：上次下发给 Knob 的直径（Knob 无 getter）。 */
    std::array<float, kPopKnobs> knobDiameters_ { { 0, 0, 0, 0, 0, 0 } };
    bool knobsReady_ = false;

    std::array<double, kPopKnobs> values_ {};             // 物理值
    std::array<float,  kPopKnobs> normValues_ {};         // 归一化 0..1
    std::array<double, 5> sliders_ { 4.0, 1.0, 0.0, 0.0, 0.0 };  // Ratio/Knee/Ext/Detector/(Lookahead)
    bool dynamicsExpand_ = false;      // Phase C-6：false=Compress true=Expand

    // Phase：参数访问已统一走 proc_.getBackend()（PLAN Phase 6 / API 规范耦合点 D），
    // 原来这里自持的 paramMap_ 已删除。

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPopover)
};

