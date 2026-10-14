#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>
#include <unordered_map>

#include "MBTheme.h"
#include "Anim.h"
#include "Knob.h"
#include "LinkedDrawer.h"
#include "../PluginProcessor.h"
#include "../dsp/SpectrumEngine.h"

// ============================================================
// DisplayView — Layer 1 显示区（UI_SPEC v1.8 §4，mockup v9 逐值对应）
//
// Phase 4.2（当前）：静态结构
//   · 坐标映射（freqToX / specDbToY / gainToY / meterY，与 mockup 一致）
//   · 网格（缓存 Image）+ 四分区底块 + 段名 + 双刻度
//   · EQ 式瘦电平表（IN/OUT，真实数据：音频线程 atomic + UI 弹道）
//   · 交叉点手柄 + 频率标签 + 顶栏玻璃条 + 全局菜单条
// Phase 4.3 追加：双层频谱 / 仿 EQ 动态曲线 / Range 钟型层
// Phase 4.4 追加：全部手势（选段/拖拽/双击/Makeup 双入口）
// Phase A （UI_PLAN_V2 §8）：动画基础设施 + 4 个动画 bug 修复 + 参数查找缓存
//   · A1 linkedAnim_ 驱动抽屉旋钮位置（此前用开关量，滑入动画等于没做）
//   · A2 光标闪烁改为时间驱动（此前在 paint() 里翻转状态）
//   · A3 动画期间不每帧调 layoutLinkedKnobs()
//   · A4 删掉重复的 drawDrawer (g) 调用
// ============================================================
class DisplayView : public juce::Component,
                    public juce::Timer
{
public:
    explicit DisplayView (CrunchMBAudioProcessor& processor);
    ~DisplayView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress&) override;

    // ---- Phase 4.4 交互 ----
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // 主题（Phase 5 的 ⚙ 菜单接；当前提供 setter）
    void setTheme (MBTheme::Theme t);
    MBTheme::Theme getTheme() const noexcept { return theme_; }

    // 编辑器开关（懒启动频谱分析）
    void setActive (bool active) noexcept;

    // 测试钩子：离屏渲染（无窗口），供像素级视觉回归断言使用
    void paintForTest (juce::Graphics& g) { paint (g); }
    // 测试钩子：手动驱动一帧（timerCallback 为 protected）
    // Phase A：推进**虚拟时钟**再跑一帧，使动画测试确定性
    // （真实时间在紧凑循环里几乎不前进，Tween 会永远停在起点）。
    void tickForTest (double advanceMs = 16.0);
    double nowMsForTest() const { return testClockMs_; }
    // 测试钩子：直接派发鼠标事件（mouseXxx 为 protected；内部经 Desktop 取 MouseInputSource）
    void mouseDownForTest (float x, float y);
    void mouseDragForTest (float x, float y);
    void mouseUpForTest (float x, float y);
    void mouseDoubleClickForTest (float x, float y);
    // 测试钩子：取某段频点的实际屏幕位置（#5：绑 Range 顶边中点 = base，只随 Makeup 变）
    void getFreqDotPos (int band, float& x, float& y) const
    {
        const float fcen = bandCenterFreq (band);
        x = freqToX (fcen);
        float db = 0.0f;
        if (! curveDb_.empty())
        {
            int best = 0; float bd = 1.0e9f;
            for (int i = 0; i < kCurvePoints; ++i)
            {
                const float d = std::abs (curveFreqs_[(size_t) i] - fcen);
                if (d < bd) { bd = d; best = i; }
            }
            db = baseDb_[(size_t) best];
        }
        y = gainToY (db);
    }
    // 测试钩子：曲线上某频率处的 y（供鼠标模拟用）
    float getCurveYAtFreq (float f) const { return gainToY (curveDbAtFreq (f)); }
    // 测试钩子：读 range 显示值与 Range 层几何（T26）
    float getRangeDisplay (int band) const { return rangeDisplay_[(size_t) band]; }
    float getCurveRangeForTest() const { return curveRangeDisplay_; }
    int   getDisplayRangeModeForTest() const { return displayRangeMode_; }
    // 测试钩子：各层 Path 是否有内容（T29 闪烁回归）
    std::string getCurvePathLenForTest() const { return curvePath_.isEmpty() ? "" : "x"; }
    std::string getRangeEdgeLenForTest (int b) const
    {
        return rangeEdgePath_[(size_t) b].isEmpty() ? "" : "x";
    }
    // 测试钩子（T35）——Phase A：抽屉已拆为 LinkedDrawer，经它转发
    bool isLinkedOpenForTest() const { return linkedDrawer_ != nullptr && linkedDrawer_->isOpen(); }
    void applyLinkedOffsetForTest (int k, double v);
    juce::Rectangle<int> linkedKnobBoundsForTest (int k) const
    {
        return linkedDrawer_ != nullptr ? linkedDrawer_->knobBoundsForTest (k)
                                        : juce::Rectangle<int>();
    }
    // 测试钩子（T36）
    bool isPopoverOpenForTest() const { return popoverOpen_; }
    int  getPopoverBandForTest() const { return popoverBand_; }
    int  getSelectedBandForTest() const { return selectedBand_; }
    double getPopoverValueForTest (int k) const { return popoverValues_[(size_t) k]; }
    void syncPopoverFromParamsForTest() { syncPopoverFromParams(); }
    void openLinkedDrawerForTest();
    juce::Rectangle<float> popoverRectForTest() const { return popoverRect(); }
    // T38 Analyzer 测试钩子
    bool hitTestAnalyzerButtonForTest (float x, float y) const
    { return hitTestAnalyzerButton (juce::Point<float> (x, y)); }
    int   getAnalyzerFftSizeForTest() const { return analyzerFftSize_; }
    int   getAnalyzerSpeedForTest()   const { return analyzerSpeed_; }
    float getAnalyzerTiltForTest()    const { return analyzerTilt_; }
    bool  getAnalyzerFreezeForTest()  const { return analyzerFreeze_; }
    bool  getAnalyzerPreOnForTest()   const { return analyzerPreOn_; }
    bool  getAnalyzerPostOnForTest()  const { return analyzerPostOn_; }
    void  setAnalyzerPreOnForTest (bool v)  { analyzerPreOn_ = v; }
    void  setAnalyzerPostOnForTest (bool v) { analyzerPostOn_ = v; }
    void  setAnalyzerFftSizeForTest (int v)   { analyzerFftSize_ = v; }
    void  setAnalyzerSpeedForTest (int v)     { analyzerSpeed_ = v; }
    void  setAnalyzerTiltForTest (float v)    { analyzerTilt_ = v; }
    void  setAnalyzerFreezeForTest (bool v)   { analyzerFreeze_ = v; }
    void  applyAnalyzerSettingsForTest()      { applyAnalyzerSettings(); }
    // T40 Popover 控件测试钩子
    void applyPopoverKnobForTest (int k, double v) { applyPopoverKnob (k, v); }
    juce::Rectangle<float> popSliderRectForTest (int k) const { return popSliderRect (k); }
    juce::Rectangle<float> popExpertRectForTest() const { return popExpertRect(); }
    bool isPopoverExpertOpenForTest() const { return popoverExpertOpen_; }
    bool  hasSpectrumFrameForTest() const     { return spectrumHasFrame_; }
    bool  spectrumPathsReadyForTest() const
    {
        return ! spectrumPreFill_.isEmpty() && ! spectrumPreStroke_.isEmpty();
    }
    // T21/T36 诊断
    int  dbgSelectedBand() const { return selectedBand_; }
    bool dbgCurvePending() const { return curvePending_; }
    bool dbgRangePending() const { return rangePending_; }
    int  dbgRangePendingBand() const { return rangePendingBand_; }
    int  dbgDragMode() const { return (int) drag_; }
    int  dbgHitCurve (float x, float y) const { int b = -1; return hitTestCurve (juce::Point<float> (x, y), b) ? 1 : 0; }
    int  dbgHitRangeEdge (float x, float y) const { int b = -1; bool l = true; return hitTestRangeEdge (juce::Point<float> (x, y), b, l) ? 1 : 0; }
    int  dbgHitDot (float x, float y) const { int b = -1; return hitTestFreqDot (juce::Point<float> (x, y), b) ? 1 : 0; }
    int  dbgHitBtn (float x, float y) const { int b = -1, j = -1; return hitTestButton (juce::Point<float> (x, y), b, j) ? 1 : 0; }
    int  dbgHitDivider (float x, float y) const { return hitTestDivider (juce::Point<float> (x, y)); }
    int  dbgHitAnalyzer (float x, float y) const { return hitTestAnalyzerButton (juce::Point<float> (x, y)) ? 1 : 0; }
    int  dbgHitDrawer (float x, float y) const
    {
        return (linkedDrawer_ != nullptr && linkedDrawer_->hitTestButton (juce::Point<float> (x, y))) ? 1 : 0;
    }
    float curveYAtFreqForTest (float f) const { return gainToY (curveDbAtFreq (f)); }
    float rangeEdgeYForTest (int b) const
    {
        // Range 层底边的 y（取该段几何中心处）
        return getRangeEdgeRect ((size_t) b).getBottom();
    }
    // T36：在段内扫描一个真实命中 Range 缘的点（排除同时命中曲线的点）
    bool findRangeEdgePointForTest (int band, float& outX, float& outY) const
    {
        const float nx[3] = { freqToX (cachedX1_), freqToX (cachedX2_), freqToX (cachedX3_) };
        const float W = (float) getWidth();
        const float edges[5] = { 0.0f, nx[0], nx[1], nx[2], W };
        int cb = -1;
        for (int i = 1; i < 20; ++i)
        {
            const float x = edges[(size_t) band] + (edges[(size_t) band + 1] - edges[(size_t) band]) * (float) i / 20.0f;
            for (int y = (int) plotTopPx() + 4; y < (int) plotBottomPx() - 4; ++y)
            {
                int bOut = -1; bool lower = true;
                if (hitTestRangeEdge (juce::Point<float> (x, (float) y), bOut, lower) && bOut == band && lower
                    && ! hitTestCurve (juce::Point<float> (x, (float) y), cb))
                { outX = x; outY = (float) y; return true; }
            }
        }
        return false;
    }
    // T36 诊断钩子
    bool hitTestCurveForTest (float x, float y) const
    { int b = -1; return hitTestCurve (juce::Point<float> (x, y), b); }
    bool hitTestRangeEdgeForTest (float x, float y) const
    { int b = -1; bool l = true; return hitTestRangeEdge (juce::Point<float> (x, y), b, l); }
    int  hitTestFreqDotForTest (float x, float y) const
    { int b = -1; return hitTestFreqDot (juce::Point<float> (x, y), b); }
    int  hitTestButtonForTest (float x, float y, int& band, int& btn) const
    {
        return hitTestButton (juce::Point<float> (x, y), band, btn);
    }
    // T31：读绘图区边界（验证刻度不被遮挡）
    float testPlotTop() const { return plotTopPx(); }
    float testPlotBottom() const { return plotBottomPx(); }
    // T31：返回某 dB 值对应的刻度文字 y（模拟 drawScales 的 clamp 逻辑）
    float testScaleTextY (float db, bool isGainAxis) const
    {
        const float top = plotTopPx();
        const float bottom = plotBottomPx();
        const float h = 12.0f;
        const float rawY = (isGainAxis ? gainToY (db) : specDbToY (db)) - 6.0f;
        return juce::jlimit (top + 1.0f, bottom - h - 1.0f, rawY);
    }
    void mouseWheelForTest (float x, float y, float dx, float dy)
    {
        auto mkEv = [&]
        {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            return juce::MouseEvent (src, juce::Point<float> (x, y), juce::ModifierKeys(),
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     nullptr, nullptr, juce::Time(),
                                     juce::Point<float> (x, y), juce::Time(), 1, false);
        };
        juce::MouseWheelDetails w;
        w.deltaX = dx;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        mouseWheelMove (mkEv(), w);
    }
    float getFreqDotXForTest (int band) const { return freqToX (bandCenterFreq (band)); }
    float getFreqDotYForTest (int band) const
    {
        return gainToY (curveDbAtFreqBase (bandCenterFreq (band)));
    }
    juce::Rectangle<float> getRangeEdgeRect (int band) const
    {
        return rangeEdgePath_[(size_t) band].getBounds();
    }
    int  getSelectedBand() const noexcept { return selectedBand_; }
    bool isCurveVisible() const noexcept { return curveVisible_; }

private:
    // ---- 坐标映射（mockup v9 常量，按窗口尺寸缩放） ----
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float specDbToY (float db) const;
    float gainToY (float gainDb) const;
    float meterY (float db) const;
    // A8/#4：绘图区边界**完全贴合**状态栏下缘与 global bar 上缘（无间隙）
    float plotBottomPx() const noexcept { return (float) getHeight() - sy (kGlobalBarH); }
    float plotTopPx()    const noexcept { return sy (kTopStripH); }
    float plotRightPx()  const noexcept { return sx (kMeterX) - sx (4.0f); }   // #3：右缘避开 IN/OUT 音量表
    float plotWidthPx()  const noexcept { return plotRightPx(); }
    float plotHeightPx() const noexcept { return plotBottomPx() - plotTopPx(); }

    // ---- 布局（按 920×620 设计，比例缩放） ----
    float sx (float x) const noexcept { return x * (float) getWidth()  / kDesignW; }
    float sy (float y) const noexcept { return y * (float) getHeight() / kDesignH; }

    // ---- 绘制分块 ----
    void drawBackground (juce::Graphics& g);
    void drawGrid (juce::Graphics& g);
    void drawPartitions (juce::Graphics& g);
    void drawScales (juce::Graphics& g);
    void drawCrossovers (juce::Graphics& g);
    void drawBandHeaders (juce::Graphics& g);
    void drawMeters (juce::Graphics& g);
    void drawFreqInput (juce::Graphics& g);   // #7 自绘频率输入框
    void drawTopStrip (juce::Graphics& g);
    void drawGlobalBar (juce::Graphics& g);
    // Phase A：drawDrawer() 已随抽屉拆到 LinkedDrawer::paint()
    // ---- Phase 4.3 动态层 ----
    void drawSpectrum (juce::Graphics& g);
    void drawRangeBell (juce::Graphics& g);
    void drawResponseCurve (juce::Graphics& g);

    void refreshGridCache() {}
    void updateMeterBallistics (double dt);
    // ---- Phase 4.3 数据 ----
    void pollStructureParams();          // 每帧比对结构参数（变化→权重重算）
    void updateWeights();                // w_i(f)：交叉权重（结构变化时）
    void updateCurve();                  // y(f)=Σw·g：每帧
    void refreshSpectrumPaths();         // 新频谱帧到达时
    void refreshCurvePaths();            // 曲线/Range 几何变化时
    float bellWeight (int band, float freq) const;   // 段内 sin² 钟型
    float bandCenterFreq (int band) const;           // 段几何中心频率
    float freqDotScreenY (int band) const;           // 频点屏幕 y（#7 输入框定位）
    float curveDbAtFreq (float freq) const;          // 主曲线上最接近 freq 处的 dB（测试钩子）
    float curveDbAtFreqBase (float freq) const;      // base 曲线上最接近 freq 处的 dB
    int   hitTestFreqDot (juce::Point<float> p, int& bandOut) const;   // C4 频点命中
    bool  hitTestRangeEdge (juce::Point<float> p, int& bandOut, bool& lowerEdge) const;  // Range 缘拖动

    CrunchMBAudioProcessor& proc_;
    MBTheme::Theme theme_ = MBTheme::Theme::Blue;
    MBTheme::Palette pal_;

    // ---- 电平表（UI 弹道；EQ 同款参数） ----
    static constexpr int   kPeakHoldMs = 2000;
    static constexpr float kPeakFallDbPerSec = 30.0f;
    float meterInDb_ = -120.0f, meterOutDb_ = -120.0f;
    float labelInDb_ = -120.0f, labelOutDb_ = -120.0f;   // B5：柱底读数（250ms 节流）
    juce::uint32 nextMeterLabelMs_ = 0;
    float peakInDb_ = -120.0f, peakOutDb_ = -120.0f;
    juce::uint32 peakInHoldUntilMs_ = 0, peakOutHoldUntilMs_ = 0;
    juce::uint32 lastMeterMs_ = 0;

    void updatePeakHold (float& peakDb, juce::uint32& holdUntilMs,
                         float levelDb, juce::uint32 nowMs, double dt);

    // ---- 选中段 ----
    int selectedBand_ = 2;
    bool curveVisible_ = true;
    int  hoverBand_ = -1;
    int  hoverDotBand_ = -1;         // C4：hover 的频点所在段（tooltip 开关）
    int  hoverDividers_ = -1;        // C2：hover/拖动中的分割线（主题色高亮）

    // ---- Phase 4.4 交互状态 ----
    enum class DragMode { None, Divider, Makeup, Range, DotFreq };
    DragMode drag_ = DragMode::None;
    int  dragIndex_ = -1;            // 分割线索引 0..2
    int  dragBand_ = -1;             // Makeup 拖动作用的段（C4 可跨段）
    float dragStartY_ = 0.0f, dragStartMakeup_ = 0.0f;
    bool  dragAxisLocked_ = false;      // #3：主轴已锁定（首次位移超阈值后）
    bool  dragAxisIsX_ = false;         // #3：锁定的是横轴（中心频率）而非纵轴（Makeup）
    // §6 / O5-A：曲线上"按下"先挂起——点击=选中+开 Popover，真正拖动才改成 Makeup
    bool  curvePending_ = false;
    int   curvePendingBand_ = -1;
    float curveDownX_ = 0.0f, curveDownY_ = 0.0f;
    bool  rangePending_ = false;
    int   rangePendingBand_ = -1;
    bool  rangePendingLower_ = true;
    float dragRangeEdgeY_ = 0.0f;      // Range 拖动起始的下缘 y（判定是否仍是 Range 意图）
    float dragStartX_ = 0.0f;
    float dragStartRange_ = 0.0f;
    bool mouseDownOnButton_ = false;
    std::array<float, 4> bypassMemo_ { { 100.0f, 100.0f, 100.0f, 100.0f } };  // BYP 宏记忆（DESIGN §6.6）

    void setParamNorm (const juce::String& id, float norm) const;
    void setParamPhysical (const juce::String& id, float physical) const;   // 物理值（偏斜参数用）
    bool getParamBool (const juce::String& id) const;
    void setBandParamNorm (const juce::String& id, float norm) const;
    int  hitTestButton (juce::Point<float> p, int& bandOut, int& btnOut) const;
    int  hitTestDivider (juce::Point<float> p) const;
    bool hitTestCurve (juce::Point<float> p, int& bandOut) const;
    bool hitTestKnob (juce::Point<float> p, int& bandOut) const { return false; }
    void applyDividerFreq (int index, float freq);
    void applyCenterFreq (int band, float freq);     // P2：横拖频点改段中心频率（两侧 crossover 同步）
    void toggleBypass (int band, bool forceBypass = false);
    // ---- 自绘频率输入框（#7：不用 juce::TextEditor——它在宿主 VST3 编辑器里会弹独立窗口） ----
    struct FreqInputState
    {
        bool  active = false;
        int   band = -1;          // >=0 → 编辑段中心频率
        int   divider = -1;       // >=0 → 编辑分割线频率
        float x = 0.0f, y = 0.0f; // 屏幕位置
        juce::String text;
        juce::uint32 lastBlinkMs = 0;
        bool  cursorVisible = true;
    };
    FreqInputState freqIn_;

    void openFreqEditor (int index, int band);
    void closeFreqEditor (bool apply);

    // ---- Phase A：动画状态（UI_PLAN_V2 §6）----
    // Popover 用 Tween；抽屉的 Tween 归 LinkedDrawer 自己管。
    Tween popoverTween_;
    float popoverAnimTarget_ = 0.0f;
    std::array<Tween, Param::kNumBands> selT_;      // 选中填充 alpha 0.05 ↔ 0.10
    std::array<Tween, Param::kNumBands> hoverT_;    // 按钮列 hover 淡入 0 ↔ 1
    std::array<Tween, Param::kNumBands> bypT_;      // BYP 渐暗 1.0 ↔ 0.45
    std::array<Tween, 3>                divT_;      // 分割线 hover 变色 0 ↔ 1
    std::array<bool,   Param::kNumBands> bandBypassed_ {};   // BYP 状态缓存（检测变化才驱动动画）

    /** 把所有 Tween 推到当前参数状态（无动画）。用于构造/reset/主题切换。 */
    void syncAllTweens();
    /** 推进所有 Tween；有任一在动画中返回 true（用于决定是否继续 repaint）。 */
    bool advanceTweens (double nowMs);
    /** 选中段变化时驱动 selT_（选中填充 0.05 ↔ 0.10 渐变，Phase A）。 */
    void onSelectedBandChanged (int newBand);
    /** BYP 状态变化时驱动 bypT_（渐暗 1.0 ↔ 0.45，Phase A）。 */
    void onBandBypassChanged (int band, bool bypassed);
    /** 某段是否旁通（Bypass = Mix<50%，D9 方案 A 宏）。 */
    bool isBandBypassed (int band) const;
    // ---- Analyzer 设置（Pro-MB 面板对标） ----
    int   analyzerFftSize_ = 2048;      // 1024/2048/4096/8192
    int   analyzerSpeed_   = 1;         // 0=fast 1=medium 2=slow
    float analyzerTilt_    = 0.0f;      // dB/oct（Pro-MB 默认 4.5；MB 版默认 0 = 不加）
    bool  analyzerFreeze_  = false;
    bool  analyzerPreOn_   = true;
    bool  analyzerPostOn_  = true;
    void openAnalyzerMenu();
    void applyAnalyzerSettings();
    bool hitTestAnalyzerButton (juce::Point<float> p) const;

    // ---- Phase 5.3.2：Popover 控件（5 真旋钮 + 3 滑杆行 + EXPERT） ----
    static constexpr int kPopKnobs = 5;
    std::array<std::unique_ptr<Knob>, kPopKnobs> popKnobs_;
    bool  popKnobsReady_ = false;
    bool  popoverExpertOpen_ = false;
    int   popSliderDrag_ = -1;
    float popSliderDragX0_ = 0.0f, popSliderDragV0_ = 0.0f;
    juce::Point<float> lastMousePos_ { -1.0f, -1.0f };
    void  applyPopSlider (int k, double v);    void  ensurePopoverKnobs();
    void  layoutPopoverKnobs();
    void  applyPopoverKnob (int k, double v);
    juce::Rectangle<float> popKnobRect (int k) const;
    juce::Rectangle<float> popSliderRect (int k) const;
    bool  hitTestPopSlider (juce::Point<float> p, int& kOut) const;
    juce::String popParamId (int k) const;          // 该旋钮对应的 APVTS 参数 ID
    juce::Rectangle<float> popExpertRect() const;
    bool  isAllBypassed() const;
    bool hitTestGlobalBypass (juce::Point<float> p) const;
    bool hitTestDisplayRange (juce::Point<float> p) const;
    void openDisplayRangeMenu();
    // ---- Phase 5.2 LINKED CONTROL 抽屉（§5.2 / O2-A / O6-方案1） ----
    // Phase A：抽屉已拆为独立组件 LinkedDrawer（UI_PLAN_V2 §3.2），
    // DisplayView 只持有一个 unique_ptr 并转发开/关与命中分发。
    std::unique_ptr<LinkedDrawer> linkedDrawer_;
    bool  popoverOpen_ = false;                    // O6 互斥（5.3 用）
    int   popoverBand_ = -1;
    std::array<double, 5> popoverValues_ {};       // Threshold/Range/Attack/Release/Mix
    std::array<float,  5> popoverNormalisedValue_ {};   // 归一化 0..1（旋钮弧用）
    // Ratio / Knee(1=soft) / Ext / Detector / (Lookahead 占位)
    std::array<double, 5> popoverSliders_ { 4.0, 1.0, 0.0, 0.0, 0.0 };
    // ---- Phase 5.3 Popover 几何/交互 ----
    float popoverAnim_ = 0.0f;                     // 0=收起 1=展开
    juce::Rectangle<float> popoverRect() const;
    bool hitTestPopoverClose (juce::Point<float> p) const;
    void togglePopover (bool open);
    void drawPopover (juce::Graphics& g);
    void drawPopoverTopBar (juce::Graphics& g);
    void drawPopoverKnobs (juce::Graphics& g);
    void syncPopoverFromParams();
    void syncPopoverSliders();
    float getParamNorm (const juce::String& id) const;
    float paramRangeMinOf (const juce::String& id) const;
    float paramRangeMaxOf (const juce::String& id) const;
    static juce::String fmtHzShort (float hz);
    float getParamPhysical (const juce::String& id) const;
    juce::Rectangle<float> displayRangeBtn_;

    // ---- Phase 4.3：仿 EQ 动态曲线 ----
    static constexpr int kCurvePoints = 241;
    std::vector<float> curveFreqs_;
    std::array<std::array<float, kCurvePoints>, 4> bandWeights_ {};
    std::array<float, kCurvePoints> curveDb_ {};
    std::array<float, 4> bandGainTotalDb_ {};       // 段增益+Makeup（每帧）
    std::array<float, 4> bandDisplayDb_    {};      // 段显示增益（range 截断 + bypass 归原）
    std::array<float, 4> bandRawDb_       {};       // 未压缩响应（makeup only）
    std::array<float, 4> bandLiveDb_      {};      // 实际压缩响应（向下弹）
    std::array<float, 4> bandRangeDb_     {};       // 段 range 值（每帧读取）
    std::array<float, 4> rangeDisplay_    {};       // range 显示值（拖动动画平滑）
    std::array<float, kCurvePoints> baseDb_ {};     // 未压缩基准逐点（Range 层，静态）
    // P1：双刻度 —— 曲线/Range 用 ±curveRangeDb_（3/6/12/30）
    //   displayRangeMode_：0=Auto（自动扩档，Pro-MB 默认）、1..4 = 固定 ±3/±6/±12/±30
    int   displayRangeMode_ = 0;
    float curveRangeDb_ = 6.0f;          // 目标量程（瞬时）
    float curveRangeDisplay_ = 6.0f;     // 显示量程（平滑过渡 → 档位切换动画）
    std::array<float, 4> lastDrawnGainDb_  {};      // 上次绘制增益（曲线重建门控）
    std::array<float, 4> lastDrawnRange_  {};      // 上次绘制 range（Range 层重建门控）
    float lastDrawnCurveRange_ = -1.0f;            // 上次绘制的显示量程（量程动画门控）
    bool  haveDrawnGain_ = false;
    bool  weightsDirty_ = true;
    float cachedX1_ = 0.0f, cachedX2_ = 0.0f, cachedX3_ = 0.0f;
    int   cachedSlope_ = -1, cachedMode_ = -1;
    juce::Path curvePath_;
    bool curvePathsValid_ = false;

    // ---- Phase 4.3：Range 钟型层（4 段各自；#1：改任一段 range 都即时可见） ----
    std::array<juce::Path, 4> rangeFillPath_, rangeEdgePath_;
    bool  rangeBypassed_ = false;
    float cachedRange_ = -30.0f;
    int   cachedRangeBand_ = -1;

    // ---- Phase 4.3：双层频谱 ----
    static constexpr int kSpectrumPoints = SpectrumEngine::kDisplayPoints;
    std::vector<float> spectrumFreqs_;
    std::array<float, kSpectrumPoints> spectrumPre_ {}, spectrumPost_ {};
    bool spectrumHasFrame_ = false;
    juce::Path spectrumPreFill_, spectrumPreStroke_, spectrumPostFill_, spectrumPostStroke_;
    bool spectrumPathsValid_ = false;

    static constexpr float kDesignW = 920.0f;
    static constexpr float kDesignH = 620.0f;
    static constexpr float kMeterX = 886.0f;
    static constexpr float kMeterW = 32.0f;
    static constexpr float kPlotTop = 40.0f;
    static constexpr float kPlotBottom = 588.0f;   // A8：绘图区底缘 = global bar 上缘（−36 刻度不再被遮）
    static constexpr float kPlotH = 548.0f;
    static constexpr float kTopStripH = 30.0f;
    static constexpr float kGlobalBarH = 32.0f;

    // ---- Phase A：参数查找缓存（消除 39 处线性扫描 + dynamic_cast）----
    // 构造时一次性建表；之后 getParamNorm/setParamNorm 等都是 O(1)。
    // 2026-10-14 评审发现：原实现每个查找函数都是
    //   for (auto* p : proc_.getParameters()) dynamic_cast<...>
    // 共 39 个调用点分布在 mouseDown/mouseDrag/mouseUp/syncPopover* 里，
    // 按鼠标刷新率触发 ≈ 每秒十多万次 dynamic_cast。
    std::unordered_map<juce::String, juce::AudioProcessorParameter*> paramMap_;
    void buildParamCache();

    // ---- Phase A：虚拟时钟（仅供测试钩子 tickForTest 使用）----
    double testClockMs_ = 0.0;
    bool   useTestClock_ = false;
    /** 所有测试钩子入口统一切到虚拟时钟（避免真实/虚拟时钟混用导致 Tween 永不前进）。 */
    void enterTestClock();
    /** 动画/平滑用的"当前时间"。测试模式下返回虚拟时钟，否则返回真实时间。 */
    double animNowMs() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisplayView)
};
