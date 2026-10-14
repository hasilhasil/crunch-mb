#include "OutputPanel.h"
#include "../PluginProcessor.h"

// ============================================================
// OutputPanel 实现
// ============================================================

namespace
{
    constexpr float kDesignW = 920.0f;
    constexpr float kDesignH = 620.0f;
    constexpr float kGlobalBarH = 32.0f;
    constexpr float kRowH = 22.0f;
    constexpr int   kNumRows = 3;   // OutputGain / GlobalMix / GlobalBypass
}

juce::Rectangle<float> OutputPanel::panelRect() const
{
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float pw = W * (180.0f / kDesignW);
    const float ph = H * ((kNumRows * kRowH + 14.0f) / kDesignH);
    // 右对齐到 BYPASS 胶囊左侧（与 OUT 读数同侧）
    const float px = W - W * (108.0f / kDesignW) - pw - W * (6.0f / kDesignW);
    const float py = H - H * (kGlobalBarH / kDesignH) - ph - H * (4.0f / kDesignH);
    return juce::Rectangle<float> (px, py, pw, ph);
}

void OutputPanel::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    repaint();
}

// ---------------- sticky 状态机（与 AnalyzerPanel / LinkedDrawer 同一套）----------------
void OutputPanel::hoverButton (bool hovering)
{
    if (sticky_)
        return;
    if (hovering == open_)
        return;
    open_ = hovering;
    tween_.start (juce::Time::getMillisecondCounter(), open_ ? 1.0f : 0.0f,
                  Anim::kFadeMs, anim_);
    animTarget_ = open_ ? 1.0f : 0.0f;
}

bool OutputPanel::clickButton()
{
    if (! sticky_)
    {
        sticky_ = true;
        open_ = true;
    }
    else
    {
        sticky_ = false;
        open_ = false;
    }
    tween_.start (juce::Time::getMillisecondCounter(), open_ ? 1.0f : 0.0f,
                  Anim::kFadeMs, anim_);
    animTarget_ = open_ ? 1.0f : 0.0f;
    return true;
}

void OutputPanel::clickOutside()
{
    if (! open_ && ! sticky_)
        return;
    open_ = false;
    sticky_ = false;
    tween_.start (juce::Time::getMillisecondCounter(), 0.0f, Anim::kFadeMs, anim_);
    animTarget_ = 0.0f;
}

bool OutputPanel::advance (double nowMs)
{
    anim_ = tween_.value (nowMs);
    if (std::abs (anim_ - animTarget_) < Anim::kSnapEps)
    {
        anim_ = animTarget_;
        tween_.snapTo (animTarget_);
    }
    return ! tween_.done (nowMs);
}

// ---------------- 行点击 ----------------
bool OutputPanel::hitTestRow (juce::Point<float> p, int& rowOut) const
{
    if (! open_)
        return false;
    const auto pr = panelRect();
    if (! pr.contains (p))
        return false;
    const float top  = pr.getY() + pr.getHeight() * (7.0f / (kNumRows * kRowH + 14.0f));
    const float rowH = pr.getHeight() * (kRowH / (kNumRows * kRowH + 14.0f));
    const int idx = (int) ((p.y - top) / rowH);
    if (idx < 0 || idx >= kNumRows)
        return false;
    rowOut = idx;
    return true;
}

void OutputPanel::clickRow (int row)
{
    switch (row)
    {
        case 0:  outputGainDb_ += 3.0f; if (outputGainDb_ > 36.0f)  outputGainDb_ = -36.0f; break;
        case 1:  globalMixPct_ += 25.0f; if (globalMixPct_ > 200.0f) globalMixPct_ = 0.0f;   break;
        case 2:  globalBypass_ = ! globalBypass_; break;
        default: break;
    }
    if (onSettingsChanged)
        onSettingsChanged();
    repaint();
}

// ---------------- 绘制 ----------------
void OutputPanel::paint (juce::Graphics& g)
{
    if (! open_ || anim_ < 0.004f)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    const auto pr = panelRect();

    // ---- Phase E-3：多层渐变投影 ----
    {
        const float corner = pr.getWidth() * 0.06f;
        struct Layer { float dy; float alpha; float expand; };
        const Layer layers[3] = {
            { 2.0f,  0.30f, 0.0f },
            { 6.0f,  0.20f, 3.0f },
            { 14.0f, 0.14f, 7.0f },
        };
        for (const auto& L : layers)
        {
            auto shadow = pr.translated (0.0f, L.dy).expanded (L.expand);
            g.setColour (juce::Colours::black.withAlpha (L.alpha * a));
            g.fillRoundedRectangle (shadow, corner + L.expand);
        }
    }
    g.setColour (pal_.popA.withMultipliedAlpha (a));
    g.fillRoundedRectangle (pr, pr.getWidth() * 0.06f);
    g.setColour (pal_.accent.withAlpha (0.55f * a));
    g.drawRoundedRectangle (pr, pr.getWidth() * 0.06f, 1.0f);

    const float rowH = pr.getHeight() * (kRowH / (kNumRows * kRowH + 14.0f));
    const float top  = pr.getY() + pr.getHeight() * (7.0f / (kNumRows * kRowH + 14.0f));

    struct Item { const char* label; juce::String value; bool on; };
    const Item items[kNumRows] = {
        { "Output Gain", juce::String (outputGainDb_, 1) + " dB", false },
        { "Mix",         juce::String ((int) globalMixPct_) + " %",  false },
        { "Global Bypass", globalBypass_ ? "ON" : "OFF",       globalBypass_ },
    };

    g.setFont (MBTheme::font (MBTheme::regular(), 9.5f));
    for (int i = 0; i < kNumRows; ++i)
    {
        const float y = top + rowH * (float) i;
        const bool on = items[i].on;
        if (on)
        {
            g.setColour (pal_.accent.withAlpha (0.16f * a));
            g.fillRoundedRectangle (juce::Rectangle<float> (pr.getX() + pr.getWidth() * 0.04f, y,
                                                            pr.getWidth() * 0.92f, rowH * 0.9f),
                                    pr.getWidth() * 0.03f);
        }
        g.setColour (on ? pal_.text : pal_.dim);
        g.drawText (items[i].label,
                    juce::Rectangle<float> (pr.getX() + pr.getWidth() * 0.10f, y,
                                            pr.getWidth() * 0.52f, rowH),
                    juce::Justification::centredLeft);
        g.setColour (on ? pal_.accent : pal_.text);
        g.drawText (items[i].value,
                    juce::Rectangle<float> (pr.getX() + pr.getWidth() * 0.55f, y,
                                            pr.getWidth() * 0.38f, rowH),
                    juce::Justification::centredRight);
    }
}
