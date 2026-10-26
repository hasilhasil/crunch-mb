#include "FreqInputEditor.h"
#include "EditorLayout.h"

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
        t = t.trim();

        // Phase（R16 修复）：空输入/纯空白时**放弃应用**，不要当成 0。
        // 此前空文本经 getFloatValue() 得 0，再被 jlimit(20,20000,0) 钳成
        // 20 Hz——用户双击输入框后直接按回车（本意是取消），频率被静默
        // 改到最低值。空输入应视为"无输入"，保持原值不变。
        if (t.isEmpty())
        {
            active_ = false;
            text_.clear();
            repaint();
            return;
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
    // 程序化构造的 KeyPress 可能不带 textCharacter，回退 keyCode（同 Knob R15）
    const juce::juce_wchar c = (ch != 0) ? ch : (juce::juce_wchar) key.getKeyCode();
    if ((c >= '0' && c <= '9') || c == '.' || c == 'k' || c == 'K')
    {
        // 限制长度：与 Knob 一致，防无限键入
        if (text_.length() < 8)
            text_ += juce::String::charToString (c);
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

    const EditorLayout l (getLocalBounds(), layout_);
    const float s  = l.scale;
    const float bw = 92.0f * s, bh = 28.0f * s;

    float tx = x_ - bw * 0.5f, ty = y_ - bh - 10.0f * s;
    tx = juce::jlimit (4.0f * s, juce::jmax (4.0f * s, l.plotRight() - bw - 4.0f * s), tx);
    if (ty < l.topHeight() + 4.0f * s)
        ty = y_ + 10.0f * s;

    auto box = juce::Rectangle<float> (tx, ty, bw, bh);
    if (theme_.isNeumorphic)
    {
        // 新拟物输入框：聚焦松弛内凹（insetFocus）+ 微强调光环
        UiPack::NeuPainter::drawRaisedRect (g, box.expanded (3.0f * s), theme_.radiusMd * s,
                                            pal_, theme_.shadowHover, s, 0.96f, false);
        UiPack::NeuPainter::drawInsetRect  (g, box, theme_.radiusSm * s,
                                            pal_, theme_.insetFocus, s, 1.0f, true);
    }
    else
    {
        g.setColour (pal_.surface.withAlpha (0.97f));
        g.fillRoundedRectangle (box, 5.0f * s);
        g.setColour (pal_.accent);
        g.drawRoundedRectangle (box, 5.0f * s, 1.2f * s);
    }

    // 文本 + 闪烁光标
    const juce::String label = text_ + (cursorVisible_ ? "|" : " ");
    g.setColour (pal_.text);
    g.setFont (MBTheme::font (MBTheme::uiMedium(), 13.0f * s));
    g.drawText (label, box, juce::Justification::centred);

    // 单位提示（来自当前激活语言包）
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::uiRegular(), 9.5f * s));
    g.drawText (hintText_,
                box.withY (box.getBottom() + 2.0f * s).withHeight (14.0f * s).expanded (30.0f * s, 0.0f),
                juce::Justification::centredTop);
}
