#include "FreqInputEditor.h"

// ============================================================
// FreqInputEditor 实现（从 DisplayView 拆出）
// ============================================================

void FreqInputEditor::open (int divider, int band, float x, float y)
{
    active_ = true;
    divider_ = divider;
    band_    = band;
    x_ = x;
    y_ = y;
    text_.clear();
    cursorVisible_ = true;
    lastBlinkMs_ = juce::Time::getMillisecondCounter();
    repaint();
}

void FreqInputEditor::close (bool apply)
{
    if (! active_)
        return;
    if (apply && onApply)
    {
        // 支持简写：'1k' → 1000
        juce::String t = text_;
        float mult = 1.0f;
        if (t.containsChar ('k') || t.containsChar ('K'))
        {
            mult = 1000.0f;
            t = t.replaceCharacter ('k', ' ').replaceCharacter ('K', ' ');
        }
        const float f = juce::jlimit (20.0f, 20000.0f, t.getFloatValue() * mult);
        onApply (divider_, band_, f);
    }
    active_ = false;
    text_.clear();
    repaint();
}

bool FreqInputEditor::keyPressed (const juce::KeyPress& key)
{
    if (! active_)
        return false;

    if (key == juce::KeyPress::escapeKey) { close (false); return true; }
    if (key == juce::KeyPress::returnKey)  { close (true);  return true; }
    if (key == juce::KeyPress::backspaceKey)
    {
        text_ = text_.substring (0, juce::jmax (0, text_.length() - 1));
        repaint();
        return true;
    }
    const juce::juce_wchar ch = key.getTextCharacter();
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'k' || ch == 'K')
    {
        text_ += juce::String::charToString (ch);
        repaint();
        return true;
    }
    return false;
}

void FreqInputEditor::tickCursor (juce::uint32 nowMs)
{
    if (! active_)
        return;
    if (lastBlinkMs_ == 0)
        lastBlinkMs_ = nowMs;
    if ((int) (nowMs - lastBlinkMs_) >= (int) Anim::kBlinkMs)
    {
        cursorVisible_ = ! cursorVisible_;
        lastBlinkMs_ = nowMs;
        repaint();
    }
}

void FreqInputEditor::paint (juce::Graphics& g)
{
    if (! active_)
        return;

    // 以 920×620 设计稿为基准等比缩放（与 DisplayView 的 sx/sy 一致）
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    const float bw = W * (72.0f / 920.0f), bh = H * (24.0f / 620.0f);

    float tx = x_ - bw * 0.5f, ty = y_ - bh - H * (10.0f / 620.0f);
    tx = juce::jlimit (2.0f, W - bw - 2.0f, tx);
    if (ty < H * (30.0f / 620.0f))     // 顶栏高度
        ty = y_ + H * (10.0f / 620.0f);

    auto box = juce::Rectangle<float> (tx, ty, bw, bh);
    g.setColour (pal_.surface.withAlpha (0.97f));
    g.fillRoundedRectangle (box, W * (5.0f / 920.0f));
    g.setColour (pal_.accent);
    g.drawRoundedRectangle (box, W * (5.0f / 920.0f), 1.2f);

    // 文本 + 闪烁光标
    const juce::String label = text_ + (cursorVisible_ ? "|" : " ");
    g.setColour (pal_.text);
    g.setFont (MBTheme::font (MBTheme::regular(), 11.0f));
    g.drawText (label, box, juce::Justification::centred);

    // 单位提示
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::regular(), 8.0f));
    g.drawText ("Hz  (Enter \xE7\xA1\xAE\xE8\xAE\xA4 / Esc \xE5\x8F\x96\xE6\xB6\x88)",
                box.withY (box.getBottom() + 1.0f).withHeight (H * (11.0f / 620.0f)),
                juce::Justification::centredTop);
}
