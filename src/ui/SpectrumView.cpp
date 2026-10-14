#include "SpectrumView.h"
#include "../PluginProcessor.h"

// ============================================================
// SpectrumView 实现（从 DisplayView 拆出）
// ============================================================

SpectrumView::SpectrumView (CrunchMBAudioProcessor& processor)
    : proc_ (processor)
{
    // 注意：不 setInterceptsMouseClicks —— 本类不是 Component。
}

SpectrumView::~SpectrumView() = default;

void SpectrumView::setCoordMapper (std::function<float (float)> freqToX,
                                   std::function<float (float)> dbToY,
                                   std::function<float ()> bottomY)
{
    freqToX_ = std::move (freqToX);
    dbToY_   = std::move (dbToY);
    bottomY_ = std::move (bottomY);
    pathsValid_ = false;   // 映射换了，Path 必须重建
}

bool SpectrumView::updateFromEngine()
{
    const bool changed = proc_.getSpectrumEngine().getLatest (pre_, post_);
    if (! changed)
        return false;
    if (freqs_.size() != (size_t) kPoints)
        proc_.getSpectrumEngine().getFrequencies (freqs_);
    hasFrame_ = true;
    pathsValid_ = false;
    refreshPaths();
    return true;
}

void SpectrumView::refreshPaths()
{
    if (! hasFrame_ || freqs_.size() != (size_t) kPoints)
        return;

    const float bottom = bottomY_ ? bottomY_() : 0.0f;
    const auto build = [&] (const std::array<float, kPoints>& levels,
                            juce::Path& fill, juce::Path& stroke)
    {
        fill.clear();
        stroke.clear();
        for (int i = 0; i < kPoints; ++i)
        {
            const float x = freqToX_ ? freqToX_ (freqs_[(size_t) i]) : 0.0f;
            const float y = dbToY_   ? dbToY_ (levels[(size_t) i])   : 0.0f;
            if (i == 0) { fill.startNewSubPath (x, y); stroke.startNewSubPath (x, y); }
            else        { fill.lineTo (x, y);          stroke.lineTo (x, y); }
        }
        fill.lineTo (freqToX_ ? freqToX_ (freqs_.back())  : 0.0f, bottom);
        fill.lineTo (freqToX_ ? freqToX_ (freqs_.front()) : 0.0f, bottom);
        fill.closeSubPath();
    };

    build (pre_,  preFill_,  preStroke_);
    build (post_, postFill_, postStroke_);
    pathsValid_ = true;
}

void SpectrumView::paint (juce::Graphics& g)
{
    // ⚠️ guard 只用"内容为空"判断，不用 pathsValid_ 脏标志——
    // 2026-09-23 修复：脏标志为 true 的那一帧会整层不画，但 Path 里还有上一帧
    // 几何可用 → 用户看到"曲线/频谱偶发消失一瞬间"。
    if (preFill_.isEmpty() && preStroke_.isEmpty())
        return;

    // B1（用户验收指定）：主题色(accent) = Pre，灰(inFill) = Post
    if (preOn_)
    {
        g.setColour (pal_.accent.withAlpha (0.30f));
        g.fillPath (preFill_);
        g.setColour (pal_.accent.withAlpha (0.95f));
        g.strokePath (preStroke_, juce::PathStrokeType (2.0f));
    }
    if (postOn_)
    {
        g.setColour (pal_.inFill.withAlpha (0.20f));
        g.fillPath (postFill_);
        g.setColour (pal_.inFill.withAlpha (0.80f));
        g.strokePath (postStroke_, juce::PathStrokeType (1.5f));
    }
}
