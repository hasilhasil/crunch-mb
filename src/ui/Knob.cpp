#include "Knob.h"

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

float Knob::valueToAngle (double v) const
{
    const float norm = (float) ((v - min_) / (max_ - min_));
    const float start = juce::MathConstants<float>::pi * 0.75f;      // 135°
    const float end   = juce::MathConstants<float>::pi * 2.25f;      // 405°（=45°）
    return start + juce::jlimit (0.0f, 1.0f, norm) * (end - start);  // 270° 行程
}

// ---------------- 交互 ----------------
void Knob::commitValue (double v)
{
    if (interval_ > 0.0) v = std::round (v / interval_) * interval_;
    setValue (v, true);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (editing_) { closeEditor (true); return; }

    dragStartValue_ = value_;
    dragging_ = true;
    slowAccum_ = 0.0f;
    shiftFine_ = e.mods.isShiftDown();

    // Ctrl+click = 复位（Fablerler 官方：Ctrl/Cmd + click resets to default）
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        setValue (default_, true);
        dragging_ = false;
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
    slowAccum_ = 0.0f;
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    openEditor();
}

void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (editing_) return;
    const double span = max_ - min_;
    double step = (interval_ > 0.0) ? interval_ : span * 0.02;
    if (wheel.deltaY < 0) step = -step;
    commitValue (value_ + step);
}

// ---------------- 内嵌输入 ----------------
void Knob::openEditor()
{
    editing_ = true;
    editText_.clear();
    editCursorOn_ = true;
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
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
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || ch == '%')
    { editText_ += juce::String::charToString (ch); repaint(); return true; }
    return false;
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

    // ---- 弧（轨道 + 值）----§5.2：拖动中"联动脉冲"——弧加亮加粗
    const float a0 = valueToAngle (min_);
    const float a1 = valueToAngle (max_);
    const float av = valueToAngle (value_);
    const float thick = juce::jmax (2.0f, d * 0.10f) * (dragging_ ? 1.35f : 1.0f);

    juce::Path track;
    track.addCentredArc (cx, cy, r - thick * 0.5f, r - thick * 0.5f, 0.0f, a0, a1, true);
    g.setColour (pal_.track);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path val;
    const float from = bipolar_ ? valueToAngle (default_) : a0;
    val.addCentredArc (cx, cy, r - thick * 0.5f, r - thick * 0.5f, 0.0f,
                       juce::jmin (from, av), juce::jmax (from, av), true);
    g.setColour (dragging_ ? pal_.accent.brighter (0.30f) : pal_.accent);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // ---- 帽（近白）+ 深色指针 ----
    const float capR = r - thick - juce::jmax (1.0f, d * 0.05f);
    juce::ColourGradient grad (pal_.knob.brighter (0.25f), cx, cy - capR,
                               pal_.knob.darker (0.15f), cx, cy + capR, false);
    g.setGradientFill (grad);
    g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

    const float ang = av;
    const float px1 = cx + std::cos (ang) * capR * 0.28f;
    const float py1 = cy + std::sin (ang) * capR * 0.28f;
    const float px2 = cx + std::cos (ang) * capR * 0.82f;
    const float py2 = cy + std::sin (ang) * capR * 0.82f;
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
        editCursorOn_ = ! editCursorOn_;
        g.drawText (editText_ + (editCursorOn_ ? "|" : " "), box, juce::Justification::centred);
    }
}

void Knob::resized() {}
