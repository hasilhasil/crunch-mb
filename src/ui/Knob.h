// ============================================================
// Crunch MB — Knob（Phase 5.1 旋钮家族控件）
//   UI_SPEC §6.4：暖灰环 + accent 弧 + 近白帽 + 深色指针
//   交互（§6.4 + Pro-MB 官方 Knobs 帮助）：
//     · 垂直拖动：速度比例（慢拖 = 精细，FabFilter 同款逻辑）
//     · 双击：内嵌文本输入（自绘，不用 juce::TextEditor——宿主里会弹独立窗）
//     · 滚轮：按 interval 步进
//     · Shift：微调（×0.1）
//     · Ctrl+click：复位默认值
// ============================================================
#pragma once
#include <JuceHeader.h>
#include "MBTheme.h"
#include "Anim.h"

class Knob : public juce::Component
{
public:
    Knob()
    {
        // Phase（R18）：常态下就要能收键盘，否则方向键步进收不到事件。
        // 此前只在 openEditor() 里夺焦——非编辑态等于没有键盘入口。
        setWantsKeyboardFocus (true);
    }

    // ---- 配置 ----
    void setRange (double minValue, double maxValue, double interval = 0.0);
    void setDefaultValue (double d)            { default_ = juce::jlimit (min_, max_, d); }
    void setLabel (juce::String text)          { label_ = std::move (text); repaint(); }
    void setValueSuffix (juce::String s)       { suffix_ = std::move (s); repaint(); }
    void setDecimals (int d)                   { decimals_ = d; repaint(); }
    void setKnobDiameter (float px)            { knobDiameter_ = px; invalidateBackLayer(); repaint(); }
    void setShowLabel (bool s)                 { showLabel_ = s; repaint(); }
    /** 双极弧：填充从 origin（默认 0）起画，而不是从最小值起画。
     *  用于 Range / Makeup / LINKED 的 Δ 偏移这类"零点在中间"的参数。 */
    void setBipolar (bool b)                   { bipolar_ = b; repaint(); }
    /** 双极弧的原点（物理值）。默认 0；范围不含 0 的旋钮应显式给出。 */
    void setBipolarOrigin (double v)           { bipolarOrigin_ = v; repaint(); }
    /**
     * R36（用户实测）：刻度与**参数自身的 NormalisableRange** 对齐。
     *
     * 事故：Attack 参数是 `setSkewForCentre (0.1, 200, centre 10)`，
     * 默认 10 ms 归一化 = 0.5；而旋钮用线性 0.1..200，同一个 10 ms 落在
     * 线性行程的 4.95%（弧几乎空着、指针贴着最左）。同一个值两套位置，
     * 宿主动画车道和面板旋钮互相矛盾，拖动前段还极敏感。
     *
     * 这里按 JUCE `setSkewForCentre` 的同一公式复算 skew 指数，
     * 让 归一化 ↔ 物理值 与参数完全一致（指针/弧/拖动/滚轮全部走同一映射）。
     * @param centreValue 参数声明里的 centre（必须落在 (min,max) 内）
     */
    void setSkew (double centreValue)
    {
        skewCentre_ = centreValue;
        skewSet_    = true;
        updateSkew();
        repaint();
    }
    float getSkewForTest() const { return hasSkew_ ? skew_ : 1.0f; }
    void setValuePrefix (juce::String p)       { valuePrefix_ = std::move (p); repaint(); }
    void setLinkedMark (bool b)                { linkedMark_ = b; repaint(); }   // §5.2 ⊞ 联动标识
    void setPalette (MBTheme::Palette p)       { pal_ = p; invalidateBackLayer(); repaint(); }

    // ---- 值 ----
    void setValue (double v, bool sendNotification = true);
    double getValue() const                    { return value_; }
    /** 归一化位置（含 skew）：与参数的 NormalisableRange 完全一致。 */
    double getNormalisedValue() const          { return (double) toNorm (value_); }
    /** 测试钩子：归一化 ↔ 物理值 往返（验证 skew 与参数同源）。 */
    double normToValueForTest (double n) const { return fromNorm (n); }
    double valueToNormForTest (double v) const { return (double) toNorm (v); }

    /** 值变化回调（拖拽/滚轮/输入/复位）*/
    std::function<void (double)> onValueChange;
    /** 开始/结束拖拽（宿主自动化用）*/
    std::function<void()> onDragStart, onDragEnd;

    void paint (juce::Graphics&) override;
    void resized() override;
    void paintForTest (juce::Graphics& g) { paint (g); }   // T37 像素回归
    // 测试钩子
    void mouseDownForTest (float downX, float downY, juce::ModifierKeys mods,
                           float x, float y)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        mouseDown (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     nullptr, nullptr, juce::Time(),
                                     juce::Point<float> (downX, downY), juce::Time(), 1, false));
    }
    void mouseDragForTest (float downX, float downY, juce::ModifierKeys mods,
                           float x, float y)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        mouseDrag (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     nullptr, nullptr, juce::Time(),
                                     juce::Point<float> (downX, downY), juce::Time(), 1, true));
    }
    void mouseUpForTest (float downX, float downY, juce::ModifierKeys mods,
                         float x, float y)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        mouseUp (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   nullptr, nullptr, juce::Time(),
                                   juce::Point<float> (downX, downY), juce::Time(), 1, false));
    }
    void mouseWheelForTest (float x, float y, float dy,
                            juce::ModifierKeys mods = juce::ModifierKeys())
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        juce::MouseWheelDetails w; w.deltaY = dy; w.isReversed = false;
        mouseWheelMove (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          nullptr, nullptr, juce::Time(),
                                          juce::Point<float> (x, y), juce::Time(), 1, false), w);
    }
    /** T63：文本编辑测试钩子 */
    double      getValueForTest() const { return value_; }
    void        openEditorForTest()     { openEditor(); }
    void        keyPressedForTest (const juce::KeyPress& k) { keyPressed (k); }
    /** 便捷重载：直接给字符（构造 KeyPress 走单字符路径）。 */
    void        keyPressedCharForTest (juce::juce_wchar ch)
    {
        keyPressed (juce::KeyPress (ch));
    }
    juce::String editTextForTest() const { return editText_; }
    /** T66：常态键盘步进测试钩子 */
    bool   isEditingForTest() const { return editing_; }
    void   setDefaultValueForTest (double v) { default_ = v; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    /** 文本编辑态的键盘处理（R18 从 keyPressed 拆出）。 */
    bool keyPressedEditing (const juce::KeyPress&);

    // ---- Phase C-2：四态（Pro-MB Overlay{NORMAL,ACTIVE,DOWN,ROTATE}）----
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;

    /**
     * 推进四态渐变。由宿主组件的定时器调用（旋钮自身没有 Timer，
     * 避免 14 个旋钮各起一个定时器）。
     *   NORMAL  — 默认
     *   ACTIVE  — 悬停（帽略亮）
     *   DOWN    — 按下（帽再亮一档）
     *   ROTATE  — 拖动中（帽转暖色 + 弧加粗，即 Pro-MB 的 OverlayRotate）
     * 三个维度各一个 Tween，叠加成最终亮度/色相。
     */
    void advanceStates (double nowMs);
    /**
     * R36：渐变是否还在进行。宿主定时器的"空闲降频"必须把它算进去——
     * 此前没有任何地方上报旋钮动画，UI 静默 30 帧后定时器降到 4Hz（250ms），
     * 而 kKnobMs = 120ms 的渐变只可能被采样 0~1 次，高亮就是"啪"地跳。
     */
    bool isAnimating() const noexcept { return animating_; }
    /** 测试钩子：当前四态合成量（0..1）。 */
    float stateGlowForTest() const { return juce::jmax (hoverT_.value (1.0e18),
                                                          juce::jmax (downT_.value (1.0e18),
                                                                      rotateT_.value (1.0e18))); }
    /** 测试钩子：弧粗 / 弧半径 / 帽半径（验证"状态不改变几何"）。 */
    float arcThicknessForTest() const;
    float arcRadiusForTest() const;
    float capRadiusForTest() const;
    /** R36 测试钩子：当前值的指针角度（弧度，0 = 12 点钟顺时针）。 */
    float angleForTest() const { return valueToAngle (value_); }
    /** R36 测试钩子：直接置四态标志（不依赖真实鼠标事件）。 */
    void setStateForTest (bool hovered, bool down, bool dragging)
    {
        hovered_ = hovered;
        mouseDownHeld_ = down;
        dragging_ = dragging;
    }

private:
    void commitValue (double v);
    /** 细调提交：跳过 interval 量化（Shift 滚轮/拖动用）。 */
    void commitValueFine (double v);
    void openEditor();
    void closeEditor (bool apply);
    float valueToAngle (double v) const;      // 弧度
    juce::Rectangle<float> knobBounds() const;
    /** 物理值 → 归一化 0..1（含 skew，与参数的 NormalisableRange 同式）。 */
    float toNorm (double v) const;
    /** 归一化 0..1 → 物理值（含 skew）。 */
    double fromNorm (double n) const;
    /** 按 skewCentre_ 重算 skew_/hasSkew_（setSkew / setRange 后调用）。 */
    void updateSkew();

    double min_ = 0.0, max_ = 1.0, interval_ = 0.0;
    double value_ = 0.0, default_ = 0.0;
    double dragStartValue_ = 0.0;
    double dragStartNorm_  = 0.0;              // R36：拖动按归一化行程计算
    double bipolarOrigin_  = 0.0;              // R36：双极弧原点（物理值）
    float  skew_           = 1.0f;             // R36：刻度指数（1 = 线性）
    double skewCentre_     = 0.0;              // R36：参数声明里的 centre
    bool   skewSet_        = false;
    bool   hasSkew_        = false;
    int    decimals_ = 1;
    float  knobDiameter_ = 48.0f;
    bool   showLabel_ = true;
    bool   bipolar_ = false;
    bool   linkedMark_ = false;                    // §5.2：⊞ 联动标识
    bool   dragging_ = false;
    bool   shiftFine_ = false;
    bool   hovered_  = false;                      // Phase C-2：ACTIVE 态
    bool   mouseDownHeld_ = false;                 // Phase C-2：DOWN 态（按下未起拖）
    bool   animating_ = false;                     // R36：宿主的空闲降频要读它
    double lastNowMs_ = 0.0;                       // R36：paint() 用宿主注入的时钟
    Tween  hoverT_, downT_, rotateT_;              // Phase C-2：三个维度各一个 Tween
    float  slowAccum_ = 0.0f;                  // 慢拖累积（速度比例）

    juce::String label_, suffix_, valuePrefix_;
    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);

    // 内嵌编辑（自绘）
    bool editing_ = false;
    juce::String editText_;
    // A2 修复（UI_PLAN_V2 §6.3）：光标可见性改为**时间驱动**。
    // 此前在 Knob::paint() 里写 `editCursorOn_ = ! editCursorOn_;`，
    // 导致闪烁频率 = 重绘率（DisplayView 60Hz 时光标以 30Hz 狂闪），
    // 且不可预期。现在由 setEditCursorBlink() 按时间推进，paint() 只读。
    bool editCursorOn_ = true;
    juce::uint32 editCursorLastToggleMs_ = 0;

    // ---- Phase C-1：旋钮 BACK 层缓存（Pro-MB KNOBLARGEBACK* 对标）----
    // BACK = 外圈刻度环 + 深色圆体底 + 径向渐变，**与当前值无关**，
    // 所以缓存成 Image，只在直径/主题变化时重建（原来每帧全画）。
    // OVERLAY = 值弧 + 帽 + 指针，每帧画（随值与四态变）。
    juce::Image backLayer_;
    float  backLayerDiameter_ = 0.0f;      // 缓存对应的直径（0 = 未建）
    int    backLayerTheme_    = -1;        // 缓存对应的主题
    void   buildBackLayer (float d);
    void   invalidateBackLayer() { backLayerDiameter_ = 0.0f; }

public:
    /** 按当前时间推进光标闪烁（半周期 Anim::kBlinkMs）。由宿主组件定时调用。 */
    void setEditCursorBlink (juce::uint32 nowMs);
    /** 主题/尺寸变化时调用，令 BACK 层缓存失效。 */
    void refreshBackLayer() { invalidateBackLayer(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};
