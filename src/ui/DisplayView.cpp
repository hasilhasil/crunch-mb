#include "DisplayView.h"

// ============================================================
// DisplayView 实现 — Phase 4.2 静态结构（对照 mockup v9 逐值）
// ============================================================

// R32：空闲巡检频率（Hz）。Phase E-2 原本空闲时 stopTimer()，但停表后
// **没有任何唤醒路径**（startTimerHz 仅在构造和 setActive(true) 时出现），
// 用户实测"UI 卡住、完全不刷新，必须关开编辑器才好"（2026-10-15）。
// 改为降到 4Hz 巡检而非停表：参数变化后 250ms 内即可发现并恢复满帧，
// 空闲成本可忽略，且不存在死结。
static constexpr int kIdleHz = 4;
// R32：降频滞后帧数。连续这么多帧都空闲才降到 kIdleHz。
// 不能一帧空闲就降——参数变化那一帧刚提回满帧，下一帧就被打回巡检频率，
// 用户看到的仍是"改参数没反应"。
static constexpr int kIdleHoldFrames = 30;   // 30 帧 @60fps ≈ 500ms

DisplayView::DisplayView (CrunchMBAudioProcessor& processor)
    : proc_ (processor), pal_ (MBTheme::palette (theme_))
{
    setOpaque (true);
    startTimerHz (60);          // 电平表弹道 + 频谱帧拾取 + 曲线刷新

    // R32/R35：向处理器注册"参数变了就刷新"的回调。
    // Phase E-2 空闲时定时器降到 4Hz 巡检，没有这个回调的话宿主自动化
    // 改参数后最多要等 250ms 才反映；更重要的是 N4：
    // popover / 抽屉 / Output 面板各自持有参数快照，必须靠这里精确刷新。
    proc_.setUiWakeCallback ([this] (const juce::String& id) { uiWakeFromParam (id); });

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

    // Phase：预热 facade 的参数查表（一次性，此后 O(1)）
    proc_.getBackend().buildParamCache();
    // Phase A：LINKED CONTROL 抽屉拆为独立组件（UI_PLAN_V2 §3.2）
    linkedDrawer_ = std::make_unique<LinkedDrawer> (proc_);
    addAndMakeVisible (*linkedDrawer_);
    linkedDrawer_->setBounds (getLocalBounds());
    linkedDrawer_->setHostBounds (getLocalBounds());
    linkedDrawer_->onValuesChanged=[this](int index){linkedFeedbackIndex_=index;linkedFeedbackUntilMs_=animNowMs()+2200;ensureTimerRunning();repaint();};
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
    // R35（N14）：用 processor 里持久化的设置初始化，否则关开编辑器 /
    // 宿主重载工程后 Pre/Post/Resolution/Speed/Tilt/Freeze 全部回到默认。
    {
        const auto& as = proc_.getAnalyzerSettings();
        analyzerPanel_->setPreOnForTest  (as.preOn);
        analyzerPanel_->setPostOnForTest (as.postOn);
        analyzerPanel_->setFftSizeForTest (as.fftSize);
        analyzerPanel_->setSpeedForTest  (as.speed);
        analyzerPanel_->setTiltForTest   (as.tilt);
        analyzerPanel_->setFrozenForTest (as.frozen);
        analyzerPreOn_   = as.preOn;
        analyzerPostOn_  = as.postOn;
        analyzerFftSize_ = as.fftSize;
        analyzerSpeed_   = as.speed;
        analyzerTilt_    = as.tilt;
        analyzerFreeze_  = as.frozen;
    }
    addAndMakeVisible (*analyzerPanel_);
    // Phase D-4：Output 自绘面板（同一套 sticky 语义）
    outputPanel_ = std::make_unique<OutputPanel>();
    outputPanel_->setHostBounds (getLocalBounds());
    outputPanel_->onSettingsChanged = [this] { applyOutputSettings(); };
    outputPanel_->onGesture=[this](int index,bool begin){const char* ids[]={"inputGain","outputGain","globalMix"};
        if(auto* parameter=proc_.apvts.getParameter(ids[juce::jlimit(0,2,index)])){if(begin)parameter->beginChangeGesture();else parameter->endChangeGesture();}};
    addAndMakeVisible (*outputPanel_);
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

void DisplayView::bandEdgesPx (int band, float& leftX, float& rightX) const
{
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], plotRightPx() };
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
    // ---- 开合仲裁（放在本函数里，任何打开路径都经过）----
    // O6 方案1：与 LINKED 抽屉互斥。
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->setOpen (false);
    // R35（C-c）：Analyzer / Output 面板的 z 序在 bandPopover_ 之上
    //   （由子组件构造顺序决定），它们 sticky 开着时会把 popover 盖住。
    //   做成**双向互斥**：开 popover 就收起那两个面板，反过来开面板也会
    //   收起 popover（见 mouseDown 的对应分支）。
    //   ⚠️ 不用 toFront()：那会把 popover 抬到 freqInput_ 之上，
    //      双击改分频时的输入框反而会被遮住。
    if (analyzerPanel_ != nullptr) analyzerPanel_->clickOutside();
    if (outputPanel_ != nullptr)   outputPanel_->clickOutside();

    float lx = 0.0f, rx = 0.0f;
    bandEdgesPx (band, lx, rx);
    bandPopover_->setBand (band);
    bandPopover_->setBandEdges (lx, rx);
    // R36（B6）：频率文本用真实 Hz、并且之后每次分频点变化都会重推（见 timerCallback）
    pushPopoverBandRange (true);
    bandPopover_->setOpen (true);
    bandPopover_->syncFromParams();
}

// R36（B6）：把"该段的像素边界 + 真实频率范围"推给二级菜单。
// 用户实测：面板左上角那段频率数字在拖动分频点时不跟着变（原来只在打开时算一次，
// 而且塞进 fmtHzShort 的是像素坐标）。现在每帧比对，变了就推。
void DisplayView::pushPopoverBandRange (bool force)
{
    if (bandPopover_ == nullptr || (! bandPopover_->isOpen() && ! force))
        return;
    const int b = juce::jlimit (0, Param::kNumBands - 1, bandPopover_->band());
    float lx = 0.0f, rx = 0.0f;
    bandEdgesPx (b, lx, rx);
    if (! force && std::abs (lx - lastPopEdgeL_) < 0.5f && std::abs (rx - lastPopEdgeR_) < 0.5f
        && b == lastPopEdgeBand_)
        return;
    lastPopEdgeL_    = lx;
    lastPopEdgeR_    = rx;
    lastPopEdgeBand_ = b;
    bandPopover_->setBandEdges (lx, rx);
    // 频率：绘图区右缘 = 22.1 kHz（20·1105），显示上按 20 kHz 收口更符合直觉
    const float hzLo = juce::jlimit (20.0f, 20000.0f, xToFreq (lx));
    const float hzHi = juce::jlimit (20.0f, 20000.0f, xToFreq (rx));
    bandPopover_->setBandFreqRange (hzLo, hzHi);
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
    // R35（N4）：必须先摘掉回调。processor 的生命周期长于编辑器，
    // 残留的回调会指向已释放的 DisplayView（参数一变就是 use-after-free）。
    proc_.setUiWakeCallback (nullptr);
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
    if (outputPanel_ != nullptr)
        outputPanel_->setTheme (t);
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
        ensureTimerRunning();
        startTimerHz (60);         // R32：重新激活一律回满帧（巡检频率不适用于交互）
        proc_.getSpectrumEngine().setUIActive (true);
        // Phase A：抽屉重新激活时吸附到当前状态，避免停表期间动画卡在中途
        if (linkedDrawer_ != nullptr)
            linkedDrawer_->snapToCurrent();
        if (bandPopover_ != nullptr)
            bandPopover_->snapToCurrent();
    }
}

// ---------------- R32/R35：参数变化时的立即唤醒 + 精确刷新 ----------------
// 由 processor 的 parameterChanged 在**消息线程**调用（音频线程自动化只置
// 原子标志，见 PluginProcessor::parameterChanged）。
//
// N4 的本质是：popover / LINKED 抽屉 / Output 面板各自持有参数快照，
// 以前只在"打开那一刻"同步一次，之后单向写参数。于是宿主自动化、
// MIDI、或其它控件改同一参数后，这些面板的读数会永久停在旧值
// （Output 面板的 Auto-Gain / Monitor 行甚至会显示与真实相反）。
//
// 这里把 id 收集起来，由定时器统一消费——这样：
//   · 一次自动化 sweep 产生几十次回调时只刷新一次；
//   · 刷新动作仍在消息线程、且不在回调栈里（避免回调中改组件状态）。
void DisplayView::uiWakeFromParam (const juce::String& id)
{
    if (isTimerRunning())
        startTimerHz (60);
    idleFrames_ = 0;
    uiStatePending_ = true;
    if (id.isNotEmpty() && ! pendingParamIds_.contains (id))
        pendingParamIds_.add (id);          // 空 id = 全量同步（音频线程兜底）
    repaint();
}

// 把"参数已变"落到三个副本上。id 只是**选择器**（决定谁需要刷新），
// 真正的取值仍由各组件向 backend 读——副本不持有第二份真源。
void DisplayView::syncUiFromParams (bool fullSync)
{
    // ---- popover ----
    if (bandPopover_ != nullptr && bandPopover_->isOpen())
    {
        if (fullSync)
            bandPopover_->syncFromParams();
        else
            for (const auto& id : pendingParamIds_)
                if (bandPopover_->syncParam (id))
                    break;                  // 已在内部按需刷新
    }

    // ---- LINKED 抽屉（只有 MIX 是绝对值，偏移旋钮按定义不需要回读）----
    if (linkedDrawer_ != nullptr && linkedDrawer_->isOpen())
        linkedDrawer_->syncMixFromParams();

    // ---- Output 面板 ----
    if (outputPanel_ != nullptr)
        outputPanel_->syncFromBackend (
            getParamPhysical (Param::ID::inputGain),
            getParamPhysical (Param::ID::outputGain),
            getParamPhysical (Param::ID::globalMix),
            isAllBypassed(),
            getParamPhysical (Param::ID::autoGain) >= 0.5f,
            Param::choiceIndexFromValue (getParamPhysical (Param::ID::monitorMode)));
}

// ---------------- R35 测试钩子 ----------------
bool DisplayView::hasUiWakeCallbackForTest() const
{
    // 通过"能不能触发一次空 id 唤醒"间接判断太脆弱，这里直接问处理器。
    return proc_.hasUiWakeCallbackForTest();
}

double DisplayView::drawerMixValueForTest() const
{
    return linkedDrawer_ != nullptr ? linkedDrawer_->mixValueForTest() : 0.0;
}

bool DisplayView::expertLayerOnTopForTest() const
{
    return bandPopover_ != nullptr && bandPopover_->expertLayerIsTopmost();
}

// ---------------- 坐标映射（mockup v9 常量） ----------------
float DisplayView::freqToX (float f) const
{
    // 横轴：20 Hz .. 22100 Hz，宽度 = 绘图区宽（右缘避开 IN/OUT 音量表，#3）
    const float w = plotWidthPx();
    return std::log (juce::jmax (1.0f, f) / 20.0f) / std::log (1000.0f) * w;
}

float DisplayView::xToFreq (float x) const
{
    const float w = juce::jmax (1.0f, plotWidthPx());
    const double t = (double) x / (double) w;
    return (float) (20.0 * std::pow (1000.0, t));
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

    const EditorLayout l(getLocalBounds());const float top=l.topHeight()+8*l.scale,bottom=l.globalTop()-32*l.scale;
    return bottom-juce::jlimit(0.0f,1.0f,(db+60)/60)*(bottom-top);

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
        // Phase E-4：hairline 提亮，抵消 AA 发暗
        drawHairlineH (g, gainToY (db), 0.0f, (float) getWidth(),
                       std::abs (db) < 0.01f ? pal_.gridZ : pal_.grid);
    }
    for (int db = 0; db >= -100; db -= 20)
    {
        g.setColour (pal_.grid.withAlpha (0.55f));
        drawHairlineH (g, specDbToY ((float) db), 0.0f, (float) getWidth(),
                       pal_.grid.withAlpha (0.55f));
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

    float left,right;bandEdgesPx(selectedBand_,left,right);
    g.setColour(pal_.accent.withAlpha(.035f));g.fillRect(left,plotTopPx(),juce::jmin(right,plotRightPx())-left,plotHeightPx());

}

void DisplayView::drawScales (juce::Graphics& g)
{
    // P1 双刻度：
    //   右轴 = 曲线/Range 增益域（accent，±curveRangeDisplay_）
    //   左轴 = 频谱/电平域（dim，0…−100）
    // #4：刻度文字 clamp 在绘图区内（上下各留一字高），避免首尾被状态栏/global bar 盖住
    //
    // ⚠️ R35（D）：这里所有字号与文字框高度都必须随窗口缩放。
    //   原先字号写死 13.0f、框高写死 14/16px，而**右轴基准 X 用的是未缩放的
    //   常量 kGainScaleRightX=882**——窗口拉到 1840 宽时右轴读数画在 x≈770，
    //   而绘图区右缘在 1764，标签直接跑到画面中间。
    //   用户实测"音量数字刻度表为固定缩放值不会跟着 UI 放大"。
    const float fs = uiFontScale();          // 以 620 高为基准的字号倍率
    const float top = plotTopPx();
    const float bottom = plotBottomPx();
    const float W = (float) getWidth();
    auto clampTextBox = [&] (juce::Rectangle<float> r) -> juce::Rectangle<float>
    {
        const float h = r.getHeight();
        const float y = juce::jlimit (juce::jmin (top + h, bottom - h),
                                      juce::jmax (top + h, bottom - h * 2.0f),
                                      r.getY());
        const float x = juce::jmin (r.getX(), W - r.getWidth() - 1.0f);
        return juce::Rectangle<float> (x, y, r.getWidth(), h);
    };

    g.setFont (MBTheme::font (MBTheme::regular(), 13.0f * fs));   // #5：字号 10→13（D：随缩放）

    // 右轴：曲线/Range 增益刻度（accent）
    // #2：对齐 Pro-MB 截图——步长随量程：±3/±6 → 1dB、±12 → 2dB、±30 → 10dB；
    //     0 线标签用亮色强调（Pro-MB 的 0 基准线）
    {
        const float R = curveRangeDisplay_;
        const float step = (R <= 6.5f) ? 1.0f : (R <= 12.5f) ? 2.0f : 10.0f;
        const float labelW = sx (56.0f);
        const float labelH = sy (14.0f);
        // R35（D）：基准 X 必须走 sx()，否则窗口变宽后跑到画面中间
        const float scaleRightX = plotRightPx() - 5.0f * fs;
        for (float db = -std::floor (R / step) * step; db <= R + 1.0e-4f; db += step)
        {
            const juce::String t = (db > 0.0f ? "+" : "") + juce::String (db, (step < 1.0f ? 1 : 0));
            g.setColour (std::abs (db) < 1.0e-4f ? pal_.text : pal_.accent);
            g.drawText (t, clampTextBox (juce::Rectangle<float> (scaleRightX - labelW,
                                                                 gainToY (db) - labelH * 0.5f,
                                                                 labelW, labelH)),
                        juce::Justification::centredRight);
        }
    }

    // 左轴：频谱/电平刻度（dim）——回滚为 20dB 一步进（#1 回滚）
    g.setColour (pal_.dim);
    for (int db = 0; db >= -100; db -= 20)
        g.drawText (juce::String (db),
                    clampTextBox (juce::Rectangle<float> (sx (4.0f),
                                                          specDbToY ((float) db) - sy (7.0f),
                                                          sx (48.0f), sy (14.0f))),
                    juce::Justification::centredLeft);

    // 底部频率刻度（常显）——#3：右缘标签右对齐并提亮；#5：字号 10.5→13
    g.setFont (MBTheme::font (MBTheme::regular(), 13.0f * fs));
    struct FLabel { float f; const char* t; bool bold; };
    for (const auto& l : { FLabel { 50, "50", false }, FLabel { 100, "100", false },
                           FLabel { 200, "200", false }, FLabel { 500, "500", false },
                           FLabel { 1000, "1k", false }, FLabel { 2000, "2k", false },
                           FLabel { 5000, "5k", false }, FLabel { 10000, "10k", false },
                           FLabel { 20000, "20k", true } })
    {
        g.setColour (l.bold ? pal_.text : pal_.dim);      // #3：20k 用亮色
        const float w = sx (46.0f);
        const float h = sy (16.0f);                      // R35（D）：随缩放
        float x = freqToX (l.f) - w * 0.5f;
        // #3：右缘附近的标签右对齐到**绘图区右缘**（避开 IN/OUT 音量表）
        if (freqToX (l.f) > plotRightPx() - w)
            x = plotRightPx() - w;
        g.drawText (l.t, juce::Rectangle<float> (x, plotBottomPx() - 25.0f * fs, w, h),
                    juce::Justification::centredTop);
    }
}

void DisplayView::drawCrossovers (juce::Graphics& g)
{

    const float s=EditorLayout(getLocalBounds()).scale;
    const float xs[]={freqToX(cachedX1_),freqToX(cachedX2_),freqToX(cachedX3_)};
    const float y=plotTopPx()+plotHeightPx()*.44f;
    for(int i=0;i<3;++i){const float x=xs[i];const bool hot=hoverDividers_==i||(drag_==DragMode::Divider&&dragBand_==i);
        drawHairlineV(g,x,plotTopPx(),plotBottomPx(),pal_.gridZ);
        g.setColour(hot?pal_.accent.withAlpha(.35f):pal_.track);g.fillRoundedRectangle({x-11*s,y-11*s,22*s,22*s},6*s);
        g.setColour(hot?pal_.accent:pal_.dim);g.drawLine(x-3*s,y-5*s,x-3*s,y+5*s,1.3f*s);g.drawLine(x+3*s,y-5*s,x+3*s,y+5*s,1.3f*s);}

}

// 频率短格式（与 BandPopover::fmtHzShort 同规则；此处为 DisplayView 本地副本）
juce::String DisplayView::fmtHzShort (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + "k";
    return juce::String ((int) std::round (hz));
}

// R34（N7）：状态圆点 x —— 与 drawBandHeaders 的按钮列共用同一条右缘。
//   b<3 用该段右侧分割线；b==3 用 kMeterX−8（电平表左边界−8），
//   与 hitTestButton / 按钮列绘制完全一致。圆点再往里收 28px。
float DisplayView::stateDotX (int band) const
{
    const float W = (float) getWidth();
    const int b = juce::jlimit (0, 3, band);
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float edge = (b < 3) ? nx[b]
                               : juce::jmin (W, sx (kMeterX) - sx (8.0f));
    return edge - sx (30.0f);       // R36（B4）：按钮列加宽到 28px 后圆点跟着左移
}

juce::String DisplayView::linkedFeedbackText(int band) const
{
    if(linkedFeedbackIndex_<0||linkedFeedbackIndex_>=6)return {};
    const char* suffixes[]={"Threshold","Attack","Release","Makeup","Mix","Range"};
    const char* units[]={"dB","ms","ms","dB","%","dB"};
    return juce::String(getParamPhysical("band"+juce::String(band)+suffixes[linkedFeedbackIndex_]),1)+" "+units[linkedFeedbackIndex_];
}

void DisplayView::drawBandHeaders (juce::Graphics& g)
{

    const float W=(float)getWidth(),scale=EditorLayout(getLocalBounds()).scale;
    const char* names[]={"LOW","LOW MID","HIGH MID","HIGH"};
    for(int b=0;b<4;++b){
        g.setFont(MBTheme::font(MBTheme::regular(),13*scale));g.setColour(b==selectedBand_?pal_.accent:pal_.dim);
        g.drawText(names[b],juce::Rectangle<float>{freqToX(bandCenterFreq(b))-70*scale,plotTopPx()+12*scale,140*scale,18*scale},juce::Justification::centred);
        if(b==selectedBand_){const juce::String labels[]={"S","M","B",juce::String::fromUTF8("Δ")};
            for(int j=0;j<4;++j){bool on=j==0?getParamBool("band"+juce::String(b)+"Solo"):j==1?getParamBool("band"+juce::String(b)+"Mute"):j==2?isBandBypassed(b):getParamBool("band"+juce::String(b)+"Delta");
                auto r=bandButtonRect(b,j);g.setColour(on?pal_.accent.withAlpha(.3f):pal_.surfaceA);g.fillRoundedRectangle(r,5*scale);
                g.setColour(on?pal_.accent:pal_.dim);g.setFont(MBTheme::font(MBTheme::uiMedium(),13*scale));g.drawText(labels[j],r,juce::Justification::centred);}}
        if(linkedFeedbackIndex_>=0 && animNowMs()<linkedFeedbackUntilMs_){
            const float edges[]={0,freqToX(cachedX1_),freqToX(cachedX2_),freqToX(cachedX3_),plotRightPx()};
            const float width=juce::jmin(170*scale,edges[b+1]-edges[b]-12*scale);
            if(width>38*scale){const float x=juce::jlimit(edges[b]+6*scale,edges[b+1]-width-6*scale,freqToX(bandCenterFreq(b))-width*.5f);
                const auto r=juce::Rectangle<float>(x,plotTopPx()+77*scale,width,47*scale);
                const float alpha=juce::jlimit(0.0f,1.0f,(float)(linkedFeedbackUntilMs_-animNowMs())/400.0f);
                const char* labels[]={"THRESHOLD","ATTACK","RELEASE","OUTPUT","MIX","RANGE"};
                g.setColour(pal_.surfaceA.withMultipliedAlpha(alpha));g.fillRoundedRectangle(r,6*scale);
                g.setColour(pal_.accent.withAlpha(.45f*alpha));g.drawRoundedRectangle(r,6*scale,scale);
                g.setColour(pal_.dim.withMultipliedAlpha(alpha));g.setFont(MBTheme::font(MBTheme::uiMedium(),12*scale));
                g.drawText(labels[linkedFeedbackIndex_],r.withHeight(21*scale),juce::Justification::centred,false);
                g.setColour(pal_.text.withMultipliedAlpha(alpha));g.setFont(MBTheme::font(MBTheme::uiMedium(),15*scale));
                g.drawText(linkedFeedbackText(b),r.withTrimmedTop(21*scale).reduced(3*scale,0),juce::Justification::centred,false);}}
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
                const float mk = proc_.getBackend().getParamFast (Param::bandMakeup (b));
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

    const EditorLayout l(getLocalBounds());const float s=l.scale,x=l.plotRight(),top=l.topHeight()+8*s,bottom=l.globalTop()-32*s;
    g.setColour(pal_.panel.withAlpha(.67f));g.fillRect(x,0.0f,l.meterWidth(),l.globalTop());
    drawHairlineV(g,x,0,l.globalTop(),pal_.gridZ);
    const float barW=13*s,b1=x+6*s,b2=x+25*s;
    for(const float bx:{b1,b2}){g.setColour(pal_.meterBg);g.fillRect(bx,top,barW,bottom-top);}
    g.setColour(pal_.inFill.withAlpha(.65f));g.fillRect(b1,meterY(meterInDb_),barW,bottom-meterY(meterInDb_));
    g.setGradientFill(juce::ColourGradient(pal_.outLevel,0,top,pal_.accent,0,bottom,false));
    g.fillRect(b2,meterY(meterOutDb_),barW,bottom-meterY(meterOutDb_));
    g.setColour(pal_.outLevel);g.fillRect(b1,meterY(peakInDb_),barW,2*s);g.fillRect(b2,meterY(peakOutDb_),barW,2*s);
    const auto now=(juce::uint32)animNowMs();if(now>=nextMeterLabelMs_){labelInDb_=meterInDb_;labelOutDb_=meterOutDb_;nextMeterLabelMs_=now+250;}
    g.setFont(MBTheme::font(MBTheme::regular(),11*s));g.setColour(pal_.text);
    g.drawText(juce::String(labelInDb_,1),juce::Rectangle<float>{x,l.globalTop()-24*s,22*s,16*s},juce::Justification::centred,false);
    g.drawText(juce::String(labelOutDb_,1),juce::Rectangle<float>{x+22*s,l.globalTop()-24*s,22*s,16*s},juce::Justification::centred,false);

}

void DisplayView::drawTopStrip (juce::Graphics& g)
{

    const EditorLayout l (getLocalBounds()); const float s=l.scale;
    g.setColour (pal_.surfaceA); g.fillRect (0.0f,0.0f,l.width,l.topHeight());
    drawHairlineH (g,l.topHeight()-1,0,l.width,pal_.gridZ.withAlpha (.6f));
    g.setColour (pal_.accent); g.fillEllipse (17*s,20*s,6*s,6*s);
    g.setFont (MBTheme::font (MBTheme::uiMedium(),18*s));
    g.setColour (pal_.text); g.drawText ("CRUNCH",juce::Rectangle<float>{30*s,0,75*s,l.topHeight()},juce::Justification::centredLeft);
    const float mbX=30*s+juce::GlyphArrangement::getStringWidth(MBTheme::font(MBTheme::uiMedium(),18*s),"CRUNCH")+6*s;
    g.setColour (pal_.accent); g.drawText ("MB",juce::Rectangle<float>{mbX,0,30*s,l.topHeight()},juce::Justification::centredLeft);
    powerBtnRect_=l.power();
    g.setColour (isPluginEnabled()?pal_.dim:pal_.accent);
    const auto c=powerBtnRect_.getCentre();
    juce::Path power; power.addCentredArc (c.x,c.y,7*s,7*s,0,.6f,juce::MathConstants<float>::twoPi-.6f,true);
    g.strokePath (power,juce::PathStrokeType (1.6f*s)); g.drawLine(c.x,c.y-9*s,c.x,c.y-1*s,1.6f*s);
    const char* names[]={"LOW","LOW MID","HIGH MID","HIGH"};
    g.setFont (MBTheme::font(MBTheme::uiMedium(),13*s));
    for(int b=0;b<4;++b){const auto r=l.bandTab(b);const bool selected=b==selectedBand_;
        if(selected){g.setColour(pal_.accent.withAlpha(.13f));g.fillRoundedRectangle(r,5*s);g.setColour(pal_.accent);g.fillRect(r.withTop(r.getBottom()-2*s));}
        g.setColour(selected?pal_.accent:pal_.dim);g.drawText(names[b],r,juce::Justification::centred);}
    displayRangeBtn_=l.range(); g.setColour(pal_.dim);g.setFont(MBTheme::font(MBTheme::uiRegular(),13*s));
    const juce::String modes[]={"Auto",juce::String::fromUTF8("±3 dB"),juce::String::fromUTF8("±6 dB"),juce::String::fromUTF8("±12 dB"),juce::String::fromUTF8("±30 dB")};
    g.drawText(modes[displayRangeMode_],displayRangeBtn_.withTrimmedRight(10*s),juce::Justification::centred);
    const float ax=displayRangeBtn_.getRight()-7*s,ay=displayRangeBtn_.getCentreY();
    g.drawLine(ax-3*s,ay-2*s,ax,ay+1*s,s);g.drawLine(ax,ay+1*s,ax+3*s,ay-2*s,s);
    const auto t=l.theme();g.setColour(pal_.track);g.fillRoundedRectangle(t,6*s);
    // Centre the circular mark and distribute its three dots evenly. The former
    // dots were all clustered on one side, making the button look tilted.
    const auto centre=t.getCentre();
    g.setColour(pal_.text);
    g.drawEllipse(juce::Rectangle<float>(16*s,16*s).withCentre(centre),1.3f*s);
    for(int i=0;i<3;++i){const float a=-juce::MathConstants<float>::halfPi+i*juce::MathConstants<float>::twoPi/3;
        const auto dot=centre+juce::Point<float>(std::cos(a),std::sin(a))*(4.2f*s);
        g.fillEllipse(juce::Rectangle<float>(2.4f*s,2.4f*s).withCentre(dot));}

}

void DisplayView::drawGlobalBar (juce::Graphics& g)
{

    const EditorLayout l(getLocalBounds());const float s=l.scale;
    g.setColour(pal_.surfaceA);g.fillRect(0.0f,l.globalTop(),l.width,l.globalHeight());
    drawHairlineH(g,l.globalTop(),0,l.width,pal_.gridZ);
    globalSelectorRects_.clear();globalSelectorIds_.clear();globalBarY_=l.globalTop();
    const juce::String labels[]={"PHASE","SLOPE","QUALITY","LOOKAHEAD","OVERSAMPLING"};
    const juce::String ids[]={Param::ID::phaseMode,Param::ID::xoverSlope,Param::ID::linearQuality,Param::ID::lookahead,Param::ID::oversampling};
    const auto labelFont=MBTheme::font(MBTheme::uiMedium(),13*s);
    const auto valueFont=MBTheme::font(MBTheme::uiRegular(),13*s);
    for(int i=0;i<5;++i){auto r=l.globalCell(i);globalSelectorRects_.add(r);globalSelectorIds_.add(ids[i]);
        juce::String value="-";
        if(auto* p=dynamic_cast<juce::AudioParameterChoice*>(proc_.apvts.getParameter(ids[i])))value=p->getCurrentChoiceName();
        if(r.contains(lastMousePos_)){g.setColour(pal_.accent.withAlpha(.08f));g.fillRoundedRectangle(r,4*s);}
        // Identical font height and text rectangle keep label and state on one baseline.
        auto text=r.withSizeKeepingCentre(r.getWidth(),20*s);
        const float lw=juce::GlyphArrangement::getStringWidth(labelFont,labels[i]);
        g.setFont(labelFont);g.setColour(pal_.dim);g.drawText(labels[i],text.withWidth(lw),juce::Justification::centredLeft,false);
        auto vr=text.withTrimmedLeft(lw+5*s).withTrimmedRight(7*s);
        g.setFont(valueFont);g.setColour(pal_.text);g.drawText(value,vr,juce::Justification::centredLeft,false);
        const float x=r.getRight()-3*s,y=r.getCentreY();g.setColour(pal_.dim);
        g.drawLine(x-2*s,y-s,x,y+s,s);g.drawLine(x,y+s,x+2*s,y-s,s);
    }
    const juce::String buttons[]={"Analyzer","OUT "+juce::String(getParamPhysical(Param::ID::outputGain),1)+" dB","BYPASS"};
    for(int i=5;i<8;++i){auto r=l.globalCell(i);const bool active=i==7?isAllBypassed():i==5?analyzerFreeze_:false;
        g.setColour(active?pal_.accent.withAlpha(.22f):pal_.track);g.fillRoundedRectangle(r,6*s);
        g.setColour(active?pal_.accent:pal_.text);g.setFont(MBTheme::font(MBTheme::uiMedium(),13*s));g.drawText(buttons[i-5],r,juce::Justification::centred,false);}

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

// ---------------- Phase E-1：backdrop blur ----------------
// UI_SPEC §12.2 档 b：4× 下采样 + 盒模糊，15Hz 更新，约 0.3ms/帧。
// 做法：把玻璃条对应的屏幕区域截取到一张小图（宽高各 1/4），
// 做两遍 3×3 盒模糊（等效近似高斯），需要时再放大贴回去。
void DisplayView::buildBlurCache (juce::Rectangle<float> srcRectF)
{
    const auto src = srcRectF.getSmallestIntegerContainer().getIntersection (
        juce::Rectangle<int> (0, 0, getWidth(), getHeight()));
    if (src.isEmpty())
        return;

    // 下采样系数：模糊不需要高频细节，1/4 足够且省 16× 像素量
    const int kDown = 4;
    const int cw = juce::jmax (1, src.getWidth()  / kDown);
    const int ch = juce::jmax (1, src.getHeight() / kDown);

    if (blurCache_.getWidth() != cw || blurCache_.getHeight() != ch)
    {
        blurCache_ = juce::Image (juce::Image::ARGB, cw, ch, true);
        blurCacheW_ = cw;
        blurCacheH_ = ch;
    }
    blurCache_.clear (blurCache_.getBounds());

    // 1) 截取背景层到小图（用 Image 版 Graphics 缩放绘制 = 自动下采样）
    {
        juce::Graphics cg (blurCache_);
        // 背景层已在主 Graphics 上画好，这里重新画一份缩放版。
        // 只画"模糊可见"的层：背景/网格/分区/频谱。Range/曲线/刻度/控件不参与，
        // 因为玻璃条区域内的它们本就该被糊掉。
        cg.reduceClipRegion (blurCache_.getBounds());
        const float kx = (float) cw / (float) src.getWidth();
        const float ky = (float) ch / (float) src.getHeight();
        cg.addTransform (juce::AffineTransform::scale (kx, ky, 0.0f, 0.0f)
                            .translated ((float) -src.getX(), (float) -src.getY()));
        drawBackground (cg);
        drawGrid (cg);
        drawPartitions (cg);
        if (spectrumView_ != nullptr)
            spectrumView_->paint (cg);
    }

    // 2) 两遍 3×3 盒模糊（近似高斯，成本 O(n) 且可用可分离写法）
    //    这里用手写循环，避免为小图引入额外依赖。
    {
        const int w = blurCacheW_, h = blurCacheH_;
        // 用一个临时缓冲做分离卷积（先横向后纵向）
        juce::Image tmp (juce::Image::ARGB, w, h, true);
        tmp.clear (tmp.getBounds());

        for (int pass = 0; pass < 2; ++pass)
        {
            // 横向
            {
                juce::Graphics tg (tmp);
                tg.drawImageAt (blurCache_, 0, 0);
            }
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float r = 0, g = 0, b = 0, a = 0;
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int xx = juce::jlimit (0, w - 1, x + dx);
                        const juce::Colour c = tmp.getPixelAt (xx, y);
                        r += c.getFloatRed();   g += c.getFloatGreen();
                        b += c.getFloatBlue();  a += c.getFloatAlpha();
                    }
                    blurCache_.setPixelAt (x, y, juce::Colour::fromFloatRGBA (r / 3, g / 3, b / 3, a / 3));
                }
            // 纵向
            {
                juce::Graphics tg (tmp);
                tg.drawImageAt (blurCache_, 0, 0);
            }
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float r = 0, g = 0, b = 0, a = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        const int yy = juce::jlimit (0, h - 1, y + dy);
                        const juce::Colour c = tmp.getPixelAt (x, yy);
                        r += c.getFloatRed();   g += c.getFloatGreen();
                        b += c.getFloatBlue();  a += c.getFloatAlpha();
                    }
                    blurCache_.setPixelAt (x, y, juce::Colour::fromFloatRGBA (r / 3, g / 3, b / 3, a / 3));
                }
        }
    }
}

bool DisplayView::drawBlurredBackdrop (juce::Graphics& g, juce::Rectangle<float> dstRect)
{
    if (blurCacheW_ <= 0 || blurCacheH_ <= 0)
        return false;
    g.drawImage (blurCache_, dstRect, juce::RectanglePlacement::stretchToFit);
    return true;
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
    spectrumHadNewFrame_ = false;
    if (spectrumView_ != nullptr)
        spectrumHadNewFrame_ = spectrumView_->updateFromEngine();

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

    // R35（N4）：消费"参数已变"的通知，刷新各面板里的参数副本。
    //   正常路径：uiWakeFromParam 收集了 parameterID → 精确刷新。
    //   兜底路径：只有原子标志被置（音频线程自动化 / setStateInformation）
    //             → 做一次全量同步。
    {
        const bool flagSet  = proc_.consumeUiWakeFlag();
        const bool fullSync = flagSet && pendingParamIds_.isEmpty();
        if (fullSync || ! pendingParamIds_.isEmpty())
        {
            syncUiFromParams (fullSync);
            pendingParamIds_.clearQuick();
        }
    }

    // range 参数先读（autoExpand 与 Range 层都需要最新值）
    for (int b = 0; b < Param::kNumBands; ++b)
        bandRangeDb_[(size_t) b] =
            proc_.getBackend().getParamFast (Param::bandRange (b));

    // R32：检测"用户可见参数"是否变化（宿主自动化 / 外部 setValueNotifyingHost）。
    // 变化即置 uiStatePending_，阻止 Phase E-2 停表；直到 repaint 后才清除。
    // ⚠️ 只列**改变画面**的参数——把它们全轮询一遍比停表后永不刷新便宜得多。
    if (! uiStatePending_)
    {
        if (bandRangeDb_ != lastDrawnBandRangeParam_)
            uiStatePending_ = true;
        else
        {
            for (int b = 0; b < Param::kNumBands; ++b)
            {
                const float th = proc_.getBackend().getParamFast (Param::bandThreshold (b));
                const float mk = proc_.getBackend().getParamFast (Param::bandMakeup  (b));
                const float mx = proc_.getBackend().getParamFast (Param::bandMix     (b));
                if (th != lastDrawnThreshold_[(size_t) b]
                 || mk != lastDrawnMakeup_[(size_t) b]
                 || mx != lastDrawnMix_[(size_t) b])
                { uiStatePending_ = true; break; }
            }
        }
    }

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

    updateCurve (dt);

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
    // R32：本帧已把当前参数画出去 → 刷新快照，允许下一帧停表。
    snapshotDrawnParams();
    // ---- Phase 5.2 / 5.3：抽屉与 Popover 滑入/收起动画 ----
    // Phase A：两者均已拆为独立组件，这里只推进它们的 Tween。
    //
    // ⚠️ R36（K3）：`advance()` 的返回值是"仍在动画中"，**必须**收集起来。
    //   此前四个面板的返回值全被丢弃，于是开着/关着面板的那 220~250ms 里
    //   空闲判定仍认为"没事干"，定时器降到 4Hz（250ms/帧）——开合动画
    //   可能只被画到 1 帧，用户看到的就是一顿一顿。
    bool panelBusy = false;
    if (linkedDrawer_ != nullptr)
        panelBusy |= linkedDrawer_->advance (nowMs);
    if (bandPopover_ != nullptr)
        panelBusy |= bandPopover_->advance (nowMs);
    // Phase D：Analyzer 面板开合动画
    if (analyzerPanel_ != nullptr)
        panelBusy |= analyzerPanel_->advance (nowMs);
    // Phase D-4：Output 面板开合动画
    if (outputPanel_ != nullptr)
        panelBusy |= outputPanel_->advance (nowMs);

    // R36（B6）：二级菜单开着时，分频点一变就把新的频率范围推下去
    pushPopoverBandRange();

    // ---- Phase A：其余 Tween（选中/hover/BYP/分割线）推进 ----
    const bool tweensBusy = advanceTweens (nowMs);

    // ---- Phase E-2：空闲降频 ----
    // 没有动效、没有新频谱帧、没有拖拽、没有待绘参数时，把 60fps 降到
    // kIdleHz 巡检频率。音频仍在跑（DSP 在音频线程），降的只是 UI 重绘。
    //
    // ⚠️ R32 修复：原先这里是 **stopTimer()**，而 startTimerHz() 只在构造和
    // setActive(true) 时出现——停表后除"关开编辑器"外无任何唤醒路径，
    // 用户实测"UI 卡住、完全不刷新"（2026-10-15）。
    // 改为降频而非停表：kIdleHz 巡检仍能发现参数变化并恢复满帧。
    //
    // ⚠️ 降频必须有**滞后**：连续 kIdleHoldFrames 帧都空闲才降。
    // 一帧空闲就降的话，参数变化那一帧刚提回 60Hz、下一帧就被打回 4Hz
    // （因为 snapshotDrawnParams 在同帧末尾清了 pending），用户看到的仍是
    // "改参数没反应"。
    // ⚠️ 只在真实运行时生效：测试用的虚拟时钟下不动，否则 tickForTest 会失效。
    if (! useTestClock_)
    {
        // R36（K2/K3）：旋钮的四态渐变与四个面板的开合动画都要算作"在动"。
        //   此前 `advanceKnobStates()` 无返回值、面板 `advance()` 的返回值被丢弃，
        //   120ms 的悬停渐变在 4Hz 巡检下只被采样 0~1 次 → 高亮"啪"地跳。
        const bool knobsBusy = (linkedDrawer_  != nullptr && linkedDrawer_->isKnobAnimating())
                            || (bandPopover_   != nullptr && bandPopover_->isKnobAnimating());
        const bool idleNow = nowMs>=linkedFeedbackUntilMs_ && ! tweensBusy && ! panelBusy && ! knobsBusy
                          && ! spectrumHadNewFrame_ && ! uiStatePending_;
        if (idleNow)
            ++idleFrames_;
        else
            idleFrames_ = 0;

        if (! idleNow && isTimerRunning())
            startTimerHz (60);                       // 有正事 → 满帧
        else if (idleFrames_ >= kIdleHoldFrames && isTimerRunning()
                 && drag_ == DragMode::None)
            startTimerHz (kIdleHz);                  // 持续空闲 → 降频巡检
    }

    repaint();
}

// ---------------- Phase 4.3：交叉权重 / 曲线 / Range ----------------
void DisplayView::pollStructureParams()
{
    // 每帧比对结构参数（读 APVTS 原子）；变化才重算权重
    const float x1 = proc_.getBackend().getParamFast (Param::ID::xover1);
    const float x2 = proc_.getBackend().getParamFast (Param::ID::xover2);
    const float x3 = proc_.getBackend().getParamFast (Param::ID::xover3);
    // ⚠️ xoverSlope / phaseMode 都是 Choice 参数，getParamFast 返回的实际值
    //    **就是选项索引**，不是 normalized。迁移前这里是
    //    `getRawParameterValue(...) * 3.0f` / `* 2.0f` 把 normalized 转索引；
    //    迁移后两条 API 都返回实际值，再乘就等于把索引又放大一遍
    //    （slope 0..3 → 0..9，mode 0..2 → 0..4）——结构与相位模式的缓存比对
    //    会永久失配，导致每帧都判定"结构变了"。
    const int slope = Param::slopeIndexToDb (
        Param::choiceIndexFromValue (proc_.getBackend().getParamFast (Param::ID::xoverSlope)));
    const int mode = Param::choiceIndexFromValue (
        proc_.getBackend().getParamFast (Param::ID::phaseMode));

    if (x1 != cachedX1_ || x2 != cachedX2_ || x3 != cachedX3_
        || slope != cachedSlope_ || mode != cachedMode_)
    {
        cachedX1_ = x1; cachedX2_ = x2; cachedX3_ = x3;
        cachedSlope_ = slope; cachedMode_ = mode;
        weightsDirty_ = true;
        curvePathsValid_ = false;      // → timerCallback 门控据此强制重建曲线/Range
    }
}

// ---------------- R32：参数绘制快照 ----------------
// 记录"本帧画出去时的参数值"。timerCallback 用它判断"参数变了但还没画"，
// 从而阻止 Phase E-2 的停表——否则宿主自动化改参数时定时器一停，
// 唯一的参数检测点不再运行，画面永久停在旧值。
void DisplayView::snapshotDrawnParams() noexcept
{
    lastDrawnBandRangeParam_ = bandRangeDb_;
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        lastDrawnThreshold_[(size_t) b] =
            proc_.getBackend().getParamFast (Param::bandThreshold (b));
        lastDrawnMakeup_[(size_t) b] =
            proc_.getBackend().getParamFast (Param::bandMakeup (b));
        lastDrawnMix_[(size_t) b] =
            proc_.getBackend().getParamFast (Param::bandMix (b));
    }
    uiStatePending_ = false;
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

void DisplayView::updateCurve (double dtSeconds)
{
    if (weightsDirty_)
        updateWeights();

    // Phase：SPI 的显示层平滑辅助（时间常数型，与 curveRangeDisplay_ 同手法）。
    // dt = 帧间隔（秒）；tau = 时间常数（秒）。首帧（缓存为哨兵值）直接吸附，
    // 避免插件刚打开时曲线从 0 dB 慢慢爬上来；收敛后精确吸附，避免无限微动。
    auto smoothTowards = [] (float& cur, float target, double dt, float tau)
    {
        if (cur <= kUninitDb * 0.5f)      // 哨兵值 → 首帧吸附
        {
            cur = target;
            return target;
        }
        if (std::abs (target - cur) < 0.005f)
        {
            cur = target;                    // 收敛后吸附，避免无限微动
            return target;
        }
        const float k = (float) (1.0 - std::exp (-dt / (double) tau));
        cur += (target - cur) * k;
        return cur;
    };

    // 三层曲线模型（Pro-MB 语义，2026-09-23 用户纠正）：
    //   raw    = Σw×makeup          未压缩响应（BYP 打开时的"原定 EQ 曲线"）
    //   live   = Σw×(makeup+gain)   实时压缩响应
    //   disp   = max(live, raw+range) 被 range 截断后的 EQ 层（吸附 range 地板）
    //   rngEdge= Σw×makeup + bell×range  Range 地板线（**不随实时压缩运动**）
    for (int b = 0; b < 4; ++b)
    {
        const float mk = proc_.getBackend().getParamFast (Param::bandMakeup (b));
        const float rangeDb = proc_.getBackend().getParamFast (Param::bandRange (b));
        bandRangeDb_[(size_t) b] = rangeDb;
        // ⚠️ band{i}Mix 是 0-100 的物理百分比参数，getParamFast 返回实际值，
        //    必须比 50.0f（不是 0.5f）。上一轮 facade 迁移时这里误按
        //    normalized 0..1 比较，导致 BYP 永不触发——所有段都显示 live 曲线。
        //    与 isBandBypassed() 的判据保持严格一致。
        const bool bypassed = proc_.getBackend().getParamFast (Param::bandMix (b)) < 50.0f;

        // Phase：SPI（Smart Parameter Interpolation，Pro-MB 对标）
        // 原来 raw/live 直接取参数瞬时值 → 宿主自动化、切 preset、
        // 或拖动滑杆时曲线**逐帧跳变**。这里给显示层加时间常数平滑
        // （τ=60ms，与 curveRangeDisplay_ 的 90ms 同一手法），
        // 让曲线连续移动而不是瞬移。
        // ⚠️ 只平滑**显示层**，不碰 proc_.getBandGainDb() 的音频路径——
        //    声音完全不受影响（这是纯视觉改动）。
        const float rawTarget  = mk;
        const float liveTarget = mk + proc_.getBandGainDb (b);

        bandRawDb_[(size_t) b]  = smoothTowards (bandRawDb_[(size_t) b],  rawTarget,  dtSeconds, 0.060f);
        bandLiveDb_[(size_t) b] = smoothTowards (bandLiveDb_[(size_t) b], liveTarget, dtSeconds, 0.060f);

        // P0：一条实时粗线 = 实际响应（**不做 range 截断**，Pro-MB 语义：
        // 黄线允许压过 range 深度；range 只决定"潜在范围"钟型区的底板）
        //
        // ⚠️ BYP 切换这一路也必须平滑：原来这里在 raw 和 live 之间**瞬时切换**，
        //    若该段正被大幅压缩（live = −20 dB、raw = 0 dB），曲线会瞬间跳 20 dB
        //    —— 正是 SPI 要消除的现象，却从 BYP 这条路漏了过去。
        //    displayTarget 平滑后再赋给 bandDisplayDb_。
        const float displayTarget = bypassed ? bandRawDb_[(size_t) b]
                                             : bandLiveDb_[(size_t) b];
        bandDisplayDb_[(size_t) b] = smoothTowards (bandDisplayDb_[(size_t) b],
                                                    displayTarget, dtSeconds, 0.060f);
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
    //   ⚠️ Makeup/Range 拖动期间冻结目标量程：否则拖动让曲线值变化 → 触发升档
    //   → 量程平滑追赶 → gainToY 映射持续微动 → 用户看到"卡顿/抖动"（2026-09-23）
    //
    //   Phase D-5 例外：Auto 模式下，如果拖动把曲线**拖出当前显示范围之外**，
    //   仍然要升档——否则用户把 Makeup 往上拖时曲线会跑到屏幕外看不见，
    //   这就是"拖不动了"的手感。Pro-MB 的 Auto-Adjust Display Range 正是这个行为。
    //   实现上只认"超出范围"这一种情况，曲线值本身的变化不触发升档，
    //   所以原注释里担心的"量程追赶导致抖动"不会出现。
    if (drag_ == DragMode::Makeup || drag_ == DragMode::Range)
    {
        if (displayRangeMode_ == 0)
        {
            float mx = 0.0f;
            for (int i = 0; i < kCurvePoints; ++i)
                mx = juce::jmax (mx, std::abs (curveDb_[(size_t) i]));
            // 只有**超出**当前量程才升档（不是"曲线值变了就升"）
            if (mx > curveRangeDb_ * 0.98f)
            {
                const float autoTarget = (mx <= 3.0f) ? 3.0f : (mx <= 6.0f) ? 6.0f
                                       : (mx <= 12.0f) ? 12.0f : 30.0f;
                curveRangeDb_ = juce::jmax (curveRangeDb_, autoTarget);
            }
        }
        // 手动档位下不自动扩（用户明确选了固定量程）
    }
    else
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

    // Reuse the continuous, normalised crossover weights for the Range envelope.
    // The outer bands stay flat to the plot edges instead of turning into isolated bells.
    const float t=juce::jlimit(0.0f,1.0f,std::log(juce::jmax(20.0f,freq)/20.0f)/std::log(1000.0f));
    const float pos=t*(kCurvePoints-1);const int i=juce::jmin(kCurvePoints-2,(int)pos);
    const auto& w=bandWeights_[(size_t)juce::jlimit(0,3,band)];return w[(size_t)i]+(w[(size_t)i+1]-w[(size_t)i])*(pos-i);

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
            // ⚠️ 同 updateCurve：band{i}Mix 是 0-100 物理值，须比 50.0f。
            const bool bypassed = proc_.getBackend().getParamFast (Param::bandMix (b)) < 50.0f;

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
        rangeBypassed_ = proc_.getBackend().getParamFast (Param::bandMix (selectedBand_)) < 50.0f;
    }
    curvePathsValid_ = true;
}

// Phase A：refreshSpectrumPaths() / drawSpectrum() 已整体拆到 SpectrumView 组件。
// DisplayView 只注入坐标映射（构造时）并每帧调 updateFromEngine()。

void DisplayView::drawRangeBell (juce::Graphics& g)
{
    // #1：4 段都画；非选中段低透明度，选中段正常
    for (int b = selectedBand_; b <= selectedBand_; ++b)
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
        const float rx = sx (11.0f);                     // 命中半径（含光环）
        // R35（N12）：纵向容差必须走 sy()。原先 x/y 都用 sx(11)，
        //   窗口宽高比一变，纵向手感就跟横向不一致。
        const float ry = sy (11.0f);
        if (std::abs (p.x - px) <= rx && std::abs (p.y - py) <= ry)
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
    juce::MouseEvent mkTestMouse (juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys())
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent (src, p, mods,
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
void DisplayView::mouseDownForTestMods (float x, float y, juce::ModifierKeys m) { enterTestClock(); mouseDown (mkTestMouse (juce::Point<float> (x, y), m)); }
void DisplayView::mouseDragForTestMods (float x, float y, juce::ModifierKeys m) { enterTestClock(); mouseDrag (mkTestMouse (juce::Point<float> (x, y), m)); }
void DisplayView::mouseUpForTestMods   (float x, float y, juce::ModifierKeys m) { enterTestClock(); mouseUp   (mkTestMouse (juce::Point<float> (x, y), m)); }
void DisplayView::mouseMoveForTest (float x, float y) { enterTestClock(); mouseMove (mkTestMouse (juce::Point<float> (x, y))); }

// Phase A：推进虚拟时钟再跑一帧。advanceMs 默认 16ms ≈ 60fps。
void DisplayView::tickForTest (double advanceMs)
{
    useTestClock_ = true;
    testClockMs_ += advanceMs;
    timerCallback();
}

// R32 测试钩子：以**真实时钟模式**跑一帧 timerCallback。
// tickForTest 会把 useTestClock_ 置 true，而 Phase E-2 的空闲逻辑明确跳过
// 虚拟时钟（否则 tickForTest 会失效）。要测空闲降频/恢复，必须经此入口。
void DisplayView::tickRealClockForTest (double advanceMs)
{
    const bool saved = useTestClock_;
    useTestClock_ = false;
    testClockMs_ += advanceMs;
    timerCallback();
    useTestClock_ = saved;
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
    // Phase：改走 facade（PLAN Phase 6：UI 不直触 APVTS）。
    // 原来这里自持一份 paramMap_ 缓存——那正是"D UI 直触 APVTS"耦合点：
    // 同一份 id→参数映射在 facade 和 UI 各存一份，迟早漂移。
    // ⚠️ 语义：本函数的入参是 **normalized**（历史调用点 39 处都按此用），
    //    所以必须走 setParamNormFast 而不是 setParam（后者入参是实际值）。
    juce::ignoreUnused (id);
    proc_.getBackend().setParamNormFast (id, norm);
}

// 按物理值设置（经 NormalisableRange 反归一化；xover 等偏斜参数必须走此路）
void DisplayView::setParamPhysical (const juce::String& id, float physical) const
{
    // Phase：同上，走 facade。setParam 的入参就是实际值，内部做反归一化。
    proc_.getBackend().setParam (id, physical);
}

bool DisplayView::getParamBool (const juce::String& id) const
{
    // Phase：走 facade（PLAN Phase 6：UI 不直触 APVTS）
    return proc_.getBackend().getParamFast (id) >= 0.5f;
}

float DisplayView::getParamPhysical (const juce::String& id) const
{
    return proc_.getBackend().getParamFast (id);
}

float DisplayView::paramRangeMinOf (const juce::String& id) const
{
    // 范围元数据不在 facade 热路径上，经 listParams 查一次（调用频率低）
    for (const auto& pi : proc_.getBackend().listParams())
        if (pi.id == id)
            return pi.minValue;
    return 0.0f;
}

float DisplayView::paramRangeMaxOf (const juce::String& id) const
{
    for (const auto& pi : proc_.getBackend().listParams())
        if (pi.id == id)
            return pi.maxValue;
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
    // R36（B4）：按钮列几何收敛到 bandButtonRect()，绘制 / 命中 / 测试共用一份。
    // band3 的"右缘"取 **IN/OUT 电平表左边界 −8px**（不是窗口右缘），
    // 这样按钮列在分区右侧、又完全不与音量表（x 886..918）重叠。
    for (int b = selectedBand_; b <= selectedBand_; ++b)
        for (int j = 0; j < 4; ++j)
            if (bandButtonRect (b, j).contains (p))
            {
                bandOut = b;
                btnOut = j;
                return 1;
            }
    return 0;
}

// R36（B4）：段内 S/M/B/Δ 按钮列。用户实测"按钮太小"：
//   24×24 / 间距 30 → 28×28 / 间距 34；列右缘仍是"距分界线 16"，
//   所以只向左变宽，不会压到 IN/OUT 电平表（在 kMeterX−8 处收口）。
juce::Rectangle<float> DisplayView::bandButtonRect (int band, int btn) const
{

    const float s=EditorLayout(getLocalBounds()).scale;
    const float start=juce::jlimit(10*s,juce::jmax(10*s,plotRightPx()-126*s),freqToX(bandCenterFreq(band))-60*s);
    return {start+31*s*(float)btn,plotTopPx()+40*s,27*s,27*s};

}

int DisplayView::hitTestDivider (juce::Point<float> p) const
{

    const float s=EditorLayout(getLocalBounds()).scale;
    if(p.y<plotTopPx()||p.y>plotBottomPx())return -1;
    const float xs[]={freqToX(cachedX1_),freqToX(cachedX2_),freqToX(cachedX3_)};
    const float y=plotTopPx()+plotHeightPx()*.44f;
    for(int i=0;i<3;++i)if(std::abs(p.x-xs[i])<4*s||(std::abs(p.x-xs[i])<12*s&&std::abs(p.y-y)<12*s))return i;
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
    float x1 = proc_.getBackend().getParamFast (Param::ID::xover1);
    float x2 = proc_.getBackend().getParamFast (Param::ID::xover2);
    float x3 = proc_.getBackend().getParamFast (Param::ID::xover3);
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
    const float cur = proc_.getBackend().getParamFast (id);
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
    // ⚠️ band{i}Mix 是 0-100 物理百分比，getParamFast 返回实际值，
    //    须比 50.0f（不是 0.5f）。这类错误在 §8.2 记过一次（getParamBool("Mix")），
    //    facade 迁移时又复发——所以这里把判据和 isBandBypassed() 显式对齐。
    for (int b = 0; b < 4; ++b)
        if (! isBandBypassed (b))
            return false;
    return true;
}

bool DisplayView::hitTestGlobalBypass (juce::Point<float> p) const
{
    return EditorLayout(getLocalBounds()).globalCell(7).contains(p);
}

void DisplayView::applyBandToggle (int band, int btn, bool exclusive, bool /*cmd*/)
{
    // Phase（R26）：Solo(0) / Mute(1) / Delta(3) 的三语义开关。
    //
    //   · 普通点击   → latching 切换（原行为）
    //   · Ctrl/Cmd+点击 → exclusive：设它的同时把**其他段的同类按钮**清掉。
    //                     Pro-MB 语义：solo 一段时其他段自动 unsolo。
    //   · 按住        → 临时：由 mouseDown 记原状态、mouseUp 还原
    //                     （holdBand_/holdBtn_/holdWasOn_）。
    //
    // ⚠️ Delta 不参与 exclusive：TECH_SPEC §3.7 规定 Delta 可多段同时开
    //    （"Delta 监听"本来就是对比多段用的），独占会破坏这个用法。
    const juce::String suffix = (btn == 0) ? "Solo"
                             : (btn == 1) ? "Mute" : "Delta";
    const juce::String id = "band" + juce::String (band) + suffix;

    if (exclusive && btn != 3)
    {
        const bool turnOn = ! getParamBool (id);
        for (int i = 0; i < Param::kNumBands; ++i)
            setParamNorm ("band" + juce::String (i) + suffix,
                          (i == band && turnOn) ? 1.0f : 0.0f);
    }
    else
    {
        setParamNorm (id, getParamBool (id) ? 0.0f : 1.0f);
    }
}

void DisplayView::endBandHold()
{
    // Phase（R26）：松开 → 还原按住前的状态（"按住 = 临时"语义）。
    if (holdBand_ < 0 || holdBtn_ < 0)
        return;
    const juce::String id = "band" + juce::String (holdBand_)
                          + ((holdBtn_ == 0) ? "Solo" : (holdBtn_ == 1) ? "Mute" : "Delta");
    setParamNorm (id, holdWasOn_ ? 1.0f : 0.0f);
    holdBand_ = -1;
    holdBtn_  = -1;
}

bool DisplayView::isPluginEnabled() const
{
    // Phase（R25）：插件总开关（enabled）。Bool 参数实际值 0/1。
    // 与 isAllBypassed()（四段 Mix=0 宏）是两回事，见 powerBtnRect_ 处注释。
    return proc_.getBackend().getParamFast (Param::ID::enabled) >= 0.5f;
}

bool DisplayView::hitTestPowerButton (juce::Point<float> p) const
{
    return EditorLayout(getLocalBounds()).power().contains(p);
}

void DisplayView::togglePluginEnabled()
{
    // Phase（R25）：切换总开关。直接写参数，DSP 侧 readScalars 每块读
    // （实际是 processBlock 开头读），所以下一块立即生效。
    setParamPhysical (Param::ID::enabled, isPluginEnabled() ? 0.0f : 1.0f);
    repaint();
}

bool DisplayView::hitTestOutputButton (juce::Point<float> p) const
{
    return outputButtonRect().contains (p);
}

// R36（B4）：OUT 胶囊按钮的唯一几何来源（见 analyzerButtonRect 注释）
juce::Rectangle<float> DisplayView::outputButtonRect() const
{
    return EditorLayout(getLocalBounds()).globalCell(6);
}

// ---------------- Phase D-7：MIDI Learn ----------------
bool DisplayView::hitTestMidiButton (juce::Point<float> p) const
{
    juce::ignoreUnused(p); return false; // MIDI Learn remains available in the settings menu.
}

int DisplayView::midiControlId (juce::Point<float> p) const
{
    // 把屏幕坐标映射成稳定的控件 id。id 空间分段，避免相互碰撞：
    //   0..3   : 四个分割线（crossover 1..3 + 保留 1）
    //   10..13: 四段频点（band level dot）
    //   20..23: 四段 S 按钮
    //   30..33: 四段 M 按钮
    //   40..43: 四段 BYP 按钮
    //   50..53: 四段 Δ 按钮
    //   100    : 全局 BYPASS
    //   101    : Display Range
    //   102    : Analyzer
    //   103    : OUT
    //   104    : LINKED CONTROL
    //   105    : MIDI Learn（自身不可学）
    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };

    // 分割线
    for (int e = 0; e < 3; ++e)
        if (std::abs (p.x - nx[e]) <= sx (23.0f)
            && p.y >= (float) getHeight() - sy (kGlobalBarH) - sy (22.0f))
            return 0 + e;

    // 按钮列（S/M/BYP/Δ）
    int b = -1, j = -1;
    if (hitTestButton (p, b, j) != 0 && b >= 0 && j >= 0)
        return 20 + b * 10 + j;

    // 频点
    int dotBand = -1;
    if (hitTestFreqDot (p, dotBand) != 0 && dotBand >= 0)
        return 10 + dotBand;

    // 全局条按钮
    const float barY = (float) getHeight() - sy (kGlobalBarH);
    if (p.y >= barY)
    {
        if (hitTestGlobalBypass (p)) return 100;
        if (hitTestDisplayRange (p))  return 101;
        if (hitTestAnalyzerButton (p)) return 102;
        if (hitTestOutputButton (p))  return 103;
        // LINKED CONTROL 梯形按钮（居中，宽 456，高 29）
        const float cw = sx (456.0f), ch = sy (29.0f);
        const float cx = (W - cw) * 0.5f;
        const float cy = (float) getHeight() - sy (kGlobalBarH) - ch + sy (1.0f);
        if (p.x >= cx && p.x <= cx + cw && p.y >= cy && p.y <= cy + ch) return 104;
    }
    return -1;
}

void DisplayView::toggleMidiLearn()
{
    midiLearnActive_ = ! midiLearnActive_;
    midiLearnTarget_ = -1;
    repaint();
}

bool DisplayView::handleMidiLearnClick (juce::Point<float> p)
{
    if (! midiLearnActive_)
        return false;

    // 点 MIDI 按钮自身 → 退出学习模式（由调用方处理，这里不消费）
    if (midiBtnRect_.contains (p))
        return false;

    const int id = midiControlId (p);
    if (id < 0)
        return false;      // 没命中可学控件，不消费点击

    const auto it = midiMap_.find (id);
    if (it == midiMap_.end())
    {
        // 首次点击 → 关联一个演示用的控制器号（无真实 MIDI 输入时）
        // 用 id 派生一个 0..127 的稳定编号，保证可复现
        midiMap_[id] = 1 + (id * 7) % 120;
    }
    else
    {
        midiMap_.erase (it);   // 再点一次 → 清除关联
    }
    repaint();
    return true;
}

void DisplayView::drawMidiLearnOverlay (juce::Graphics& g)
{
    if (! midiLearnActive_)
        return;

    // 学习模式下：所有可学控件画一圈呼吸的高亮框。
    // 用时间驱动的 alpha（不用 paint 次数），避免与之前 A2 相同的 bug。
    const double nowMs = animNowMs();
    const float pulse = 0.5f + 0.5f * (float) std::sin (nowMs * 0.004);

    auto highlight = [&] (juce::Rectangle<float> r, int id)
    {
        const bool learned = midiMap_.find (id) != midiMap_.end();
        const juce::Colour c = learned ? pal_.accent : pal_.text;
        g.setColour (c.withAlpha (learned ? 0.55f + 0.35f * pulse : 0.25f + 0.25f * pulse));
        g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 1.5f);
        if (learned)
        {
            // 已关联：画控制器号徽标
            const juce::String txt = "CC " + juce::String (midiMap_[id]);
            g.setFont (MBTheme::font (MBTheme::semiBold(), 8.5f));
            const float bw = sx (34.0f), bh = sy (13.0f);
            auto badge = juce::Rectangle<float> (r.getCentreX() - bw * 0.5f,
                                                 r.getY() - bh - sy (3.0f), bw, bh);
            g.setColour (pal_.accent.withAlpha (0.92f));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::white);
            g.drawText (txt, badge, juce::Justification::centred);
        }
    };

    const float W = (float) getWidth();
    const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
    const float barY = (float) getHeight() - sy (kGlobalBarH);

    for (int e = 0; e < 3; ++e)
        highlight (juce::Rectangle<float> (nx[e] - sx (23.0f), barY - sy (22.0f),
                                           sx (46.0f), sy (22.0f)), e);

    for (int b = 0; b < 4; ++b)
    {
        const float edge = (b < 3) ? nx[b] : juce::jmin (W, sx (kMeterX) - sx (8.0f));
        const float bx = edge - sx (40.0f);
        for (int j = 0; j < 4; ++j)
            highlight (juce::Rectangle<float> (bx, sy (118.0f + j * 30.0f), sx (24.0f), sy (24.0f)),
                       20 + b * 10 + j);
        // 频点
        int dotBand = -1;
        juce::Point<float> dp ((float) getWidth() * 0.5f, (float) getHeight() * 0.5f);
        (void) dotBand; (void) dp;
    }

    // 全局条按钮
    highlight (juce::Rectangle<float> (W - sx (218.0f), sy (3.0f), sx (86.0f), sy (24.0f)), 101);
    highlight (juce::Rectangle<float> (W * 0.5f - sx (150.0f), barY + sy (5.0f), sx (104.0f), sy (22.0f)), 102);
    highlight (juce::Rectangle<float> (W - sx (196.0f), barY + sy (5.0f), sx (78.0f), sy (22.0f)), 103);
    highlight (juce::Rectangle<float> ((W - sx (456.0f)) * 0.5f, barY - sy (29.0f) + sy (1.0f),
                                       sx (456.0f), sy (29.0f)), 104);
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
    ensureTimerRunning();      // R32：键盘操作同样要唤醒（停表后键盘无视觉反馈）
    if (freqInput_ != nullptr && freqInput_->keyPressed (key))
        return true;

    // Phase（R17 修复）：自绘下拉的键盘导航。
    // 此前 keyPressed 只转发给 freqInput_，下拉打开时键盘完全无响应——
    // 键盘用户（以及不便用鼠标的场景）无法操作这 5 个选择器。
    // Pro-MB / Future MB 的下拉均支持 ↑/↓/Enter/Esc。
    if (dropDownOpen_ && dropDownCount_ > 0)
    {
        const auto kc = key.getKeyCode();

        if (key == juce::KeyPress::escapeKey)
        {
            closeDropDown();
            return true;
        }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            // R34（N5）：没有 hover 时回落到**当前项**，而不是第 0 项。
            // 原先取 0：当前是 Linear（索引 2）时按回车会被静默改成 Natural。
            const int item = (dropDownHover_ >= 0 && dropDownHover_ < dropDownCount_)
                                 ? dropDownHover_
                                 : juce::jlimit (0, juce::jmax (0, dropDownCount_ - 1),
                                                 dropDownCurrentIndex());
            clickDropDownItem (item);
            return true;
        }
        if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey)
        {
            const int step = (kc == juce::KeyPress::downKey) ? 1 : -1;
            int next = (dropDownHover_ < 0) ? (step > 0 ? 0 : dropDownCount_ - 1)
                                            : dropDownHover_ + step;
            // 环绕：从末项按 ↑ 回首项，从首项按 ↓ 到末项
            if (next < 0) next = dropDownCount_ - 1;
            if (next >= dropDownCount_) next = 0;
            dropDownHover_ = next;
            repaint();
            return true;
        }
        if (kc == juce::KeyPress::homeKey)
        {
            dropDownHover_ = 0;
            repaint();
            return true;
        }
        if (kc == juce::KeyPress::endKey)
        {
            dropDownHover_ = dropDownCount_ - 1;
            repaint();
            return true;
        }
    }
    // Phase（R19）：popover 内自绘滑轨的键盘步进（↑/↓/←/→，Shift = 细调）。
    // 此前这些滑块只有鼠标拖动，键盘完全无效——R17/R18 清点漏掉的第三例。
    if (bandPopover_ != nullptr && bandPopover_->isOpen())
    {
        int sk = -1;
        const bool hoverOnSlider = bandPopover_->hitTestSlider (lastMousePos_, sk);
        if (! hoverOnSlider)
            sk = sliderFocus_;                 // 无 hover 时用上次聚焦的滑块
        if (sk >= 0)
        {
            const auto kc = key.getKeyCode();
            if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::rightKey)
            {
                if (bandPopover_->stepSlider (sk, 1, key.getModifiers().isShiftDown()))
                { sliderFocus_ = sk; repaint(); return true; }
            }
            if (kc == juce::KeyPress::downKey || kc == juce::KeyPress::leftKey)
            {
                if (bandPopover_->stepSlider (sk, -1, key.getModifiers().isShiftDown()))
                { sliderFocus_ = sk; repaint(); return true; }
            }
        }
    }

    return false;
}

void DisplayView::mouseMove (const juce::MouseEvent& e)
{
    ensureTimerRunning();      // Phase E-2：悬停可能触发面板开合动画
    const auto p = e.position;

    // Phase：自绘下拉的 hover 高亮
    if (dropDownOpen_)
    {
        int item = -1;
        const bool hit = hitTestDropDownItem (p, item);
        if (hit != (dropDownHover_ == item) || (hit && item != dropDownHover_))
        {
            dropDownHover_ = hit ? item : -1;
            repaint();
        }
    }
    lastMousePos_ = p;
    if (bandPopover_ != nullptr)
        bandPopover_->setLastMousePos (p);
    // R36（B3）：标签页与「锁定」按钮的 hover 态需要鼠标位置
    if (linkedDrawer_ != nullptr)
        linkedDrawer_->setLastMousePos (p);

    // ---- R36（B2）：面板只由**点击**打开，取消"鼠标移进检测区就展开" ----
    // 用户实测："底下 global 条的部分菜单检测到鼠标移到检测区域就展开，影响操作"。
    // 原先 ANALYZER / LINKED / OUT 三个按钮走 hoverButton() → 鼠标划过整条底栏
    // 会连续弹出三个面板，既挡视线又误触。Pro-MB 的底栏按钮是点击展开。
    // 这里只停止**调用**，按钮的 sticky 点击语义（clickButton）保持不变。

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
    return EditorLayout(getLocalBounds()).range().contains(p);
}

// ---------------- Phase E-4：1px 细线 AA 提亮 ----------------
// 1px 线的抗鸾齿问题：线的两个边缘各被覆盖约 0.5px，中间只有 ~50% 不透明，
// 结果亮度约为设定值的一半，细线看起来发灰发虚。
// 补偿两步：
//   1) 像素对齐——把线中心放到 floor(y)+0.5，让边缘对称分摊，覆盖最大化；
//   2) alpha 提亮——按覆盖率倒数放大 alpha，让最终视觉亮度接近设定值。
void DisplayView::drawHairlineH (juce::Graphics& g, float y, float x0, float x1,
                                 juce::Colour c) const
{
    const float ya = std::floor (y) + 0.5f;
    const float alpha = juce::jmin (1.0f, c.getFloatAlpha() * 2.0f);
    g.setColour (c.withMultipliedAlpha (alpha / juce::jmax (1.0e-4f, c.getFloatAlpha())));
    g.fillRect (x0, ya, x1 - x0, 1.0f);
}

void DisplayView::drawHairlineV (juce::Graphics& g, float x, float y0, float y1,
                                 juce::Colour c) const
{
    const float xa = std::floor (x) + 0.5f;
    const float alpha = juce::jmin (1.0f, c.getFloatAlpha() * 2.0f);
    g.setColour (c.withMultipliedAlpha (alpha / juce::jmax (1.0e-4f, c.getFloatAlpha())));
    g.fillRect (xa, y0, 1.0f, y1 - y0);
}

// ---------------- Phase：自绘 TextSelect 下拉 ----------------
// Pro-MB drop-down 对标：与插件同色的圆角面板、当前项打勾、hover 高亮。
// 原生 juce::PopupMenu 是操作系统外观，破坏主题一致性——这是替换它的理由。
//
// 两种用法：
//   · Display Range 档位（dropDownParamId_ 为空）——内置 5 档
//   · 全局 Choice 参数（phaseMode / xoverSlope / linearQuality / lookahead）
//     ——选项表从 APVTS 的 AudioParameterChoice 取（RESEARCH_UI §五 P1）
juce::Rectangle<float> DisplayView::dropDownRect() const
{

    const EditorLayout l(getLocalBounds());const float s=l.scale,w=180*s,h=28*s*juce::jmax(1,dropDownCount_);
    if(dropDownThemeMode_)return {l.width-w-6*s,l.topHeight()+2*s,w,h};
    if(dropDownParamId_.isEmpty())return {juce::jmin(l.range().getX(),l.width-w-6*s),l.topHeight()+2*s,w,h};
    return {juce::jlimit(6*s,juce::jmax(6*s,l.width-w-6*s),globalBarAnchorX_),l.globalTop()-h-6*s,w,h};

}

bool DisplayView::hitTestDropDownItem (juce::Point<float> p, int& itemOut) const
{

    if(!dropDownOpen_)return false;const auto r=dropDownRect();if(!r.contains(p))return false;
    itemOut=juce::jlimit(0,dropDownCount_-1,(int)((p.y-r.getY())/(r.getHeight()/dropDownCount_)));return true;

}

void DisplayView::openDropDown (int count)
{
    dropDownOpen_ = true;
    dropDownCount_ = count;
    dropDownHover_ = -1;
    // Phase（R17）：打开即夺焦，否则 keyPressed 收不到键盘事件
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

void DisplayView::closeDropDown()
{
    if (! dropDownOpen_)
        return;
    dropDownOpen_ = false;
    dropDownHover_ = -1;
    dropDownParamId_.clear();
    dropDownChoices_.clear();
    dropDownThemeMode_ = false;      // R34（N6）
    repaint();
}

// R34（N5）：当前下拉的"当前项"。绘制高亮与键盘回车共用。
// 原先键盘回车在 dropDownHover_ < 0 时直接取 item = 0：
//   当前值是 Linear（索引 2）时，打开下拉按回车 → 被静默改成第 0 项 Natural。
int DisplayView::dropDownCurrentIndex() const
{
    if (dropDownThemeMode_)
        return juce::jlimit (0, 2, (int) theme_);

    if (! dropDownParamId_.isEmpty())
    {
        // ⚠️ Choice 参数：getParamFast 返回的实际值**就是选项索引**，
        //    不是 normalized，不能再乘 (count−1)。
        const float idx = proc_.getBackend().getParamFast (dropDownParamId_);
        return juce::jlimit (0, juce::jmax (0, dropDownCount_ - 1),
                             juce::roundToInt (idx));
    }

    // Display Range 档位
    return juce::jlimit (0, 4, displayRangeMode_);
}

// R34（N6）：齿轮按钮命中——与 drawTopStrip 的绘制矩形严格一致
//   （W−30..W−6，y 3..27）。原先齿轮只是个画上去的符号，点了没有任何反应。
bool DisplayView::hitTestGearButton (juce::Point<float> p) const
{
    return EditorLayout(getLocalBounds()).theme().contains(p);
}

// R34（N6）：齿轮 → 设置下拉。当前提供主题切换（Blue / Red / Cream）。
// 主题选择的落点是已有的 DisplayView::setTheme()——它此前没有任何 UI 入口，
// 三个调色板等于白写。
void DisplayView::openThemeMenu()
{
    togglePopover(false);
    if(linkedDrawer_) linkedDrawer_->setOpen(false);
    if(analyzerPanel_) analyzerPanel_->clickOutside();
    if(outputPanel_) outputPanel_->clickOutside();
    if (dropDownOpen_ && dropDownThemeMode_)
    {
        closeDropDown();             // 再点一次 = 收起
        return;
    }
    dropDownParamId_.clear();
    dropDownChoices_.clear();
    dropDownThemeMode_ = true;
    dropDownCount_     = 4;          // Blue / Red / Cream
    dropDownHover_     = -1;
    dropDownOpen_      = true;
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

void DisplayView::clickDropDownItem (int item)
{
    if (item < 0 || item >= dropDownCount_)
    {
        closeDropDown();
        return;
    }

    // R34（N6）：主题菜单
    if (dropDownThemeMode_)
    {
        if (item == 3)
        {
            closeDropDown();
            toggleMidiLearn();
            return;
        }
        const MBTheme::Theme t = (item == 1) ? MBTheme::Theme::Red
                               : (item == 2) ? MBTheme::Theme::Cream
                                             : MBTheme::Theme::Blue;
        setTheme (t);
        closeDropDown();
        return;
    }

    if (dropDownParamId_.isEmpty())
    {
        // ---- Display Range 档位（内置 5 档）----
        displayRangeMode_ = item;
        if (displayRangeMode_ != 0)
        {
            const float fixed[5] = { 6.0f, 3.0f, 6.0f, 12.0f, 30.0f };
            curveRangeDb_ = fixed[displayRangeMode_];
        }
    }
    else
    {
        // ---- 全局 Choice 参数：写 normalized 索引 ----
        const float norm = (float) item / (float) juce::jmax (1, dropDownCount_ - 1);
        proc_.getBackend().setParamNormFast (dropDownParamId_, norm);
    }
    closeDropDown();
}

void DisplayView::openGlobalParamDropDown (const juce::String& paramId,
                                           juce::Rectangle<float> anchorRect)
{
    togglePopover(false);
    if(linkedDrawer_) linkedDrawer_->setOpen(false);
    if(analyzerPanel_) analyzerPanel_->clickOutside();
    if(outputPanel_) outputPanel_->clickOutside();
    dropDownThemeMode_ = false;
    // 从 facade 拿该参数的选项表（Choice 型才有）
    dropDownChoices_.clear();
    for (const auto& pi : proc_.getBackend().listParams())
    {
        if (pi.id == paramId && pi.isChoice)
        {
            dropDownChoices_ = pi.choices;
            break;
        }
    }
    if (dropDownChoices_.isEmpty())
        return;

    // 已经开着同一个参数的下拉 → 视为再次点击，收起
    if (dropDownOpen_ && dropDownParamId_ == paramId)
    {
        closeDropDown();
        return;
    }

    dropDownParamId_  = paramId;
    dropDownCount_    = dropDownChoices_.size();
    dropDownHover_    = -1;
    globalBarY_       = anchorRect.getY();
    globalBarAnchorX_ = anchorRect.getX();
    dropDownOpen_     = true;
    // Phase（R17）：打开即夺焦，与 openDropDown 一致
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
}

void DisplayView::drawDropDown (juce::Graphics& g)
{
    if (! dropDownOpen_)
        return;
    const auto r = dropDownRect();
    const bool themeMode  = dropDownThemeMode_;                    // R34（N6）
    const bool globalMode = (! themeMode) && dropDownParamId_.isNotEmpty();
    const float rowH = r.getHeight() / (float)juce::jmax(1,dropDownCount_);

    // 当前选中索引（三种模式统一经 dropDownCurrentIndex()，
    // 与键盘回车共用同一份推导 —— R34（N5））
    const int curIdx = juce::jlimit (0, juce::jmax (0, dropDownCount_ - 1),
                                     dropDownCurrentIndex());
    juce::StringArray labels;
    if (themeMode)
    {
        labels.add ("Blue");
        labels.add ("Red");
        labels.add ("Cream");
        labels.add (midiLearnActive_ ? "MIDI Learn: On" : "MIDI Learn: Off");
    }
    else if (globalMode)
    {
        labels = dropDownChoices_;
    }
    else
    {
        const char* dr[5] = { "Auto", "+/- 3 dB", "+/- 6 dB",
                              "+/- 12 dB", "+/- 30 dB" };
        for (int i = 0; i < dropDownCount_; ++i) labels.add (dr[i]);
    }

    // 多层投影（与弹层面板同一套深度语言）
    // R35（N11）：偏移/扩张必须随面板高度缩放，否则窗口放大后阴影贴死在
    //   文本边上（BandPopover 的面板阴影早已乘了 k，这里一直没乘）。
    {
        const float k = juce::jmax (0.6f, r.getHeight() / (22.0f * 5.0f));
        struct Layer { float dy; float alpha; float expand; };
        const Layer layers[3] = {
            { 2.0f, 0.30f, 0.0f },
            { 6.0f, 0.20f, 3.0f },
            { 14.0f, 0.14f, 7.0f },
        };
        for (const auto& L : layers)
        {
            auto shadow = r.translated (0.0f, L.dy * k).expanded (L.expand * k);
            g.setColour (juce::Colours::black.withAlpha (L.alpha));
            g.fillRoundedRectangle (shadow, r.getWidth() * 0.08f + L.expand * k);
        }
    }

    g.setColour (pal_.popA);
    g.fillRoundedRectangle (r, r.getWidth() * 0.08f);
    g.setColour (pal_.accent.withAlpha (0.55f));
    g.drawRoundedRectangle (r, r.getWidth() * 0.08f, 1.0f);

    // R35（N10）：原为 `globalMode ? 10.0f : 10.0f`（两个分支相同，恒等三元），
    //   作者显然想区分两种模式字号但没写出来。这里按实际需要保留单一字号。
    g.setFont (MBTheme::font (MBTheme::uiRegular(), 13.0f * EditorLayout(getLocalBounds()).scale));
    for (int i = 0; i < dropDownCount_; ++i)
    {
        const auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) i,
                                                 r.getWidth(), rowH);
        const bool cur = (i == curIdx);
        const bool hot = (i == dropDownHover_);
        if (hot && ! cur)
        {
            g.setColour (pal_.track.withAlpha (0.7f));
            g.fillRoundedRectangle (row.reduced (r.getWidth() * 0.02f), 4.0f);
        }
        // 当前项：accent 底 + 白字 + 左侧勾
        if (cur)
        {
            g.setColour (pal_.accent.withAlpha (0.85f));
            g.fillRoundedRectangle (row.reduced (r.getWidth() * 0.02f), 4.0f);
        }
        g.setColour (cur ? juce::Colours::white
                         : (hot ? pal_.text : pal_.dim));
        g.drawText (cur ? juce::String::fromUTF8 ("\xE2\x9C\x93  ") + labels[i] : labels[i],
                    row.withTrimmedLeft (r.getWidth() * 0.08f),
                    juce::Justification::centredLeft);
    }
}

void DisplayView::openDisplayRangeMenu()
{
    togglePopover(false);
    if(linkedDrawer_) linkedDrawer_->setOpen(false);
    if(analyzerPanel_) analyzerPanel_->clickOutside();
    if(outputPanel_) outputPanel_->clickOutside();
    // Phase：改用自绘下拉（Pro-MB drop-down 对标）。
    // 原来是 juce::PopupMenu —— 操作系统外观，破坏主题一致性。
    if (dropDownOpen_)
        closeDropDown();
    else
        openDropDown (5);
}

void DisplayView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    ensureTimerRunning();      // Phase E-2

    // Phase（R19）：popover 打开且指针在其中 → 滚轮归面板，不再改分频。
    // R35（N3）：原先只在命中**滑轨**时消费滚轮，落在旋钮/EXPERT/空白上
    //   仍会穿到下面的"按分区改 crossover"分支——用户在二级菜单里滚一下就
    //   把分频点改了。现在只要在面板矩形内就整体拦下。
    if (bandPopover_ != nullptr && bandPopover_->isOpen()
        && bandPopover_->popoverRectForTest().contains (e.position))
    {
        int sk = -1;
        if (bandPopover_->hitTestSlider (e.position, sk))
        {
            const int notches = (wheel.deltaY > 0.0f) ? 1 : -1;
            const bool fine = e.mods.isShiftDown();
            if (bandPopover_->stepSlider (sk, notches, fine))
                return;
        }
        return;      // R35（N3）：旋钮/EXPERT/空白上的滚轮一律不落到分频
    }

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
    ensureTimerRunning();      // Phase E-2：任何交互都唤醒定时器
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
    const EditorLayout layout(getLocalBounds());
    for(int b=0;b<4;++b)if(layout.bandTab(b).contains(p)){selectedBand_=b;onSelectedBandChanged(b);openPopoverForBand(b);repaint();return;}
    if(linkedDrawer_!=nullptr&&linkedDrawer_->hitTestClose(p)){linkedDrawer_->setOpen(false);repaint();return;}

    // -1) Phase（R25）：顶栏 POWER 胶囊（插件总开关 enabled）。
    //     放在最前面：它是最高层级的控件，任何时候点击都应立即响应，
    //     即使 Popover/抽屉/面板都开着（关掉后那些也就没意义了）。
    if (hitTestPowerButton (p))
    {
        togglePluginEnabled();
        return;
    }

    // 0) Phase 5.3：Popover 打开时，点 ✕ → 收起
    if (bandPopover_ != nullptr && bandPopover_->isOpen() && bandPopover_->hitTestClose (p))
    {
        togglePopover (false);
        return;
    }

    // -2) R34（N6）：顶栏齿轮 = 设置（主题切换）。
    //     放在很前面：它是顶栏按钮，任何情况下点击都该先响应。
    if (hitTestGearButton (p))
    {
        openThemeMenu();
        return;
    }

    // -1.5) R35（C-a）：打开的下拉**优先**消费自己矩形内的点击。
    //   原先这段排在 LINKED 按钮/选择器之后，而底栏下拉改成**向上**展开后
    //   会和 LINKED 梯形按钮（y 560..589）重叠 → 选最后一项时点到的是按钮，
    //   下拉项永远选不中（T53.7/T53.9 就是这么失败的）。
    //   ⚠️ 这里**只**处理"点中某一项"，不在点外面时关掉——关闭必须留给后面
    //     的原始分支，否则"再点同一个选择器 = 收起"的 toggle 语义会被破坏
    //     （先关掉再开，永远是打开）。
    if (dropDownOpen_)
    {
        int item = -1;
        if (hitTestDropDownItem (p, item))
        {
            clickDropDownItem (item);
            return;
        }
    }

    // 0a) Analyzer 按钮（Pro-MB 底栏 Analyzer 对标，当前占原 IN 读数位）
    // Phase D：**Pro-MB 签名式交互** —— 悬停打开（非 sticky）、点击变 sticky、再点收起。
    // 原来只有"点击弹原生菜单"，没有悬停语义，也没有 sticky。
    if (hitTestAnalyzerButton (p))
    {
        if(linkedDrawer_) linkedDrawer_->setOpen(false);
        if(outputPanel_) outputPanel_->clickOutside();
        if (analyzerPanel_ != nullptr)
        {
            togglePopover (false);       // R35（C-c）：双向互斥，避免被盖住
            analyzerPanel_->clickButton();
        }
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
                const auto row=bandPopover_->popSliderRect(sk);
                const float buttonStart=row.getX()+90*layout.scale;
                if(p.x>=buttonStart)
                    bandPopover_->applySliderForTest(sk,p.x>(buttonStart+row.getRight())*.5f?1.0:0.0);
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
        // Phase C-6：Dynamics 分段按钮（Compress/Expand）
        if (bandPopover_->hitTestDynamicsSeg (p))
        {
            bandPopover_->clickDynamicsSeg(p.x > bandPopover_->dynamicsSegRect().getCentreX());
            repaint();
            return;
        }
        // Phase C-5：EXPERT 展开内容的两行可点（Detector 翻转 / Lookahead 循环）
        {
            const auto row = bandPopover_->hitTestExpertRow (p);
            if (row != BandPopover::ExpertRow::None)
            {
                bandPopover_->selectExpertAt (p);
                repaint();
                return;
            }
        }

        // R35（N2）：Popover 面板必须"实心"——落在面板内、但没命中任何控件的
        // 点击要就地吃掉。此前没有这一步，于是点面板标题/空隙会一路走到
        // 下面的绘图区分支：同段 = togglePopover 把面板**关掉**；
        // 落在别的段 x 范围 = 切到那段并重开；还会顺手布防 BandSweep，
        // 从面板内拖动就变成扫段。用户实测"点二级菜单空白处会误关/跳段"。
        if (bandPopover_->popoverRectForTest().contains (p))
            return;
    }

    // 0) LINKED 抽屉标签页（§5.2）：R36（B2）改为**纯点击**展开。
    //    点击 = 打开并钉住；再点 = 收起并解除。
    if (linkedDrawer_ != nullptr && linkedDrawer_->hitTestButton (p))
    {
        if(analyzerPanel_) analyzerPanel_->clickOutside();
        if(outputPanel_) outputPanel_->clickOutside();
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

    // Panel whitespace must not fall through into spectrum selection or dragging.
    if(linkedDrawer_ != nullptr && linkedDrawer_->hitTestPanel(p))
        return;

    // 0c) 全局栏四个 Choice 选择器（Phase / Slope / Quality / Lookahead）
    //     RESEARCH_UI §五 P1：原来是画着 ▼ 却点不动的静态文字。
    const juce::String globalIds[] = {Param::ID::phaseMode, Param::ID::xoverSlope,
        Param::ID::linearQuality, Param::ID::lookahead, Param::ID::oversampling};
    for (int i = 0; i < 5; ++i)
    {
        const auto cell = layout.globalCell(i);
        if (cell.contains (p))
        {
            openGlobalParamDropDown (globalIds[i], cell);
            return;
        }
    }

    // 0a) Display Range 档位：自绘下拉（Pro-MB drop-down 语义）
    if (hitTestDisplayRange (p))
    {
        openDisplayRangeMenu();
        return;
    }
    // 下拉打开时：点外面收起
    //   （"点中某一项"已在 mouseDown 开头优先处理，见 R35/C-a）
    if (dropDownOpen_)
        closeDropDown();

    // 0e) Phase D-7：MIDI Learn 按钮（顶栏）
    // ⚠️ 命中要现算，不能读 midiBtnRect_——那个成员是 drawTopStrip 里赋值的，
    //    首次点击时还是空的（R5 同类问题：命中几何不要依赖 paint 的副作用）。
    if (hitTestMidiButton (p))
    {
        toggleMidiLearn();
        return;
    }
    // 学习模式下：可学控件的点击由学习逻辑消费
    if (midiLearnActive_ && handleMidiLearnClick (p))
        return;

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

    // 0d) Phase D-4：OUT 按钮 → Output 面板（悬停打开 / 点击 sticky）
    // Phase D-6：**Alt+拖 OUT 按钮 = 直接调 Output Gain**
    //   （Pro-MB：Alt-drag Output = makeup gain，不用打开面板就能快速校正输出电平）
    if (hitTestOutputButton (p))
    {
        if(linkedDrawer_) linkedDrawer_->setOpen(false);
        if(analyzerPanel_) analyzerPanel_->clickOutside();
        if (e.mods.isAltDown())
        {
            drag_ = DragMode::OutputGain;
            dragStartY_ = p.y;
            dragStartOutputGainDb_ = outputPanel_ != nullptr
                                   ? outputPanel_->outputGainDb()
                                   : getParamPhysical (Param::ID::outputGain);
            if (outputPanel_ != nullptr)
                outputPanel_->setOutputGainForTest (dragStartOutputGainDb_);
            repaint();
            return;
        }
        if (outputPanel_ != nullptr)
        {
            togglePopover (false);       // R35（C-c）：双向互斥
            // 打开前先从参数同步当前值，避免显示过期副本
            outputPanel_->setInputGainForTest (getParamPhysical (Param::ID::inputGain));
            outputPanel_->setOutputGainForTest (getParamPhysical (Param::ID::outputGain));
            outputPanel_->setGlobalMixForTest (getParamPhysical (Param::ID::globalMix));
            outputPanel_->setGlobalBypassForTest (isAllBypassed());
            // Auto-Gain：Bool 参数实际值 0/1
            outputPanel_->setAutoGainForTest (
                getParamPhysical (Param::ID::autoGain) >= 0.5f);
            // Monitor：Choice 参数实际值 = 选项索引
            outputPanel_->setMonitorForTest (Param::choiceIndexFromValue (
                getParamPhysical (Param::ID::monitorMode)));
            outputPanel_->clickButton();
            applyOutputSettings();
        }
        repaint();
        return;
    }
    // Output 面板行点击
    if (outputPanel_ != nullptr && outputPanel_->isOpen())
    {
        int row = -1;
        if (outputPanel_->hitTestRow (p, row))
        {
            outputPanel_->clickRow (row);
            repaint();
            return;
        }
        if (! outputPanel_->panelRect().contains (p))
            outputPanel_->clickOutside();
    }

    // 1) 按钮列（S/M/BYP/Δ）
    // Phase（R26）：Solo/Mute/Delta 支持 Pro-MB 三语义——
    //   点击 = 切换；按住 = 临时（mouseUp 还原）；Ctrl+点击 = 独占。
    int b = -1, j = -1;
    if (hitTestButton (p, b, j) != 0)
    {
        if (selectedBand_ != b) { selectedBand_ = b; onSelectedBandChanged (b); }   // Phase A
        const juce::String bp = "band" + juce::String (b);
        if (j == 2)
        {
            toggleBypass (b);
            onBandBypassChanged (b, isBandBypassed (b));
        }
        else if (j == 0 || j == 1 || j == 3)
        {
            // Phase（R26）：三语义开关。
            //   · mouseDown 立即切换（保留原行为——有些调用方只发 mouseDown）
            //   · 若按住 ≥ kHoldMs 才松手，则还原成按下前的状态（"按住=临时"）
            //   · Ctrl/Cmd 按下时改为独占语义
            holdBand_  = b;
            holdBtn_   = j;
            holdWasOn_ = getParamBool (bp + ((j == 0) ? "Solo"
                                                     : (j == 1) ? "Mute" : "Delta"));
            holdCtrl_    = e.mods.isCtrlDown() || e.mods.isCommandDown();
            applyBandToggle (b, j, holdCtrl_, false);
        }
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
        dragStartMakeup_ = proc_.getBackend().getParamFast ("band" + juce::String (dotBand) + "Makeup");
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
    ensureTimerRunning();      // R32：拖拽中也要保持重绘（停表后拖拽会看起来卡住）
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

    // Phase D-6：Alt+拖 OUT 按钮 = Output Gain（Pro-MB Alt-drag Output = makeup gain）
    // 垂直拖拽：上拖 = 增益增大。220px 走完 ±72dB 全程（与旋钮同一速度比例）。
    if (drag_ == DragMode::OutputGain)
    {
        const float dy = dragStartY_ - p.y;
        const float span = 72.0f;                     // −36..+36 dB
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        float ng = dragStartOutputGainDb_ + dy / 220.0f * span * fine;
        ng = juce::jlimit (-36.0f, 36.0f, ng);
        if (outputPanel_ != nullptr)
            outputPanel_->setOutputGainForTest (ng);
        applyOutputSettings();
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
            const float startRange = proc_.getBackend().getParamFast ("band" + juce::String (rangePendingBand_) + "Range");
            const float newRange = juce::jlimit (-30.0f, 30.0f,
                startRange + (curveDownY_ - p.y) * dbPerPx);
            // R36（K9）：Range 参数是 −30..+30（正 = 向上压缩），
            //   这里原先把拖动钳在 [−30, 0] 且按 /30 归一化 —— 于是
            //   "向上压缩"在图形区永远拖不出来，而旋钮却能转到正区间。
            setParamNorm ("band" + juce::String (rangePendingBand_) + "Range",
                          (newRange + 30.0f) / 60.0f);
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
        dragStartMakeup_ = proc_.getBackend().getParamFast ("band" + juce::String (dragBand_) + "Makeup");
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
        // Range 拖动：上拖 range 增大（向上压缩），下拖为向下压缩；用动态量程
        const float dbPerPx = (2.0f * curveRangeDb_) / plotHeightPx();
        const float newRange = juce::jlimit (-30.0f, 30.0f,
                                             dragStartRange_ + (dragStartY_ - p.y) * dbPerPx);
        setParamNorm ("band" + juce::String (dragBand_) + "Range",
                      (newRange + 30.0f) / 60.0f);      // R36（K9）：全量程 60 dB
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

void DisplayView::mouseUp (const juce::MouseEvent& e)
{
    ensureTimerRunning();      // R32：松手后按钮/参数状态可能还要几帧才稳定
    drag_ = DragMode::None;
    dragIndex_ = -1;
    dragBand_ = -1;
    dragAxisLocked_ = false;
    curvePending_ = false;
    rangePending_ = false;
    bandSweepActive_ = false;     // Phase D-3：多选拖拽结束
    if (bandPopover_ != nullptr)
        bandPopover_->endSliderDrag();   // ⚠️ 必须复位：否则滑杆拖拽状态会吞掉后续所有拖拽

    // Phase（R26）：松开"按住"的 Solo/Mute/Delta → 还原原状态。
    // 时长 ≥ kHoldMs 的算"按住"（临时）；更短的算"点击"，
    // 而点击的 latching 切换已经在 mouseDown 里做掉了。
    if (holdBand_ >= 0 && holdBtn_ >= 0)
    {
        // Phase（R26）：短按/长按分流。
        //   · 真实运行：用时差 = eventTime − mouseDownTime
        //   · 测试：mouseUpForTest 无法控制真实时差，故用 holdElapsedMsForTest_
        //     （≥0 时生效）；默认 −1 = 用时差。
        int heldMs;
        if (holdElapsedMsForTest_ >= 0)
        {
            heldMs = holdElapsedMsForTest_;
            holdElapsedMsForTest_ = -1;      // 一次性，避免影响后续交互
        }
        else
        {
            heldMs = (int) (e.eventTime.toMilliseconds() - e.mouseDownTime.toMilliseconds());
        }

        if (heldMs >= kHoldMs)
            endBandHold();                       // 长按 → 还原成按下前（临时语义）
        else
        {
            // 短按 → 保留 mouseDown 已做的切换/独占结果
            holdBand_ = -1;
            holdBtn_  = -1;
        }
    }
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
    return analyzerButtonRect().contains (p);
}

// R36（B4）：底栏两个胶囊按钮的唯一几何来源（绘制 / 命中 / 测试钩子共用）。
//   用户实测"按钮和 UI 都太小"：22px 胶囊 + 10px 字放大后需要更多空间，
//   高度 22 → 26，宽度也跟着放宽。
juce::Rectangle<float> DisplayView::analyzerButtonRect() const
{
    return EditorLayout(getLocalBounds()).globalCell(5);
}

void DisplayView::syncSpectrumLayers()
{
    if (spectrumView_ != nullptr)
        spectrumView_->setLayerVisible (analyzerPreOn_, analyzerPostOn_);
}

void DisplayView::applyOutputSettings()
{
    if (outputPanel_ == nullptr)
        return;
    // Output Gain 直接写全局参数（现有 outputGain，±36 dB）
    setParamPhysical (Param::ID::outputGain, outputPanel_->outputGainDb());

    // Input Gain（Pro-MB Output 面板对标，R24 补 UI 暴露）
    setParamPhysical (Param::ID::inputGain, outputPanel_->inputGainDb());

    // Phase：全局 Mix 0-200% 已接线 DSP（MultibandDSP::globalMixLin_）
    setParamPhysical (Param::ID::globalMix, outputPanel_->globalMixPct());

    // Auto-Gain（Future MB 对标）：直接写全局 Bool 参数。
    // DSP 侧 readScalars 每块读它，所以这里写完立即生效。
    setParamPhysical (Param::ID::autoGain, outputPanel_->autoGain() ? 1.0f : 0.0f);

    // Monitor（Future MB 对标）：写 Choice 参数（实际值 = 选项索引）。
    setParamPhysical (Param::ID::monitorMode, (float) outputPanel_->monitorIndex());

    // Global Bypass：与既有全局 BYPASS 胶囊同一语义（四段 Mix=0 宏）
    const bool wantBypass = outputPanel_->globalBypass();
    if (wantBypass != isAllBypassed())
    {
        const bool allBypassed = isAllBypassed();
        for (int i = 0; i < 4; ++i)
        {
            toggleBypass (i, ! allBypassed);
            onBandBypassChanged (i, ! allBypassed);
        }
    }
}


void DisplayView::openAnalyzerMenu()
{
    // Phase D：已改为自绘 AnalyzerPanel（悬停打开 / 点击 sticky）。
    // 这个函数保留给测试钩子用：等价于"点击按钮"。
    // （曾注释说"留给键盘路径用"，但当时并没有键盘路径调用它——
    //   见 §8.2 教训 18：注释里的错误论断比没有注释更危险。）
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

    // R35（N14）：把显示设置写回 processor，随工程状态一起持久化。
    // 这 6 项不是 APVTS 参数（不该被宿主自动化），所以单独存进 XML 属性。
    CrunchMBAudioProcessor::AnalyzerSettings as;
    as.preOn   = analyzerPreOn_;
    as.postOn  = analyzerPostOn_;
    as.fftSize = analyzerFftSize_;
    as.speed   = analyzerSpeed_;
    as.tilt    = analyzerTilt_;
    as.frozen  = analyzerFreeze_;
    proc_.setAnalyzerSettings (as);
}

// ---------------- 入口 ----------------
void DisplayView::paint (juce::Graphics& g)
{
    pollStructureParams();      // 分割线/分区/段名跟随参数（C2）；首帧 cachedX 兜底

    // R32/R35：参数变化的处理统一挪到 timerCallback 的 syncUiFromParams()。
    // R35（N9）：这里原来是「paint() 里 consumeUiWakeFlag() + startTimerHz(60)
    //   + repaint()」——在绘制中再请求绘制，语义混乱且会多一帧无效绘制。
    //   现在时序是：parameterChanged 置脏 → 定时器消费 → 刷新副本 → repaint()。

    drawBackground (g);
    g.saveState();
    g.reduceClipRegion(juce::Rectangle<float>(0.0f, plotTopPx(), plotRightPx(), plotHeightPx()).toNearestInt());
    drawGrid (g);
    drawPartitions (g);
    // 频谱在第 4 位（分区之后、Range/曲线/刻度/分割线/段名/电平表之前）——
    // 与抽取前的 drawSpectrum(g) 位置一致。⚠️ 不能改成子组件，见 SpectrumView.h
    if (spectrumView_ != nullptr)
        spectrumView_->paint (g);
    drawRangeBell (g);       // Phase 4.3：Range 钟型层
    drawResponseCurve (g);   // Phase 4.3：仿 EQ 动态总曲线
    g.restoreState();
    drawScales (g);          // A3：刻度在上层、100% 不透明
    drawCrossovers (g);
    drawBandHeaders (g);
    // Phase A：抽屉与 Popover 均已整体拆为子组件自绘；
    // 频率输入框同样是子组件（FreqInputEditor）。
    drawMeters (g);
    drawTopStrip (g);
    drawGlobalBar (g);
    // Phase D-7：MIDI Learn 高亮覆盖层（最上层，覆盖所有控件）

    // Phase：自绘 TextSelect 下拉（最上层）

}

void DisplayView::resized()
{
    ensureTimerRunning();      // Phase E-2：尺寸变化需要重建几何
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
    if (outputPanel_ != nullptr)
    {
        outputPanel_->setBounds (getLocalBounds());
        outputPanel_->setHostBounds (getLocalBounds());
    }
    // Phase A：频谱 Path 按新坐标重建（它不是子组件，不需要 setBounds）
    if (spectrumView_ != nullptr)
        spectrumView_->invalidate();
    // Reproject existing curve data immediately, including when audio is stopped.
    refreshCurvePaths();
    pushPopoverBandRange(true);
}
