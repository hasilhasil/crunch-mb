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
    // R34（N1）：必须是 (false, true)。
    //   · 第 1 个 false = Popover 自身不拦截 → 点空白仍由 DisplayView 分发；
    //   · 第 2 个**必须 true** = 允许子组件（5 个 Knob）接收点击。
    // 原先写的是 (false, false)，而 JUCE 文档（juce_Component.h:969-973）明确：
    //   "if this is false and allowClicksOnThisComponent is false, then neither
    //    this component nor any child components can be clicked on"
    // 且 DisplayView::mouseDown 里从来没有分发到 popKnobRect 的分支
    // → THRESHOLD/RANGE/ATTACK/RELEASE/MIX 五个旋钮鼠标完全无效（用户实测）。
    setInterceptsMouseClicks (false, true);
    buildParamCache();
}

BandPopover::~BandPopover() = default;

void BandPopover::buildParamCache()
{
    // Phase：改走 facade 的查表（PLAN Phase 6：UI 不直触 APVTS）。
    // 原来这里也自持一份 paramMap_——与 LinkedDrawer、DisplayView 三份重复，
    // 正是规范 §6.2 的 D 类耦合。现在统一由 facade 独占。
    proc_.getBackend().buildParamCache();
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
    // Phase：EXPERT 内容从 2 行扩到 5 行（Detector / Lookahead / Filtering / SC Low / SC High）
    const float eh = r.getHeight() * (100.0f / 200.0f);
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
    // 5 行等分：Detector / Lookahead / Filtering / SC Low / SC High
    const int idx = (int) ((p.y - box.getY()) / (box.getHeight() / 5.0f));
    switch (juce::jlimit (0, 4, idx))
    {
        case 0:  return ExpertRow::Detector;
        case 1:  return ExpertRow::Lookahead;
        case 2:  return ExpertRow::Filtering;
        case 3:  return ExpertRow::SCLow;
        default: return ExpertRow::SCHigh;
    }
}

void BandPopover::setSideChainFreq (int band, bool isLow)
{
    // Pro-MB：Free 模式下侧链有一个高通和一个低通频率，用来让该段触发于
    // 不同的频率范围。这里用"点击循环预设档"的方式暴露（与 Lookahead 同样的
    // 交互语言，不需要额外拖拽手势）。
    const juce::String id = "band" + juce::String (band)
                         + (isLow ? "SCLow" : "SCHigh");
    const float choicesLow[4]  = { 20.0f, 100.0f, 500.0f, 2000.0f };
    const float choicesHigh[4] = { 500.0f, 2000.0f, 5000.0f, 20000.0f };
    const float* choices = isLow ? choicesLow : choicesHigh;

    const float cur = getParamPhysical (id);
    int next = 0;
    for (int i = 0; i < 4; ++i)
        if (cur >= choices[i] - 1.0f) next = (i + 1) % 4;
    setParamPhysical (id, choices[next]);
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
                // ⚠️ lookahead 是 Choice 参数，实际值**就是选项索引**（0/1/2）。
                //   这里必须用 getParamPhysical（实际值语义）而不是
                //   getParamNorm（normalized）——R19 把 getParamNorm 改成真正的
                //   归一化后，若继续用它会读到 0/0.5/1，roundToInt 得 0/0/1，
                //   循环立刻错乱（T55.4 实测回归）。
                const int cur = (int) juce::roundToInt (getParamPhysical ("lookahead"));
                const int next = (cur + 1) % 3;
                setParamNorm ("lookahead", next * 0.5f);   // 写入侧仍是 normalized
                sliders_[4] = next * 0.5f;
            }
            break;
        case ExpertRow::Filtering:
            // Band → Free 循环（Pro-MB Side Chain Filtering）
            {
                const float cur = getParamNorm ("band" + juce::String (b) + "SCFilter");
                setParamNorm ("band" + juce::String (b) + "SCFilter", cur > 0.5f ? 0.0f : 1.0f);
            }
            break;
        case ExpertRow::SCLow:
            // 高通频率：20 → 100 → 500 → 2000 Hz 循环
            setSideChainFreq (b, true);
            break;
        case ExpertRow::SCHigh:
            // 低通频率：500 → 2000 → 5000 → 20000 Hz 循环
            setSideChainFreq (b, false);
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
        // R34（A）：去掉纵向滑入，只保留淡入。
        // 原先这里是 `rc.getY() + (1.0f - a) * rc.getHeight()`——把旋钮下移最多
        // 一整个旋钮行高（默认 74px），而 paint() 只对**面板**做 alpha 淡入、
        // 不做位移，于是动画期间旋钮跑到面板下缘之外滑上来，
        // 用户实测"弹出二级菜单时里面的旋钮跟菜单窗口不同步"。
        // 现在位置恒等于最终槽位，透明度跟随面板 anim_ 一起淡入淡出。
        const int nx = (int) rc.getX(), ny = (int) rc.getY();
        auto* kb = knobs_[(size_t) k].get();
        if (force || kb->getX() != nx || kb->getY() != ny
                 || kb->getWidth() != (int) rc.getWidth() || kb->getHeight() != (int) rc.getHeight())
            kb->setBounds (nx, ny, (int) rc.getWidth(), (int) rc.getHeight());
        kb->setAlpha (a);
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

bool BandPopover::stepSlider (int k, int notches, bool fine)
{
    // Phase（R19 修复）：滑块此前只有鼠标拖动，键盘与滚轮完全无效——
    // 这是 R17/R18 清点清单漏掉的第三个实例（Popup 里的 5 个自绘滑块）。
    // Pro-MB / Future MB 的滑轨控件均可用方向键与滚轮调整。
    if (k < 0 || k > 4)
        return false;

    // 步长：连续值用 1/20 量程（fine 用 1/100）；分档值用整档
    const double coarse = fine ? 0.01 : 0.05;
    const int steps = fine ? 1 : notches;
    const double delta = coarse * steps;

    if (k == 0)
    {
        // Ratio：1..20（线性）
        const double cur = sliders_[0];
        const double next = juce::jlimit (1.0, 20.0, cur + delta * 19.0);
        if (std::abs (next - cur) < 1.0e-9) return false;
        applySlider (0, next);
        return true;
    }
    if (k == 1)
    {
        // Knee：0..48 dB（归一化存储）
        const double cur = sliders_[1];
        const double next = juce::jlimit (0.0, 1.0, cur + delta);
        if (std::abs (next - cur) < 1.0e-9) return false;
        applySlider (1, next);
        return true;
    }
    if (k == 2 || k == 3)
    {
        // Ext / Detector：布尔开关（一次 notches 翻一个；fine 不动）
        if (fine) return false;
        applySlider (k, sliders_[(size_t) k] > 0.5 ? 0.0 : 1.0);
        return true;
    }
    // k == 4：Lookahead 三档（0=Off, 0.5=1ms, 1=3ms）。
    // ⚠️ applySlider 对 k=4 不写参数（全局参数另走 clickExpertRow），
    //    所以这里必须显式写，否则 syncFromParams() 会把 sliders_[4]
    //    覆盖回实际参数值，步进看起来"无效"。
    {
        const double cur = sliders_[4];
        double next;
        if (notches > 0)      next = (cur < 0.25) ? 0.5 : 1.0;
        else if (notches < 0) next = (cur > 0.75) ? 0.5 : 0.0;
        else                  next = cur;
        if (std::abs (next - cur) < 1.0e-9) return false;
        setParamNorm ("lookahead", (float) next);      // 0/0.5/1 → 索引 0/1/2
        sliders_[4] = next;
        return true;
    }
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
    sliders_[1] = getParamNorm ("band" + juce::String (b) + "Knee");        // 0..1
    sliders_[2] = getParamNorm ("band" + juce::String (b) + "Ext");         // 0..1
    sliders_[3] = getParamNorm ("band" + juce::String (b) + "Detector");    // 0..1
    // ⚠️ lookahead 是 Choice：存**归一化** 0/0.5/1（与 stepSlider 的三档一致）。
    //    实际值是索引 0..2，用 getParamPhysical 读后换算。
    sliders_[4] = juce::jlimit (0.0f, 1.0f, getParamPhysical ("lookahead") * 0.5f);
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
    // ---- 面板阴影：多层渐变（Phase E-3，Pro-MB 投影对标）----
    // 原来是一层硬边黑影（alpha 0.35 + 下移 3px），看起来像"贴"在界面上。
    // 现在用三层由深到浅、由近到远的渐变，真正"浮"起来。
    // Pro-MB 的投影参数约 0 14px 44px rgba(0,0,0,.5)。
    {
        const float corner = r.getWidth() * (12.0f / 472.0f);
        // 三层：近（深、偏移小）、中、远（浅、偏移大）
        struct Layer { float dy; float alpha; float expand; };
        const Layer layers[3] = {
            { 2.0f,  0.30f, 0.0f  },
            { 6.0f,  0.20f, 3.0f  },
            { 14.0f, 0.14f, 7.0f  },
        };
        for (const auto& L : layers)
        {
            const float k = r.getHeight() / 200.0f;      // 设计稿 200px 高 → 比例
            auto shadow = r.translated (0.0f, L.dy * k).expanded (L.expand * k);
            g.setColour (juce::Colours::black.withAlpha (L.alpha * a));
            g.fillRoundedRectangle (shadow, corner + L.expand * k);
        }
    }

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

    // ---- EXPERT 展开内容：5 行低频操作项（Phase：加侧链滤波三行）----
    if (expertOpen_)
    {
        const auto box = expertContentRect();
        g.setColour (pal_.surface.withAlpha (0.96f * a));
        g.fillRoundedRectangle (box, r.getWidth() * (6.0f / 472.0f));
        g.setColour (pal_.accent.withAlpha (0.6f * a));
        g.drawRoundedRectangle (box, r.getWidth() * (6.0f / 472.0f), 1.0f);

        const int bShow = (band_ >= 0) ? band_ : 0;
        const float rowH = box.getHeight() / 5.0f;
        g.setFont (MBTheme::font (MBTheme::regular(), 8.5f));

        struct Row { const char* label; juce::String value; };
        const Row rows[5] = {
            { "DETECTOR",  sliders_[3] > 0.5 ? "RMS" : "PEAK" },
            { "LOOKAHEAD", sliders_[4] == 0 ? "OFF"
                             : sliders_[4] == 1 ? "1 ms" : "3 ms" },
            { "FILTERING", getParamNorm ("band" + juce::String (bShow) + "SCFilter") > 0.5f
                             ? "FREE" : "BAND" },
            { "SC HIGH-PASS", fmtHzShort (getParamPhysical (
                             "band" + juce::String (bShow) + "SCLow")) + " Hz" },
            { "SC LOW-PASS",  fmtHzShort (getParamPhysical (
                             "band" + juce::String (bShow) + "SCHigh")) + " Hz" },
        };

        for (int i = 0; i < 5; ++i)
        {
            const float y = box.getY() + rowH * (float) i;
            g.setColour (pal_.dim.withMultipliedAlpha (a * 0.9f));
            g.drawText (rows[i].label,
                        juce::Rectangle<float> (box.getX() + box.getWidth() * 0.06f, y,
                                                box.getWidth() * 0.56f, rowH),
                        juce::Justification::centredLeft);
            g.setColour (pal_.text.withMultipliedAlpha (a));
            g.drawText (rows[i].value,
                        juce::Rectangle<float> (box.getX() + box.getWidth() * 0.58f, y,
                                                box.getWidth() * 0.36f, rowH),
                        juce::Justification::centredRight);
        }

        // 提示这些行都是可点的
        g.setColour (pal_.accent.withAlpha (0.7f * a));
        g.setFont (MBTheme::font (MBTheme::regular(), 7.5f));
        g.drawText ("(click to cycle)",
                    juce::Rectangle<float> (box.getX() + box.getWidth() * 0.06f,
                                            box.getBottom() - rowH * 0.45f,
                                            box.getWidth() * 0.88f, rowH * 0.45f),
                    juce::Justification::centredLeft);
    }
}

void BandPopover::resized()
{
    layoutKnobs (true);
}

// ---------------- 参数读写（Phase：统一走 facade）----------------
// ⚠️ 语义警示（R7/R9 的根源）：getParamFast() 返回**实际值**。
//   · Bool 参数：实际值 0/1，与 normalized 相同
//   · Choice 参数：实际值是**选项索引**（0..N-1），与 normalized 不同
//     （3 选项的 normalized 是 0/0.5/1，索引是 0/1/2）
//   · Float 参数：实际值是 dB/Hz/ms/%
// 所以"按 normalized 用"的调用点必须自己转，不能假设两者等价。
float BandPopover::getParamNorm (const juce::String& id) const
{
    // Phase（R19）：委托给 facade，UI 层不再自己实现 physical→normalized。
    // 此前这里有一份等价实现、DisplayView 里还有一份错的（直接 return
    // getParamFast）——同一个换算写两遍，必然一处漂移。
    return proc_.getBackend().getParamNorm (id);
}

float BandPopover::paramRangeMinOf (const juce::String& id) const
{
    for (const auto& pi : proc_.getBackend().listParams())
        if (pi.id == id) return pi.minValue;
    return 0.0f;
}

float BandPopover::paramRangeMaxOf (const juce::String& id) const
{
    for (const auto& pi : proc_.getBackend().listParams())
        if (pi.id == id) return pi.maxValue;
    return 1.0f;
}

float BandPopover::getParamPhysical (const juce::String& id) const
{
    return proc_.getBackend().getParamFast (id);
}

void BandPopover::setParamNorm (const juce::String& id, float norm) const
{
    // ⚠️ 语义：入参是 **normalized**（与 setParamPhysical 的实际值语义不同），
    //    走 setParamNormFast 而非 setParam。
    proc_.getBackend().setParamNormFast (id, norm);
}

void BandPopover::setParamPhysical (const juce::String& id, float physical) const
{
    proc_.getBackend().setParam (id, physical);
}

juce::String BandPopover::fmtHzShort (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + "k";
    return juce::String ((int) std::round (hz));
}
