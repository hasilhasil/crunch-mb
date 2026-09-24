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
    }
}

// ---------------- 坐标映射（mockup v9 常量） ----------------
float DisplayView::freqToX (float f) const
{
    const float w = (float) getWidth();
    return std::log (juce::jmax (1.0f, f) / 20.0f) / std::log (1000.0f) * w;
}

float DisplayView::xToFreq (float x) const
{
    const float w = (float) getWidth();
    const double t = (double) x / (double) juce::jmax (1.0f, w);
    return (float) (20.0 * std::pow (1000.0, t));
}

float DisplayView::specDbToY (float db) const
{
    // db: −100 → plotBottom(), 0 → plotTop（按高度比例缩放）
    const float t = (db + 100.0f) / 100.0f;
    return plotBottomPx() - t * plotHeightPx();
}

float DisplayView::gainToY (float gainDb) const
{
    // P1：双刻度 —— 曲线增益轴用 ±curveRangeDb_（默认 ±6 dB，Pro-MB 的 Display Range）
    // gain: −R → plotBottom(), +R → plotTop
    const float t = (gainDb + curveRangeDb_) / (2.0f * curveRangeDb_);
    return plotBottomPx() - juce::jlimit (0.0f, 1.0f, t) * plotHeightPx();
}

float DisplayView::meterY (float db) const
{
    const float top = sy (kTopStripH);                 // A6：音量表顶端避开顶栏
    const float bottom = plotBottomPx();               // A8：底缘 = global bar 顶
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
    const float top = sy (kTopStripH);

    const int steps = 4;
    for (int i = 0; i <= steps; ++i)
    {
        const float db = curveRangeDb_ - (2.0f * curveRangeDb_) * (float) i / (float) steps;
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

    for (int b = 0; b < 4; ++b)
    {
        const bool sel = (b == selectedBand_);
        const bool byp = getParamBool ("band" + juce::String (b) + "Mix") == false
                      && proc_.apvts.getRawParameterValue (
                             "band" + juce::String (b) + "Mix")->load() < 0.5f;
        // P3：BYP 段 dim（Pro-MB：bypassed band 在显示里变暗）
        const float dimK = byp ? 0.45f : 1.0f;
        const float x0 = edges[b] + 1.0f, w = edges[b + 1] - edges[b] - 2.0f;
        const float h = sy (kDesignH);
        g.setColour (pal_.accent.withAlpha ((sel ? 0.10f : 0.05f) * dimK));
        g.fillRect (x0, 0.0f, w, h);
        if (sel)
        {
            // A2.1/C2：只画上/下两条横边；左边框取消（避免与分割线双边重叠变厚）
            g.setColour (pal_.accent.withAlpha (0.9f * dimK));
            g.drawHorizontalLine ((int) 1.5f, x0, x0 + w);
            g.drawHorizontalLine ((int) (h - 1.5f), x0, x0 + w);
            if (b == 3)
                g.drawVerticalLine ((int) (x0 + w), 1.5f, h - 1.5f);
        }
    }
}

void DisplayView::drawScales (juce::Graphics& g)
{
    // P1 双刻度：
    //   右轴 = 曲线/Range 增益域（accent，±curveRangeDb_）
    //   左轴 = 频谱/电平域（dim，0…−100）
    g.setFont (MBTheme::font (MBTheme::regular(), 10.0f));

    g.setColour (pal_.accent);
    for (int i = 0; i <= 4; ++i)
    {
        const float db = curveRangeDb_ - (2.0f * curveRangeDb_) * (float) i / 4.0f;
        if (std::abs (db) < 0.01f) continue;
        g.drawText ((db > 0 ? "+" : "") + juce::String (db, 1),
                    juce::Rectangle<float> (sx (838.0f), gainToY (db) - 6.0f, sx (50.0f), 12.0f),
                    juce::Justification::centredRight);
    }

    g.setColour (pal_.dim);
    for (int db = 0; db >= -100; db -= 20)
        g.drawText (juce::String (db),
                    juce::Rectangle<float> (sx (4.0f), specDbToY ((float) db) - 6.0f, sx (44.0f), 12.0f),
                    juce::Justification::centredLeft);

    // 底部频率刻度（常显）
    g.setFont (MBTheme::font (MBTheme::regular(), 10.5f));
    g.setColour (pal_.dim);
    struct FLabel { float f; const char* t; };
    for (const auto& l : { FLabel { 50, "50" }, FLabel { 100, "100" }, FLabel { 200, "200" },
                           FLabel { 500, "500" }, FLabel { 1000, "1k" }, FLabel { 2000, "2k" },
                           FLabel { 5000, "5k" }, FLabel { 10000, "10k" }, FLabel { 20000, "20k" } })
        g.drawText (l.t, juce::Rectangle<float> (freqToX (l.f) - sx (20.0f), sy (546.0f), sx (40.0f), 14.0f),
                    juce::Justification::centredTop);
}

void DisplayView::drawCrossovers (juce::Graphics& g)
{
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };

    for (int e = 0; e < 3; ++e)
    {
        const float x = nx[e];
        // C2：hover/拖动中的分割线转主题色，其余默认灰
        const bool hot = (hoverDividers_ == e);
        g.setColour (hot ? pal_.accent : pal_.gridZ);
        g.fillRect (x - 0.75f, 0.0f, 1.5f, sy (588.0f));

        // 手柄 12×26 @ y=500（圆角）
        auto handle = juce::Rectangle<float> (x - sx (6.0f), sy (500.0f), sx (12.0f), sy (26.0f));
        g.setColour (pal_.track);
        g.fillRoundedRectangle (handle, sx (4.0f));
        g.setColour (pal_.gridZ);
        g.drawRoundedRectangle (handle, sx (4.0f), 1.0f);
    }
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
                case 2: lit = getParamBool ("band" + juce::String (b) + "Mix") < 0.5f; break;
                case 3: lit = getParamBool ("band" + juce::String (b) + "Delta"); break;
                default: break;
            }
            g.setColour (lit ? pal_.accent : pal_.track);
            g.fillEllipse (sxp, sy (42.0f + j * 9.0f) - 1.5f, 3.0f, 3.0f);
        }

        // C6–C9：S/M/BYP/Δ 按钮列（24×24，hover 或选中该段时显示）
        const bool showBtns = (b == selectedBand_ || b == hoverBand_);
        if (showBtns)
        {
            const float bx = (b < 3 ? nx[b] : W) - sx (40.0f);
            const juce::String labels[4] = { "S", "M", "B", juce::String::fromUTF8 ("\xCE\x94") };
            for (int j = 0; j < 4; ++j)
            {
                bool on = false;
                switch (j)
                {
                    case 0: on = getParamBool ("band" + juce::String (b) + "Solo");  break;
                    case 1: on = getParamBool ("band" + juce::String (b) + "Mute");  break;
                    case 2: on = getParamBool ("band" + juce::String (b) + "Mix") < 0.5f; break;
                    case 3: on = getParamBool ("band" + juce::String (b) + "Delta"); break;
                    default: break;
                }
                auto r = juce::Rectangle<float> (bx, sy (118.0f + j * 30.0f), sx (24.0f), sy (24.0f));
                g.setColour (on ? pal_.accent : pal_.track.withAlpha (0.92f));
                g.fillRoundedRectangle (r, sx (6.0f));
                g.setColour (on ? juce::Colours::white.withAlpha (0.92f) : pal_.dim);
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

// #7：自绘频率输入框（画在频点/分割线上方，宿主内嵌，不弹独立窗口）
void DisplayView::drawFreqInput (juce::Graphics& g)
{
    if (! freqIn_.active)
        return;

    const float bw = sx (72.0f), bh = sy (24.0f);
    float tx = freqIn_.x - bw * 0.5f, ty = freqIn_.y - bh - sy (10.0f);
    tx = juce::jlimit (2.0f, (float) getWidth() - bw - 2.0f, tx);
    if (ty < sy (kTopStripH)) ty = freqIn_.y + sy (10.0f);

    auto box = juce::Rectangle<float> (tx, ty, bw, bh);
    g.setColour (pal_.surface.withAlpha (0.97f));
    g.fillRoundedRectangle (box, sx (5.0f));
    g.setColour (pal_.accent);
    g.drawRoundedRectangle (box, sx (5.0f), 1.2f);

    // 文本 + 闪烁光标
    const juce::String label = freqIn_.text + (freqIn_.cursorVisible ? "|" : " ");
    g.setColour (pal_.text);
    g.setFont (MBTheme::font (MBTheme::regular(), 11.0f));
    g.drawText (label, box, juce::Justification::centred);

    // 单位提示
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::regular(), 8.0f));
    g.drawText ("Hz  (Enter 确认 / Esc 取消)",
                box.withTrimmedBottom (-sy (12.0f)).withY (box.getBottom() + 1.0f).withHeight (sy (11.0f)),
                juce::Justification::centredTop);
}

void DisplayView::drawMeters (juce::Graphics& g)
{
    const float barW = sx (13.0f);
    const float b1 = sx (kMeterX) + sx (2.0f);              // IN 柱
    const float b2 = sx (kMeterX) + sx (4.0f) + barW;       // OUT 柱
    const float top = sy (kTopStripH);                      // A6：条带顶端避开顶栏状态栏
    const float bottom = plotBottomPx();                    // A8：底缘与 global bar 顶对齐

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
    const juce::uint32 nowMs2 = juce::Time::getMillisecondCounter();
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

    // 玻璃条（surfaceA；模糊由后续 Phase 5 的 backdrop 实现，当前平涂）
    g.setColour (pal_.surfaceA);
    g.fillRect (0.0f, 0.0f, W, h);

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

    // A8：global 条 100% 不透明（surface，不再玻璃）
    g.setColour (pal_.surface);
    g.fillRect (0.0f, y0, W, h);
    g.setColour (pal_.gridZ);
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

    // IN/OUT 读数（B5：与电平表同步做 250ms 节流，避免数字跳动无法阅读）
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::regular(), 11.0f));
    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
    if (nowMs >= nextMeterLabelMs_)
    {
        labelInDb_  = meterInDb_;
        labelOutDb_ = meterOutDb_;
        nextMeterLabelMs_ = nowMs + 250;
    }
    const juce::String inTxt = "IN " + juce::String (labelInDb_, 2) + " dB";
    const juce::String outTxt = "OUT " + juce::String (labelOutDb_, 2) + " dB";
    g.drawText (inTxt,  juce::Rectangle<float> (W - sx (250.0f), y0 + sy (8.0f), sx (110.0f), sy (16.0f)),
                juce::Justification::centredRight);
    g.drawText (outTxt, juce::Rectangle<float> (W - sx (130.0f), y0 + sy (8.0f), sx (110.0f), sy (16.0f)),
                juce::Justification::centredRight);

    // BYPASS 胶囊（C10：可点按；命中区与绘制严格一致）
    auto byp = juce::Rectangle<float> (W - sx (108.0f), y0 + sy (5.0f), sx (92.0f), sy (22.0f));
    g.setColour (isAllBypassed() ? pal_.accent : pal_.track);
    g.fillRoundedRectangle (byp, sx (11.0f));
    g.setColour (isAllBypassed() ? juce::Colours::white.withAlpha (0.92f) : pal_.text);
    g.setFont (MBTheme::font (MBTheme::semiBold(), 10.5f));
    g.drawText ("BYPASS", byp, juce::Justification::centred);
}

void DisplayView::drawDrawer (juce::Graphics& g)
{
    // S 边梯形抽屉（A7：底边与 global 条 +1px 叠合，消除接缝）
    const float W = (float) getWidth();
    const float cw = sx (456.0f), ch = sy (29.0f);
    const float cx = (W - cw) * 0.5f;
    const float cy = (float) getHeight() - sy (kGlobalBarH) - ch + 1.0f;

    juce::Path tp;
    tp.startNewSubPath (cx + sx (44.0f), cy + sy (2.0f));
    tp.lineTo (cx + sx (412.0f), cy + sy (2.0f));
    tp.cubicTo (cx + sx (434.0f), cy + sy (2.0f), cx + sx (422.0f), cy + sy (26.0f), cx + sx (444.0f), cy + sy (26.0f));
    tp.lineTo (cx + sx (12.0f), cy + sy (26.0f));
    tp.cubicTo (cx + sx (34.0f), cy + sy (26.0f), cx + sx (22.0f), cy + sy (2.0f), cx + sx (44.0f), cy + sy (2.0f));
    tp.closeSubPath();

    g.setColour (pal_.panel.withAlpha (0.9f));
    g.fillPath (tp);
    g.setColour (pal_.gridZ);
    g.strokePath (tp, juce::PathStrokeType (1.0f));

    g.setColour (pal_.accent);
    g.setFont (MBTheme::font (MBTheme::bold(), 11.0f));
    g.drawText ("LINKED CONTROL",
                juce::Rectangle<float> (cx + sx (120.0f), cy + sy (6.0f), sx (180.0f), sy (14.0f)),
                juce::Justification::centredLeft);
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::regular(), 9.0f));
    g.drawText (juce::String::fromUTF8 ("\xE5\x9B\x9B\xE6\xAE\xB5\xE8\x81\x94\xE5\x8A\xA8 \xC2\xB7 \xE7\xBA\xBF\xE6\x80\xA7\xE5\x81\x8F\xE7\xA7\xBB"),
                juce::Rectangle<float> (cx + sx (260.0f), cy + sy (7.0f), sx (150.0f), sy (12.0f)),
                juce::Justification::centredLeft);
}

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

void DisplayView::timerCallback()
{
    const auto nowMs = juce::Time::getMillisecondCounter();
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

    // ---- Phase 4.3：频谱帧拾取 + 曲线刷新 ----
    bool spectrumChanged = proc_.getSpectrumEngine().getLatest (spectrumPre_, spectrumPost_);
    if (spectrumChanged)
    {
        if (spectrumFreqs_.size() != (size_t) kSpectrumPoints)
            proc_.getSpectrumEngine().getFrequencies (spectrumFreqs_);
        spectrumHasFrame_ = true;
        spectrumPathsValid_ = false;
    }

    // #7：输入框光标闪烁（500ms 半周期）
    if (freqIn_.active)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - freqIn_.lastBlinkMs > 500)
        {
            freqIn_.lastBlinkMs = now;
            freqIn_.cursorVisible = ! freqIn_.cursorVisible;
        }
    }

    pollStructureParams();

    // range 参数先读（autoExpand 与 Range 层都需要最新值）
    for (int b = 0; b < Param::kNumBands; ++b)
        bandRangeDb_[(size_t) b] =
            proc_.apvts.getRawParameterValue (Param::bandRange (b))->load();

    updateCurve();

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
        bool needRebuild = ! haveDrawnGain_ || ! curvePathsValid_;
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
            haveDrawnGain_ = true;
            refreshCurvePaths();
        }
    }
    refreshSpectrumPaths();     // 有新帧才重建

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

    // P1：Display Range 自动扩展——曲线值**或 Range 层深度**超出当前量程时升档
    //     （Range 层 = base + bell×range，最深可达 base+range；否则大 range 值被 clamp 看不见）
    if (drag_ == DragMode::None || drag_ == DragMode::Divider || drag_ == DragMode::DotFreq)
    {
        float mx = 0.0f;
        for (int i = 0; i < kCurvePoints; ++i)
            mx = juce::jmax (mx, std::abs (curveDb_[(size_t) i]));
        for (int b = 0; b < Param::kNumBands; ++b)
            mx = juce::jmax (mx, std::abs (bandRangeDb_[(size_t) b]));
        const float target = (mx <= 3.0f) ? 3.0f : (mx <= 6.0f) ? 6.0f
                        : (mx <= 12.0f) ? 12.0f : 30.0f;
        if (target > curveRangeDb_)
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

void DisplayView::refreshSpectrumPaths()
{
    if (! spectrumHasFrame_ || spectrumFreqs_.size() != (size_t) kSpectrumPoints)
        return;

    const float bottom = specDbToY (-100.0f);
    const auto build = [&] (const std::array<float, kSpectrumPoints>& levels,
                            juce::Path& fill, juce::Path& stroke)
    {
        fill.clear();
        stroke.clear();
        for (int i = 0; i < kSpectrumPoints; ++i)
        {
            const float x = freqToX (spectrumFreqs_[(size_t) i]);
            const float y = specDbToY (levels[(size_t) i]);
            if (i == 0) { fill.startNewSubPath (x, y); stroke.startNewSubPath (x, y); }
            else        { fill.lineTo (x, y);          stroke.lineTo (x, y); }
        }
        fill.lineTo (freqToX (spectrumFreqs_.back()), bottom);
        fill.lineTo (freqToX (spectrumFreqs_.front()), bottom);
        fill.closeSubPath();
    };

    build (spectrumPre_,  spectrumPreFill_,  spectrumPreStroke_);
    build (spectrumPost_, spectrumPostFill_, spectrumPostStroke_);
    spectrumPathsValid_ = true;
}

// ---------------- Phase 4.3 绘制 ----------------
void DisplayView::drawSpectrum (juce::Graphics& g)
{
    if (! spectrumPathsValid_)
        return;

    // B1（用户验收指定）：主题色(accent) = Pre，灰(inFill) = Post
    g.setColour (pal_.accent.withAlpha (0.30f));
    g.fillPath (spectrumPreFill_);
    g.setColour (pal_.accent.withAlpha (0.95f));
    g.strokePath (spectrumPreStroke_, juce::PathStrokeType (2.0f));

    g.setColour (pal_.inFill.withAlpha (0.20f));
    g.fillPath (spectrumPostFill_);
    g.setColour (pal_.inFill.withAlpha (0.80f));
    g.strokePath (spectrumPostStroke_, juce::PathStrokeType (1.5f));
}

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
    }
}

void DisplayView::drawResponseCurve (juce::Graphics& g)
{
    // P0：只画一条粗线（Pro-MB 的黄线 = 实时动态总响应）
    if (! curveVisible_ || ! curvePathsValid_ || curvePath_.isEmpty())
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
    if (! curveVisible_ || curveDb_.empty())
        return false;
    const int b = selectedBand_;
    const float rangeDb = rangeDisplay_[(size_t) b];
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float f = curveFreqs_[(size_t) i];
        const float w = bellWeight (b, f);
        const float x = freqToX (f);
        const float yLo = gainToY (baseDb_[(size_t) i] + w * rangeDb);   // 下缘：Range 地板（静态）
        const float yTop = gainToY (curveDb_[(size_t) i]);                // 上缘：当前曲线
        if (std::abs (p.x - x) > sx (4.0f))          // #3：窗收到 ±4px（原 6px 太宽，
            continue;                               // 与频点命中区重叠 → "抓错目标卡住")
        if (std::abs (p.y - yLo) <= sy (6.0f) && w > 0.05f)
        {
            bandOut = b;
            lowerEdge = true;
            return true;
        }
        if (std::abs (p.y - yTop) <= sy (6.0f) && w > 0.05f)
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

void DisplayView::mouseDownForTest (float x, float y) { mouseDown (mkTestMouse (juce::Point<float> (x, y))); }
void DisplayView::mouseDragForTest (float x, float y) { mouseDrag (mkTestMouse (juce::Point<float> (x, y))); }
void DisplayView::mouseUpForTest   (float x, float y) { mouseUp   (mkTestMouse (juce::Point<float> (x, y))); }
void DisplayView::mouseDoubleClickForTest (float x, float y)
{
    mouseDoubleClick (mkTestMouse (juce::Point<float> (x, y)));
}

void DisplayView::setParamNorm (const juce::String& id, float norm) const
{
    for (auto* p : proc_.getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (pw->getParameterID() == id)
            { p->setValueNotifyingHost (norm); return; }
}

// 按物理值设置（经 NormalisableRange 反归一化；xover 等偏斜参数必须走此路）
void DisplayView::setParamPhysical (const juce::String& id, float physical) const
{
    for (auto* p : proc_.getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (pw->getParameterID() == id)
            {
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    p->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (physical));
                else
                    p->setValueNotifyingHost (physical);
                return;
            }
}

bool DisplayView::getParamBool (const juce::String& id) const
{
    for (auto* p : proc_.getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (pw->getParameterID() == id)
                return pw->getValue() >= 0.5f;
    return false;
}

int DisplayView::hitTestButton (juce::Point<float> p, int& bandOut, int& btnOut) const
{
    // 每段右缘 [线−40, 线−16]，y 从 118 起，4 个 24×24（间距 30）
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    for (int b = 0; b < 4; ++b)
    {
        const float edge = (b < 3) ? nx[b] : W;
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
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    for (int e = 0; e < 3; ++e)
        if (std::abs (p.x - nx[e]) <= sx (4.0f) && p.y >= 0.0f && p.y <= sy (588.0f))
            return e;
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
    // P2：横拖频点 = 改该段中心频率（两侧 crossover 同步平移，保持段宽）
    float x1 = cachedX1_, x2 = cachedX2_, x3 = cachedX3_;
    float lo = 0.0f, hi = 0.0f;
    if (band == 0)      { lo = 20.0f;  hi = x1; }
    else if (band == 1) { lo = x1;     hi = x2; }
    else if (band == 2) { lo = x2;     hi = x3; }
    else                { lo = x3;     hi = 20000.0f; }
    if (hi <= lo) return;

    const float newC  = juce::jlimit (lo * 1.02f, hi / 1.02f, freq);
    const float ratio = newC / std::sqrt (lo * hi);
    float newLo = lo * ratio, newHi = hi * ratio;
    if (newLo < lo) { newHi *= lo / newLo; newLo = lo; }
    if (newHi > hi) { newLo *= hi / newHi; newHi = hi; }

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

void DisplayView::openFreqEditor (int index, int band)
{
    // #7：自绘输入框（不再用 juce::TextEditor——宿主 VST3 编辑器里会弹独立顶层窗口）
    //     index >= 0 → 编辑分割线频率；band >= 0 → 编辑段中心频率
    freqIn_.active  = true;
    freqIn_.band    = band;
    freqIn_.divider = index;
    freqIn_.text.clear();
    freqIn_.cursorVisible = true;
    freqIn_.lastBlinkMs = juce::Time::getMillisecondCounter();

    if (band >= 0)
    {
        // #7：输入框出现在 hover 简易信息窗的位置（频点上方）
        freqIn_.x = freqToX (bandCenterFreq (band));
        freqIn_.y = freqDotScreenY (band) - sy (14.0f);
    }
    else
    {
        freqIn_.x = (index == 0) ? freqToX (cachedX1_)
                   : (index == 1) ? freqToX (cachedX2_) : freqToX (cachedX3_);
        freqIn_.y = sy (470.0f);
    }
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

void DisplayView::closeFreqEditor (bool apply)
{
    if (! freqIn_.active)
        return;
    if (apply)
    {
        // 支持简写：'1k' → 1000，'2x' → +6 dB 不适用；此处仅 Hz 数值
        const float f = juce::jlimit (20.0f, 20000.0f, freqIn_.text.getFloatValue());
        if (freqIn_.band >= 0) applyCenterFreq (freqIn_.band, f);
        else                   applyDividerFreq (freqIn_.divider, f);
    }
    freqIn_.active = false;
    freqIn_.text.clear();
    repaint();
}

bool DisplayView::keyPressed (const juce::KeyPress& key)
{
    if (! freqIn_.active)
        return false;

    if (key == juce::KeyPress::escapeKey)
    {
        closeFreqEditor (false);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        closeFreqEditor (true);
        return true;
    }
    if (key == juce::KeyPress::backspaceKey)
    {
        freqIn_.text = freqIn_.text.substring (0, juce::jmax (0, freqIn_.text.length() - 1));
        repaint();
        return true;
    }
    const juce::juce_wchar ch = key.getTextCharacter();
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'k' || ch == 'K')
    {
        freqIn_.text += juce::String::charToString (ch);
        repaint();
        return true;
    }
    return false;
}

void DisplayView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
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
    if (newHover != hoverBand_) { hoverBand_ = newHover; repaint(); }

    // C2：分割线 hover/拖动主题色
    if (div != hoverDividers_) { hoverDividers_ = div; repaint(); }

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

void DisplayView::mouseDown (const juce::MouseEvent& e)
{
    // #7：输入框激活时，先点别处 = 取消
    if (freqIn_.active)
    {
        closeFreqEditor (false);
        return;
    }

    const auto p = e.position;

    // 0) 全局 BYPASS 胶囊（A8 区，global bar 右侧）
    if (hitTestGlobalBypass (p))
    {
        const bool allBypassed = isAllBypassed();
        for (int i = 0; i < 4; ++i)
            toggleBypass (i, ! allBypassed);
        repaint();
        return;
    }

    // 1) 按钮列（S/M/BYP/Δ）
    int b = -1, j = -1;
    if (hitTestButton (p, b, j) != 0)
    {
        selectedBand_ = b;
        const juce::String bp = "band" + juce::String (b);
        if (j == 0) setParamNorm (bp + "Solo",  getParamBool (bp + "Solo")  ? 0.0f : 1.0f);
        if (j == 1) setParamNorm (bp + "Mute",  getParamBool (bp + "Mute")  ? 0.0f : 1.0f);
        if (j == 2) toggleBypass (b);
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
        selectedBand_ = dotBand;
        drag_ = DragMode::Makeup;
        dragBand_ = dotBand;
        dragStartY_ = p.y;
        dragStartX_ = p.x;
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

    // 4) EQ 曲线纵向拖拽 = Makeup（O5-A）
    int curveBand = -1;
    if (hitTestCurve (p, curveBand))
    {
        drag_ = DragMode::Makeup;
        dragBand_ = selectedBand_;
        dragStartY_ = p.y;
        dragStartX_ = p.x;
        dragStartMakeup_ = proc_.apvts.getRawParameterValue (
            "band" + juce::String (selectedBand_) + "Makeup")->load();
        return;
    }

    // 4.5) Range 钟型缘（曲线未命中才轮到；避免抢占曲线邻域）
    {
        int rb = -1; bool lower = true;
        if (hitTestRangeEdge (p, rb, lower))
        {
            selectedBand_ = rb;
            drag_ = DragMode::Range;
            dragStartY_ = p.y;
            dragStartRange_ = proc_.apvts.getRawParameterValue (
                "band" + juce::String (rb) + "Range")->load();
            return;
        }
    }

    // 5) 其余 = 选段
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
    for (int i = 0; i < 4; ++i)
        if (p.x >= edges[i] && p.x < edges[i + 1])
        {
            if (selectedBand_ != i)
            {
                selectedBand_ = i;
                curvePathsValid_ = false;     // #4：切段 → 强制重建（否则 Range 层不刷新）
            }
            break;
        }
    repaint();
}

void DisplayView::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;

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
        setParamNorm ("band" + juce::String (selectedBand_) + "Range",
                      (newRange + 30.0f) / 30.0f);
        return;
    }

    if (drag_ == DragMode::Makeup || drag_ == DragMode::DotFreq)
    {
        // P2+#6：位移占优方向决定语义——纵向=Makeup，横向=段中心频率
        const float dx = p.x - dragStartX_;
        const float dy = dragStartY_ - p.y;
        if (drag_ == DragMode::Makeup && std::abs (dx) > std::abs (dy) && std::abs (dx) > 3.0f)
            drag_ = DragMode::DotFreq;
        if (drag_ == DragMode::DotFreq)
        {
            applyCenterFreq (dragBand_, juce::jlimit (20.0f, 20000.0f, xToFreq (p.x)));
            return;
        }
        // 增益刻度：±curveRangeDb_ 满量程（上拖=增益增大）
        const float dbPerPx = (2.0f * curveRangeDb_) / plotHeightPx();
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

// ---------------- 入口 ----------------
void DisplayView::paint (juce::Graphics& g)
{
    pollStructureParams();      // 分割线/分区/段名跟随参数（C2）；首帧 cachedX 兜底

    drawBackground (g);
    drawGrid (g);
    drawPartitions (g);
    drawSpectrum (g);        // Phase 4.3：双层频谱
    drawRangeBell (g);       // Phase 4.3：Range 钟型层
    drawResponseCurve (g);   // Phase 4.3：仿 EQ 动态总曲线
    drawScales (g);          // A3：刻度在上层、100% 不透明
    drawCrossovers (g);
    drawBandHeaders (g);
    drawFreqInput (g);       // #7：自绘频率输入框（最上层）
    drawDrawer (g);
    drawDrawer (g);
    drawMeters (g);
    drawTopStrip (g);
    drawGlobalBar (g);
}

void DisplayView::resized()
{
    // D3：尺寸变化 → 曲线与频谱几何全部失效（坐标基于实际像素，必须重建）
    curvePathsValid_ = false;
    spectrumPathsValid_ = false;
}
