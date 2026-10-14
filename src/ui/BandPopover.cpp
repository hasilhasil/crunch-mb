#include "BandPopover.h"
#include "../PluginProcessor.h"

// ============================================================
// BandPopover 实现（UI_SPEC §6，Pro-MB 样式）
// 从 DisplayView 拆出：约 −400 行。DisplayView 只负责开合仲裁与命中分发。
// ============================================================

namespace
{
    // 设计稿基准（与 mockup v9 一致）
    constexpr float kDesignW = 920.0f;
    constexpr float kDesignH = 620.0f;
    constexpr float kTopStripH = 30.0f;
    constexpr float kGlobalBarH = 32.0f;
    constexpr float kDrawerBtnH = 29.0f;
}

BandPopover::BandPopover (CrunchMBAudioProcessor& processor)
    : juce::Component ("CrunchMBBandPopover"), proc_ (processor)
{
    setInterceptsMouseClicks (false, false);   // 命中由 DisplayView 统一分发
    buildParamCache();
}

BandPopover::~BandPopover() = default;

void BandPopover::buildParamCache()
{
    paramMap_.clear();
    for (auto* p : proc_.getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            paramMap_[pw->getParameterID()] = pw;
}

void BandPopover::setTheme (MBTheme::Theme t)
{
    pal_ = MBTheme::palette (t);
    for (auto& kb : knobs_)
        if (kb != nullptr) kb->setPalette (pal_);
    repaint();
}

void BandPopover::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    layoutKnobs (true);
    repaint();
}

void BandPopover::setBand (int band)
{
    if (band == band_)
        return;
    band_ = juce::jlimit (0, 3, band);
    syncFromParams();
    layoutKnobs (true);
    repaint();
}

void BandPopover::setBandEdges (float leftX, float rightX)
{
    bandLeftX_ = leftX;
    bandRightX_ = rightX;
    layoutKnobs (true);
    repaint();
}

// ---------------- 几何 ----------------
juce::Rectangle<float> BandPopover::popoverRect() const
{
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float pw = W * (472.0f / kDesignW);
    const float ph = H * (200.0f / kDesignH);

    // 随分段居中：取该段左右边界的像素中点
    const float midX = (bandLeftX_ + bandRightX_) * 0.5f;
    float px = midX - pw * 0.5f;
    px = juce::jlimit (W * (8.0f / kDesignW), W - pw - W * (8.0f / kDesignW), px);

    const float plotTop    = H * (kTopStripH / kDesignH);
    const float plotBottom = H - H * (kGlobalBarH / kDesignH) - H * (kDrawerBtnH / kDesignH);
    // 垂直位置：绘图区 58% 处
    const float py = plotTop + (plotBottom - plotTop) * 0.58f - ph * 0.5f;
    return juce::Rectangle<float> (px, py, pw, ph);
}

juce::Rectangle<float> BandPopover::popKnobRect (int k) const
{
    const auto r = popoverRect();
    const float unitW = (r.getWidth() - r.getWidth() * (16.0f / 472.0f)) / (float) kPopKnobs;
    const float x = r.getX() + r.getWidth() * (8.0f / 472.0f) + unitW * (float) k + 2.0f;
    return juce::Rectangle<float> (x, r.getY() + r.getHeight() * (34.0f / 200.0f),
                                   unitW - 4.0f, r.getHeight() * (74.0f / 200.0f));
}

juce::Rectangle<float> BandPopover::popSliderRect (int k) const
{
    const auto r = popoverRect();
    const float rowW = (r.getWidth() - r.getWidth() * (20.0f / 472.0f)) / 3.0f;
    const float x0 = r.getX() + r.getWidth() * (10.0f / 472.0f) + rowW * (float) k;
    return juce::Rectangle<float> (x0, r.getY() + r.getHeight() * (142.0f / 200.0f),
                                   rowW - r.getWidth() * (10.0f / 472.0f),
                                   r.getHeight() * (22.0f / 200.0f));
}

juce::Rectangle<float> BandPopover::expertRect() const
{
    // Phase C-5：EXPERT 从底部横排改成**右缘竖排标签**（Pro-MB 的标志性布局）。
    // 原来占面板左下角一块，现在占右缘一条窄带，点击展开的内容也改成
    // 从右缘向左展开，不占用面板底部原本属于旋钮/滑杆的空间。
    const auto r = popoverRect();
    const float tabW = r.getWidth() * (22.0f / 472.0f);
    return juce::Rectangle<float> (r.getRight() - tabW, r.getY() + r.getHeight() * (34.0f / 200.0f),
                                   tabW, r.getHeight() * (100.0f / 200.0f));
}

juce::Rectangle<float> BandPopover::expertContentRect() const
{
    const auto r = popoverRect();
    const auto er = expertRect();
    const float ew = r.getWidth() * (200.0f / 472.0f);
    const float eh = r.getHeight() * (60.0f / 200.0f);
    return juce::Rectangle<float> (er.getX() - ew - r.getWidth() * (6.0f / 472.0f),
                                   er.getY(), ew, eh);
}

// ---------------- Phase C-6：Dynamics 分段按钮 ----------------
// Pro-MB 的 segmented control：两段等宽，选中段用 accent 底 + 白字，
// 未选中段透明底 + dim 字。点击翻转 Compress/Expand。
juce::Rectangle<float> BandPopover::dynamicsSegRect() const
{
    const auto r = popoverRect();
    // 放在滑杆行上方、旋钮行下方（r 的 118/200 处），宽约为面板的 40%
    const float w = r.getWidth() * (188.0f / 472.0f);
    const float h = r.getHeight() * (20.0f / 200.0f);
    const float x = r.getX() + r.getWidth() * (10.0f / 472.0f);
    const float y = r.getY() + r.getHeight() * (118.0f / 200.0f);
    return juce::Rectangle<float> (x, y, w, h);
}

bool BandPopover::hitTestDynamicsSeg (juce::Point<float> p) const
{
    return open_ && dynamicsSegRect().contains (p);
}

bool BandPopover::clickDynamicsSeg()
{
    dynamicsExpand_ = ! dynamicsExpand_;
    const int b = (band_ >= 0) ? band_ : 0;
    setParamNorm ("band" + juce::String (b) + "Dynamics", dynamicsExpand_ ? 1.0f : 0.0f);
    repaint();
    return true;
}

BandPopover::ExpertRow BandPopover::hitTestExpertRow (juce::Point<float> p) const
{
    if (! open_ || ! expertOpen_)
        return ExpertRow::None;
    const auto box = expertContentRect();
    if (! box.contains (p))
        return ExpertRow::None;
    // 两块等分：上 = Detector，下 = Lookahead
    return (p.y < box.getCentreY()) ? ExpertRow::Detector : ExpertRow::Lookahead;
}

void BandPopover::clickExpertRow (ExpertRow row)
{
    const int b = (band_ >= 0) ? band_ : 0;
    switch (row)
    {
        case ExpertRow::Detector:
            // 翻转 Peak/RMS（与滑杆 k=3 同语义）
            applySlider (3, sliders_[3] > 0.5 ? 0.0 : 1.0);
            break;
        case ExpertRow::Lookahead:
            // Off → 1ms → 3ms 循环（lookahead 是全局参数，k=4 只显示不在此写；
            // 这里显式循环全局 lookahead 参数）
            {
                const int cur = (int) juce::roundToInt (
                    getParamNorm ("lookahead") * 2.0f);
                const int next = (cur + 1) % 3;
                setParamNorm ("lookahead", next * 0.5f);
                sliders_[4] = next;
            }
            break;
        case ExpertRow::None:
            break;
    }
    (void) b;
    repaint();
}

// ---------------- 开合 / 动画 ----------------
void BandPopover::setOpen (bool open)
{
    if (open == open_)
        return;
    open_ = open;
    if (open)
    {
        if (band_ < 0) band_ = 0;
        ensureKnobs();
        syncFromParams();
    }
    tween_.start (juce::Time::getMillisecondCounter(), open ? 1.0f : 0.0f,
                  Anim::kPopoverMs, anim_);
    animTarget_ = open ? 1.0f : 0.0f;
    layoutKnobs (false);
    repaint();
}

bool BandPopover::advance (double nowMs)
{
    anim_ = tween_.value (nowMs);
    if (std::abs (anim_ - animTarget_) < Anim::kSnapEps)
    {
        anim_ = animTarget_;
        tween_.snapTo (animTarget_);
    }
    layoutKnobs (false);
    if (! open_ && anim_ <= Anim::kSnapEps)
        for (auto& kb : knobs_)
            if (kb != nullptr) kb->setVisible (false);
    return ! tween_.done (nowMs);
}

void BandPopover::snapToCurrent()
{
    anim_ = animTarget_ = open_ ? 1.0f : 0.0f;
    tween_.snapTo (anim_);
    if (open_) { ensureKnobs(); syncFromParams(); }
    layoutKnobs (true);
}

// ---------------- 旋钮 ----------------
void BandPopover::ensureKnobs()
{
    if (knobsReady_)
        return;

    // 参数范围与显示格式（§6.2：旋钮下显示范围标注）
    struct Cfg { double mn, mx, iv, def; int dec; const char* suf; const char* label; };
    const Cfg cfgs[kPopKnobs] = {
        { -60.0,    0.0, 0.1, -20.0, 1, "dB", "THRESHOLD" },
        { -30.0,   30.0, 0.1,  -6.0, 1, "dB", "RANGE"     },
        {   0.1,  200.0, 0.1,  10.0, 1, "ms", "ATTACK"    },
        {  10.0, 1000.0, 0.1, 100.0, 1, "ms", "RELEASE"   },
        {   0.0,  100.0, 1.0, 100.0, 0, "%",  "MIX"       },
    };

    for (int k = 0; k < kPopKnobs; ++k)
    {
        auto* kb = new Knob();
        kb->setRange (cfgs[k].mn, cfgs[k].mx, cfgs[k].iv);
        kb->setDefaultValue (cfgs[k].def);
        kb->setValue (cfgs[k].def, false);
        kb->setLabel (cfgs[k].label);
        kb->setValueSuffix (cfgs[k].suf);
        kb->setDecimals (cfgs[k].dec);
        kb->setKnobDiameter (38.0f);
        kb->setShowLabel (true);
        kb->setPalette (pal_);
        const int kk = k;
        kb->onValueChange = [this, kk] (double v) { applyPopoverKnob (kk, v); };
        addAndMakeVisible (kb);
        kb->setVisible (false);
        knobs_[(size_t) k].reset (kb);
    }
    knobsReady_ = true;
}

void BandPopover::layoutKnobs (bool force)
{
    if (! knobsReady_)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    for (int k = 0; k < kPopKnobs; ++k)
    {
        if (knobs_[(size_t) k] == nullptr)
            continue;
        const auto rc = popKnobRect (k);
        const float y = rc.getY() + (1.0f - a) * rc.getHeight();
        const int nx = (int) rc.getX(), ny = (int) y;
        auto* kb = knobs_[(size_t) k].get();
        if (force || kb->getX() != nx || kb->getY() != ny
                 || kb->getWidth() != (int) rc.getWidth() || kb->getHeight() != (int) rc.getHeight())
            kb->setBounds (nx, ny, (int) rc.getWidth(), (int) rc.getHeight());
        kb->setVisible (open_ || anim_ > 0.001f);
    }
    if (! open_ && anim_ <= 0.001f)
        for (auto& kb : knobs_)
            if (kb != nullptr) kb->setVisible (false);
}

void BandPopover::applyPopoverKnob (int k, double v)
{
    const int b = (band_ >= 0) ? band_ : 0;
    static const juce::String ids[kPopKnobs] = {
        "Threshold", "Range", "Attack", "Release", "Mix"
    };
    setParamPhysical ("band" + juce::String (b) + ids[juce::jlimit (0, kPopKnobs - 1, k)], (float) v);
    syncFromParams();
}

// ---------------- 滑杆 ----------------
void BandPopover::beginSliderDrag (int k, float mouseX)
{
    sliderDrag_ = k;
    sliderDragX0_ = mouseX;
    sliderDragV0_ = sliders_[(size_t) juce::jlimit (0, 4, k)];
}

void BandPopover::dragSlider (int k, float mouseX)
{
    if (sliderDrag_ != k)
        return;
    const float w = popSliderRect (k).getWidth();
    float frac = (float) sliderDragV0_ + (mouseX - sliderDragX0_) / juce::jmax (1.0f, w);
    frac = juce::jlimit (0.0f, 1.0f, frac);

    if (k == 0)
        applySlider (0, 1.0 + (double) frac * 19.0);      // ratio 1..20
    else
        applySlider (k, (double) frac);                    // knee 0..48 dB（归一化）
}

void BandPopover::endSliderDrag()
{
    sliderDrag_ = -1;
}

void BandPopover::applySlider (int k, double v)
{
    sliders_[(size_t) juce::jlimit (0, 4, k)] = v;
    const int b = (band_ >= 0) ? band_ : 0;
    switch (k)
    {
        case 0:  setParamPhysical (Param::bandRatio (b), (float) v);  break;
        // 2026-10-14：Knee 连续 0–48 dB，直接写归一化值
        case 1:  setParamNorm ("band" + juce::String (b) + "Knee", (float) v);  break;
        case 2:  setParamNorm ("band" + juce::String (b) + "Ext",  v > 0.5 ? 1.0 : 0.0);     break;
        case 3:  setParamNorm ("band" + juce::String (b) + "Detector", v > 0.5 ? 1.0 : 0.0); break;
        default: break;   // Lookahead 是全局参数（k=4 不在此写）
    }
    syncFromParams();
}

// ---------------- 参数同步 ----------------
void BandPopover::syncFromParams()
{
    const int b = (band_ >= 0) ? band_ : 0;
    static const juce::String ids[kPopKnobs] = {
        "Threshold", "Range", "Attack", "Release", "Mix"
    };
    for (int k = 0; k < kPopKnobs; ++k)
    {
        const juce::String id = "band" + juce::String (b) + ids[k];
        values_[(size_t) k] = getParamPhysical (id);
        normValues_[(size_t) k] = juce::jlimit (0.0f, 1.0f,
            (float) ((values_[(size_t) k] - paramRangeMinOf (id))
                   / (double) juce::jmax (1.0e-6f, paramRangeMaxOf (id) - paramRangeMinOf (id))));
    }
    for (int k = 0; k < kPopKnobs; ++k)
        if (knobs_[(size_t) k] != nullptr)
            knobs_[(size_t) k]->setValue (values_[(size_t) k], false);

    sliders_[0] = getParamPhysical (Param::bandRatio (b));
    sliders_[1] = getParamNorm ("band" + juce::String (b) + "Knee");
    sliders_[2] = getParamNorm ("band" + juce::String (b) + "Ext");
    sliders_[3] = getParamNorm ("band" + juce::String (b) + "Detector");
    sliders_[4] = getParamNorm ("lookahead") * 2.0f;   // 全局 lookahead 的索引 0..2
    dynamicsExpand_ = sliders_[3] >= 0.0f && getParamNorm ("band" + juce::String (b) + "Dynamics") > 0.5f;
}

void BandPopover::tickEditCursors (juce::uint32 nowMs)
{
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->setEditCursorBlink (nowMs);
}

void BandPopover::advanceKnobStates (double nowMs)
{
    // Phase C-2：四态渐变（旋钮无自有 Timer，由宿主 60fps 定时器统一推进）
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->advanceStates (nowMs);
}

// ---------------- 命中测试 ----------------
bool BandPopover::hitTestClose (juce::Point<float> p) const
{
    const auto r = popoverRect();
    const float s = r.getWidth() * (22.0f / 472.0f);
    return juce::Rectangle<float> (r.getRight() - s - r.getWidth() * (10.0f / 472.0f),
                                   r.getY() + r.getHeight() * (8.0f / 200.0f), s, s)
             .contains (p);
}

bool BandPopover::hitTestPanel (juce::Point<float> p) const
{
    return open_ && popoverRect().contains (p);
}

bool BandPopover::hitTestSlider (juce::Point<float> p, int& kOut) const
{
    if (! open_)
        return false;
    for (int k = 0; k < 3; ++k)
        if (popSliderRect (k).contains (p)) { kOut = k; return true; }
    return false;
}

// ---------------- 绘制 ----------------
void BandPopover::paint (juce::Graphics& g)
{
    if (! open_ || anim_ < 0.004f)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    const auto r = popoverRect();

    // 半透明玻璃面板 + 阴影边
    g.setColour (juce::Colours::black.withAlpha (0.35f * a));
    g.fillRoundedRectangle (r.translated (0.0f, r.getHeight() * (3.0f / 200.0f)),
                            r.getWidth() * (12.0f / 472.0f));
    g.setColour (pal_.popA.withMultipliedAlpha (a));
    g.fillRoundedRectangle (r, r.getWidth() * (12.0f / 472.0f));
    g.setColour (pal_.accent.withAlpha (0.5f * a));
    g.drawRoundedRectangle (r, r.getWidth() * (12.0f / 472.0f), 1.0f);

    // ---- 顶栏：⚡BYP 宏 + 段名 + 频率范围 + ✕ ----
    {
        const float boltS = r.getWidth() * (20.0f / 472.0f);
        auto bolt = juce::Rectangle<float> (r.getX() + r.getWidth() * (10.0f / 472.0f),
                                            r.getY() + r.getHeight() * (7.0f / 200.0f),
                                            boltS, boltS);
        g.setColour (pal_.track);
        g.fillRoundedRectangle (bolt, r.getWidth() * (5.0f / 472.0f));
        g.setColour (pal_.accent);
        g.setFont (MBTheme::font (MBTheme::regular(), 11.0f));
        g.drawText (juce::String::fromUTF8 ("\xE2\x9A\xA1"), bolt, juce::Justification::centred);

        static const char* kNames[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };
        g.setColour (pal_.text);
        g.setFont (MBTheme::font (MBTheme::semiBold(), 12.0f));
        g.drawText (juce::String (kNames[juce::jlimit (0, 3, band_)]) + "  "
                    + fmtHzShort (bandLeftX_) + " - " + fmtHzShort (bandRightX_),
                    juce::Rectangle<float> (bolt.getRight() + r.getWidth() * (8.0f / 472.0f),
                                            r.getY() + r.getHeight() * (7.0f / 200.0f),
                                            r.getWidth() * (300.0f / 472.0f),
                                            r.getHeight() * (20.0f / 200.0f)),
                    juce::Justification::centredLeft);

        const float cs = r.getWidth() * (22.0f / 472.0f);
        auto closeBtn = juce::Rectangle<float> (r.getRight() - cs - r.getWidth() * (10.0f / 472.0f),
                                                r.getY() + r.getHeight() * (8.0f / 200.0f), cs, cs);
        g.setColour (pal_.track);
        g.fillRoundedRectangle (closeBtn, r.getWidth() * (6.0f / 472.0f));
        g.setColour (pal_.text);
        g.drawText ("\xE2\x9C\x95", closeBtn, juce::Justification::centred);

        // 分隔线
        g.setColour (pal_.grid);
        g.fillRect (r.getX() + r.getWidth() * (10.0f / 472.0f),
                    r.getY() + r.getHeight() * (30.0f / 200.0f),
                    r.getWidth() - r.getWidth() * (20.0f / 472.0f), 1.0f);
    }

    // ---- 旋钮行底板 + 范围标注（弧/帽/指针由 Knob 子组件绘制）----
    static const char* kRange[5] = { "-60..0 dB", "-30..30 dB", "0..200 ms", "10..1000 ms", "0..100 %" };
    const float unitW = (r.getWidth() - r.getWidth() * (16.0f / 472.0f)) / (float) kPopKnobs;
    for (int k = 0; k < kPopKnobs; ++k)
    {
        const auto kr = popKnobRect (k);
        const float cx = kr.getCentreX();
        const bool hot = kr.contains (lastMousePos_);
        g.setColour (pal_.track.withAlpha ((hot ? 0.50f : 0.28f) * a));
        g.fillRoundedRectangle (kr.expanded (r.getWidth() * (2.0f / 472.0f)),
                                r.getWidth() * (6.0f / 472.0f));
        g.setColour (pal_.dim.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (MBTheme::regular(), 8.5f));
        g.drawText (kRange[k],
                    juce::Rectangle<float> (cx - unitW * 0.5f, kr.getBottom() - r.getHeight() * (12.0f / 200.0f),
                                            unitW, 11.0f),
                    juce::Justification::centredTop);
    }

    // ---- Phase C-6：Dynamics 分段按钮（Compress / Expand，Pro-MB segmented 对标）----
    {
        const auto seg = dynamicsSegRect();
        const float halfW = seg.getWidth() * 0.5f;
        const juce::String labels[2] = { "COMPRESS", "EXPAND" };
        for (int i = 0; i < 2; ++i)
        {
            const bool on = ((i == 1) == dynamicsExpand_);
            auto cell = juce::Rectangle<float> (seg.getX() + halfW * (float) i, seg.getY(),
                                                halfW, seg.getHeight());
            if (on)
            {
                g.setColour (pal_.accent.withAlpha (0.90f * a));
                g.fillRoundedRectangle (cell.reduced (r.getWidth() * 0.004f),
                                        cell.getHeight() * 0.28f);
                g.setColour (juce::Colours::white.withAlpha (0.95f * a));
            }
            else
            {
                g.setColour (pal_.dim.withMultipliedAlpha (a));
            }
            g.setFont (MBTheme::font (on ? MBTheme::semiBold() : MBTheme::regular(), 9.5f));
            g.drawText (labels[i], cell, juce::Justification::centred);
        }
        // 外框（分段控件的容器感）
        g.setColour (pal_.gridZ.withMultipliedAlpha (a));
        g.drawRoundedRectangle (seg, seg.getHeight() * 0.28f, 1.0f);
    }

    // ---- 滑杆行：RATIO / KNEE(0-48dB) / EXT ----
    for (int k = 0; k < 3; ++k)
    {
        const auto sr = popSliderRect (k);
        const bool hot = (sliderDrag_ == k) || sr.contains (lastMousePos_);
        g.setColour (pal_.track.withAlpha ((hot ? 0.75f : 0.50f) * a));
        g.fillRoundedRectangle (sr, r.getWidth() * (11.0f / 472.0f));

        float frac = 0.0f;
        if (k == 0)      frac = (float) ((sliders_[0] - 1.0) / 19.0);
        else if (k == 1) frac = (float) sliders_[1];
        else             frac = (float) sliders_[2];
        frac = juce::jlimit (0.0f, 1.0f, frac);
        g.setColour (pal_.accent.withAlpha (0.30f * a));
        g.fillRoundedRectangle (sr.withWidth (juce::jmax (2.0f, sr.getWidth() * frac)),
                                r.getWidth() * (11.0f / 472.0f));

        g.setColour (hot ? pal_.text.withMultipliedAlpha (a) : pal_.dim.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (MBTheme::semiBold(), 10.0f));
        const juce::String kneeTxt = "KNEE  " + juce::String (sliders_[1] * 48.0, 1) + " dB";
        const juce::String txt = (k == 0) ? ("RATIO  " + juce::String (sliders_[0], 1) + " : 1")
                              : (k == 1) ? kneeTxt
                                         : (sliders_[2] > 0.5 ? "EXT  SIDECHAIN" : "EXT  INTERNAL");
        g.drawText (txt, sr, juce::Justification::centred);
    }

    // ---- EXPERT 右缘竖排标签（Phase C-5，Pro-MB 对标）----
    const auto er = expertRect();
    {
        // 竖排底带：贴右缘的一条窄带，比面板底色略亮以区分
        g.setColour (pal_.track.withAlpha (0.75f * a));
        g.fillRoundedRectangle (er, er.getWidth() * 0.3f);
        g.setColour (expertOpen_ ? pal_.accent.withAlpha (0.9f * a)
                                 : pal_.dim.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (expertOpen_ ? MBTheme::semiBold() : MBTheme::regular(), 9.0f));

        // 竖排文字：逐字符往下排（JUCE 无竖排 drawText，手工排）
        const juce::String txt = expertOpen_ ? "EXPERT \xE2\x96\xBC" : "EXPERT \xE2\x96\xB6";
        const float lineH = 10.0f;
        const float startY = er.getCentreY() - txt.length() * lineH * 0.5f + lineH * 0.5f;
        for (int i = 0; i < txt.length(); ++i)
        {
            const juce::String ch = txt.substring (i, i + 1);
            if (ch != " ")
                g.drawText (ch, juce::Rectangle<float> (er.getX(), startY + i * lineH,
                                                        er.getWidth(), lineH),
                            juce::Justification::centred);
        }
    }

    // ---- EXPERT 展开内容：从右缘向左列出的低频操作项 ----
    if (expertOpen_)
    {
        const auto box = expertContentRect();
        g.setColour (pal_.surface.withAlpha (0.96f * a));
        g.fillRoundedRectangle (box, r.getWidth() * (6.0f / 472.0f));
        g.setColour (pal_.accent.withAlpha (0.6f * a));
        g.drawRoundedRectangle (box, r.getWidth() * (6.0f / 472.0f), 1.0f);

        g.setColour (pal_.dim.withMultipliedAlpha (a * 0.9f));
        g.setFont (MBTheme::font (MBTheme::regular(), 9.0f));
        g.drawText ("DETECTOR  " + juce::String (sliders_[3] > 0.5 ? "RMS" : "PEAK"),
                    box.withTrimmedTop (box.getHeight() * 0.12f).withHeight (box.getHeight() * 0.26f),
                    juce::Justification::centredLeft);
        g.drawText ("LOOKAHEAD  " + juce::String (sliders_[4] == 0 ? "OFF"
                                                                 : sliders_[4] == 1 ? "1 ms" : "3 ms"),
                    box.withTrimmedTop (box.getHeight() * 0.42f).withHeight (box.getHeight() * 0.26f),
                    juce::Justification::centredLeft);
        // 提示这两项也是可点的
        g.setColour (pal_.accent.withAlpha (0.7f * a));
        g.setFont (MBTheme::font (MBTheme::regular(), 8.0f));
        g.drawText ("(click to toggle)",
                    box.withTrimmedTop (box.getHeight() * 0.72f).withHeight (box.getHeight() * 0.22f),
                    juce::Justification::centredLeft);
    }
}

void BandPopover::resized()
{
    layoutKnobs (true);
}

// ---------------- 参数读写 ----------------
float BandPopover::getParamNorm (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    return (it != paramMap_.end() && it->second != nullptr) ? it->second->getValue() : 0.0f;
}

float BandPopover::paramRangeMinOf (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    if (it != paramMap_.end() && it->second != nullptr)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
            return (float) rp->getNormalisableRange().start;
    return 0.0f;
}

float BandPopover::paramRangeMaxOf (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    if (it != paramMap_.end() && it->second != nullptr)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
            return (float) rp->getNormalisableRange().end;
    return 1.0f;
}

float BandPopover::getParamPhysical (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    if (it == paramMap_.end() || it->second == nullptr)
        return 0.0f;
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
        return (float) rp->getNormalisableRange().convertFrom0to1 (it->second->getValue());
    return it->second->getValue();
}

void BandPopover::setParamNorm (const juce::String& id, float norm) const
{
    const auto it = paramMap_.find (id);
    if (it != paramMap_.end() && it->second != nullptr)
        it->second->setValueNotifyingHost (norm);
}

void BandPopover::setParamPhysical (const juce::String& id, float physical) const
{
    const auto it = paramMap_.find (id);
    if (it == paramMap_.end() || it->second == nullptr)
        return;
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
        it->second->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (physical));
    else
        it->second->setValueNotifyingHost (physical);
}

juce::String BandPopover::fmtHzShort (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + "k";
    return juce::String ((int) std::round (hz));
}
