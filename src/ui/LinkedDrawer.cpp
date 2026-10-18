#include "LinkedDrawer.h"
#include "../PluginProcessor.h"

// ============================================================
// LinkedDrawer 实现（UI_PLAN_V2 §3.2）
// 从 DisplayView 拆出：−250 行，DisplayView 不再持有抽屉的任何状态。
// ============================================================

namespace
{
    // 与 DisplayView 的 sx/sy 等价：以 920×620 设计稿为基准等比缩放。
    // 抽屉自身不知道宿主缩放因子，由 setHostBounds 提供。
    struct Scale { float x = 1.0f, y = 1.0f; };
}

LinkedDrawer::LinkedDrawer (CrunchMBAudioProcessor& processor)
    : juce::Component ("CrunchMBLinkedDrawer"), proc_ (processor)
{
    // R34（N1）：必须是 (false, true)。
    //   · 第 1 个 false = 抽屉自身不拦截 → 点空白仍由 DisplayView 分发；
    //   · 第 2 个**必须 true** = 允许子组件（5 个 Knob）接收点击。
    // 原先写的是 (false, false)，而 JUCE 文档（juce_Component.h:969-973）明确：
    //   "if this is false and allowClicksOnThisComponent is false, then neither
    //    this component nor any child components can be clicked on"
    // 即整棵子树都点不动 → 用户实测"二级菜单里的旋钮完全没法操作"。
    setInterceptsMouseClicks (false, true);
}

LinkedDrawer::~LinkedDrawer() = default;

void LinkedDrawer::setTheme (MBTheme::Theme t)
{
    pal_ = MBTheme::palette (t);
    // R34（N6/N8）：必须把新调色板下发给 5 个 Knob。
    // Knob 是独立子组件、自带 pal_，只 repaint() 自己是刷不动它们的——
    // 齿轮接上主题切换后，抽屉旋钮会停留在旧配色。
    // 对照 BandPopover::setTheme，那里一直是循环设置的。
    for (auto& kb : knobs_)
        if (kb != nullptr) kb->setPalette (pal_);
    repaint();
}

void LinkedDrawer::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    layoutKnobs (true);
    repaint();
}

juce::Rectangle<float> LinkedDrawer::panelRect() const
{
    // 面板：宽 5×92 = 460（注意：梯形**按钮**是 456，面板是 460，二者不同宽），
    // 居中；贴在梯形按钮上方（留 4px 间隙）
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float pw = W * (460.0f / 920.0f);          // sx(5 × 92)
    const float ph = H * (120.0f / 620.0f);          // sy(120)
    const float px = (W - pw) * 0.5f;
    const float buttonTop = H - H * (32.0f / 620.0f) - H * (29.0f / 620.0f);   // global bar + 按钮高
    return juce::Rectangle<float> (px, buttonTop - ph - H * (4.0f / 620.0f), pw, ph);
}

juce::Rectangle<float> LinkedDrawer::buttonRect() const
{
    const float W = (float) hostBounds_.getWidth();
    const float H = (float) hostBounds_.getHeight();
    const float cw = W * (456.0f / 920.0f), ch = H * (29.0f / 620.0f);
    const float cx = (W - cw) * 0.5f;
    const float cy = H - H * (32.0f / 620.0f) - ch + 1.0f;
    return juce::Rectangle<float> (cx, cy, cw, ch);
}

bool LinkedDrawer::hitTestButton (juce::Point<float> p) const
{
    return buttonRect().contains (p);
}

bool LinkedDrawer::hitTestPanel (juce::Point<float> p) const
{
    return open_ && panelRect().contains (p);
}

void LinkedDrawer::setOpen (bool open)
{
    if (open == open_)
        return;
    open_ = open;
    if (open)
        ensureKnobs();
    // 目标变化时从"当前显示值"起跳，不回弹
    tween_.start (juce::Time::getMillisecondCounter(), open ? 1.0f : 0.0f,
                  Anim::kDrawerMs, anim_);
    animTarget_ = open ? 1.0f : 0.0f;
    layoutKnobs (false);
    repaint();
}

void LinkedDrawer::toggle()
{
    setOpen (! open_);
}

// ---------------- Phase D-2：Pro-MB sticky 语义 ----------------
// 与 AnalyzerPanel 同一套状态机（悬停打开 / 点击 sticky / 再点或点外面收起）。
void LinkedDrawer::hoverButton (bool hovering)
{
    // 只有**非 sticky** 时才跟随悬停开合。sticky 面板已"钉住"，悬停不影响它。
    if (sticky_)
        return;
    if (hovering == open_)
        return;
    setOpen (hovering);
}

bool LinkedDrawer::clickButton()
{
    if (! sticky_)
    {
        // 第一次点击：变 sticky 并保持打开
        sticky_ = true;
        setOpen (true);
    }
    else
    {
        // 已 sticky：再点 = 收起并解除 sticky
        sticky_ = false;
        setOpen (false);
    }
    return true;
}

void LinkedDrawer::clickOutside()
{
    if (! open_ && ! sticky_)
        return;
    sticky_ = false;                 // 点外面 = 完全收起（连 sticky 一起解除）
    setOpen (false);
}

bool LinkedDrawer::advance (double nowMs)
{
    anim_ = tween_.value (nowMs);
    if (std::abs (anim_ - animTarget_) < Anim::kSnapEps)
    {
        anim_ = animTarget_;
        tween_.snapTo (animTarget_);
    }
    layoutKnobs (false);
    syncKnobVisibility();
    return ! tween_.done (nowMs);
}

void LinkedDrawer::snapToCurrent()
{
    anim_ = animTarget_ = open_ ? 1.0f : 0.0f;
    tween_.snapTo (anim_);
    if (open_) ensureKnobs();
    layoutKnobs (true);
    syncKnobVisibility();
}

void LinkedDrawer::ensureKnobs()
{
    if (knobs_[0] != nullptr)
        return;

    // 4 个偏移旋钮（Δ 语义）+ 1 个 Mix（绝对值）
    struct Cfg { const char* label; double mn, mx, def; int dec; const char* suf; };
    const Cfg cfgs[kLinkedKnobs] = {
        { "THRESHOLD", -60.0,   60.0,  0.0, 1, "dB" },
        { "ATTACK",   -200.0,  200.0,  0.0, 1, "ms" },
        { "RELEASE",  -900.0,  900.0,  0.0, 1, "ms" },
        { "MAKEUP",    -36.0,   36.0,  0.0, 1, "dB" },
        { "MIX",         0.0,  100.0, 100.0, 0, "%" },
    };

    for (int k = 0; k < kLinkedKnobs; ++k)
    {
        auto* kb = new Knob();
        kb->setRange (cfgs[k].mn, cfgs[k].mx, (k == kLinkedMixIndex) ? 1.0 : 0.1);
        kb->setDefaultValue (cfgs[k].def);
        kb->setValue (cfgs[k].def, false);
        kb->setLabel (cfgs[k].label);
        kb->setValueSuffix (cfgs[k].suf);
        kb->setDecimals (cfgs[k].dec);
        kb->setKnobDiameter (52.0f);
        kb->setShowLabel (true);
        kb->setLinkedMark (true);                       // §5.2：⊞ 联动标识
        if (k != kLinkedMixIndex)
            kb->setValuePrefix (juce::String::fromUTF8 ("\xCE\x94 "));   // §5.2：偏移读数带 Δ
        const int kk = k;
        kb->onValueChange = [this, kk] (double v) { applyOffset (kk, v); };
        addAndMakeVisible (kb);
        knobs_[(size_t) k].reset (kb);
    }
}

void LinkedDrawer::layoutKnobs (bool force)
{
    const auto panel = panelRect();
    const float unitW = panel.getWidth() / (float) kLinkedKnobs;
    const float knobW = unitW * (84.0f / 92.0f);
    const float knobH = panel.getHeight() * (96.0f / 120.0f);
    for (int k = 0; k < kLinkedKnobs; ++k)
    {
        if (knobs_[(size_t) k] == nullptr)
            continue;
        // 旋钮顶部留 20/120 给标签行（Knob 的标签画在旋钮上方），
        // 否则标签会突出到面板外、贴到绘图区上
        const float x = panel.getX() + unitW * (float) k + (unitW - knobW) * 0.5f;
        const float y = panel.getY() + panel.getHeight() * (20.0f / 120.0f)
                      + (1.0f - juce::jlimit (0.0f, 1.0f, anim_)) * panel.getHeight();
        const int nx = (int) x, ny = (int) y;
        auto* kb = knobs_[(size_t) k].get();
        // A3：只在位置变化超过 1px 时才 setBounds，避免每帧 5 次 resized()
        if (force || kb->getX() != nx || kb->getY() != ny
                 || kb->getWidth() != (int) knobW || kb->getHeight() != (int) knobH)
            kb->setBounds (nx, ny, (int) knobW, (int) knobH);
    }
}

void LinkedDrawer::syncKnobVisibility()
{
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->setVisible (open_ || anim_ > 0.001f);
}

void LinkedDrawer::tickEditCursors (juce::uint32 nowMs)
{
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->setEditCursorBlink (nowMs);
}

void LinkedDrawer::advanceKnobStates (double nowMs)
{
    // Phase C-2：四态渐变。旋钮自身没有 Timer（14 个旋钮各起一个太浪费），
    // 由宿主的 60fps 定时器统一推进。
    for (auto& kb : knobs_)
        if (kb != nullptr)
            kb->advanceStates (nowMs);
}

void LinkedDrawer::applyOffset (int k, double newValue)
{
    // ---- Mix（k=4）：**绝对值语义**（§5.2 v1.6 例外）——四段 Mix 同设为该值 ----
    if (k == kLinkedMixIndex)
    {
        const double old = getParamPhysical ("band0Mix");
        if (std::abs (newValue - old) < 1.0e-9)
            return;
        for (int b = 0; b < Param::kNumBands; ++b)
            setParamPhysical ("band" + juce::String (b) + "Mix", (float) newValue);
        return;
    }

    // ---- 前 4 个旋钮：Δ 偏移语义 —— 四段同名参数线性偏移 ----
    const double delta = newValue - offset_[(size_t) k];
    if (std::abs (delta) < 1.0e-9)
    {
        offset_[(size_t) k] = newValue;
        return;
    }
    offset_[(size_t) k] = newValue;

    const juce::String name = (k == 0) ? "Threshold"
                           : (k == 1) ? "Attack"
                           : (k == 2) ? "Release" : "Makeup";
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const juce::String id = "band" + juce::String (b) + name;
        setParamPhysical (id, getParamPhysical (id) + (float) delta);   // 内部钳到参数范围
    }
}

float LinkedDrawer::getParamPhysical (const juce::String& id) const
{
    // Phase：走 facade（原为 proc_.getParameters() 线性扫描 + 两次 dynamic_cast）
    return proc_.getBackend().getParamFast (id);
}

void LinkedDrawer::setParamPhysical (const juce::String& id, float physical) const
{
    proc_.getBackend().setParam (id, physical);
}

void LinkedDrawer::paint (juce::Graphics& g)
{
    if (anim_ < 0.004f && ! open_)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);

    // ---- 梯形按钮（A7：底边与 global 条 +1px 叠合，消除接缝）----
    const auto br = buttonRect();
    const float W = br.getWidth(), H = br.getHeight();

    juce::Path tp;
    tp.startNewSubPath (br.getX() + W * (44.0f / 456.0f), br.getY() + H * (2.0f / 29.0f));
    tp.lineTo (br.getX() + W * (412.0f / 456.0f), br.getY() + H * (2.0f / 29.0f));
    tp.cubicTo (br.getX() + W * (434.0f / 456.0f), br.getY() + H * (2.0f / 29.0f),
                br.getX() + W * (422.0f / 456.0f), br.getY() + H * (26.0f / 29.0f),
                br.getX() + W * (444.0f / 456.0f), br.getY() + H * (26.0f / 29.0f));
    tp.lineTo (br.getX() + W * (12.0f / 456.0f), br.getY() + H * (26.0f / 29.0f));
    tp.cubicTo (br.getX() + W * (34.0f / 456.0f), br.getY() + H * (26.0f / 29.0f),
                br.getX() + W * (22.0f / 456.0f), br.getY() + H * (2.0f / 29.0f),
                br.getX() + W * (44.0f / 456.0f), br.getY() + H * (2.0f / 29.0f));
    tp.closeSubPath();

    g.setColour (pal_.panel.withAlpha (0.9f));
    g.fillPath (tp);
    g.setColour (pal_.gridZ);
    g.strokePath (tp, juce::PathStrokeType (1.0f));

    g.setColour (pal_.accent);
    g.setFont (MBTheme::font (MBTheme::bold(), 11.0f));
    g.drawText (open_ ? "LINKED CONTROL  \xE2\x96\xB2" : "LINKED CONTROL",
                juce::Rectangle<float> (br.getX() + W * (120.0f / 456.0f), br.getY() + H * (6.0f / 29.0f),
                                        W * (180.0f / 456.0f), H * (14.0f / 29.0f)),
                juce::Justification::centredLeft);
    g.setColour (pal_.dim);
    g.setFont (MBTheme::font (MBTheme::regular(), 9.0f));
    g.drawText (juce::String::fromUTF8 ("\xE5\x9B\x9B\xE6\xAE\xB5\xE8\x81\x94\xE5\x8A\xA8 \xC2\xB7 \xE7\xBA\xBF\xE6\x80\xA7\xE5\x81\x8F\xE7\xA7\xBB"),
                juce::Rectangle<float> (br.getX() + W * (260.0f / 456.0f), br.getY() + H * (7.0f / 29.0f),
                                        W * (150.0f / 456.0f), H * (12.0f / 29.0f)),
                juce::Justification::centredLeft);

    // ---- 半透明面板（§12：alpha ~0.62/0.68 的玻璃感）----
    if (a > 0.004f)
    {
        const auto panel = panelRect();
        g.setColour (pal_.popA.withMultipliedAlpha (a));
        g.fillRoundedRectangle (panel, panel.getWidth() * (10.0f / 456.0f));
        g.setColour (pal_.accent.withAlpha (0.55f * a));
        g.drawRoundedRectangle (panel, panel.getWidth() * (10.0f / 456.0f), 1.0f);
    }
}

void LinkedDrawer::resized()
{
    layoutKnobs (true);
}
