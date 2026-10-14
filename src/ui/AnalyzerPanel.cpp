#include "AnalyzerPanel.h"
#include "../PluginProcessor.h"

// ============================================================
// AnalyzerPanel 实现
// ============================================================

namespace
{
    constexpr float kDesignW = 920.0f;
    constexpr float kDesignH = 620.0f;
    constexpr float kGlobalBarH = 32.0f;
    constexpr float kRowH = 20.0f;      // 每行高度（设计稿像素）
    constexpr int   kNumRows = 14;      // Pre/Post/4 Res/3 Speed/4 Tilt/Freeze
}

juce::Rectangle<float> AnalyzerPanel::panelRect() const
{
    // 贴在 global bar 上方、水平居中于 Analyzer 按钮（与 hitTestAnalyzerButton 同位）
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float pw = W * (190.0f / kDesignW);
    const float ph = H * ((kNumRows * kRowH + 16.0f) / kDesignH);
    const float px = W * 0.5f - W * (150.0f / kDesignW) + W * (52.0f / kDesignW) - pw * 0.5f;
    const float py = H - H * (kGlobalBarH / kDesignH) - ph - H * (4.0f / kDesignH);
    return juce::Rectangle<float> (px, py, pw, ph);
}

void AnalyzerPanel::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    repaint();
}

// ---------------- 状态机（Phase D：Pro-MB sticky 语义）----------------
void AnalyzerPanel::hoverButton (bool hovering)
{
    // 只有**非 sticky** 时才跟随悬停开合。
    // sticky 面板已经"钉住"，悬停不影响它。
    if (sticky_)
        return;
    if (hovering == open_)
        return;
    open_ = hovering;
    tween_.start (juce::Time::getMillisecondCounter(), open_ ? 1.0f : 0.0f,
                  Anim::kFadeMs, anim_);
    animTarget_ = open_ ? 1.0f : 0.0f;
}

bool AnalyzerPanel::clickButton()
{
    if (! sticky_)
    {
        // 第一次点击：变 sticky 并保持打开
        sticky_ = true;
        open_ = true;
    }
    else
    {
        // 已 sticky：再点 = 收起并解除 sticky
        sticky_ = false;
        open_ = false;
    }
    tween_.start (juce::Time::getMillisecondCounter(), open_ ? 1.0f : 0.0f,
                  Anim::kFadeMs, anim_);
    animTarget_ = open_ ? 1.0f : 0.0f;
    return true;
}

void AnalyzerPanel::clickOutside()
{
    if (! open_)
        return;
    open_ = false;
    sticky_ = false;                 // 点外面 = 完全收起（连 sticky 一起解除）
    tween_.start (juce::Time::getMillisecondCounter(), 0.0f, Anim::kFadeMs, anim_);
    animTarget_ = 0.0f;
}

bool AnalyzerPanel::advance (double nowMs)
{
    anim_ = tween_.value (nowMs);
    if (std::abs (anim_ - animTarget_) < Anim::kSnapEps)
    {
        anim_ = animTarget_;
        tween_.snapTo (animTarget_);
    }
    return ! tween_.done (nowMs);
}

// ---------------- 行布局与点击 ----------------
void AnalyzerPanel::layoutRows() {}

bool AnalyzerPanel::hitTestRow (juce::Point<float> p, int& rowOut) const
{
    if (! open_)
        return false;
    const auto pr = panelRect();
    if (! pr.contains (p))
        return false;
    // 行区从面板顶部 8px 开始，每行 kRowH
    const float top = pr.getY() + pr.getHeight() * (8.0f / (kNumRows * kRowH + 16.0f));
    const float rowH = pr.getHeight() * (kRowH / (kNumRows * kRowH + 16.0f));
    const int idx = (int) ((p.y - top) / rowH);
    if (idx < 0 || idx >= kNumRows)
        return false;
    rowOut = idx;
    return true;
}

void AnalyzerPanel::clickRow (int row)
{
    switch (row)
    {
        case 0:  preOn_  = ! preOn_;  break;
        case 1:  postOn_ = ! postOn_; break;
        case 2:  fftSize_ = 1024; break;
        case 3:  fftSize_ = 2048; break;
        case 4:  fftSize_ = 4096; break;
        case 5:  fftSize_ = 8192; break;
        case 6:  speed_ = 0; break;
        case 7:  speed_ = 1; break;
        case 8:  speed_ = 2; break;
        case 9:  tilt_ = 0.0f; break;
        case 10: tilt_ = 1.5f; break;
        case 11: tilt_ = 3.0f; break;
        case 12: tilt_ = 4.5f; break;
        case 13: frozen_ = ! frozen_; break;
        default: break;
    }
    if (onSettingsChanged)
        onSettingsChanged();
    repaint();
}

// ---------------- 绘制 ----------------
void AnalyzerPanel::paint (juce::Graphics& g)
{
    if (! open_ || anim_ < 0.004f)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    const auto pr = panelRect();

    // 面板：深色圆角 + accent 描边（Pro-MB 的深灰圆角设置面板）
    g.setColour (juce::Colours::black.withAlpha (0.35f * a));
    g.fillRoundedRectangle (pr.translated (0.0f, pr.getHeight() * (3.0f / pr.getHeight())),
                            pr.getWidth() * 0.06f);
    g.setColour (pal_.popA.withMultipliedAlpha (a));
    g.fillRoundedRectangle (pr, pr.getWidth() * 0.06f);
    g.setColour (pal_.accent.withAlpha (0.55f * a));
    g.drawRoundedRectangle (pr, pr.getWidth() * 0.06f, 1.0f);

    const float rowH = pr.getHeight() * (kRowH / (kNumRows * kRowH + 16.0f));
    const float top  = pr.getY() + pr.getHeight() * (8.0f / (kNumRows * kRowH + 16.0f));

    struct Item { const char* label; bool on; bool isHeader; };
    const Item items[kNumRows] = {
        { "Pre spectrum",              preOn_,                 false },
        { "Post spectrum",             postOn_,                false },
        { "Resolution  Low (1024)",    fftSize_ == 1024,       false },
        { "Resolution  Medium (2048)", fftSize_ == 2048,       false },
        { "Resolution  High (4096)",   fftSize_ == 4096,       false },
        { "Resolution  Max (8192)",    fftSize_ == 8192,       false },
        { "Speed  Fast",               speed_ == 0,            false },
        { "Speed  Medium",             speed_ == 1,            false },
        { "Speed  Slow",               speed_ == 2,            false },
        { "Tilt  Off (0 dB/oct)",      std::abs (tilt_) < 0.01f, false },
        { "Tilt  1.5 dB/oct",          std::abs (tilt_ - 1.5f) < 0.01f, false },
        { "Tilt  3.0 dB/oct",          std::abs (tilt_ - 3.0f) < 0.01f, false },
        { "Tilt  4.5 dB/oct",          std::abs (tilt_ - 4.5f) < 0.01f, false },
        { "Freeze",                    frozen_,                false },
    };

    g.setFont (MBTheme::font (MBTheme::regular(), 9.5f));
    for (int i = 0; i < kNumRows; ++i)
    {
        const float y = top + rowH * (float) i;
        const bool on = items[i].on;
        // 选中行：accent 淡底 + 左侧小圆点
        if (on)
        {
            g.setColour (pal_.accent.withAlpha (0.16f * a));
            g.fillRoundedRectangle (juce::Rectangle<float> (pr.getX() + pr.getWidth() * 0.04f, y,
                                                            pr.getWidth() * 0.92f, rowH * 0.9f),
                                    pr.getWidth() * 0.03f);
        }
        g.setColour (on ? pal_.text : pal_.dim);
        g.drawText (items[i].label,
                    juce::Rectangle<float> (pr.getX() + pr.getWidth() * 0.12f, y,
                                            pr.getWidth() * 0.84f, rowH),
                    juce::Justification::centredLeft);
        // 左侧圆点（选中=实心 accent，未选=空心 gridZ）
        const float dotR = juce::jmax (2.0f, pr.getWidth() * 0.014f);
        const float dx = pr.getX() + pr.getWidth() * 0.075f;
        const float dy = y + rowH * 0.5f;
        if (on)
        {
            g.setColour (pal_.accent.withAlpha (0.95f * a));
            g.fillEllipse (dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
        }
        else
        {
            g.setColour (pal_.gridZ.withMultipliedAlpha (a));
            g.drawEllipse (dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f, 1.0f);
        }
    }
}
