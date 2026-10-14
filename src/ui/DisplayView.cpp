#include "DisplayView.h"

// ============================================================
// DisplayView 实现 — Phase 4.2 静态结构（对照 mockup v9 逐值）
// ============================================================

DisplayView::DisplayView (CrunchMBAudioProcessor& processor)
    : proc_ (processor), pal_ (MBTheme::palette (theme_))
{
    setOpaque (true);
    startTimerHz (60);          // 电平表弹道 + 频谱帧拾取 + 曲线刷新

    // 仿 EQ 曲线频率点（mockup：20Hz→20kHz 对数，241 点）
    curveFreqs_.resize ((size_t) kCurvePoints);
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const double t = (double) i / (double) (kCurvePoints - 1);
        curveFreqs_[(size_t) i] = (float) (20.0 * std::pow (1000.0, t));
    }

    // ⚠️ 立即读一次结构参数：hitTest/mouseDown 不经过 paint，
    // cachedX_ 为 0 会让所有分区边界计算退化成负数 → 选段/命中全错。
    pollStructureParams();
    haveDrawnGain_ = false;

    // Phase A：参数查找缓存（一次性建表，此后 O(1)）
    buildParamCache();
    // Phase A：LINKED CONTROL 抽屉拆为独立组件（UI_PLAN_V2 §3.2）
    linkedDrawer_ = std::make_unique<LinkedDrawer> (proc_);
    addAndMakeVisible (*linkedDrawer_);
    linkedDrawer_->setBounds (getLocalBounds());
    linkedDrawer_->setHostBounds (getLocalBounds());
    // Phase A：频段 Popover 拆为独立组件（UI_PLAN_V2 §3.2）
    bandPopover_ = std::make_unique<BandPopover> (proc_);
    addAndMakeVisible (*bandPopover_);
    bandPopover_->setBounds (getLocalBounds());
    bandPopover_->setHostBounds (getLocalBounds());
    // Phase A：频率输入框拆为独立组件（UI_PLAN_V2 §3.2）
    freqInput_ = std::make_unique<FreqInputEditor>();
    freqInput_->onApply = [this] (int divider, int band, float hz)
    {
        if (band >= 0) applyCenterFreq (band, hz);
        else           applyDividerFreq (divider, hz);
    };
    addAndMakeVisible (*freqInput_);
    freqInput_->setBounds (getLocalBounds());
    // Phase A：双层频谱抽为独立类（UI_PLAN_V2 §3.2），但**不是**子组件——
    // 它在下面的 paint() 第 4 位被直接调用，否则会盖住曲线/刻度/分割线等层。
    spectrumView_ = std::make_unique<SpectrumView> (proc_);
    spectrumView_->setCoordMapper (
        [this] (float f) { return freqToX (f); },
        [this] (float db) { return specDbToY (db); },
        [this] { return specDbToY (-100.0f); });
    // Phase D：Analyzer 自绘面板（替换原生 PopupMenu，获得 hover-open / click-sticky）
    analyzerPanel_ = std::make_unique<AnalyzerPanel>();
    analyzerPanel_->setHostBounds (getLocalBounds());
    analyzerPanel_->onSettingsChanged = [this] { applyAnalyzerSettings(); };
    addAndMakeVisible (*analyzerPanel_);
    // Phase A：Tween 初值直接吸附到当前状态，避免首次绘制时从 0 淡入
    syncAllTweens();
}

void DisplayView::applyLinkedOffsetForTest (int k, double v)
{
    // Phase A：抽屉已拆出，经测试钩子触达（LinkedDrawer::applyOffset 为 private）
    if (linkedDrawer_ == nullptr)
        return;
    linkedDrawer_->applyOffsetForTest (k, v);
}

void DisplayView::applyPopoverKnobForTest (int k, double v)
{
    if (bandPopover_ == nullptr)
        return;
    // T40 只验证"旋钮写入 APVTS"与曲线联动，经 BandPopover 的旋钮路径施加
    bandPopover_->applyKnobForTest (k, v);
}

juce::Rectangle<float> DisplayView::popSliderRectForTest (int k) const
{
    return bandPopover_ != nullptr ? bandPopover_->popSliderRectForTest (k)
                                   : juce::Rectangle<float>();
}

juce::Rectangle<float> DisplayView::popExpertRectForTest() const
{
    return bandPopover_ != nullptr ? bandPopover_->popExpertRectForTest()
                                   : juce::Rectangle<float>();
}

juce::Rectangle<float> DisplayView::popExpertContentRectForTest() const
{
    return bandPopover_ != nullptr ? bandPopover_->popExpertContentRectForTest()
                                   : juce::Rectangle<float>();
}

bool DisplayView::isPopoverExpertOpenForTest() const
{
    return bandPopover_ != nullptr && bandPopover_->isPopoverExpertOpenForTest();
}

void DisplayView::openLinkedDrawerForTest()
{
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->setOpen (true);
}

void DisplayView::buildParamCache()
{
    paramMap_.clear();
    for (auto* p : proc_.getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            paramMap_[pw->getParameterID()] = pw;
}

void DisplayView::bandEdgesPx (int band, float& leftX, float& rightX) const
{
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
    leftX  = edges[(size_t) juce::jlimit (0, 4, band)];
    rightX = edges[(size_t) juce::jlimit (0, 4, band + 1)];
}

void DisplayView::togglePopover (bool open)
{
    if (bandPopover_ == nullptr)
        return;
    if (open == bandPopover_->isOpen())
        return;
    if (open)
    {
        // O6 方案1：与抽屉互斥
        if (linkedDrawer_ != nullptr)
            linkedDrawer_->setOpen (false);
        openPopoverForBand (selectedBand_);
    }
    else
        bandPopover_->setOpen (false);
    repaint();
}

void DisplayView::openPopoverForBand (int band)
{
    if (bandPopover_ == nullptr)
        return;
    float lx = 0.0f, rx = 0.0f;
    bandEdgesPx (band, lx, rx);
    bandPopover_->setBand (band);
    bandPopover_->setBandEdges (lx, rx);
    bandPopover_->setOpen (true);
    bandPopover_->syncFromParams();
}

void DisplayView::syncAllTweens()
{
    const double nowMs = animNowMs();
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        bandBypassed_[(size_t) b] = isBandBypassed (b);
        selT_[(size_t) b].snapTo   ((b == selectedBand_) ? 1.0f : 0.0f);
        hoverT_[(size_t) b].snapTo ((b == hoverBand_) ? 1.0f : 0.0f);
        bypT_[(size_t) b].snapTo   (bandBypassed_[(size_t) b] ? 0.0f : 1.0f);
    }
    for (int e = 0; e < 3; ++e)
        divT_[(size_t) e].snapTo ((e == hoverDividers_) ? 1.0f : 0.0f);
    // Phase A：抽屉与 Popover 的 Tween 归各自组件管
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->snapToCurrent();
    if (bandPopover_ != nullptr)
        bandPopover_->snapToCurrent();
    (void) nowMs;
}

bool DisplayView::advanceTweens (double nowMs)
{
    bool any = false;
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        if (! selT_[(size_t) b].done (nowMs))   any = true;
        if (! hoverT_[(size_t) b].done (nowMs)) any = true;
        if (! bypT_[(size_t) b].done (nowMs))   any = true;
    }
    for (int e = 0; e < 3; ++e)
        if (! divT_[(size_t) e].done (nowMs))   any = true;
    return any;
}

void DisplayView::onSelectedBandChanged (int newBand)
{
    const double nowMs = animNowMs();
    for (int b = 0; b < Param::kNumBands; ++b)
        tweenTo (selT_[(size_t) b], nowMs, (b == newBand) ? 1.0f : 0.0f, Anim::kFadeMs);
}

void DisplayView::onBandBypassChanged (int band, bool bypassed)
{
    if (band < 0 || band >= Param::kNumBands)
        return;
    const double nowMs = animNowMs();
    tweenTo (bypT_[(size_t) band], nowMs, bypassed ? 0.0f : 1.0f, Anim::kFadeMs);
}

DisplayView::~DisplayView()
{
    stopTimer();
}

void DisplayView::setTheme (MBTheme::Theme t)
{
    if (theme_ == t)
        return;
    theme_ = t;
    pal_ = MBTheme::palette (t);
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->setTheme (t);
    if (bandPopover_ != nullptr)
        bandPopover_->setTheme (t);
    if (freqInput_ != nullptr)
        freqInput_->setTheme (t);
    if (spectrumView_ != nullptr)
        spectrumView_->setTheme (t);
    if (analyzerPanel_ != nullptr)
        analyzerPanel_->setTheme (t);
    curvePathsValid_ = false;
    repaint();
}

void DisplayView::setActive (bool active) noexcept
{
    proc_.setEditorActive (active);
    if (! active)
    {
        stopTimer();            // 停 60fps 定时器（省 CPU/电量）
        proc_.getSpectrumEngine().suspend();
    }
    else
    {
        startTimerHz (60);
        proc_.getSpectrumEngine().setUIActive (true);
        // Phase A：抽屉重新激活时吸附到当前状态，避免停表期间动画卡在中途
        if (linkedDrawer_ != nullptr)
            linkedDrawer_->snapToCurrent();
        if (bandPopover_ != nullptr)
            bandPopover_->snapToCurrent();
    }
}

// ---------------- 坐标映射（mockup v9 常量） ----------------
float DisplayView::freqToX (float f) const
{
    // 横轴：20 Hz .. 22100 Hz，宽度 = 绘图区宽（右缘避开 IN/OUT 音量表，#3）
    const float w = plotWidthPx();
    return std::log (juce::jmax (1.0f, f) / 20.0f) / std::log (1105.0f) * w;
}

float DisplayView::xToFreq (float x) const
{
    const float w = juce::jmax (1.0f, plotWidthPx());
    const double t = (double) x / (double) w;
    return (float) (20.0 * std::pow (1105.0, t));
}

float DisplayView::specDbToY (float db) const
{
    // db: −100 → plotBottom(), 0 → plotTop()
    // ⚠️ clamp 到绘图区：强信号 dB>0 也不能画进顶栏区域（#2）
    const float t = (db + 100.0f) / 100.0f;
    return juce::jmax (plotTopPx(), plotBottomPx() - t * plotHeightPx());
}

float DisplayView::gainToY (float gainDb) const
{
    // P1：双刻度 —— 曲线增益轴用**显示量程** curveRangeDisplay_（平滑）
    const float R = juce::jmax (0.5f, curveRangeDisplay_);
    const float t = (gainDb + R) / (2.0f * R);
    return plotBottomPx() - juce::jlimit (0.0f, 1.0f, t) * plotHeightPx();
}

float DisplayView::meterY (float db) const
{
    const float top = plotTopPx();                      // A6/#4：避开顶栏
    const float bottom = plotBottomPx();                // #4：避开 global bar
    const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    return bottom - t * (bottom - top);
}

void DisplayView::drawGrid (juce::Graphics& g)
{
    // P1 双刻度双网格：
    //   横线① 曲线/Range 增益域（±curveRangeDb_，每 1/4 量程一格）——accent 系
    //   横线② 频谱/电平域（−100…0，每 20dB）——dim 系
    //   纵线   频率刻度
    const float bottom = plotBottomPx();
    const float top = plotTopPx();        // #2：网格不伸进顶栏区域

    // 横线：曲线/ Range 增益域（步长同右轴刻度：±6→1dB、±12→2dB、±30→10dB）
    //      0 线用 gridZ 强调（Pro-MB 的 0 基准线）
    const int steps = (int) std::floor (curveRangeDisplay_ / 1.0f);
    for (int i = -steps; i <= steps; ++i)
    {
        const float db = (float) i;
        if (std::abs (db) > curveRangeDisplay_ + 1.0e-4f) continue;
        g.setColour (std::abs (db) < 0.01f ? pal_.gridZ : pal_.grid);
        g.fillRect (0.0f, gainToY (db), (float) getWidth(), 1.0f);
    }
    for (int db = 0; db >= -100; db -= 20)
    {
        g.setColour (pal_.grid.withAlpha (0.55f));
        g.fillRect (0.0f, specDbToY ((float) db), (float) getWidth(), 1.0f);
    }
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        g.fillRect (freqToX (f), top, 1.0f, bottom - top);
}

// ---------------- 网格缓存 ----------------
// ---------------- 绘制 ----------------
void DisplayView::drawBackground (juce::Graphics& g)
{
    // Phase B-3：频谱全出血——背景只铺底色，**不再画**绘图区上下硬边框。
    // 原来的 1px gridZ 线（y=30 与 y=588）会把频谱区框成一个盒子，
    // 与 Pro-MB"频谱铺满、顶/底栏半透明压在上面"的观感相反。
    // 分区之间的分隔由全高分割线负责，顶/底栏各自有一条淡边。
    g.fillAll (pal_.bg);
}

float DisplayView::curveDbAtFreq (float freq) const
{
    if (curveDb_.empty())
        return 0.0f;
    int best = 0; float bd = 1.0e9f;
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float d = std::abs (curveFreqs_[(size_t) i] - freq);
        if (d < bd) { bd = d; best = i; }
    }
    return curveDb_[(size_t) best];
}

float DisplayView::curveDbAtFreqBase (float freq) const
{
    if (baseDb_.empty())
        return 0.0f;
    int best = 0; float bd = 1.0e9f;
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float d = std::abs (curveFreqs_[(size_t) i] - freq);
        if (d < bd) { bd = d; best = i; }
    }
    return baseDb_[(size_t) best];
}

float DisplayView::freqDotScreenY (int band) const
{
    return gainToY (curveDbAtFreqBase (bandCenterFreq (band)));
}

float DisplayView::bandCenterFreq (int band) const
{
    const float lo = (band == 0) ? 20.0f
                   : (band == 1) ? cachedX1_
                   : (band == 2) ? cachedX2_ : cachedX3_;
    const float hi = (band == 0) ? cachedX1_
                   : (band == 1) ? cachedX2_
                   : (band == 2) ? cachedX3_ : 20000.0f;
    return std::sqrt (lo * hi);      // 几何中心（对数域中点）
}

void DisplayView::drawPartitions (juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
    const double nowMs = animNowMs();

    for (int b = 0; b < 4; ++b)
    {
        // Phase A：选中填充 / BYP 渐暗都走 Tween（原为开关量硬跳）
        const float selK = selT_[(size_t) b].value (nowMs);       // 0..1
        const float bypK = bypT_[(size_t) b].value (nowMs);       // 0..1（1 = 正常）
        const float dimK = 0.45f + 0.55f * bypK;                  // P3：BYP 段 dim（Pro-MB：bypassed band 在显示里变暗）
        const float x0 = edges[b] + 1.0f, w = edges[b + 1] - edges[b] - 2.0f;
        const float h = sy (kDesignH);
        const float fillA = 0.05f + 0.05f * selK;                 // 0.05 ↔ 0.10
        g.setColour (pal_.accent.withAlpha (fillA * dimK));
        g.fillRect (x0, 0.0f, w, h);   // 分区底色全高（mockup: height=620），描边则贴绘图区
        if (selK > 0.01f)
        {
            // A2.1/C2/#4：只画上/下两条横边；描边贴**绘图区边界**（状态栏下缘 / global bar 上缘），
            // 不再伸进状态栏；左边框取消（避免与分割线双边重叠变厚）
            g.setColour (pal_.accent.withAlpha (0.9f * selK * dimK));
            g.drawHorizontalLine ((int) plotTopPx(), x0, x0 + w);
            g.drawHorizontalLine ((int) plotBottomPx(), x0, x0 + w);
            if (b == 3)
                g.drawVerticalLine ((int) (x0 + w), plotTopPx(), plotBottomPx());
        }
    }
}

void DisplayView::drawScales (juce::Graphics& g)
{
    // P1 双刻度：
    //   右轴 = 曲线/Range 增益域（accent，±curveRangeDisplay_）
    //   左轴 = 频谱/电平域（dim，0…−100）
    // #4：刻度文字 clamp 在绘图区内（上下各留一字高），避免首尾被状态栏/global bar 盖住
    const float top = plotTopPx();
    const float bottom = plotBottomPx();
    const float W = (float) getWidth();
    auto clampTextBox = [&] (juce::Rectangle<float> r) -> juce::Rectangle<float>
    {
        const float h = r.getHeight();
        const float y = juce::jlimit (top + h, bottom - h * 2.0f, r.getY());
        const float x = juce::jmin (r.getX(), W - r.getWidth() - 1.0f);
        return juce::Rectangle<float> (x, y, r.getWidth(), h);
    };

    g.setFont (MBTheme::font (MBTheme::regular(), 13.0f));   // #5：字号 10→13

    // 右轴：曲线/Range 增益刻度（accent）
    // #2：对齐 Pro-MB 截图——步长随量程：±3/±6 → 1dB、±12 → 2dB、±30 → 10dB；
    //     0 线标签用亮色强调（Pro-MB 的 0 基准线）
    {
        const float R = curveRangeDisplay_;
        const float step = (R <= 6.5f) ? 1.0f : (R <= 12.5f) ? 2.0f : 10.0f;
        for (float db = -std::floor (R / step) * step; db <= R + 1.0e-4f; db += step)
        {
            const juce::String t = (db > 0.0f ? "+" : "") + juce::String (db, (step < 1.0f ? 1 : 0));
            g.setColour (std::abs (db) < 1.0e-4f ? pal_.text : pal_.accent);
            g.drawText (t, clampTextBox (juce::Rectangle<float> (kGainScaleRightX - sx (56.0f), gainToY (db) - 7.0f,
                                                                 sx (56.0f), 14.0f)),
                        juce::Justification::centredRight);
        }
    }

    // 左轴：频谱/电平刻度（dim）——回滚为 20dB 一步进（#1 回滚）
    g.setColour (pal_.dim);
    for (int db = 0; db >= -100; db -= 20)
        g.drawText (juce::String (db),
                    clampTextBox (juce::Rectangle<float> (sx (4.0f), specDbToY ((float) db) - 7.0f,
                                                          sx (48.0f), 14.0f)),
                    juce::Justification::centredLeft);

    // 底部频率刻度（常显）——#3：右缘标签右对齐并提亮；#5：字号 10.5→13
    g.setFont (MBTheme::font (MBTheme::regular(), 13.0f));
    struct FLabel { float f; const char* t; bool bold; };
    for (const auto& l : { FLabel { 50, "50", false }, FLabel { 100, "100", false },
                           FLabel { 200, "200", false }, FLabel { 500, "500", false },
                           FLabel { 1000, "1k", false }, FLabel { 2000, "2k", false },
                           FLabel { 5000, "5k", false }, FLabel { 10000, "10k", false },
                           FLabel { 20000, "20k", true } })
    {
        g.setColour (l.bold ? pal_.text : pal_.dim);      // #3：20k 用亮色
        const float w = sx (46.0f);
        float x = freqToX (l.f) - w * 0.5f;
        // #3：右缘附近的标签右对齐到**绘图区右缘**（避开 IN/OUT 音量表）
        if (freqToX (l.f) > plotRightPx() - w)
            x = plotRightPx() - w;
        g.drawText (l.t, juce::Rectangle<float> (x, sy (546.0f), w, 16.0f),
                    juce::Justification::centredTop);
    }
}

void DisplayView::drawCrossovers (juce::Graphics& g)
{
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const double nowMs = animNowMs();
    // Phase B-2：手柄移到绘图区**底部**（原在 y=500 中部），并在手柄内显示频率值。
    // 对齐 Pro-MB：分割线全高 + 底部手柄 + 手柄上标频率。
    const float plotBottom = plotBottomPx();
    const float handleH = sy (22.0f);
    const float handleW = sx (46.0f);
    const float handleY = plotBottom - handleH;

    for (int e = 0; e < 3; ++e)
    {
        const float x = nx[e];
        // Phase A：hover/拖动主题色渐变（原为硬跳 hot ? accent : gridZ）
        const float hot = divT_[(size_t) e].value (nowMs);
        const bool dragging = (drag_ == DragMode::Divider && dragBand_ == e);
        const juce::Colour c = pal_.gridZ.interpolatedWith (pal_.accent,
                                                            juce::jlimit (0.0f, 1.0f, hot));
        g.setColour (c);
        g.fillRect (x - 0.75f, 0.0f, 1.5f, plotBottom);

        // 底部手柄（圆角胶囊），hover/拖动时向主题色插值
        auto handle = juce::Rectangle<float> (x - handleW * 0.5f, handleY, handleW, handleH);
        g.setColour (pal_.track.interpolatedWith (pal_.accent,
                                                  juce::jlimit (0.0f, 1.0f, hot * 0.55f)));
        g.fillRoundedRectangle (handle, handleH * 0.5f);
        g.setColour (dragging ? pal_.accent : pal_.gridZ);
        g.drawRoundedRectangle (handle, handleH * 0.5f, 1.0f);

        // 手柄内频率读数（Pro-MB：手柄上直接显示频率，不用额外 tooltip）
        const float f = (e == 0) ? cachedX1_ : (e == 1) ? cachedX2_ : cachedX3_;
        g.setColour (hot > 0.5f ? pal_.text : pal_.dim);
        g.setFont (MBTheme::font (hot > 0.5f ? MBTheme::semiBold() : MBTheme::regular(), 10.0f));
        g.drawText (fmtHzShort (f), handle, juce::Justification::centred);
    }
}

// 频率短格式（与 BandPopover::fmtHzShort 同规则；此处为 DisplayView 本地副本）
juce::String DisplayView::fmtHzShort (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + "k";
    return juce::String ((int) std::round (hz));
}

void DisplayView::drawBandHeaders (juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
    const char* names[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };

    g.setFont (MBTheme::font (MBTheme::semiBold(), 11.5f));
    for (int b = 0; b < 4; ++b)
    {
        // A2.2/A2.3：段名右移避开左音量刻度、整体下移至 y=40
        g.setColour (pal_.accent.withAlpha (0.9f));
        g.drawText (names[b],
                    juce::Rectangle<float> (edges[b] + sx (36.0f), sy (40.0f), sx (140.0f), sy (16.0f)),
                    juce::Justification::centredLeft);

        // 状态点列（S/M/BYP/Δ，4 个 3px 圆点，右缘）——实时反映参数
        const float sxp = (b < 3 ? nx[b] : W) - sx (28.0f);
        for (int j = 0; j < 4; ++j)
        {
            bool lit = false;
            switch (j)
            {
                case 0: lit = getParamBool ("band" + juce::String (b) + "Solo");  break;
                case 1: lit = getParamBool ("band" + juce::String (b) + "Mute");  break;
                // 2026-10-14：BYP = Mix<50%（D9 方案 A）。band{i}Mix 是浮点参数，
                // getRawParameterValue 返回物理百分比（0..100），必须直接比 50.0f——
                // 此前误用 getParamBool（内部 >=0.5f 判定）导致 Mix=30% 时 BYP 灯误亮。
                case 2: lit = getParamPhysical ("band" + juce::String (b) + "Mix") < 50.0f; break;
                case 3: lit = getParamBool ("band" + juce::String (b) + "Delta"); break;
                default: break;
            }
            g.setColour (lit ? pal_.accent : pal_.track);
            g.fillEllipse (sxp, sy (42.0f + j * 9.0f) - 1.5f, 3.0f, 3.0f);
        }

        // C6–C9：S/M/BYP/Δ 按钮列（24×24，hover 或选中该段时淡入）
        //     band3 的右缘 = 电平表左边界−8 → 按钮在分区右侧且不压音量表
        // Phase A：UI_SPEC §4.2 原话"hover 该段时淡入、移出淡出"——用 hoverT_ 驱动 alpha。
        //         选中态常驻（selT_ 已含），hover 在上层叠加。
        const float hoverK = hoverT_[(size_t) b].value (animNowMs());
        const float selK   = selT_[(size_t) b].value (animNowMs());
        const float btnA   = juce::jmax (selK, hoverK);       // 选中常驻 / hover 淡入
        if (btnA > 0.01f)
        {
            const float edge = (b < 3) ? nx[b]
                                       : juce::jmin (W, sx (kMeterX) - sx (8.0f));
            const float bx = edge - sx (40.0f);
            const juce::String labels[4] = { "S", "M", "B", juce::String::fromUTF8 ("\xCE\x94") };
            for (int j = 0; j < 4; ++j)
            {
                bool on = false;
                switch (j)
                {
                    case 0: on = getParamBool ("band" + juce::String (b) + "Solo");  break;
                    case 1: on = getParamBool ("band" + juce::String (b) + "Mute");  break;
                    // 2026-10-14：BYP 判定用物理 Mix 值比 50%
                    case 2: on = getParamPhysical ("band" + juce::String (b) + "Mix") < 50.0f; break;
                    case 3: on = getParamBool ("band" + juce::String (b) + "Delta"); break;
                    default: break;
                }
                auto r = juce::Rectangle<float> (bx, sy (118.0f + j * 30.0f), sx (24.0f), sy (24.0f));
                g.setColour (on ? pal_.accent.withMultipliedAlpha (btnA)
                                : pal_.track.withAlpha (0.92f * btnA));
                g.fillRoundedRectangle (r, sx (6.0f));
                g.setColour (on ? juce::Colours::white.withAlpha (0.92f * btnA)
                                : pal_.dim.withMultipliedAlpha (btnA));
                g.setFont (MBTheme::font (MBTheme::semiBold(), 11.5f));
                g.drawText (labels[j], r, juce::Justification::centred);
            }
        }

        // C4+#1+#5：段中心频点 —— 绑在 **Range 填充区最顶边的中点**（= 未压缩基准），
        // 只随 Makeup 改变；Range 参数变化时只动钟型描边，频点不动（用户明确要求）
        {
            const float fcen = bandCenterFreq (b);
            const float px = freqToX (fcen);
            int best = 0; float bd = 1.0e9f;
            for (int i = 0; i < kCurvePoints; ++i)
            {
                const float d = std::abs (curveFreqs_[(size_t) i] - fcen);
                if (d < bd) { bd = d; best = i; }
            }
            const float py = gainToY (baseDb_[(size_t) best]);
            const bool sel = (b == selectedBand_);
            const bool hov = (hoverDotBand_ == b);

            // ---- Phase C-3：圆形电平表环（Pro-MB Threshold 外圈环对标）----
            // 画在频点外侧：内圈=实时段电平，外圈一点=峰值保持。
            // 只在有声段有意义，静默段（<= -60dB）不画，避免满屏灰环。
            {
                const float lvl = bandLevelDb_[(size_t) b];
                if (lvl > -60.0f)
                {
                    const float ringR = sx (11.0f);
                    // −60..0 dB → 0..1
                    const float frac = juce::jlimit (0.0f, 1.0f, (lvl + 60.0f) / 60.0f);
                    const float a0 = juce::MathConstants<float>::pi * 1.25f;   // 7:30，与旋钮同
                    const float a1 = a0 + frac * juce::MathConstants<float>::pi * 1.5f;  // 270° 行程

                    juce::Path ring;
                    ring.addCentredArc (px, py, ringR, ringR, 0.0f, a0, a1, true);
                    g.setColour (pal_.accent.withAlpha (0.85f));
                    g.strokePath (ring, juce::PathStrokeType (juce::jmax (1.5f, sx (2.0f)),
                                                              juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));

                    // 峰值保持：环末端一个小亮点
                    const float pk = bandPeakDb_[(size_t) b];
                    if (pk > -60.0f)
                    {
                        const float pf = juce::jlimit (0.0f, 1.0f, (pk + 60.0f) / 60.0f);
                        const float pa = a0 + pf * juce::MathConstants<float>::pi * 1.5f;
                        const float pr = juce::jmax (1.5f, sx (2.2f));
                        g.setColour (pal_.light ? pal_.inFill : pal_.outLevel);
                        g.fillEllipse (px + std::sin (pa) * ringR - pr * 0.5f,
                                       py - std::cos (pa) * ringR - pr * 0.5f, pr, pr);
                    }
                }
            }

            if (sel)                                          // #2：仅选中段有外层
            {
                const float rad = hov ? sx (7.0f) : sx (5.0f);
                g.setColour (pal_.accent.withAlpha (0.25f));
                g.fillEllipse (px - rad * 2.0f, py - rad * 2.0f, rad * 4.0f, rad * 4.0f);
                g.setColour (pal_.accent);
                g.fillEllipse (px - rad, py - rad, rad * 2.0f, rad * 2.0f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (px - rad * 0.35f, py - rad * 0.35f, rad * 0.7f, rad * 0.7f);
            }
            else                                              // #2：未选中段只画实心圆
            {
                const float rad = hov ? sx (6.0f) : sx (4.5f);
                g.setColour (pal_.accent);
                g.fillEllipse (px - rad, py - rad, rad * 2.0f, rad * 2.0f);
            }

            // C4：hover 频点浮出小窗（段名 / Makeup / 中心 Hz）——更小更精致
            if (hov)
            {
                const float mk = proc_.apvts.getRawParameterValue (Param::bandMakeup (b))->load();
                const juce::String l1 = names[b];
                const juce::String l2 = "MAKEUP " + juce::String (mk, 1) + " dB";
                const juce::String l3 = juce::String ((int) fcen) + " Hz";
                const float bw = sx (104.0f), bh = sy (38.0f);
                float tx = px - bw * 0.5f, ty = py - bh - sy (12.0f);
                tx = juce::jlimit (2.0f, W - bw - 2.0f, tx);
                if (ty < sy (32.0f)) ty = py + sy (12.0f);
                auto box = juce::Rectangle<float> (tx, ty, bw, bh);
                g.setColour (pal_.surface.withAlpha (0.96f));
                g.fillRoundedRectangle (box, sx (5.0f));
                g.setColour (pal_.accent);
                g.drawRoundedRectangle (box, sx (5.0f), 1.0f);
                g.setColour (pal_.text);
                g.setFont (MBTheme::font (MBTheme::semiBold(), 9.5f));
                g.drawText (l1, box.withTrimmedTop (sy (3.0f)).withHeight (sy (11.0f)),
                            juce::Justification::centredTop);
                g.setColour (pal_.accent);
                g.setFont (MBTheme::font (MBTheme::regular(), 9.0f));
                g.drawText (l2, box.withTrimmedTop (sy (14.0f)).withHeight (sy (11.0f)),
                            juce::Justification::centredTop);
                g.setColour (pal_.dim);
                g.drawText (l3, box.withTrimmedTop (sy (25.0f)).withHeight (sy (10.0f)),
                            juce::Justification::centredTop);
            }
        }
    }
}

// #7：drawFreqInput() 已整体拆到 FreqInputEditor 子组件自绘。

void DisplayView::drawMeters (juce::Graphics& g)
{
    const float barW = sx (13.0f);
    const float b1 = sx (kMeterX) + sx (2.0f);              // IN 柱
    const float b2 = sx (kMeterX) + sx (4.0f) + barW;       // OUT 柱
    const float top = plotTopPx();                          // A6/#2：条带顶端避开顶栏
    const float bottom = plotBottomPx();                    // #4：底缘避开 global bar

    // 条带底 + 网格（0…−60，每 20dB）
    g.setColour (pal_.meterBg);
    g.fillRect (sx (kMeterX), top, sx (kMeterW), bottom - top);
    g.setColour (pal_.grid);
    for (int db = 0; db >= -60; db -= 20)
        g.fillRect (sx (kMeterX), meterY ((float) db), sx (kMeterW), 1.0f);

    // IN 柱：inputFill 平涂 0.35α
    const float yIn = meterY (meterInDb_);
    g.setColour (pal_.inFill.withAlpha (0.35f));
    g.fillRect (b1, yIn, barW, bottom - yIn);

    // OUT 柱：accent → outLevel 垂直渐变
    juce::ColourGradient grad (pal_.accent, 0.0f, top, pal_.outLevel, 0.0f, bottom, false);
    g.setGradientFill (grad);
    const float yOut = meterY (meterOutDb_);
    g.fillRect (b2, yOut, barW, bottom - yOut);

    // 峰值保持顶线 2px
    g.setColour (pal_.light ? pal_.inFill : pal_.outLevel);
    g.fillRect (b1, meterY (peakInDb_) - 1.0f, barW, 2.0f);
    g.fillRect (b2, meterY (peakOutDb_) - 1.0f, barW, 2.0f);

    // 柱顶标签 9px SemiBold + 深色描边
    // A6：下移至顶栏玻璃条（30px）之下，避免被遮住
    g.setFont (MBTheme::font (MBTheme::semiBold(), 9.0f));
    g.setColour (pal_.text);
    auto haloText = [&] (const juce::String& s, juce::Rectangle<float> r)
    {
        g.drawText (s, r, juce::Justification::centred);
    };
    haloText ("IN",  juce::Rectangle<float> (b1, sy (32.0f), barW, 11.0f));
    haloText ("OUT", juce::Rectangle<float> (b2, sy (32.0f), barW, 11.0f));

    // 柱底 dB 读数 8px Medium（B5：250ms 节流显示，柱体仍 60fps 实时）
    g.setFont (MBTheme::font (MBTheme::medium(), 8.0f));
    g.setColour (pal_.text);
    const juce::uint32 nowMs2 = animNowMs();
    if (nowMs2 >= nextMeterLabelMs_)
    {
        labelInDb_  = meterInDb_;
        labelOutDb_ = meterOutDb_;
        nextMeterLabelMs_ = nowMs2 + 250;
    }
    g.drawText (juce::String (labelInDb_, 1),
                juce::Rectangle<float> (b1 - 4.0f, bottom - 10.0f, barW + 8.0f, 10.0f),
                juce::Justification::centred);
    g.drawText (juce::String (labelOutDb_, 1),
                juce::Rectangle<float> (b2 - 4.0f, bottom - 10.0f, barW + 8.0f, 10.0f),
                juce::Justification::centred);
}

void DisplayView::drawTopStrip (juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float h = sy (kTopStripH);

    // Phase B-3：顶栏改**半透明玻璃**（原 100% 不透明 surface）。
    // 对齐 Pro-MB：顶栏是压在频谱之上的半透明条，频谱从它下面透出来。
    // 模糊留到 Phase E（UI_SPEC §12.2 档 b：4× 下采样 + 15Hz 更新）。
    g.setColour (pal_.surfaceA);
    g.fillRect (0.0f, 0.0f, W, h);
    // 底缘 1px 分隔（比之前的 gridZ 更淡，玻璃条不需要硬边）
    g.setColour (pal_.gridZ.withAlpha (0.6f));
    g.fillRect (0.0f, h - 1.0f, W, 1.0f);

    // A9：模式信息文字已删除（用户不需要这行解释）
    // 段名（选中段）
    const char* names[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    g.setColour (pal_.accent);
    g.fillEllipse (sx (10.0f), sy (11.0f), 8.0f, 8.0f);
    g.setFont (MBTheme::font (MBTheme::semiBold(), 12.0f));
    g.setColour (pal_.text);
    g.drawText (names[selectedBand_],
                juce::Rectangle<float> (sx (26.0f), sy (6.0f), sx (160.0f), sy (18.0f)),
                juce::Justification::centredLeft);

    // CURVE 开关 + ⚙（Phase 4.4/5 接交互）
    auto curveBtn = juce::Rectangle<float> (W - sx (124.0f), sy (3.0f), sx (86.0f), sy (24.0f));
    g.setColour (pal_.track);
    g.fillRoundedRectangle (curveBtn, sx (12.0f));
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::semiBold(), 10.5f));
    g.drawText ("CURVE", curveBtn, juce::Justification::centred);

    // P1：Display Range 档位按钮（#4：从 global bar 移到顶栏，避免与 IN 读数冲突）
    //     点击弹出二级菜单（Auto / ±3 / ±6 / ±12 / ±30）
    displayRangeBtn_ = juce::Rectangle<float> (W - sx (218.0f), sy (3.0f), sx (86.0f), sy (24.0f));
    g.setColour (displayRangeMode_ == 0 ? pal_.accent.withAlpha (0.85f) : pal_.track);
    g.fillRoundedRectangle (displayRangeBtn_, sx (12.0f));
    g.setColour (displayRangeMode_ == 0 ? juce::Colours::white.withAlpha (0.92f) : pal_.dim);
    g.setFont (MBTheme::font (MBTheme::semiBold(), 10.0f));
    {
        const char* modes[5] = { "Auto", "3 dB", "6 dB", "12 dB", "30 dB" };
        g.drawText (juce::String ("+/- ") + modes[displayRangeMode_] + "  " + juce::String::fromUTF8 ("\xE2\x96\xBC"),
                    displayRangeBtn_, juce::Justification::centred);
    }

    auto gearBtn = juce::Rectangle<float> (W - sx (30.0f), sy (3.0f), sx (24.0f), sy (24.0f));
    g.setColour (pal_.track);
    g.fillRoundedRectangle (gearBtn, sx (6.0f));
    g.setColour (pal_.text);
    g.setFont (MBTheme::font (MBTheme::regular(), 12.0f));
    g.drawText (juce::String::fromUTF8 ("\xE2\x9A\x99"), gearBtn, juce::Justification::centred);
}

void DisplayView::drawGlobalBar (juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float y0 = (float) getHeight() - sy (kGlobalBarH);
    const float h = sy (kGlobalBarH);

    // Phase B-3：global 条同样改半透明玻璃（对齐 Pro-MB 底栏压在频谱之上）
    g.setColour (pal_.surfaceA);
    g.fillRect (0.0f, y0, W, h);
    g.setColour (pal_.gridZ.withAlpha (0.6f));
    g.fillRect (0.0f, y0, W, 1.0f);

    // 全局选择器（Phase 4.4 接下拉）
    g.setFont (MBTheme::font (MBTheme::regular(), 11.5f));
    float x = sx (14.0f);
    auto item = [&] (const juce::String& label, const juce::String& value)
    {
        g.setColour (pal_.dim);
        g.drawText (label, juce::Rectangle<float> (x, y0 + sy (8.0f), sx (52.0f), sy (16.0f)),
                    juce::Justification::centredLeft);
        x += sx (52.0f);
        g.setColour (pal_.text);
        g.drawText (value + " " + juce::String::fromUTF8 ("\xE2\x96\xBC"),
                    juce::Rectangle<float> (x, y0 + sy (8.0f), sx (110.0f), sy (16.0f)),
                    juce::Justification::centredLeft);
        x += sx (118.0f);
        g.setColour (pal_.gridZ);
        g.fillRect (x, y0 + sy (9.0f), 1.0f, sy (14.0f));
        x += sx (10.0f);
    };
    item ("Phase:", "Natural Phase");
    item ("Slope:", "24 dB/oct");
    item ("Quality:", "Medium");
    item ("Lookahead:", "Off");

    // Analyzer 按钮（Pro-MB 底栏 Analyzer 对标）——占据原 IN 读数位置；
    // 点击弹出二级菜单（Resolution / Speed / Tilt / Freeze / Pre-Post）。
    // 视觉样式（图标+高亮态）留到 global bar 流程统一做，这里先给可点的胶囊。
    auto anBtn = juce::Rectangle<float> (W * 0.5f - sx (150.0f), y0 + sy (5.0f),
                                         sx (104.0f), sy (22.0f));
    g.setColour (analyzerFreeze_ ? pal_.accent : pal_.track);
    g.fillRoundedRectangle (anBtn, sx (11.0f));
    g.setColour (analyzerFreeze_ ? juce::Colours::white.withAlpha (0.92f) : pal_.text);
    g.setFont (MBTheme::font (MBTheme::semiBold(), 10.0f));
    {
        juce::String resTag = (analyzerFftSize_ == 1024) ? "1024"
                           : (analyzerFftSize_ == 2048) ? "2048"
                           : (analyzerFftSize_ == 4096) ? "4096" : "8192";
        static const char* kSpd[3] = { "FAST", "MED", "SLOW" };
        g.drawText (juce::String ("ANALYZER ") + resTag + " " + kSpd[juce::jlimit (0, 2, analyzerSpeed_)]
                    + (analyzerTilt_ > 0.01f ? " TILT" : "")
                    + (analyzerFreeze_ ? " FRZ" : ""),
                    anBtn, juce::Justification::centred);
    }

    // OUT 读数（保留）
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::regular(), 11.0f));
    const juce::uint32 nowMs = animNowMs();
    if (nowMs >= nextMeterLabelMs_)
    {
        labelInDb_  = meterInDb_;
        labelOutDb_ = meterOutDb_;
        nextMeterLabelMs_ = nowMs + 250;
    }
    g.drawText ("OUT " + juce::String (labelOutDb_, 2) + " dB",
                juce::Rectangle<float> (W - sx (130.0f), y0 + sy (8.0f), sx (110.0f), sy (16.0f)),
                juce::Justification::centredRight);

    // BYPASS 胶囊（C10：可点按；命中区与绘制严格一致）
    auto byp = juce::Rectangle<float> (W - sx (108.0f), y0 + sy (5.0f), sx (92.0f), sy (22.0f));
    g.setColour (isAllBypassed() ? pal_.accent : pal_.track);
    g.fillRoundedRectangle (byp, sx (11.0f));
    g.setColour (isAllBypassed() ? juce::Colours::white.withAlpha (0.92f) : pal_.text);
    g.setFont (MBTheme::font (MBTheme::semiBold(), 10.5f));
    g.drawText ("BYPASS", byp, juce::Justification::centred);
}

// Phase A：drawDrawer() 已随抽屉整体拆到 LinkedDrawer::paint()。
// ---------------- 电平表弹道（EQ 同款） ----------------
void DisplayView::updatePeakHold (float& peakDb, juce::uint32& holdUntilMs,
                                  float levelDb, juce::uint32 nowMs, double dt)
{
    if (levelDb >= peakDb)
    {
        peakDb = levelDb;
        holdUntilMs = nowMs + (juce::uint32) kPeakHoldMs;
    }
    else if (nowMs > holdUntilMs)
    {
        peakDb = juce::jmax (levelDb, peakDb - (float) (kPeakFallDbPerSec * dt));
    }
}

// ---- Phase C-3：频段圆形电平表弹道 ----
// 对齐 Pro-MB：Threshold 旋钮外圈那圈环就是该段的实时检测电平。
// 这里把它画在频段电平点外圈，达到同样效果——不用开 Popover 就知道
// 每段在被什么电平驱动。
void DisplayView::updateBandLevels (double nowMs, double dt)
{
    const float fallDbPerSec = 60.0f;      // 与 IN/OUT 电平表同弹道
    const juce::uint32 holdMs = 2000;      // 峰值保持 2s（与 kPeakHoldMs 一致）

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const float raw = proc_.getDSP().getBandDetectorDb (b);

        // 平滑：上升瞬时跟上，下降按 60dB/s（与 IN/OUT 一致的手感）
        if (raw > bandLevelDb_[(size_t) b])
            bandLevelDb_[(size_t) b] = raw;
        else
            bandLevelDb_[(size_t) b] =
                juce::jmax (raw, bandLevelDb_[(size_t) b] - fallDbPerSec * (float) dt);

        // 峰值保持
        if (raw >= bandPeakDb_[(size_t) b])
        {
            bandPeakDb_[(size_t) b] = raw;
            bandPeakHoldMs_[(size_t) b] = (juce::uint32) nowMs + holdMs;
        }
        else if ((juce::uint32) nowMs > bandPeakHoldMs_[(size_t) b])
        {
            bandPeakDb_[(size_t) b] =
                juce::jmax (raw, bandPeakDb_[(size_t) b] - fallDbPerSec * (float) dt);
        }
    }
}

void DisplayView::timerCallback()
{
    const auto nowMs = animNowMs();
    const double dt = (lastMeterMs_ == 0) ? 1.0 / 60.0
                     : juce::jlimit (0.001, 0.1, (double) (nowMs - lastMeterMs_) / 1000.0);
    lastMeterMs_ = nowMs;

    // ---- 电平表弹道 ----
    const float inLin  = proc_.getInputPeak();
    const float outLin = proc_.getOutputPeak();
    const float inDb   = juce::Decibels::gainToDecibels (inLin,  -120.0f);
    const float outDb  = juce::Decibels::gainToDecibels (outLin, -120.0f);

    meterInDb_  = (inDb  > meterInDb_)  ? inDb  : juce::jmax (meterInDb_  - 60.0f * (float) dt, -60.0f);
    meterOutDb_ = (outDb > meterOutDb_) ? outDb : juce::jmax (meterOutDb_ - 60.0f * (float) dt, -60.0f);

    updatePeakHold (peakInDb_,  peakInHoldUntilMs_,  inDb,  nowMs, dt);
    updatePeakHold (peakOutDb_, peakOutHoldUntilMs_, outDb, nowMs, dt);

    // Phase C-3：频段圆形电平表弹道
    updateBandLevels (nowMs, dt);

    // ---- Phase 4.3：频谱帧拾取（Phase A：已拆为 SpectrumView）----
    if (spectrumView_ != nullptr)
        spectrumView_->updateFromEngine();

    // #7：输入框光标闪烁（500ms 半周期）
    // A2 修复：闪烁由时间驱动，paint() 只读。所有可能处于编辑态的控件都要推进。
    if (freqInput_ != nullptr)
        freqInput_->tickCursor ((juce::uint32) nowMs);
    if (linkedDrawer_ != nullptr)
    {
        linkedDrawer_->tickEditCursors ((juce::uint32) nowMs);
        linkedDrawer_->advanceKnobStates (nowMs);       // Phase C-2：旋钮四态渐变
    }
    if (bandPopover_ != nullptr)
    {
        bandPopover_->tickEditCursors ((juce::uint32) nowMs);
        bandPopover_->advanceKnobStates (nowMs);        // Phase C-2：旋钮四态渐变
    }

    pollStructureParams();

    // range 参数先读（autoExpand 与 Range 层都需要最新值）
    for (int b = 0; b < Param::kNumBands; ++b)
        bandRangeDb_[(size_t) b] =
            proc_.apvts.getRawParameterValue (Param::bandRange (b))->load();

    // Phase A：BYP 状态轮询（Mix 可被宿主自动化改写 → bypT_ 目标要跟着走）
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const bool bp = isBandBypassed (b);
        if (bp != bandBypassed_[(size_t) b])
        {
            bandBypassed_[(size_t) b] = bp;
            onBandBypassChanged (b, bp);
        }
    }

    updateCurve();

    // P1：显示量程平滑追赶目标（τ=90ms，档位切换动画）
    if (std::abs (curveRangeDisplay_ - curveRangeDb_) < 0.01f)
        curveRangeDisplay_ = curveRangeDb_;
    else
        curveRangeDisplay_ += (curveRangeDb_ - curveRangeDisplay_)
                            * (float) (1.0 - std::exp (-dt / 0.090));

    // range 拖动动画（每帧向目标值收敛；time-constant 型平滑）
    {
        const double dt2 = dt;
        const float k = (float) (1.0 - std::exp (-dt2 / 0.050));   // τ=50ms
        for (int b = 0; b < Param::kNumBands; ++b)
        {
            const float target = bandRangeDb_[(size_t) b];
            if (std::abs (rangeDisplay_[(size_t) b] - target) < 0.01f)
                rangeDisplay_[(size_t) b] = target;
            else
                rangeDisplay_[(size_t) b] += (target - rangeDisplay_[(size_t) b]) * k;
        }
    }

    // 曲线/Range 几何重建门控：
    //   · bandDisplayDb_ 变化（压缩量 / Makeup / BYP）→ 重建
    //   · rangeDisplay_ 变化（Range 参数/平滑动画）→ **必须重建**
    //     ⚠️ 2026-09-23 修复：早期门控只比对 bandDisplayDb_，导致改 Range 参数后
    //     Range 层 Path 不刷新——用户要切到别的段再切回来才看到变化。
    //   · curvePathsValid_ == false（结构/尺寸/主题/切段）→ 强制重建
    {
        bool needRebuild = ! haveDrawnGain_ || ! curvePathsValid_
                        || std::abs (curveRangeDisplay_ - lastDrawnCurveRange_) > 0.01f;
        for (int b = 0; b < Param::kNumBands && ! needRebuild; ++b)
        {
            if (std::abs (bandDisplayDb_[(size_t) b] - lastDrawnGainDb_[(size_t) b]) > 0.02f
             || std::abs (rangeDisplay_[(size_t) b]   - lastDrawnRange_[(size_t) b]) > 0.02f)
                needRebuild = true;
        }
        if (needRebuild)
        {
            lastDrawnGainDb_  = bandDisplayDb_;
            lastDrawnRange_   = rangeDisplay_;
            lastDrawnCurveRange_ = curveRangeDisplay_;
            haveDrawnGain_ = true;
            refreshCurvePaths();
        }
    }
    // ---- Phase 5.2 / 5.3：抽屉与 Popover 滑入/收起动画 ----
    // Phase A：两者均已拆为独立组件，这里只推进它们的 Tween。
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->advance (nowMs);
    if (bandPopover_ != nullptr)
        bandPopover_->advance (nowMs);
    // Phase D：Analyzer 面板开合动画
    if (analyzerPanel_ != nullptr)
        analyzerPanel_->advance (nowMs);

    // ---- Phase A：其余 Tween（选中/hover/BYP/分割线）推进 ----
    advanceTweens (nowMs);

    repaint();
}

// ---------------- Phase 4.3：交叉权重 / 曲线 / Range ----------------
void DisplayView::pollStructureParams()
{
    // 每帧比对结构参数（读 APVTS 原子）；变化才重算权重
    const float x1 = proc_.apvts.getRawParameterValue (Param::ID::xover1)->load();
    const float x2 = proc_.apvts.getRawParameterValue (Param::ID::xover2)->load();
    const float x3 = proc_.apvts.getRawParameterValue (Param::ID::xover3)->load();
    const int slope = (int) juce::roundToInt (
        Param::slopeIndexToDb ((int) juce::roundToInt (
            proc_.apvts.getRawParameterValue (Param::ID::xoverSlope)->load() * 3.0f)));
    const int mode = (int) juce::roundToInt (
        proc_.apvts.getRawParameterValue (Param::ID::phaseMode)->load() * 2.0f);

    if (x1 != cachedX1_ || x2 != cachedX2_ || x3 != cachedX3_
        || slope != cachedSlope_ || mode != cachedMode_)
    {
        cachedX1_ = x1; cachedX2_ = x2; cachedX3_ = x3;
        cachedSlope_ = slope; cachedMode_ = mode;
        weightsDirty_ = true;
        curvePathsValid_ = false;      // → timerCallback 门控据此强制重建曲线/Range
    }
}

void DisplayView::updateWeights()
{
    const float n = (float) (cachedSlope_ / 6);
    // 24/48 档（LR 树）|LP| = 1/(1+(f/fc)^n)；12/36 与 Linear（BW）|LP| = 1/sqrt(1+(f/fc)^2n)
    const bool lr = (cachedSlope_ == 24 || cachedSlope_ == 48)
                 && cachedMode_ != (int) Param::PhaseMode::Linear;

    const auto lpMag = [&] (float f, float fc)
    {
        const double r = (double) f / (double) juce::jmax (1.0f, fc);
        return lr ? (float) (1.0 / (1.0 + std::pow (r, (double) n)))
                  : (float) (1.0 / std::sqrt (1.0 + std::pow (r, 2.0 * (double) n)));
    };
    const auto hpMag = [&] (float f, float fc)
    {
        const double r = (double) juce::jmax (1.0f, fc) / (double) juce::jmax (1.0f, f);
        return lr ? (float) (1.0 / (1.0 + std::pow (r, (double) n)))
                  : (float) (1.0 / std::sqrt (1.0 + std::pow (r, 2.0 * (double) n)));
    };

    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float f = curveFreqs_[(size_t) i];
        const float m0 = lpMag (f, cachedX1_);
        const float h1 = hpMag (f, cachedX1_);
        const float m1 = h1 * lpMag (f, cachedX2_);
        const float h2 = h1 * hpMag (f, cachedX2_);
        const float m2 = h2 * lpMag (f, cachedX3_);
        const float m3 = h2 * hpMag (f, cachedX3_);
        const float sum = m0 + m1 + m2 + m3 + 1.0e-9f;
        bandWeights_[0][(size_t) i] = m0 / sum;
        bandWeights_[1][(size_t) i] = m1 / sum;
        bandWeights_[2][(size_t) i] = m2 / sum;
        bandWeights_[3][(size_t) i] = m3 / sum;
    }
    weightsDirty_ = false;
}

void DisplayView::updateCurve()
{
    if (weightsDirty_)
        updateWeights();

    // 三层曲线模型（Pro-MB 语义，2026-09-23 用户纠正）：
    //   raw    = Σw×makeup          未压缩响应（BYP 打开时的"原定 EQ 曲线"）
    //   live   = Σw×(makeup+gain)   实时压缩响应
    //   disp   = max(live, raw+range) 被 range 截断后的 EQ 层（吸附 range 地板）
    //   rngEdge= Σw×makeup + bell×range  Range 地板线（**不随实时压缩运动**）
    for (int b = 0; b < 4; ++b)
    {
        const float mk = proc_.apvts.getRawParameterValue (Param::bandMakeup (b))->load();
        const float rangeDb = proc_.apvts.getRawParameterValue (Param::bandRange (b))->load();
        bandRangeDb_[(size_t) b] = rangeDb;
        const bool bypassed = proc_.apvts.getRawParameterValue (Param::bandMix (b))->load() < 0.5f;

        bandRawDb_[(size_t) b]  = mk;                                  // 未压缩
        bandLiveDb_[(size_t) b] = mk + proc_.getBandGainDb (b);        // 实际响应

        // P0：一条实时粗线 = 实际响应（**不做 range 截断**，Pro-MB 语义：
        // 黄线允许压过 range 深度；range 只决定"潜在范围"钟型区的底板）
        bandDisplayDb_[(size_t) b] = bypassed ? bandRawDb_[(size_t) b]
                                              : bandLiveDb_[(size_t) b];
    }

    for (int i = 0; i < kCurvePoints; ++i)
    {
        float d = 0.0f, base = 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            const float w = bandWeights_[(size_t) b][(size_t) i];
            d    += w * bandDisplayDb_[(size_t) b];
            base += w * bandRawDb_[(size_t) b];
        }
        curveDb_[(size_t) i]  = d;           // EQ 粗线（实时总响应）
        baseDb_[(size_t) i]   = base;        // Range 层顶边基准（静态）
    }

    // P1：Display Range 目标量程（平滑动画在 timerCallback 里做，那里有 dt）
    //   · Auto（模式 0）：按曲线值/Range 深度自动选档（3/6/12/30），只升不降
    //   · 手动（1..4）：固定档位
    //   ⚠️ Makeup/Range 拖动期间**冻结目标量程**：否则拖动让曲线值变化 → 触发升档
    //   → 量程平滑追赶 → gainToY 映射持续微动 → 用户看到"卡顿/抖动"（2026-09-23）
    if (drag_ != DragMode::Makeup && drag_ != DragMode::Range)
    {
        float target = curveRangeDb_;
        if (displayRangeMode_ == 0)
        {
            float mx = 0.0f;
            for (int i = 0; i < kCurvePoints; ++i)
                mx = juce::jmax (mx, std::abs (curveDb_[(size_t) i]));
            for (int b = 0; b < Param::kNumBands; ++b)
                mx = juce::jmax (mx, std::abs (bandRangeDb_[(size_t) b]));
            const float autoTarget = (mx <= 3.0f) ? 3.0f : (mx <= 6.0f) ? 6.0f
                                   : (mx <= 12.0f) ? 12.0f : 30.0f;
            target = juce::jmax (curveRangeDb_, autoTarget);      // 只升不降
        }
        else
        {
            const float fixed[5] = { 6.0f, 3.0f, 6.0f, 12.0f, 30.0f };
            target = fixed[displayRangeMode_];
        }
        curveRangeDb_ = target;
    }
}

float DisplayView::bellWeight (int band, float freq) const
{
    // B3（Pro-MB 样式）：以段几何中心为中心的钟形，宽度 = 段宽 × 1.5，
    // 允许溢出分割线（用户验收要求）；形状 sin²（边缘归零）。
    const float lo = (band == 0) ? 20.0f
                   : (band == 1) ? cachedX1_
                   : (band == 2) ? cachedX2_ : cachedX3_;
    const float hi = (band == 0) ? cachedX1_
                   : (band == 1) ? cachedX2_
                   : (band == 2) ? cachedX3_ : 20000.0f;
    if (hi <= lo || freq <= 0.0f) return 0.0f;

    const float w = (std::log2 (hi) - std::log2 (lo)) * 1.5f;      // 1.5× 段宽
    const float c = 0.5f * (std::log2 (hi) + std::log2 (lo));      // 几何中心（log 域）
    const float u = (std::log2 (freq) - c) / w + 0.5f;
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    const float s = std::sin (juce::MathConstants<float>::pi * u);
    return s * s;
}

void DisplayView::refreshCurvePaths()
{
    // EQ 曲线路径：直接操作成员 Path（clear + lineTo），避免"新建局部 Path 再赋值"
    // 触发的 COW 拷贝（早期每帧 3 次 Path 分配 + 引用计数搬移）
    // EQ 曲线路径：直接操作成员 Path（clear + lineTo），避免"新建局部 Path 再赋值"
    // 触发的 COW 拷贝（早期每帧 3 次 Path 分配 + 引用计数搬移）
    // EQ 曲线路径：直接操作成员 Path（clear + lineTo），避免"新建局部 Path 再赋值"
    // 触发的 COW 拷贝（早期每帧 3 次 Path 分配 + 引用计数搬移）
    curvePath_.clear();
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float x = freqToX (curveFreqs_[(size_t) i]);
        const float yMain = gainToY (curveDb_[(size_t) i]);
        if (i == 0) curvePath_.startNewSubPath (x, yMain);
        else        curvePath_.lineTo (x, yMain);
    }

    // Range 钟型层（#1 修复：**4 段都画**，改任一段 range 参数都即时可见）
    //   顶边 = baseDb_（未压缩基准，静态），底边 = baseDb_ + bell×range
    {
        for (int b = 0; b < 4; ++b)
        {
            const float rangeDb = rangeDisplay_[(size_t) b];
            const bool bypassed = proc_.apvts.getRawParameterValue (
                Param::bandMix (b))->load() < 0.5f;

            auto& fill = rangeFillPath_[(size_t) b];
            auto& edge = rangeEdgePath_[(size_t) b];
            fill.clear();
            edge.clear();
            for (int i = 0; i < kCurvePoints; ++i)
            {
                const float f = curveFreqs_[(size_t) i];
                const float w = bellWeight (b, f);
                const float x = freqToX (f);
                const float yTop = gainToY (baseDb_[(size_t) i]);
                const float yBot = gainToY (baseDb_[(size_t) i] + w * rangeDb);
                if (i == 0) { fill.startNewSubPath (x, yTop); edge.startNewSubPath (x, yBot); }
                else        { fill.lineTo (x, yTop);          edge.lineTo (x, yBot); }
            }
            if (! bypassed)                                  // BYP：只描边
            {
                for (int i = kCurvePoints - 1; i >= 0; --i)
                    fill.lineTo (freqToX (curveFreqs_[(size_t) i]),
                                 gainToY (baseDb_[(size_t) i]
                                          + bellWeight (b, curveFreqs_[(size_t) i]) * rangeDb));
                fill.closeSubPath();
            }
        }
        cachedRange_ = rangeDisplay_[(size_t) selectedBand_];
        cachedRangeBand_ = selectedBand_;
        rangeBypassed_ = proc_.apvts.getRawParameterValue (
            Param::bandMix (selectedBand_))->load() < 0.5f;
    }
    curvePathsValid_ = true;
}

// Phase A：refreshSpectrumPaths() / drawSpectrum() 已整体拆到 SpectrumView 组件。
// DisplayView 只注入坐标映射（构造时）并每帧调 updateFromEngine()。

void DisplayView::drawRangeBell (juce::Graphics& g)
{
    // #1：4 段都画；非选中段低透明度，选中段正常
    for (int b = 0; b < 4; ++b)
    {
        const bool sel = (b == selectedBand_);
        const float a = sel ? 1.0f : 0.45f;
        if (! rangeFillPath_[(size_t) b].isEmpty())
        {
            g.setColour (pal_.accent.withAlpha (0.14f * a));
            g.fillPath (rangeFillPath_[(size_t) b]);
        }
        g.setColour (pal_.accent.withAlpha (0.55f * a));
        g.strokePath (rangeEdgePath_[(size_t) b], juce::PathStrokeType (1.2f));

        // ---- Phase C-4：Range 滑杆手柄（Pro-MB range slider 对标）----
        // 钟型下缘本来就能拖，但**没有可见抓手**，用户不知道那里可交互。
        // 在段中心频率处的下缘上放一个胶囊手柄，让"可拖"这件事不言自明。
        // 只在选中或 hover 时显示（避免 4 段常驻造成视觉噪音）。
        const float hoverK = hoverT_[(size_t) b].value (animNowMs());
        const float selK   = selT_[(size_t) b].value (animNowMs());
        const float showK  = juce::jmax (selK, hoverK);
        if (showK > 0.01f && ! curveDb_.empty())
        {
            const float fcen = bandCenterFreq (b);
            const float px   = freqToX (fcen);
            // 找段中心处钟型的权重，算出下缘 y（与 hitTestRangeEdge 同公式）
            int best = 0; float bd = 1.0e9f;
            for (int i = 0; i < kCurvePoints; ++i)
            {
                const float d = std::abs (curveFreqs_[(size_t) i] - fcen);
                if (d < bd) { bd = d; best = i; }
            }
            const float w    = bellWeight (b, fcen);
            const float yLo  = gainToY (baseDb_[(size_t) best]
                                        + w * rangeDisplay_[(size_t) b]);
            if (w > 0.05f)
            {
                const float hw = sx (18.0f), hh = sy (7.0f);
                auto handle = juce::Rectangle<float> (px - hw * 0.5f, yLo - hh * 0.5f, hw, hh);
                const bool dragging = (drag_ == DragMode::Range && dragBand_ == b);
                const float glow = dragging ? 1.0f : 0.0f;
                g.setColour (pal_.accent.interpolatedWith (pal_.accent.brighter (0.35f), glow)
                                          .withAlpha (0.95f * showK));
                g.fillRoundedRectangle (handle, hh * 0.5f);
                // 左右两道小刻痕，暗示"可横向/纵向拖"
                g.setColour (pal_.light ? juce::Colours::black.withAlpha (0.55f * showK)
                                        : juce::Colours::white.withAlpha (0.75f * showK));
                g.fillRect (px - hw * 0.26f, yLo - hh * 0.18f, sx (1.5f), hh * 0.36f);
                g.fillRect (px + hw * 0.26f - sx (1.5f), yLo - hh * 0.18f, sx (1.5f), hh * 0.36f);
            }
        }
    }
}

void DisplayView::drawResponseCurve (juce::Graphics& g)
{
    // P0：只画一条粗线（Pro-MB 的黄线 = 实时动态总响应）
    // ⚠️ 同 drawSpectrum：不用 valid 脏标志做 guard（会闪）
    if (! curveVisible_ || curvePath_.isEmpty())
        return;
    g.setColour (pal_.accent);
    g.strokePath (curvePath_, juce::PathStrokeType (2.2f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
}

// ---------------- Phase 4.4 交互层 ----------------
// 手势分区（UI_SPEC §4.2 / D14）：按钮列 [线−40,线−16] / 抓取带 [线±4] / 频点 / 曲线 / 其余=选段
// O5-A：EQ 曲线或频点纵向拖动 = Makeup（可跨段操作，C4）
// D14：分割线双击 = 精确频率输入
// ============================================================

bool DisplayView::hitTestRangeEdge (juce::Point<float> p, int& bandOut, bool& lowerEdge) const
{
    // Range 钟型的下缘（压缩方向那条）：命中后可上下拖动改 range 值（用户额外要求）
    // Phase C-4：下缘在段中心处放了个**可见胶囊手柄**（drawRangeBell），
    // 所以那里的命中区从 ±6px 加宽到 ±10px，让抓手真的好抓；
    // 非中心处仍保持 ±6px（避免与频点/曲线命中区互相抢占）。
    if (! curveVisible_ || curveDb_.empty())
        return false;
    const int b = selectedBand_;
    const float rangeDb = rangeDisplay_[(size_t) b];
    const float fcen = bandCenterFreq (b);
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float f = curveFreqs_[(size_t) i];
        const float w = bellWeight (b, f);
        const float x = freqToX (f);
        const float yLo = gainToY (baseDb_[(size_t) i] + w * rangeDb);   // 下缘：Range 地板（静态）
        const float yTop = gainToY (curveDb_[(size_t) i]);                // 上缘：当前曲线
        if (std::abs (p.x - x) > sx (4.0f))          // #3：窗收到 ±4px（原 6px 太宽，
            continue;                               // 与频点命中区重叠 → "抓错目标卡住")
        const bool atCentre = std::abs (f - fcen) < fcen * 0.02f;
        const float tolY = atCentre ? sy (10.0f) : sy (6.0f);
        if (std::abs (p.y - yLo) <= tolY && w > 0.05f)
        {
            bandOut = b;
            lowerEdge = true;
            return true;
        }
        if (std::abs (p.y - yTop) <= tolY && w > 0.05f)
        {
            bandOut = b;
            lowerEdge = false;
            return true;
        }
    }
    return false;
}

int DisplayView::hitTestFreqDot (juce::Point<float> p, int& bandOut) const
{
    if (! curveVisible_ || curveDb_.empty())
        return 0;
    for (int b = 0; b < 4; ++b)
    {
        const float fcen = bandCenterFreq (b);
        const float px = freqToX (fcen);
        // 曲线上该点高度：取最近曲线点
        int best = 0; float bd = 1.0e9f;
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const float d = std::abs (curveFreqs_[(size_t) i] - fcen);
            if (d < bd) { bd = d; best = i; }
        }
        const float py = gainToY (baseDb_[(size_t) best]);       // #5：只绑基准线（Makeup）
        const float r = sx (11.0f);                      // 命中半径（含光环）
        if (std::abs (p.x - px) <= r && std::abs (p.y - py) <= r)
        {
            bandOut = b;
            return 1;
        }
    }
    return 0;
}

namespace
{
    // 构造测试用鼠标事件（无真实桌面时经 Desktop 取 MouseInputSource）
    juce::MouseEvent mkTestMouse (juce::Point<float> p)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent (src, p, juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,   // pressure/orientation/rotation/tiltX/tiltY
                                 nullptr, nullptr,                // eventComponent/originator
                                 juce::Time(), p,                 // eventTime/mouseDownPos
                                 juce::Time(),                    // mouseDownTime
                                 1, false);                       // numberOfClicks/mouseWasDragged
    }
}

// Phase A：所有测试钩子统一进入**虚拟时钟**。
// ⚠️ 若不在这里也切时钟，mouseDownForTest 会用真实时间启动 Tween，
//    而 tickForTest 用从 0 开始的虚拟时钟求值 → t 为巨大负值 → 动画永远停在起点。
void DisplayView::enterTestClock()
{
    useTestClock_ = true;
}

void DisplayView::mouseDownForTest (float x, float y) { enterTestClock(); mouseDown (mkTestMouse (juce::Point<float> (x, y))); }
void DisplayView::mouseDragForTest (float x, float y) { enterTestClock(); mouseDrag (mkTestMouse (juce::Point<float> (x, y))); }
void DisplayView::mouseUpForTest   (float x, float y) { enterTestClock(); mouseUp   (mkTestMouse (juce::Point<float> (x, y))); }
void DisplayView::mouseMoveForTest (float x, float y) { enterTestClock(); mouseMove (mkTestMouse (juce::Point<float> (x, y))); }

// Phase A：推进虚拟时钟再跑一帧。advanceMs 默认 16ms ≈ 60fps。
void DisplayView::tickForTest (double advanceMs)
{
    useTestClock_ = true;
    testClockMs_ += advanceMs;
    timerCallback();
}

double DisplayView::animNowMs() const
{
    return useTestClock_ ? testClockMs_ : (double) juce::Time::getMillisecondCounter();
}

void DisplayView::mouseDoubleClickForTest (float x, float y)
{
    enterTestClock();
    mouseDoubleClick (mkTestMouse (juce::Point<float> (x, y)));
}

void DisplayView::setParamNorm (const juce::String& id, float norm) const
{
    // Phase A：O(1) 查表（原为 79 参数线性扫描 + dynamic_cast）
    const auto it = paramMap_.find (id);
    if (it != paramMap_.end() && it->second != nullptr)
        it->second->setValueNotifyingHost (norm);
}

// 按物理值设置（经 NormalisableRange 反归一化；xover 等偏斜参数必须走此路）
void DisplayView::setParamPhysical (const juce::String& id, float physical) const
{
    const auto it = paramMap_.find (id);
    if (it == paramMap_.end() || it->second == nullptr)
        return;
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
        it->second->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (physical));
    else
        it->second->setValueNotifyingHost (physical);
}

bool DisplayView::getParamBool (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    return (it != paramMap_.end() && it->second != nullptr)
               ? (it->second->getValue() >= 0.5f) : false;
}

float DisplayView::getParamNorm (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    return (it != paramMap_.end() && it->second != nullptr) ? it->second->getValue() : 0.0f;
}

float DisplayView::getParamPhysical (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    if (it == paramMap_.end() || it->second == nullptr)
        return 0.0f;
    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
        return (float) rp->getNormalisableRange().convertFrom0to1 (it->second->getValue());
    return it->second->getValue();
}

float DisplayView::paramRangeMinOf (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    if (it != paramMap_.end() && it->second != nullptr)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
            return (float) rp->getNormalisableRange().start;
    return 0.0f;
}

float DisplayView::paramRangeMaxOf (const juce::String& id) const
{
    const auto it = paramMap_.find (id);
    if (it != paramMap_.end() && it->second != nullptr)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (it->second))
            return (float) rp->getNormalisableRange().end;
    return 1.0f;
}

bool DisplayView::isBandBypassed (int band) const
{
    // D9 方案 A：Bypass = Mix<50% 宏。band{i}Mix 是浮点参数（0..100），
    // getRawParameterValue 返回物理百分比，必须直接比 50.0f。
    return getParamPhysical ("band" + juce::String (band) + "Mix") < 50.0f;
}

int DisplayView::hitTestButton (juce::Point<float> p, int& bandOut, int& btnOut) const
{
    // 每段按钮列 [缘−40, 缘−16]，y 从 118 起，4 个 24×24（间距 30）
    // ⚠️ 2026-09-23：band3 的"右缘"取 **IN/OUT 电平表左边界 −8px**（不是窗口右缘），
    //    这样按钮列在分区右侧、又完全不与音量表（x 886..918）重叠。
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    for (int b = 0; b < 4; ++b)
    {
        const float edge = (b < 3) ? nx[b] : juce::jmin (W, sx (kMeterX) - sx (8.0f));
        const float x0 = edge - sx (40.0f), x1 = edge - sx (16.0f);
        if (p.x < x0 || p.x > x1) continue;
        for (int j = 0; j < 4; ++j)
        {
            const float y0 = sy (118.0f + j * 30.0f), y1 = y0 + sy (24.0f);
            if (p.y >= y0 && p.y <= y1) { bandOut = b; btnOut = j; return 1; }
        }
    }
    return 0;
}

int DisplayView::hitTestDivider (juce::Point<float> p) const
{
    // Phase B-2：抓取带与底部手柄位置对齐（手柄 46×22，放在绘图区底缘）。
    // 竖直分割线仍是全高可抓（Pro-MB：整条线都能拖），底部手柄加宽命中区。
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float plotBottom = plotBottomPx();
    for (int e = 0; e < 3; ++e)
    {
        const bool onLine = std::abs (p.x - nx[e]) <= sx (4.0f)
                         && p.y >= 0.0f && p.y <= plotBottom;
        const bool onHandle = std::abs (p.x - nx[e]) <= sx (23.0f)
                           && p.y >= plotBottom - sy (22.0f) && p.y <= plotBottom;
        if (onLine || onHandle)
            return e;
    }
    return -1;
}

bool DisplayView::hitTestCurve (juce::Point<float> p, int& bandOut) const
{
    if (! curveVisible_ || curveDb_.empty())
        return false;
    int best = -1;
    float bestDx = 1.0e9f;
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float dx = std::abs (freqToX (curveFreqs_[(size_t) i]) - p.x);
        if (dx < bestDx) { bestDx = dx; best = i; }
    }
    if (best < 0 || bestDx > sx (12.0f))
        return false;
    const float y = gainToY (curveDb_[(size_t) best]);
    if (std::abs (y - p.y) <= sy (10.0f))
    {
        bandOut = selectedBand_;
        return true;
    }
    return false;
}

void DisplayView::applyDividerFreq (int index, float freq)
{
    float x1 = proc_.apvts.getRawParameterValue (Param::ID::xover1)->load();
    float x2 = proc_.apvts.getRawParameterValue (Param::ID::xover2)->load();
    float x3 = proc_.apvts.getRawParameterValue (Param::ID::xover3)->load();
    if (index == 0) x1 = freq;
    if (index == 1) x2 = freq;
    if (index == 2) x3 = freq;
    Param::clampCrossovers (x1, x2, x3);       // ≥1 八度链（D3 / §3.8）
    setParamPhysical (Param::ID::xover1, x1);
    setParamPhysical (Param::ID::xover2, x2);
    setParamPhysical (Param::ID::xover3, x3);
}

void DisplayView::applyCenterFreq (int band, float freq)
{
    // P2 + #1：横拖频点 = 改该段"中心频率"
    //   · band1/2（两缘自由）：整段平移
    //   · band0（左缘锁 20Hz）/ band3（右缘锁 20kHz）：中心移动时**活动边跟随**，
    //     左缘/右缘固定 → 中心左移=收缩、右移=扩宽（修"只能单向拖"的 bug）
    float x1 = cachedX1_, x2 = cachedX2_, x3 = cachedX3_;
    const float kLo = 20.0f, kHi = 20000.0f;

    if (band == 0)
    {
        // 右缘 = 中心² / 左缘(20)
        const float maxC = std::sqrt (kLo * x2 * 0.5f);      // 保证 x1 ≤ x2/2（≥1 八度）
        const float newC = juce::jlimit (kLo, maxC, freq);
        x1 = juce::jlimit (kLo, x2 * 0.5f, (newC * newC) / kLo);
    }
    else if (band == 3)
    {
        // 左缘 = 中心² / 右缘(20k)；中心下界由"x3 ≥ x2·2"决定：minC = sqrt(2·x2·kHi)
        const float minC = std::sqrt (2.0f * x2 * kHi);
        const float newC = juce::jlimit (minC, kHi, freq);
        x3 = juce::jlimit (x2 * 2.0f, kHi, (newC * newC) / kHi);
    }
    else
    {
        float lo = (band == 1) ? x1 : x2;
        float hi = (band == 1) ? x2 : x3;
        if (hi <= lo) return;
        const float oldC = std::sqrt (lo * hi);
        float newC = juce::jlimit (lo, hi, freq);
        const float shift = std::log2 (newC / oldC);
        float newLo = lo * std::pow (2.0f, shift);
        float newHi = hi * std::pow (2.0f, shift);
        if (newLo < kLo)    { newHi *= kLo / newLo;    newLo = kLo; }
        if (newHi > kHi)    { newLo *= kHi / newHi;    newHi = kHi; }
        if (band == 1) { x1 = newLo; x2 = newHi; }
        else           { x2 = newLo; x3 = newHi; }
    }

    Param::clampCrossovers (x1, x2, x3);
    setParamPhysical (Param::ID::xover1, x1);
    setParamPhysical (Param::ID::xover2, x2);
    setParamPhysical (Param::ID::xover3, x3);
    cachedX1_ = x1; cachedX2_ = x2; cachedX3_ = x3;
    weightsDirty_ = true;
    curvePathsValid_ = false;
}

void DisplayView::toggleBypass (int band, bool forceBypass)
{
    // 方案 A UI 宏：按下记忆当前 Mix→设 0；再按恢复（DESIGN §6.6）
    const juce::String id = "band" + juce::String (band) + "Mix";
    const float cur = proc_.apvts.getRawParameterValue (id)->load();
    if (forceBypass)
    {
        if (cur > 0.5f)                            // 未 bypass → 置 0（记忆原值）
        {
            bypassMemo_[(size_t) band] = cur;
            setParamNorm (id, 0.0f);
        }
        return;                                    // 已 bypass 则保持
    }
    if (cur > 0.5f)                                // 单段点击：未 bypass → 置 0
    {
        bypassMemo_[(size_t) band] = cur;
        setParamNorm (id, 0.0f);
    }
    else
        setParamNorm (id, bypassMemo_[(size_t) band] / 100.0f);   // 已 bypass → 恢复
}

bool DisplayView::isAllBypassed() const
{
    for (int b = 0; b < 4; ++b)
        if (proc_.apvts.getRawParameterValue ("band" + juce::String (b) + "Mix")->load() > 0.5f)
            return false;
    return true;
}

bool DisplayView::hitTestGlobalBypass (juce::Point<float> p) const
{
    // global bar 的 BYPASS 胶囊：与 drawGlobalBar 绘制位置严格一致
    // （W−108..W−16，y = H−32+5，高 22）
    const float W = (float) getWidth();
    const float x0 = W - sx (108.0f), x1 = W - sx (16.0f);
    const float y0 = (float) getHeight() - sy (kGlobalBarH) + sy (5.0f);
    const float y1 = y0 + sy (22.0f);
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
}

// #7：自绘频率输入框 —— Phase A 已拆为 FreqInputEditor 组件。
// 这里只保留"打开"逻辑（关闭/绘制/键盘都在组件内）。
void DisplayView::openFreqEditor (int index, int band)
{
    if (freqInput_ == nullptr)
        return;
    float x = 0.0f, y = 0.0f;
    if (band >= 0)
    {
        // #7：输入框出现在 hover 简易信息窗的位置（频点上方）
        x = freqToX (bandCenterFreq (band));
        y = freqDotScreenY (band) - sy (14.0f);
    }
    else
    {
        x = (index == 0) ? freqToX (cachedX1_)
          : (index == 1) ? freqToX (cachedX2_) : freqToX (cachedX3_);
        y = sy (470.0f);
    }
    freqInput_->open (index, band, x, y);
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

bool DisplayView::keyPressed (const juce::KeyPress& key)
{
    if (freqInput_ != nullptr && freqInput_->keyPressed (key))
        return true;
    return false;
}

void DisplayView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    lastMousePos_ = p;
    if (bandPopover_ != nullptr)
        bandPopover_->setLastMousePos (p);

    // Phase D：Analyzer 按钮悬停 → 面板打开（非 sticky 时）
    // 这是 Pro-MB 的签名式交互：悬停即开，点击才钉住。
    if (analyzerPanel_ != nullptr)
        analyzerPanel_->hoverButton (hitTestAnalyzerButton (p)
                                     || analyzerPanel_->panelRect().contains (p));
    // Phase D-2：LINKED 抽屉按钮同样的悬停/sticky 语义
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->hoverButton (linkedDrawer_->hitTestButton (p)
                                     || linkedDrawer_->hitTestPanel (p));

    int b = -1, j = -1;
    const bool onBtn = hitTestButton (p, b, j) != 0;
    int dotBand = -1;
    const bool onDot = hitTestFreqDot (p, dotBand) != 0;
    const int div = hitTestDivider (p);

    int newHover = -1;
    if (onBtn) newHover = b;
    else if (onDot) newHover = dotBand;
    else if (div < 0)
    {
        const float W = (float) getWidth();
        const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
        const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
        for (int i = 0; i < 4; ++i)
            if (p.x >= edges[i] && p.x < edges[i + 1]) newHover = i;
    }
    if (newHover != hoverBand_)
    {
        hoverBand_ = newHover;
        // Phase A：hover 淡入淡出（UI_SPEC §4.2 原话"hover 该段时淡入、移出淡出"）
        const double nowMs = animNowMs();
        for (int b = 0; b < Param::kNumBands; ++b)
            tweenTo (hoverT_[(size_t) b], nowMs, (b == newHover) ? 1.0f : 0.0f, Anim::kFadeMs);
        repaint();
    }

    // C2：分割线 hover/拖动主题色（Phase A：渐变）
    if (div != hoverDividers_)
    {
        hoverDividers_ = div;
        const double nowMs = animNowMs();
        for (int e = 0; e < 3; ++e)
            tweenTo (divT_[(size_t) e], nowMs, (e == div) ? 1.0f : 0.0f, Anim::kFadeMs);
        repaint();
    }

    // C4：hover 频点 → 置 hoverDotBand_（驱动 tooltip 浮出）
    if (onDot && hoverDotBand_ != dotBand) { hoverDotBand_ = dotBand; repaint(); }
    else if (! onDot && hoverDotBand_ != -1) { hoverDotBand_ = -1; repaint(); }

    // 光标：#6 用户要求——频点上保持默认指针，不切换样式；分割线仍用左右箭头
    int rbTest = -1; bool lowerTest = true;
    if (div >= 0)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
    (void) rbTest; (void) lowerTest; (void) onBtn; (void) onDot;
}

bool DisplayView::hitTestDisplayRange (juce::Point<float> p) const
{
    // 与 drawTopStrip 中的按钮位置严格一致：W−218..W−132，y 3..27
    const float W = (float) getWidth();
    const juce::Rectangle<float> r (W - sx (218.0f), sy (3.0f), sx (86.0f), sy (24.0f));
    return r.contains (p);
}

void DisplayView::openDisplayRangeMenu()
{
    juce::PopupMenu m;
    const char* labels[5] = { "Auto (自动扩档)", "+/- 3 dB", "+/- 6 dB", "+/- 12 dB", "+/- 30 dB" };
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, labels[i], true, i == displayRangeMode_);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int result)
        {
            if (result <= 0 || result > 5) return;
            displayRangeMode_ = result - 1;
            if (displayRangeMode_ != 0)
            {
                const float fixed[5] = { 6.0f, 3.0f, 6.0f, 12.0f, 30.0f };
                curveRangeDb_ = fixed[displayRangeMode_];
            }
            repaint();
        });
}

void DisplayView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // #4 修订：只要在**分区内**滚轮就可改该段大小（不要求 hover 在频点上）
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };

    int band = -1;
    for (int i = 0; i < 4; ++i)
        if (e.position.x >= edges[i] && e.position.x < edges[i + 1]) { band = i; break; }
    if (band < 0)                                   // 落在分割线抓取带/音量表 → 不处理
        return;

    // 滚轮上（deltaY>0）→ 扩宽；下 → 收窄。步进 6%
    const float k = (wheel.deltaY > 0.0f ? 1.06f : 1.0f / 1.06f);

    float x1 = cachedX1_, x2 = cachedX2_, x3 = cachedX3_;
    float lo, hi;
    if (band == 0)      { lo = 20.0f;  hi = x1; }
    else if (band == 1) { lo = x1;     hi = x2; }
    else if (band == 2) { lo = x2;     hi = x3; }
    else                { lo = x3;     hi = 20000.0f; }
    if (hi <= lo) return;

    float newLo = lo / k, newHi = hi * k;
    newLo = juce::jmax (20.0f,    newLo);
    newHi = juce::jmin (20000.0f, newHi);

    if (band == 0)      x1 = newHi;
    else if (band == 1) { x1 = newLo; x2 = newHi; }
    else if (band == 2) { x2 = newLo; x3 = newHi; }
    else                x3 = newLo;

    Param::clampCrossovers (x1, x2, x3);
    setParamPhysical (Param::ID::xover1, x1);
    setParamPhysical (Param::ID::xover2, x2);
    setParamPhysical (Param::ID::xover3, x3);
    cachedX1_ = x1; cachedX2_ = x2; cachedX3_ = x3;
    weightsDirty_ = true;
    curvePathsValid_ = false;
    repaint();
}

void DisplayView::mouseDown (const juce::MouseEvent& e)
{
    // Phase D-3：每次 mouseDown 都重置拖拽状态——新手势开始。
    // ⚠️ 不清零的后果：若上一次 mouseDown 布防了 BandSweep 而 mouseUp 从未到达
    //    （鼠标移出窗口、模态吞掉事件、测试里只按不松），drag_ 会卡在 BandSweep，
    //    之后在 Popover 滑杆/旋钮上起手的拖拽会被这个陈旧状态劫走。
    //    这是 T40.8 回归的根因，也是真实存在的陈旧手势 bug。
    drag_ = DragMode::None;
    bandSweepActive_ = false;
    curvePending_ = false;
    rangePending_ = false;

    // #7：输入框激活时，先点别处 = 取消
    if (freqInput_ != nullptr && freqInput_->isActive())
    {
        freqInput_->close (false);
        return;
    }

    const auto p = e.position;

    // 0) Phase 5.3：Popover 打开时，点 ✕ → 收起
    if (bandPopover_ != nullptr && bandPopover_->isOpen() && bandPopover_->hitTestClose (p))
    {
        togglePopover (false);
        return;
    }

    // 0a) Analyzer 按钮（Pro-MB 底栏 Analyzer 对标，当前占原 IN 读数位）
    // Phase D：**Pro-MB 签名式交互** —— 悬停打开（非 sticky）、点击变 sticky、再点收起。
    // 原来只有"点击弹原生菜单"，没有悬停语义，也没有 sticky。
    if (hitTestAnalyzerButton (p))
    {
        if (analyzerPanel_ != nullptr)
            analyzerPanel_->clickButton();
        repaint();
        return;
    }
    // Analyzer 面板行点击（面板自己画，命中在这里分发）
    if (analyzerPanel_ != nullptr && analyzerPanel_->isOpen())
    {
        int row = -1;
        if (analyzerPanel_->hitTestRow (p, row))
        {
            analyzerPanel_->clickRowPublic (row);
            repaint();
            return;
        }
        // 点在面板外（且不在按钮上）→ 收起
        if (! analyzerPanel_->panelRect().contains (p))
            analyzerPanel_->clickOutside();
    }

    // 0b) Popover 内部控制：滑杆行 + EXPERT（旋钮由 Knob 子组件自行处理命中）
    if (bandPopover_ != nullptr && bandPopover_->isOpen())
    {
        int sk = -1;
        if (bandPopover_->hitTestSlider (p, sk))
        {
            bandPopover_->beginSliderDrag (sk, p.x);
            // 二态项（EXT/DETECTOR，k>=2）：点击即翻转，不进入拖动。
            // 2026-10-14：KNEE(k=1) 改为连续 0–48 dB，现在是可拖动滑杆。
            if (sk >= 2)
            {
                bandPopover_->applySliderForTest (sk,
                    bandPopover_->sliderValue (sk) > 0.5 ? 0.0 : 1.0);
                bandPopover_->endSliderDrag();
            }
            repaint();
            return;
        }
        if (bandPopover_->expertRect().contains (p))
        {
            bandPopover_->toggleExpert();
            repaint();
            return;
        }
        // Phase C-5：EXPERT 展开内容的两行可点（Detector 翻转 / Lookahead 循环）
        {
            const auto row = bandPopover_->hitTestExpertRow (p);
            if (row != BandPopover::ExpertRow::None)
            {
                bandPopover_->clickExpertRow (row);
                repaint();
                return;
            }
        }
    }

    // 0) LINKED 抽屉梯形按钮（§5.2）：Phase D-2 sticky 语义
    //    悬停打开（非 sticky）→ 点击变 sticky 钉住 → 再点收起并解除。
    //    与 AnalyzerPanel（D-1）同一套状态机，对齐 Pro-MB 底栏按钮行为。
    if (linkedDrawer_ != nullptr && linkedDrawer_->hitTestButton (p))
    {
        // O6 方案1：与 Popover 互斥
        togglePopover (false);
        linkedDrawer_->clickButton();
        repaint();
        return;
    }
    // 抽屉打开时，点面板外 = 收起并解除 sticky
    if (linkedDrawer_ != nullptr && linkedDrawer_->isOpen()
        && ! linkedDrawer_->hitTestPanel (p))
        linkedDrawer_->clickOutside();

    // 0a) Display Range 档位：点击弹出二级菜单（Pro-MB drop-down 语义）
    if (hitTestDisplayRange (p))
    {
        openDisplayRangeMenu();
        return;
    }

    // 0) 全局 BYPASS 胶囊（A8 区，global bar 右侧）
    if (hitTestGlobalBypass (p))
    {
        const bool allBypassed = isAllBypassed();
        for (int i = 0; i < 4; ++i)
            toggleBypass (i, ! allBypassed);
        for (int i = 0; i < 4; ++i)                     // Phase A：驱动渐暗动画
            onBandBypassChanged (i, ! allBypassed);
        repaint();
        return;
    }

    // 1) 按钮列（S/M/BYP/Δ）
    int b = -1, j = -1;
    if (hitTestButton (p, b, j) != 0)
    {
        if (selectedBand_ != b) { selectedBand_ = b; onSelectedBandChanged (b); }   // Phase A
        const juce::String bp = "band" + juce::String (b);
        if (j == 0) setParamNorm (bp + "Solo",  getParamBool (bp + "Solo")  ? 0.0f : 1.0f);
        if (j == 1) setParamNorm (bp + "Mute",  getParamBool (bp + "Mute")  ? 0.0f : 1.0f);
        if (j == 2) { toggleBypass (b); onBandBypassChanged (b, isBandBypassed (b)); }  // Phase A
        if (j == 3) setParamNorm (bp + "Delta", getParamBool (bp + "Delta") ? 0.0f : 1.0f);
        repaint();
        return;
    }

    // 2) 频点（C4+P2）：选中该段；纵拖=Makeup、横拖=中心频率
    //    #3：优先按"距离最近的目标"判定，避免 Range 缘抢占频点邻域
    int dotBand = -1;
    const bool onDot = hitTestFreqDot (p, dotBand) != 0;
    if (onDot)
    {
        if (selectedBand_ != dotBand) { selectedBand_ = dotBand; onSelectedBandChanged (dotBand); }  // Phase A
        drag_ = DragMode::Makeup;
        dragBand_ = dotBand;
        dragStartY_ = p.y;
        dragStartX_ = p.x;
        dragAxisLocked_ = false;               // #3：重置主轴锁定
        dragAxisIsX_ = false;
        dragStartMakeup_ = proc_.apvts.getRawParameterValue (
            "band" + juce::String (dotBand) + "Makeup")->load();
        repaint();
        return;
    }

    // 2.5) Range 钟型下缘（额外要求：拖动改 range 值）——放在曲线之后，
    //      避免曲线下方 6px 邻域被 Range 缘抢先占用（2026-09-23 T21.4 回归教训）
    // 3) 分割线抓取带
    const int div = hitTestDivider (p);
    if (div >= 0)
    {
        drag_ = DragMode::Divider;
        dragIndex_ = div;
        return;
    }

    // 4) EQ 曲线（O5-A）——**按下先挂起**：点击=选中+开 Popover，真正拖动才改成 Makeup。
    //    （§6「点分区→选中+Popover 弹出」与 O5-A「曲线纵拖=Makeup」在同一像素区冲突，
    //      按 Pro-MB 语义用"是否发生位移"区分二者）
    int curveBand = -1;
    const bool onCurve = hitTestCurve (p, curveBand);
    if (onCurve)
    {
        curvePending_ = true;
        curvePendingBand_ = selectedBand_;
        curveDownX_ = p.x;
        curveDownY_ = p.y;
        // 不 return：继续往下走选中/Popover 分支
    }

    // 4.5) Range 钟型**下缘**（仅当曲线未命中时；否则曲线永远是赢家）
    {
        int rb = -1; bool lower = true;
        if (! onCurve && hitTestRangeEdge (p, rb, lower) && lower)
        {
            curvePending_ = true;
            curvePendingBand_ = (rb >= 0) ? rb : selectedBand_;
            curveDownX_ = p.x;
            curveDownY_ = p.y;
            rangePending_ = true;
            rangePendingBand_ = rb;
            rangePendingLower_ = lower;
        }
    }

    // 5) 点分区 → 选中 + Popover 弹出（§4.1「选段开菜单」/§6）
    //    ⚠️ 仅响应**绘图区**内的点击（顶栏/global bar/抽屉按钮不算分区）
    //    Phase D-3：在绘图区背景按下时**同时**准备多选（BandSweep）。
    //    单击仍走原来的"选中+开 Popover"；一旦发生位移就升级为多选，
    //    扫过的频段全部加入选中（Pro-MB："click and drag across the display
    //    background to quickly select multiple bands"）。
    const float plotTop = plotTopPx();
    const float plotBottom = plotBottomPx();
    const bool inPlotArea = (p.y >= plotTop && p.y <= plotBottom);
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
    if (inPlotArea)
    {
        // 记录多选快照：拖拽开始时的各段选中态
        // ⚠️ 只有在曲线/Range 缘都没有被命中时才布防多选——
        //    那两个分支靠 curvePending_/rangePending_ 在随后的 drag 中接管，
        //    若这里无条件布防，BandSweep 会抢走它们的拖拽。
        if (! curvePending_ && ! rangePending_)
        {
            bandSweepActive_ = true;
            drag_ = DragMode::BandSweep;
        }

        for (int i = 0; i < 4; ++i)
            if (p.x >= edges[i] && p.x < edges[i + 1])
            {
            const bool popoverWasOpen = (bandPopover_ != nullptr && bandPopover_->isOpen());
            if (selectedBand_ != i)
            {
                selectedBand_ = i;
                hoverBand_ = i;
                onSelectedBandChanged (i);              // Phase A：选中渐变
                for (int bb = 0; bb < Param::kNumBands; ++bb)   // Phase A：hover 跟随
                    tweenTo (hoverT_[(size_t) bb], animNowMs(),
                             (bb == i) ? 1.0f : 0.0f, Anim::kFadeMs);
                curvePathsValid_ = false;          // #4：切段 → 强制重建
                if (popoverWasOpen)
                    openPopoverForBand (i);        // 跟随更新（不关闭）
                else
                    togglePopover (true);
            }
            else
                togglePopover (! popoverWasOpen);      // 同段再点 = toggle
            if (bandPopover_ != nullptr && bandPopover_->isOpen())
                bandPopover_->syncFromParams();
            repaint();
            return;
        }
    }
    // 6) 点空白 → 收起 Popover（§6）
    togglePopover (false);
    repaint();
}

void DisplayView::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;

    // Phase D-3：显示背景拖拽多选（Pro-MB multi-select）
    // Pro-MB 官方帮助："click and drag across the display background to
    // quickly select multiple bands"。
    //
    // ⚠️ 语义说明：本插件是**单选中**模型（selectedBand_ 是单个 int，没有多选集合），
    //    所以这里不做"快照 + toggle"式的真多选——那套逻辑在单选变量上是不自洽的
    //    （未被扫过的段在模型里"仍被选中"，会在循环里覆盖结果；曾因 selectedBand_
    //    默认值是 2 而非 0 导致 T43.4 回归）。
    //    实际可感知的表现：拖动过程中选中段**连续跟随光标**，一扫而过就能连续
    //    切换/预览多个频段——这正是 Pro-MB 该手势想要的速度。
    //
    // ⚠️ 只在**没有其他拖拽意图**时才接管：曲线/Range 缘起手的拖动
    //    必须继续走 curvePending_/rangePending_ 分支（O5-A 语义），
    //    否则这里会抢走 Makeup/Range 拖拽（曾因此回归 9 条测试）。
    if (drag_ == DragMode::BandSweep && bandSweepActive_
        && ! curvePending_ && ! rangePending_)
    {
        const float W = (float) getWidth();
        const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
        const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
        const int hit = (p.x < edges[1]) ? 0 : (p.x < edges[2]) ? 1 : (p.x < edges[3]) ? 2 : 3;

        if (hit != selectedBand_)
        {
            selectedBand_ = hit;
            onSelectedBandChanged (hit);
            curvePathsValid_ = false;
        }
        // hover 高亮跟随（独立于选中态，让扫过时有连续反馈）
        hoverBand_ = hit;
        for (int bb = 0; bb < Param::kNumBands; ++bb)
            tweenTo (hoverT_[(size_t) bb], animNowMs(),
                     (bb == hit) ? 1.0f : 0.0f, Anim::kFadeMs);
        repaint();
        return;
    }

    // Popover 滑杆拖动（连续项：RATIO k=0、KNEE k=1；EXT/DETECTOR 仍是点击翻转）
    if (bandPopover_ != nullptr)
    {
        const int k = bandPopover_->sliderDragIndex();
        if (k == 0 || k == 1)
        {
            bandPopover_->dragSlider (k, p.x);
            repaint();
            return;
        }
    }

    // O5-A：曲线上按下 → 真正发生位移才接管为 Makeup 拖拽，并收起 Popover
    if (curvePending_)
    {
        const float dyEvt = p.y - curveDownY_;
        if (std::abs (dyEvt) < 4.0f && std::abs (p.x - curveDownX_) < 4.0f)
            return;                                    // 抖动/静止：仍是"点击"语义
        curvePending_ = false;

        if (rangePending_)
        {
            // Range 缘拖动接管：**本帧立即应用**（内联一次 Range 计算）
            rangePending_ = false;
            const float dbPerPx = (2.0f * curveRangeDb_) / plotHeightPx();
            const float startRange = proc_.apvts.getRawParameterValue (
                "band" + juce::String (rangePendingBand_) + "Range")->load();
            const float newRange = juce::jlimit (-30.0f, 0.0f,
                startRange + (curveDownY_ - p.y) * dbPerPx);
            setParamNorm ("band" + juce::String (rangePendingBand_) + "Range",
                          (newRange + 30.0f) / 30.0f);
            drag_ = DragMode::Range;
            dragBand_ = rangePendingBand_;
            dragStartY_ = curveDownY_;
            dragStartRange_ = startRange;
            togglePopover (false);
            return;
        }

        drag_ = DragMode::Makeup;
        dragBand_ = curvePendingBand_ >= 0 ? curvePendingBand_ : selectedBand_;
        dragStartY_ = curveDownY_;
        dragStartX_ = curveDownX_;
        dragStartMakeup_ = proc_.apvts.getRawParameterValue (
            "band" + juce::String (dragBand_) + "Makeup")->load();
        togglePopover (false);        // 拖动模式：收起浮层
        // 不 return —— 继续走 Makeup 分支，让本帧位移立即生效
    }

    if (drag_ == DragMode::Divider)
    {
        const float f = juce::jlimit (40.0f, 10000.0f, xToFreq (p.x));
        applyDividerFreq (dragIndex_, f);
        return;
    }

    if (drag_ == DragMode::Range)
    {
        // Range 拖动：上拖 range 增大（向 0 dB 靠近）；用动态量程
        const float dbPerPx = (2.0f * curveRangeDb_) / plotHeightPx();
        const float newRange = juce::jlimit (-30.0f, 0.0f, dragStartRange_ + (dragStartY_ - p.y) * dbPerPx);
        setParamNorm ("band" + juce::String (dragBand_) + "Range",
                      (newRange + 30.0f) / 30.0f);
        return;
    }

    if (drag_ == DragMode::Makeup || drag_ == DragMode::DotFreq)
    {
        // P2 + #2：主轴**首次锁定后永不重锁**（完全隔离）
        //   · 首次位移超 4px 即锁定主轴，整次拖动只响应该轴
        //   · 纵拖时无论怎样左右都不改分区；横拖时无论怎样上下都不改 Makeup（用户明确要求）
        const float dx = p.x - dragStartX_;
        const float dy = dragStartY_ - p.y;
        if (! dragAxisLocked_)
        {
            if (std::abs (dx) > 4.0f || std::abs (dy) > 4.0f)
            {
                dragAxisLocked_ = true;
                dragAxisIsX_ = std::abs (dx) > std::abs (dy);
            }
        }

        drag_ = (dragAxisIsX_ && dragAxisLocked_) ? DragMode::DotFreq : DragMode::Makeup;

        if (drag_ == DragMode::DotFreq)
        {
            applyCenterFreq (dragBand_, juce::jlimit (20.0f, 20000.0f, xToFreq (p.x)));
            return;
        }
        // 增益刻度：显示量程满程（上拖=增益增大）
        const float R = juce::jmax (0.5f, curveRangeDisplay_);
        const float dbPerPx = (2.0f * R) / plotHeightPx();
        const float newMakeup = juce::jlimit (-36.0f, 36.0f, dragStartMakeup_ + dy * dbPerPx);
        setParamNorm ("band" + juce::String (dragBand_) + "Makeup",
                      (newMakeup + 36.0f) / 72.0f);
        return;
    }
}

void DisplayView::mouseUp (const juce::MouseEvent&)
{
    drag_ = DragMode::None;
    dragIndex_ = -1;
    dragBand_ = -1;
    dragAxisLocked_ = false;
    curvePending_ = false;
    rangePending_ = false;
    bandSweepActive_ = false;     // Phase D-3：多选拖拽结束
    if (bandPopover_ != nullptr)
        bandPopover_->endSliderDrag();   // ⚠️ 必须复位：否则滑杆拖拽状态会吞掉后续所有拖拽
}
void DisplayView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // P2：双击频点 → 编辑该段中心频率（Pro-MB：双击 peak dot 后 Tab 切频率）
    int dotBand = -1;
    if (hitTestFreqDot (e.position, dotBand) != 0)
    {
        openFreqEditor (-1, dotBand);
        return;
    }
    const int div = hitTestDivider (e.position);
    if (div >= 0)
        openFreqEditor (div, -1);
}

// ============================================================
// Phase 5.2 · LINKED CONTROL 抽屉 —— 已整体拆出为 LinkedDrawer 组件
//   （UI_PLAN_V2 §3.2）。DisplayView 只持有一个 unique_ptr<LinkedDrawer>，
//   负责：构造/尺寸同步、命中分发（mouseDown）、每帧 advance()。
//   梯形按钮/玻璃面板/5 个握柄旋钮/Δ 偏移语义全部在 LinkedDrawer.cpp。
// ============================================================

// ============================================================
// Phase 5.3 · 频段 Popover —— 已整体拆出为 BandPopover 组件
//   （UI_PLAN_V2 §3.2）。DisplayView 只持有一个 unique_ptr<BandPopover>，
//   负责：togglePopover/openPopoverForBand 开合仲裁（O6 与抽屉互斥）、
//   bandEdgesPx 几何边界、mouseDown/mouseDrag/mouseUp 命中分发、
//   timerCallback 里 advance() 与光标推进。
//   玻璃面板/5 旋钮/3 滑杆/EXPERT/顶栏全部在 BandPopover.cpp。
// ============================================================
// ============================================================
// Analyzer 设置面板（Pro-MB "Spectrum analyzer" 对标）
//   Resolution: Low 1024 / Medium 2048 / High 4096 / Maximum 8192
//   Speed:      release 时间常数 fast 10ms / medium 20ms / slow 60ms
//   Tilt:       绕 1kHz 的 dB/oct（Pro-MB 默认 4.5；MB 版默认 0）
//   Freeze:     停止下落，显示历史最大包络
//   Pre/Post:   双层频谱开关
// ============================================================
bool DisplayView::hitTestAnalyzerButton (juce::Point<float> p) const
{
    // global bar 中部（原 IN 读数区域）
    const float W = (float) getWidth();
    const float y0 = (float) getHeight() - sy (kGlobalBarH) + sy (5.0f);
    const float x = W * 0.5f - sx (150.0f);
    return juce::Rectangle<float> (x, y0, sx (104.0f), sy (22.0f)).contains (p);
}

void DisplayView::syncSpectrumLayers()
{
    if (spectrumView_ != nullptr)
        spectrumView_->setLayerVisible (analyzerPreOn_, analyzerPostOn_);
}


void DisplayView::openAnalyzerMenu()
{
    // Phase D：已改为自绘 AnalyzerPanel（悬停打开 / 点击 sticky）。
    // 这个函数保留给测试钩子与键盘路径用：等价于"点击按钮"。
    if (analyzerPanel_ != nullptr)
    {
        analyzerPanel_->clickButton();
        repaint();
    }
}

void DisplayView::applyAnalyzerSettings()
{
    auto& eng = proc_.getSpectrumEngine();

    // Phase D：设置的真源在 AnalyzerPanel；两边的副本在 onSettingsChanged 时同步。
    if (analyzerPanel_ != nullptr)
    {
        analyzerPreOn_  = analyzerPanel_->preOn();
        analyzerPostOn_ = analyzerPanel_->postOn();
        analyzerFftSize_ = analyzerPanel_->fftSize();
        analyzerSpeed_   = analyzerPanel_->speed();
        analyzerTilt_    = analyzerPanel_->tilt();
        analyzerFreeze_  = analyzerPanel_->frozen();
    }

    eng.setResolution (analyzerFftSize_);
    static const double kSpeedTauMs[3] = { 10.0, 20.0, 60.0 };   // fast / medium / slow
    eng.setReleaseMs (kSpeedTauMs[juce::jlimit (0, 2, analyzerSpeed_)]);
    eng.setTilt (analyzerTilt_);
    eng.setFrozen (analyzerFreeze_);
    syncSpectrumLayers();
}

// ---------------- 入口 ----------------
void DisplayView::paint (juce::Graphics& g)
{
    pollStructureParams();      // 分割线/分区/段名跟随参数（C2）；首帧 cachedX 兜底

    drawBackground (g);
    drawGrid (g);
    drawPartitions (g);
    // 频谱在第 4 位（分区之后、Range/曲线/刻度/分割线/段名/电平表之前）——
    // 与抽取前的 drawSpectrum(g) 位置一致。⚠️ 不能改成子组件，见 SpectrumView.h
    if (spectrumView_ != nullptr)
        spectrumView_->paint (g);
    drawRangeBell (g);       // Phase 4.3：Range 钟型层
    drawResponseCurve (g);   // Phase 4.3：仿 EQ 动态总曲线
    drawScales (g);          // A3：刻度在上层、100% 不透明
    drawCrossovers (g);
    drawBandHeaders (g);
    // Phase A：抽屉与 Popover 均已整体拆为子组件自绘；
    // 频率输入框同样是子组件（FreqInputEditor）。
    drawMeters (g);
    drawTopStrip (g);
    drawGlobalBar (g);
}

void DisplayView::resized()
{
    // D3：尺寸变化 → 曲线几何全部失效（坐标基于实际像素，必须重建）
    curvePathsValid_ = false;

    // Phase A：抽屉/Popover 位置依赖宿主 bounds，尺寸变化时同步
    // 子组件的 bounds 必须铺满父组件——JUCE 按子组件自身 bounds 裁剪绘制，
    // 0×0 的 bounds 会让面板整体不画（T39.17 抓到过频谱层因此消失）。
    if (linkedDrawer_ != nullptr)
    {
        linkedDrawer_->setBounds (getLocalBounds());
        linkedDrawer_->setHostBounds (getLocalBounds());
    }
    if (bandPopover_ != nullptr)
    {
        bandPopover_->setBounds (getLocalBounds());
        bandPopover_->setHostBounds (getLocalBounds());
    }
    if (freqInput_ != nullptr)
        freqInput_->setBounds (getLocalBounds());
    if (analyzerPanel_ != nullptr)
    {
        analyzerPanel_->setBounds (getLocalBounds());
        analyzerPanel_->setHostBounds (getLocalBounds());
    }
    // Phase A：频谱 Path 按新坐标重建（它不是子组件，不需要 setBounds）
    if (spectrumView_ != nullptr)
        spectrumView_->invalidate();
}
