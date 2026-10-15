#include "Knob.h"
#include "Anim.h"

// ---------------- 配置 ----------------
void Knob::setRange (double minValue, double maxValue, double interval)
{
    min_ = minValue; max_ = std::max (minValue, maxValue); interval_ = interval;
    value_ = juce::jlimit (min_, max_, value_);
    default_ = juce::jlimit (min_, max_, default_);
    repaint();
}

void Knob::setValue (double v, bool sendNotification)
{
    const double nv = juce::jlimit (min_, max_, v);
    if (std::abs (nv - value_) < 1.0e-9) return;
    value_ = nv;
    repaint();
    if (sendNotification && onValueChange) onValueChange (value_);
}

// ---------------- 几何 ----------------
juce::Rectangle<float> Knob::knobBounds() const
{
    const float w = (float) getWidth();
    const float d = juce::jmin (knobDiameter_, w);
    const float cy = showLabel_ ? knobDiameter_ * 0.5f + 1.0f
                                : (float) getHeight() * 0.5f - 4.0f;
    return juce::Rectangle<float> (w * 0.5f - d * 0.5f, cy, d, d);
}

// 角度约定统一为 JUCE addCentredArc 的"12 点钟起、顺时针"：
//   point = (cx + r·sin(θ), cy − r·cos(θ))
// 起点（最小）= 7:30 方向，终点（最大）= 4:30 方向，行程 270°，缺口在正下方。
float Knob::valueToAngle (double v) const
{
    const float norm = (float) ((v - min_) / (max_ - min_));
    const float start = juce::MathConstants<float>::pi * 1.25f;      // 7:30 方向
    const float end   = juce::MathConstants<float>::pi * 3.75f;      // 4:30 方向
    return start + juce::jlimit (0.0f, 1.0f, norm) * (end - start);  // 270° 行程
}

// ---------------- 交互 ----------------
void Knob::commitValue (double v)
{
    if (interval_ > 0.0) v = std::round (v / interval_) * interval_;
    setValue (v, true);
}

void Knob::commitValueFine (double v)
{
    // Phase：细调提交——**跳过 interval 量化**。
    // 否则 Shift 细调在小 interval 参数上完全无效：step 0.1 会被
    // round(v/1.0)*1.0 量化回 0，用户按 Shift 却毫无变化。
    setValue (v, true);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (editing_) { closeEditor (true); return; }

    dragStartValue_ = value_;
    dragging_ = true;
    mouseDownHeld_ = true;          // Phase C-2：DOWN 态
    slowAccum_ = 0.0f;
    shiftFine_ = e.mods.isShiftDown();

    // Ctrl+click = 复位（FabFilter 官方：Ctrl/Cmd + click resets to default）
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        setValue (default_, true);
        dragging_ = false;
        mouseDownHeld_ = false;
        return;
    }
    if (onDragStart) onDragStart();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_) return;

    const double span = max_ - min_;
    if (span <= 0.0) return;

    // 垂直模式：上拖=增大。220px 走完全程；Shift 微调 ×0.1。
    const float dy = (float) (e.getMouseDownY() - e.getPosition().getY());
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;

    const double delta = (double) dy / 220.0 * span * (double) fine;
    commitValue (dragStartValue_ + delta);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging_ && onDragEnd) onDragEnd();
    dragging_ = false;
    mouseDownHeld_ = false;         // Phase C-2：DOWN 态解除
    slowAccum_ = 0.0f;
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    openEditor();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (editing_) return;
    const double span = max_ - min_;
    double step = (interval_ > 0.0) ? interval_ : span * 0.02;
    if (wheel.deltaY < 0) step = -step;

    // Phase：Shift = 细调（与 mouseDrag 的 shiftFine_ 语义一致）。
    // 此前滚轮没有细调，而拖动有——交互不一致：用户拖着能精调，
    // 滚轮却一跳一个 interval。Pro-MB / Future MB 的滚轮都支持 Shift 细调。
    if (e.mods.isShiftDown())
    {
        commitValueFine (value_ + step * 0.1);
        return;
    }

    // Ctrl/Cmd = 加速（大步进，用于快速跨量程）
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        step *= 5.0;

    commitValue (value_ + step);
}

// ---------------- Phase C-2：四态 ----------------
void Knob::mouseEnter (const juce::MouseEvent&) { hovered_ = true; }
void Knob::mouseExit  (const juce::MouseEvent&) { hovered_ = false; }

void Knob::advanceStates (double nowMs)
{
    // 三个维度各自独立 Tween，从"当前显示值"起跳，目标变化不打断。
    const double ms = Anim::kKnobMs;
    tweenTo (hoverT_,  nowMs, hovered_        ? 1.0f : 0.0f, ms);
    tweenTo (downT_,   nowMs, mouseDownHeld_  ? 1.0f : 0.0f, ms);
    tweenTo (rotateT_, nowMs, dragging_       ? 1.0f : 0.0f, ms);
}

// ---------------- 内嵌输入 ----------------
void Knob::openEditor()
{
    editing_ = true;
    editText_.clear();
    editCursorOn_ = true;
    editCursorLastToggleMs_ = juce::Time::getMillisecondCounter();
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

// A2 修复：光标闪烁由时间驱动（半周期 Anim::kBlinkMs），与重绘率解耦。
void Knob::setEditCursorBlink (juce::uint32 nowMs)
{
    if (! editing_)
        return;
    if (editCursorLastToggleMs_ == 0)
        editCursorLastToggleMs_ = nowMs;
    if ((int) (nowMs - editCursorLastToggleMs_) >= (int) Anim::kBlinkMs)
    {
        editCursorOn_ = ! editCursorOn_;
        editCursorLastToggleMs_ = nowMs;
        repaint();
    }
}

void Knob::closeEditor (bool apply)
{
    if (! editing_) return;
    editing_ = false;
    if (apply)
    {
        double v = editText_.getDoubleValue();
        // 支持 "50%" 形式
        if (editText_.containsChar ('%'))
            v = min_ + (max_ - min_) * editText_.getDoubleValue() / 100.0;
        commitValue (v);
    }
    editText_.clear();
    repaint();
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    if (! editing_) return false;

    if (key == juce::KeyPress::escapeKey)  { closeEditor (false); return true; }
    if (key == juce::KeyPress::returnKey)  { closeEditor (true);  return true; }
    if (key == juce::KeyPress::backspaceKey)
    { editText_ = editText_.substring (0, juce::jmax (0, editText_.length() - 1)); repaint(); return true; }

    const juce::juce_wchar ch = key.getTextCharacter();
    // 某些构造路径（如 KeyPress('7')）只设 keyCode 不设 textCharacter，
    // 此时 getTextCharacter() 返回 0。回退到 keyCode 兜底，保证真实键盘
    // 与程序化构造都能输入。
    const juce::juce_wchar c = (ch != 0) ? ch : (juce::juce_wchar) key.getKeyCode();
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '%')
    {
        // 限制输入长度：无上限时用户可无限键入，getDoubleValue() 对超长
        // 字符串行为未定义（且视觉上会溢出旋钮）。
        if (editText_.length() < 12)
            editText_ += juce::String::charToString (c);
        repaint(); return true;
    }
    return false;
}

// ---------------- Phase C-1：BACK 层缓存 ----------------
// 外圈刻度环 + 深色圆体底。与当前值无关 → 只在直径/主题变化时重建。
// 对齐 Pro-MB 的 KNOBLARGEBACK*（预渲染刻度环位图）：刻度环底部留缺口，
// 与 270° 行程一致；环在值弧**外侧**。
void Knob::buildBackLayer (float d)
{
    // +4px 余量给外圈刻度（刻度画在半径 r+0.075d 处）
    const int px = juce::roundToInt (d) + 4;
    backLayer_ = juce::Image (juce::Image::ARGB, px, px, true);
    juce::Graphics g (backLayer_);

    const float cx = px * 0.5f, cy = px * 0.5f;
    const float r  = d * 0.5f;
    const float a0 = valueToAngle (min_);
    const float a1 = valueToAngle (max_);

    // ---- 外圈刻度环（11 根短刻度，跨 270°，底部留缺口）----
    const int   nTicks = 11;
    const float tickInner = r + juce::jmax (1.5f, d * 0.035f);
    const float tickOuter = r + juce::jmax (3.5f, d * 0.075f);
    g.setColour (pal_.gridZ);
    for (int i = 0; i < nTicks; ++i)
    {
        const float t = (float) i / (float) (nTicks - 1);
        const float a = a0 + t * (a1 - a0);
        const float x1 = cx + std::sin (a) * tickInner;
        const float y1 = cy - std::cos (a) * tickInner;
        const float x2 = cx + std::sin (a) * tickOuter;
        const float y2 = cy - std::cos (a) * tickOuter;
        g.drawLine (x1, y1, x2, y2, juce::jmax (1.0f, d * 0.018f));
    }

    // ---- 深色圆体底（径向渐变，上亮下暗）----
    const float bodyR = r - juce::jmax (1.0f, d * 0.02f);
    juce::ColourGradient bodyGrad (pal_.track.brighter (0.10f), cx, cy - bodyR,
                                   pal_.track.darker (0.25f), cx, cy + bodyR, false);
    g.setGradientFill (bodyGrad);
    g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
}

// ---------------- 绘制 ----------------
void Knob::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const auto kb = knobBounds();
    const float d = kb.getWidth();
    const float cx = kb.getCentreX();
    const float cy = kb.getCentreY();
    const float r  = d * 0.5f;

    // ---- 标签（旋钮上方）----
    if (showLabel_ && label_.isNotEmpty())
    {
        g.setFont (MBTheme::font (MBTheme::semiBold(), juce::jmin (10.5f, d * 0.22f)));
        g.setColour (pal_.dim);
        auto labelBox = juce::Rectangle<float> (0.0f, kb.getY() - juce::jmin (15.0f, d * 0.32f), w, 14.0f);
        g.drawText (label_, labelBox, juce::Justification::centredTop);

        // §5.2：⊞ 联动标识（标签右侧小方块+加号，表示作用于四段）
        if (linkedMark_)
        {
            const float bw = juce::jmin (8.0f, d * 0.17f);
            const float bx = labelBox.getRight() - bw - 2.0f;
            const float by = labelBox.getY() + 3.0f;
            g.setColour (pal_.accent.withAlpha (0.85f));
            g.drawRoundedRectangle (bx, by, bw, bw, 2.0f, 1.0f);
            g.fillRect (bx + bw * 0.2f, by + bw * 0.45f, bw * 0.6f, juce::jmax (1.0f, bw * 0.1f));
            g.fillRect (bx + bw * 0.45f, by + bw * 0.2f, juce::jmax (1.0f, bw * 0.1f), bw * 0.6f);
        }
    }

    // ---- Phase C-1：BACK 层（刻度环 + 圆体底），缓存 Image ----
    // 直径变了才重建（主题变化经 setPalette/setKnobDiameter 失效缓存）。
    // 否则直接贴图，省掉每帧 11 根刻度 + 渐变填充。
    if (backLayerDiameter_ != d)
    {
        buildBackLayer (d);
        backLayerDiameter_ = d;
    }
    g.drawImageAt (backLayer_, (int) (cx - backLayer_.getWidth() * 0.5f),
                                  (int) (cy - backLayer_.getHeight() * 0.5f));

    // ---- Phase C-2：四态合成（Pro-MB Overlay{NORMAL,ACTIVE,DOWN,ROTATE}）----
    // 三个 Tween 取最大值作为"提亮量"，rotate 额外把帽转向暖色。
    // 原实现是 dragging_ ? 1.35f : 1.0f 硬跳；现在 120ms 渐变。
    const double nowMs = (double) juce::Time::getMillisecondCounter();
    const float hoverK  = hoverT_.value (nowMs);
    const float downK   = downT_.value (nowMs);
    const float rotateK = rotateT_.value (nowMs);
    const float glow    = juce::jmax (hoverK, juce::jmax (downK, rotateK));

    // ---- OVERLAY：值弧（轨道 + 值）----
    const float a0 = valueToAngle (min_);
    const float a1 = valueToAngle (max_);
    const float av = valueToAngle (value_);
    // 弧粗随 glow 渐变（1.0 → 1.35），不再是二态跳变
    const float thick = juce::jmax (2.0f, d * 0.10f) * (1.0f + 0.35f * glow);

    juce::Path track;
    track.addCentredArc (cx, cy, r - thick * 0.5f, r - thick * 0.5f, 0.0f, a0, a1, true);
    g.setColour (pal_.track.withAlpha (0.55f));   // BACK 已有圆体，轨道淡一点避免叠重
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path val;
    const float from = bipolar_ ? valueToAngle (default_) : a0;
    val.addCentredArc (cx, cy, r - thick * 0.5f, r - thick * 0.5f, 0.0f,
                       juce::jmin (from, av), juce::jmax (from, av), true);
    // 弧色随 glow 变亮（accent → accent.brighter(0.30)）
    g.setColour (pal_.accent.interpolatedWith (pal_.accent.brighter (0.30f), glow));
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // ---- 帽（近白）+ 深色指针 ----
    // 四态：NORMAL = knob；ACTIVE/DOWN = 逐级提亮；ROTATE = 转向暖色（accent 方向）
    const float capR = r - thick - juce::jmax (1.0f, d * 0.05f);
    juce::Colour capCol = pal_.knob;
    if (rotateK > 0.001f)
        capCol = capCol.interpolatedWith (pal_.accent.brighter (0.45f), rotateK * 0.55f);
    capCol = capCol.brighter (0.25f * hoverK + 0.15f * downK);
    juce::ColourGradient grad (capCol.brighter (0.12f), cx, cy - capR,
                               capCol.darker (0.15f), cx, cy + capR, false);
    g.setGradientFill (grad);
    g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

    const float ang = av;
    // 指针：与弧同一角度约定 —— (cx + R·sin(ang), cy − R·cos(ang))
    const float pR0 = capR * 0.22f;
    const float pR1 = r - thick * 0.5f;         // = 弧所在半径
    const float px1 = cx + std::sin (ang) * pR0;
    const float py1 = cy - std::cos (ang) * pR0;
    const float px2 = cx + std::sin (ang) * pR1;
    const float py2 = cy - std::cos (ang) * pR1;
    g.setColour (pal_.light ? juce::Colours::black.withAlpha (0.75f)
                            : juce::Colours::black.withAlpha (0.80f));
    g.drawLine (px1, py1, px2, py2, juce::jmax (1.5f, d * 0.07f));

    // ---- 数值读数（旋钮下方）----§5.2：偏移旋钮带 "Δ " 前缀
    juce::String txt = valuePrefix_ + juce::String (value_, decimals_);
    if (suffix_.isNotEmpty()) txt << " " << suffix_;
    g.setFont (MBTheme::font (MBTheme::regular(), juce::jmin (11.0f, d * 0.24f)));
    g.setColour (editing_ ? pal_.accent : pal_.text);
    g.drawText (txt, juce::Rectangle<float> (0.0f, kb.getBottom() + 1.0f, w, 14.0f),
                juce::Justification::centredTop);

    // ---- 编辑框（自绘内嵌输入）----
    if (editing_)
    {
        const float bw = juce::jmax (w, 78.0f), bh = 22.0f;
        auto box = juce::Rectangle<float> (w * 0.5f - bw * 0.5f, cy - bh * 0.5f, bw, bh);
        g.setColour (pal_.surface.withAlpha (0.97f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (pal_.accent);
        g.drawRoundedRectangle (box, 5.0f, 1.2f);
        g.setColour (pal_.text);
        g.setFont (MBTheme::font (MBTheme::regular(), 12.0f));
        // A2 修复：paint() 只读光标状态，不再翻转（翻转已移到 setEditCursorBlink）
        g.drawText (editText_ + (editCursorOn_ ? "|" : " "), box, juce::Justification::centred);
    }
}

void Knob::resized() {}
