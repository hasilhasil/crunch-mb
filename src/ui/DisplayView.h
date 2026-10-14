#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>
#include <unordered_map>

#include "MBTheme.h"
#include "Anim.h"
#include "Knob.h"
#include "LinkedDrawer.h"
#include "BandPopover.h"
#include "FreqInputEditor.h"
#include "SpectrumView.h"
#include "AnalyzerPanel.h"
#include "OutputPanel.h"
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
    // ⚠️ 只有**覆盖层**子组件需要在这里补画：抽屉 / Popover / 频率输入框。
    //    频谱（SpectrumView）不是子组件，已由上面的 paint() 在第 4 位绘制。
    //    JUCE 的子组件永远画在父组件 paint() 之后，而离屏只调 paint(g)，
    //    所以这些覆盖层在像素回归里会全部消失——必须手动补上。
    void paintForTest (juce::Graphics& g)
    {
        paint (g);
        for (auto* c : getChildren())
        {
            if (c == nullptr || ! c->isVisible())
                continue;
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::translation (
                (float) -c->getX(), (float) -c->getY()));
            c->paint (g);
        }
    }
    // 测试钩子：手动驱动一帧（timerCallback 为 protected）
    // Phase A：推进**虚拟时钟**再跑一帧，使动画测试确定性
    // （真实时间在紧凑循环里几乎不前进，Tween 会永远停在起点）。
    void tickForTest (double advanceMs = 16.0);
    double nowMsForTest() const { return testClockMs_; }
    // 测试钩子：直接派发鼠标事件（mouseXxx 为 protected；内部经 Desktop 取 MouseInputSource）
    void mouseDownForTest (float x, float y);
    void mouseDragForTest (float x, float y);
    void mouseUpForTest (float x, float y);
    /** Phase D-6：带修饰键的鼠标钩子（Alt+拖 OUT = Output Gain 测试用）。 */
    void mouseDownForTestMods (float x, float y, juce::ModifierKeys mods);
    void mouseDragForTestMods (float x, float y, juce::ModifierKeys mods);
    void mouseUpForTestMods (float x, float y, juce::ModifierKeys mods);
    /** Phase D-6 测试钩子：是否正在拖 Output Gain。 */
    bool isOutputGainDragForTest() const { return drag_ == DragMode::OutputGain; }
    /** Phase D：派发鼠标移动（悬停语义测试用，如 Analyzer 面板 hover-open）。 */
    void mouseMoveForTest (float x, float y);
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
    /** Phase D-5 测试钩子：设置 Display Range 模式（0=Auto，1..4=固定档）。 */
    void  setDisplayRangeModeForTest (int m) { displayRangeMode_ = juce::jlimit (0, 4, m); }
    /** Phase 测试钩子：自绘下拉是否打开 / 矩形。 */
    bool isDropDownOpenForTest() const { return dropDownOpen_; }
    juce::Rectangle<float> dropDownRectForTest() const { return dropDownRect(); }
    /** Phase 测试钩子：下拉项数。 */
    int dropDownCountForTest() const { return dropDownCount_; }
    /** Phase C-6 测试钩子：Dynamics 分段按钮矩形。 */
    juce::Rectangle<float> dynamicsSegRectForTest() const
    {
        return bandPopover_ != nullptr ? bandPopover_->dynamicsSegRectForTest()
                                       : juce::Rectangle<float>();
    }

    /** Phase D-7 测试钩子：MIDI Learn 状态与关联表。 */
    bool isMidiLearnActiveForTest() const { return midiLearnActive_; }
    int  midiControllerForControlForTest (int controlId) const
    {
        const auto it = midiMap_.find (controlId);
        return (it != midiMap_.end()) ? it->second : -1;
    }
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
    bool isPopoverOpenForTest() const { return bandPopover_ != nullptr && bandPopover_->isOpen(); }
    int  getPopoverBandForTest() const { return bandPopover_ != nullptr ? bandPopover_->band() : -1; }
    int  getSelectedBandForTest() const { return selectedBand_; }
    double getPopoverValueForTest (int k) const
    {
        return bandPopover_ != nullptr ? bandPopover_->popoverValueForTest (k) : 0.0;
    }
    void syncPopoverFromParamsForTest() { if (bandPopover_) bandPopover_->syncFromParams(); }
    void openLinkedDrawerForTest();
    juce::Rectangle<float> popoverRectForTest() const
    {
        return bandPopover_ != nullptr ? bandPopover_->popoverRectForTest()
                                       : juce::Rectangle<float>();
    }
    // T38 Analyzer 测试钩子
    bool hitTestAnalyzerButtonForTest (float x, float y) const
    { return hitTestAnalyzerButton (juce::Point<float> (x, y)); }
    int   getAnalyzerFftSizeForTest() const { return analyzerFftSize_; }
    int   getAnalyzerSpeedForTest()   const { return analyzerSpeed_; }
    float getAnalyzerTiltForTest()    const { return analyzerTilt_; }
    bool  getAnalyzerFreezeForTest()  const { return analyzerFreeze_; }
    bool  getAnalyzerPreOnForTest()   const { return analyzerPreOn_; }
    bool  getAnalyzerPostOnForTest()  const { return analyzerPostOn_; }
    // Phase D：Analyzer 设置的真源在 AnalyzerPanel，这些测试钩子转发过去，
    // 再同步回本地副本并 apply（否则写本地副本不产生任何效果）。
    void  setAnalyzerPreOnForTest (bool v)
    {
        if (analyzerPanel_) analyzerPanel_->setPreOnForTest (v);
        analyzerPreOn_ = v; syncSpectrumLayers();
    }
    void  setAnalyzerPostOnForTest (bool v)
    {
        if (analyzerPanel_) analyzerPanel_->setPostOnForTest (v);
        analyzerPostOn_ = v; syncSpectrumLayers();
    }
    void  setAnalyzerFftSizeForTest (int v)
    {
        if (analyzerPanel_) analyzerPanel_->setFftSizeForTest (v);
        analyzerFftSize_ = v; applyAnalyzerSettings();
    }
    void  setAnalyzerSpeedForTest (int v)
    {
        if (analyzerPanel_) analyzerPanel_->setSpeedForTest (v);
        analyzerSpeed_ = v; applyAnalyzerSettings();
    }
    void  setAnalyzerTiltForTest (float v)
    {
        if (analyzerPanel_) analyzerPanel_->setTiltForTest (v);
        analyzerTilt_ = v; applyAnalyzerSettings();
    }
    void  setAnalyzerFreezeForTest (bool v)
    {
        if (analyzerPanel_) analyzerPanel_->setFrozenForTest (v);
        analyzerFreeze_ = v; applyAnalyzerSettings();
    }
    void  applyAnalyzerSettingsForTest()      { applyAnalyzerSettings(); }
    // T40 Popover 控件测试钩子 —— Phase A：经 BandPopover 转发
    void applyPopoverKnobForTest (int k, double v);
    juce::Rectangle<float> popSliderRectForTest (int k) const;
    juce::Rectangle<float> popExpertRectForTest() const;
    /** Phase C-5：EXPERT 展开内容矩形（T40 用）。 */
    juce::Rectangle<float> popExpertContentRectForTest() const;
    /** Phase D-2 测试钩子：LINKED 抽屉是否 sticky。 */
    bool isLinkedStickyForTest() const
    {
        return linkedDrawer_ != nullptr && linkedDrawer_->isSticky();
    }
    /** Phase D-3 测试钩子：是否正在多选拖拽。 */
    bool isBandSweepActiveForTest() const { return bandSweepActive_; }
    /** Phase D 测试钩子：Analyzer 面板是否打开 / 是否 sticky。 */
    bool isAnalyzerOpenForTest() const
    {
        return analyzerPanel_ != nullptr && analyzerPanel_->isOpen();
    }
    bool isAnalyzerStickyForTest() const
    {
        return analyzerPanel_ != nullptr && analyzerPanel_->isSticky();
    }

    /** Phase D-4 测试钩子 */
    bool isOutputOpenForTest() const
    {
        return outputPanel_ != nullptr && outputPanel_->isOpen();
    }
    bool isOutputStickyForTest() const
    {
        return outputPanel_ != nullptr && outputPanel_->isSticky();
    }
    /** Phase D-4 测试钩子：Output 面板矩形。 */
    juce::Rectangle<float> outputPanelRectForTest() const
    {
        return outputPanel_ != nullptr ? outputPanel_->panelRect()
                                       : juce::Rectangle<float>();
    }
    bool  isPopoverExpertOpenForTest() const;
    bool  hasSpectrumFrameForTest() const
    {
        return spectrumView_ != nullptr && spectrumView_->hasFrameForTest();
    }
    /** Phase C-3 测试钩子：该段平滑后的电平（dB）。 */
    float bandLevelDbForTest (int b) const
    {
        return (b >= 0 && b < Param::kNumBands) ? bandLevelDb_[(size_t) b] : -100.0f;
    }
    bool  spectrumPathsReadyForTest() const
    {
        return spectrumView_ != nullptr && spectrumView_->pathsReadyForTest();
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
    void drawFreqInput (juce::Graphics& g) { }   // #7 已拆为 FreqInputEditor 子组件自绘
    void drawTopStrip (juce::Graphics& g);
    void drawGlobalBar (juce::Graphics& g);
    // Phase A：drawDrawer() 已随抽屉拆到 LinkedDrawer::paint()
    // ---- Phase 4.3 动态层 ----
    // Phase A：drawSpectrum() / refreshSpectrumPaths() 已整体拆到 SpectrumView 组件。
    void drawRangeBell (juce::Graphics& g);
    void drawResponseCurve (juce::Graphics& g);

    void refreshGridCache() {}
    void updateMeterBallistics (double dt);
    // ---- Phase 4.3 数据 ----
    void pollStructureParams();          // 每帧比对结构参数（变化→权重重算）
    void updateWeights();                // w_i(f)：交叉权重（结构变化时）
    /** y(f)=Σw·g：每帧。dtSeconds 供 Phase SPI 显示层平滑用。 */
    void updateCurve (double dtSeconds = 1.0 / 60.0);
    // Phase A：refreshSpectrumPaths() 已拆到 SpectrumView 组件。
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
    enum class DragMode { None, Divider, Makeup, Range, DotFreq, BandSweep, OutputGain };
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
    float dragStartOutputGainDb_ = 0.0f;   // Phase D-6：Alt+拖 OUT 起始增益
    float dragStartRange_ = 0.0f;
    bool mouseDownOnButton_ = false;

    // ---- Phase D-3：显示背景拖拽多选（Pro-MB multi-select）----
    // Pro-MB 官方帮助原话："click and drag across the display background to
    // quickly select multiple bands"。在频谱背景上（不命中任何控件）按下并拖动，
    // 扫过的频段全部变为选中态。
    // 注意与既有拖拽的分辨：只有在**背景**上起手才算 BandSweep，
    // 曲线/频点/分割线/Range 缘起手仍走各自的分支。
    bool bandSweepActive_ = false;
    std::array<float, 4> bypassMemo_ { { 100.0f, 100.0f, 100.0f, 100.0f } };  // BYP 宏记忆（DESIGN §6.6）

    void setParamNorm (const juce::String& id, float norm) const;
    void setParamPhysical (const juce::String& id, float physical) const;   // 物理值（偏斜参数用）
    bool getParamBool (const juce::String& id) const;
    float getParamNorm (const juce::String& id) const;
    float getParamPhysical (const juce::String& id) const;
    static juce::String fmtHzShort (float hz);
    float paramRangeMinOf (const juce::String& id) const;
    float paramRangeMaxOf (const juce::String& id) const;
    void setBandParamNorm (const juce::String& id, float norm) const;
    int  hitTestButton (juce::Point<float> p, int& bandOut, int& btnOut) const;
    int  hitTestDivider (juce::Point<float> p) const;
    bool hitTestCurve (juce::Point<float> p, int& bandOut) const;
    bool hitTestKnob (juce::Point<float> p, int& bandOut) const { return false; }
    void applyDividerFreq (int index, float freq);
    void applyCenterFreq (int band, float freq);     // P2：横拖频点改段中心频率（两侧 crossover 同步）
    void toggleBypass (int band, bool forceBypass = false);
    // ---- 自绘频率输入框（#7：不用 juce::TextEditor——它在宿主 VST3 编辑器里会弹独立窗口） ----
    // Phase A：已拆为独立组件 FreqInputEditor。DisplayView 只负责开合与键盘/光标转发。
    void openFreqEditor (int index, int band);
    std::unique_ptr<FreqInputEditor> freqInput_;

    // ---- Phase A：动画状态（UI_PLAN_V2 §6）----
    // Popover 用 Tween；抽屉的 Tween 归 LinkedDrawer 自己管。
    Tween popoverTween_;
    float popoverAnimTarget_ = 0.0f;
    std::array<Tween, Param::kNumBands> selT_;      // 选中填充 alpha 0.05 ↔ 0.10
    std::array<Tween, Param::kNumBands> hoverT_;    // 按钮列 hover 淡入 0 ↔ 1
    std::array<Tween, Param::kNumBands> bypT_;      // BYP 渐暗 1.0 ↔ 0.45
    std::array<Tween, 3>                divT_;      // 分割线 hover 变色 0 ↔ 1
    std::array<bool,   Param::kNumBands> bandBypassed_ {};   // BYP 状态缓存（检测变化才驱动动画）

    // ---- Phase D-7：MIDI Learn（Pro-MB MIDI learn 流程对标）----
    // 流程：点 MIDI Learn 按钮进入学习模式 -> 可学控件高亮
    //   -> 点某个控件 -> 显示关联的 MIDI 控制器号（未关联显示 "-"）-> 再点退出
    // 注意：本插件不真正接收 MIDI，实现的是可视化的学习流程，
    //   能记录/显示哪个控件关联到哪个控制器号，但不做 MIDI 消息路由。
    bool  midiLearnActive_ = false;
    int   midiLearnTarget_ = -1;
    std::unordered_map<int, int> midiMap_;
    int  midiControlId (juce::Point<float> p) const;
    void toggleMidiLearn();
    bool handleMidiLearnClick (juce::Point<float> p);
    void drawMidiLearnOverlay (juce::Graphics& g);

    // ---- Phase E-4：1px 细线 AA 提亮 ----
    // 1px 线用 fillRect/drawRoundedRectangle 画时，抗锯齿把边缘各吃掉约
    // 0.5px 的覆盖，实际亮度只有设定值的 ~50%，看起来发灰发虚。
    // 补偿：把线对齐到像素中心（y 取 floor+0.5），并对 alpha 做提亮。
    // 这是细节抛光，不改变布局与语义。
    /** 画一条水平 1px 细线（像素对齐 + AA 提亮）。 */
    void drawHairlineH (juce::Graphics& g, float y, float x0, float x1,
                        juce::Colour c) const;
    /** 画一条垂直 1px 细线（像素对齐 + AA 提亮）。 */
    void drawHairlineV (juce::Graphics& g, float x, float y0, float y1,
                        juce::Colour c) const;

    // ---- Phase：自绘 TextSelect 下拉（替换原生 PopupMenu）----
    // Pro-MB 的 drop-down 是自绘的：与插件同色的圆角面板、当前项打勾、
    // hover 高亮。原生 juce::PopupMenu 是操作系统外观，破坏主题一致性。
    // 首个替换对象是顶栏的 Display Range 档位选择器；
    // 其后是全局栏的 Phase / Slope / Quality / Lookahead 四个选择器
    // （RESEARCH_UI §五 P1：原来是画着 ▼ 却点不动的静态文字）。
    bool  dropDownOpen_ = false;
    int   dropDownHover_ = -1;
    int   dropDownCount_ = 0;
    /** 当前下拉绑定到哪个全局参数（空 = Display Range 档位）。 */
    juce::String dropDownParamId_;
    /** 该参数是 Choice 型时的选项表（Display Range 用内置档位，不用它）。 */
    juce::StringArray dropDownChoices_;
    /** 全局选择器下拉的锚点（global bar 内被点项的位置）。 */
    float globalBarY_ = 0.0f;
    float globalBarAnchorX_ = 0.0f;
    /** global bar 内四个全局 Choice 选择器的矩形与参数 id（绘制/命中共用）。 */
    juce::Array<juce::Rectangle<float>> globalSelectorRects_;
    juce::StringArray globalSelectorIds_;
    juce::Rectangle<float> dropDownRect() const;
    bool hitTestDropDownItem (juce::Point<float> p, int& itemOut) const;
    void openDropDown (int count);
    void closeDropDown();
    void clickDropDownItem (int item);
    void drawDropDown (juce::Graphics& g);
    /** 打开绑定到某个全局 Choice 参数的下拉。 */
    void openGlobalParamDropDown (const juce::String& paramId,
                                  juce::Rectangle<float> anchorRect);

    // ---- Phase E-1：backdrop blur（UI_SPEC §12.2 档 b）----
    // 玻璃条背后的内容做一次下采样 + 盒模糊，缓存成 Image，15Hz 更新。
    // 关键约束：
    //   · 模糊源是已画好的背景层，不含玻璃条自身与更上层控件（避免自我反馈）。
    //   · 缓存 Image 在消息线程构建，音频线程不碰。
    //   · 15Hz 更新：模糊是低频信息，人眼对模糊滞后不敏感，CPU 省 4×。
    juce::Image blurCache_;
    int       blurCacheW_ = 0, blurCacheH_ = 0;
    juce::uint32 blurLastBuildMs_ = 0;
    void buildBlurCache (juce::Rectangle<float> srcRect);
    bool drawBlurredBackdrop (juce::Graphics& g, juce::Rectangle<float> dstRect);

    // ---- Phase C-3：频段圆形电平表（Pro-MB Threshold 旋钮外圈环对标）----
    // ---- Phase E-2：空闲停 Timer ----
    // timerCallback 里记录本帧是否收到新频谱帧；无动效且无新帧时停表。
    // 任何交互/动画开始时会经 ensureTimerRunning() 重启。
    bool spectrumHadNewFrame_ = false;
    void ensureTimerRunning()
    {
        if (! isTimerRunning())
            startTimerHz (60);
    }
    // 实时值由音频线程写、UI 线程读；这里各存一份平滑后的显示值，
    // 让环的转动有弹道感而不是每帧跳。
    // ⚠️ 初值必须是静音底 −100dB，不能是 0：0 会被弹道逻辑当成"当前电平 0dB"，
    //    导致插件刚打开时四个环显示满刻度再慢慢衰减（T39.18a 抓到过）。
    std::array<float, Param::kNumBands> bandLevelDb_ { -100, -100, -100, -100 };
    std::array<float, Param::kNumBands> bandPeakDb_  { -100, -100, -100, -100 };
    std::array<juce::uint32, Param::kNumBands> bandPeakHoldMs_ {};
    void updateBandLevels (double nowMs, double dt);

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
    /** Phase A：把 Pre/Post 开关同步给 SpectrumView 子组件。 */
    void syncSpectrumLayers();
    /** Phase D-4：把 OutputPanel 的设置应用到处理器。 */
    void applyOutputSettings();
    bool hitTestAnalyzerButton (juce::Point<float> p) const;
    /** Phase D：Analyzer 自绘面板（Pro-MB hover-open / click-sticky 语义）。 */
    std::unique_ptr<AnalyzerPanel> analyzerPanel_;
    /** Phase D-4：Output 自绘面板（同一套 sticky 语义）。 */
    std::unique_ptr<OutputPanel> outputPanel_;

    // ---- Phase 5.2 / 5.3：抽屉与 Popover 组件（UI_PLAN_V2 §3.2）----
    // Phase A：两者均已拆为独立组件。DisplayView 只持 unique_ptr，
    // 负责开合仲裁（O6 互斥）、命中分发、鼠标坐标转发、每帧 advance()。
    std::unique_ptr<LinkedDrawer> linkedDrawer_;
    std::unique_ptr<BandPopover>  bandPopover_;
    juce::Point<float> lastMousePos_ { -1.0f, -1.0f };
    juce::Rectangle<float> displayRangeBtn_;

    bool  isAllBypassed() const;
    juce::Rectangle<float> midiBtnRect_;
    /** Phase D-7：MIDI Learn 按钮命中（现算，不依赖 paint 副作用）。 */
    bool hitTestMidiButton (juce::Point<float> p) const;
    bool hitTestGlobalBypass (juce::Point<float> p) const;
    /** Phase D-4：底栏 OUT 按钮（可点，悬停打开 Output 面板）。 */
    bool hitTestOutputButton (juce::Point<float> p) const;
    bool hitTestDisplayRange (juce::Point<float> p) const;
    void openDisplayRangeMenu();
    /** O6 方案1：抽屉与 Popover 互斥。 */
    void togglePopover (bool open);
    /** 打开 Popover 并把目标段与几何边界传给它。 */
    void openPopoverForBand (int band);
    /** 段的像素左右边界（供 Popover 水平居中）。 */
    void bandEdgesPx (int band, float& leftX, float& rightX) const;

    // ---- Phase 4.3：仿 EQ 动态曲线 ----
    static constexpr int kCurvePoints = 241;
    std::vector<float> curveFreqs_;
    std::array<std::array<float, kCurvePoints>, 4> bandWeights_ {};
    std::array<float, kCurvePoints> curveDb_ {};
    std::array<float, 4> bandGainTotalDb_ {};       // 段增益+Makeup（每帧）
    std::array<float, 4> bandDisplayDb_    {};      // 段显示增益（range 截断 + bypass 归原）
    // Phase：raw/live 初值用 NaN 标记"未初始化"——首帧 updateCurve 会
    // 直接吸附到真实值，避免插件刚打开时曲线从 0 dB 慢慢爬上来。
    static constexpr float kUninitDb = -1.0e30f;   // 哨兵值：isfinite 但远离任何真实 dB
    std::array<float, 4> bandRawDb_  { { kUninitDb, kUninitDb, kUninitDb, kUninitDb } };
    std::array<float, 4> bandLiveDb_ { { kUninitDb, kUninitDb, kUninitDb, kUninitDb } };
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
    // Phase A：已拆为独立组件 SpectrumView（UI_PLAN_V2 §3.2）。
    // DisplayView 只提供坐标映射（freqToX / specDbToY / specBottomY）并每帧驱动。
    std::unique_ptr<SpectrumView> spectrumView_;

    static constexpr float kDesignW = 920.0f;
    static constexpr float kDesignH = 620.0f;
    static constexpr float kMeterX = 886.0f;
    static constexpr float kMeterW = 32.0f;
    static constexpr float kPlotTop = 40.0f;
    static constexpr float kPlotBottom = 588.0f;   // A8：绘图区底缘 = global bar 上缘（−36 刻度不再被遮）
    static constexpr float kPlotH = 548.0f;
    static constexpr float kTopStripH = 30.0f;
    static constexpr float kGlobalBarH = 32.0f;
    // Phase B-4：增益刻度右缘 = IN/OUT 电平表左界(886) − 4px。
    // Pro-MB 的增益刻度与电平表紧邻，读压缩量时视线不用横跳。
    static constexpr float kGainScaleRightX = 882.0f;

    // ---- Phase：参数访问统一走 facade（PLAN Phase 6 / API 规范耦合点 D）----
    // 原来这里自持一份 id → AudioProcessorParameter* 的 paramMap_ 缓存。
    // 那正是规范 §6.2 列出的"D UI 直触 APVTS"耦合点：同一份映射在 facade
    // 和 UI 各存一份，迟早漂移；且 UI 因此能绕过 facade 的钳制/拒绝语义。
    // 现已全部改为 proc_.getBackend().getParamFast(id) / setParam(id, v)，
    // 由 facade 独占参数访问（O(1) 查表 + 钳制 + 值型拒绝）。

    // ---- Phase A：虚拟时钟（仅供测试钩子 tickForTest 使用）----
    double testClockMs_ = 0.0;
    bool   useTestClock_ = false;
    /** 所有测试钩子入口统一切到虚拟时钟（避免真实/虚拟时钟混用导致 Tween 永不前进）。 */
    void enterTestClock();
    /** 动画/平滑用的"当前时间"。测试模式下返回虚拟时钟，否则返回真实时间。 */
    double animNowMs() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisplayView)
};
