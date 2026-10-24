#include "LinkedDrawer.h"
#include "../PluginProcessor.h"
#include "EditorLayout.h"

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
    return EditorLayout(hostBounds_).linkedPanel();
}

juce::Rectangle<float> LinkedDrawer::buttonRect() const
{
    return EditorLayout(hostBounds_).linkedButton();
}

juce::Rectangle<float> LinkedDrawer::closeRect() const
{
    const float s=EditorLayout(hostBounds_).scale;const auto r=panelRect();
    return {r.getRight()-36*s,r.getY()+8*s,26*s,26*s};
}
bool LinkedDrawer::hitTestClose(juce::Point<float> p) const
{
    return open_ && closeRect().contains(p);
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
// R36（B2）：hoverButton() 已删除 —— 底栏按钮改纯点击展开。
// R36（B3）：新增 setSticky()，由面板标题栏的「锁定」按钮驱动，带按下态。
bool LinkedDrawer::clickButton()
{
    setOpen(!open_); return true;
}

void LinkedDrawer::clickOutside()
{
    setOpen(false);
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
        { "RANGE",     -60.0,   60.0,   0.0, 1, "dB" },
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
        kb->setLinkedMark (false);                       // §5.2：⊞ 联动标识
        if (k != kLinkedMixIndex)
        {
            kb->setValuePrefix (juce::String::fromUTF8 ("\xCE\x94 "));   // §5.2：偏移读数带 Δ
            // R36（K6）：4 个偏移旋钮是**双极**量（0 = 不偏移），弧必须从 12 点钟
            //   起画。此前 setBipolar 全仓库从未被调用，于是 Δ=0 也画成半圈——
            //   看起来像"已经推了一半"，与"无偏移"的语义完全相反。
            kb->setBipolar (true);
            kb->setBipolarOrigin (0.0);
        }
        const int kk = k;
        kb->onValueChange = [this, kk] (double v) { applyOffset (kk, v); };
        addAndMakeVisible (kb);
        knobs_[(size_t) k].reset (kb);
    }
}

void LinkedDrawer::layoutKnobs (bool force)
{

    const auto panel=panelRect();const float s=EditorLayout(hostBounds_).scale,cell=(panel.getWidth()-32*s)/kLinkedKnobs;
    const int order[]={0,5,1,2,3,4};
    for(int slot=0;slot<kLinkedKnobs;++slot){auto* knob=knobs_[(size_t)order[slot]].get();if(!knob)continue;
        auto r=juce::Rectangle<float>(panel.getX()+16*s+slot*cell,panel.getY()+44*s,cell,145*s).toNearestInt();
        if(force||knob->getBounds()!=r){knob->setKnobDiameter(72*s);knob->setBounds(r);}
        knob->setAlpha(juce::jlimit(0.0f,1.0f,anim_));}

}

// R35（N4）：MIX 是抽屉里**唯一绝对值**的旋钮（其余 4 个是 Δ 偏移）。
// 宿主自动化或 BYP 宏改过 Mix 后，这把旋钮的读数会停在旧值；
// 偏移旋钮按定义不需要回读（applyOffset 现场读参数再叠加）。
void LinkedDrawer::syncMixFromParams()
{
    auto& kb = knobs_[(size_t) kLinkedMixIndex];
    if (kb == nullptr)
        return;
    const double v = getParamPhysical ("band0Mix");
    if (std::abs (v - kb->getValue()) > 1.0e-6)
        kb->setValue (v, false);      // 不回告，避免与参数互相打架
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
    if(k<0||k>=kLinkedKnobs)return;
    // ---- Mix（k=4）：**绝对值语义**（§5.2 v1.6 例外）——四段 Mix 同设为该值 ----
    if (k == kLinkedMixIndex)
    {
        const double old = getParamPhysical ("band0Mix");
        if (std::abs (newValue - old) < 1.0e-9)
            return;
        for (int b = 0; b < Param::kNumBands; ++b)
            setParamPhysical ("band" + juce::String (b) + "Mix", (float) newValue);
        if(onValuesChanged)onValuesChanged(k);
        return;
    }

    // ---- 前 4 个旋钮：Δ 偏移语义 —— 四段同名参数线性偏移 ----
    const double delta = newValue - offset_[(size_t) k];
    if (std::abs (delta) < 1.0e-9)
    {
        offset_[(size_t) k] = newValue;
        return;
    }
    const juce::String name = (k == 0) ? "Threshold"
                           : (k == 1) ? "Attack"
                           : (k == 2) ? "Release" : (k == 5) ? "Range" : "Makeup";
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const juce::String id = "band" + juce::String (b) + name;
        const float current=getParamPhysical(id);
        // Keep the un-clipped reference while turning back from a limit. A
        // manual/host change rebases just that band at the previous offset.
        if(!haveReference_[(size_t)k] || std::abs(current-lastApplied_[(size_t)k][(size_t)b])>.001f)
            reference_[(size_t)k][(size_t)b]=current-(float)offset_[(size_t)k];
        setParamPhysical(id,reference_[(size_t)k][(size_t)b]+(float)newValue);
        lastApplied_[(size_t)k][(size_t)b]=getParamPhysical(id);
    }
    offset_[(size_t)k]=newValue;haveReference_[(size_t)k]=true;
    if(onValuesChanged)onValuesChanged(k);
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

    const EditorLayout l(hostBounds_);const float s=l.scale;const auto btn=buttonRect();
    g.setColour(open_?pal_.accent.withAlpha(.15f):pal_.track);g.fillRoundedRectangle(btn,6*s);
    g.setColour(open_?pal_.accent:pal_.dim);
    const float x=btn.getX()+13*s,y=btn.getCentreY();
    g.drawRoundedRectangle({x,y-4*s,10*s,7*s},3*s,1.2f*s);g.drawRoundedRectangle({x+5*s,y-1*s,10*s,7*s},3*s,1.2f*s);
    g.setFont(MBTheme::font(MBTheme::uiMedium(),13*s));
    g.drawText("LINKED CONTROL",btn.withTrimmedLeft(36*s).withTrimmedRight(10*s),juce::Justification::centredLeft,false);
    if(anim_<.001f)return;
    const auto r=panelRect();const float a=juce::jlimit(0.0f,1.0f,anim_);
    g.setColour(pal_.popA.withMultipliedAlpha(a));g.fillRoundedRectangle(r,12*s);
    g.setColour(pal_.gridZ.withMultipliedAlpha(a));g.drawRoundedRectangle(r,12*s,s);
    g.drawLine(r.getX()+14*s,r.getY()+42*s,r.getRight()-14*s,r.getY()+42*s,s);
    g.setColour(pal_.accent.withMultipliedAlpha(a));g.setFont(MBTheme::font(MBTheme::uiMedium(),14*s));
    g.drawText("LINKED CONTROL",juce::Rectangle<float>{r.getX()+18*s,r.getY()+6*s,180*s,30*s},juce::Justification::centredLeft);
    const auto close=closeRect().reduced(8*s);g.setColour(pal_.dim.withMultipliedAlpha(a));
    g.drawLine(close.getX(),close.getY(),close.getRight(),close.getBottom(),1.5f*s);g.drawLine(close.getRight(),close.getY(),close.getX(),close.getBottom(),1.5f*s);
    g.setFont(MBTheme::font(MBTheme::uiRegular(),12*s));
    g.drawText("THRESHOLD / ATTACK / RELEASE / MAKEUP / RANGE  |  RELATIVE OFFSET",juce::Rectangle<float>{r.getX()+18*s,r.getBottom()-30*s,r.getWidth()-165*s,20*s},juce::Justification::centredLeft,false);
    g.drawText("MIX  |  ALL BANDS",juce::Rectangle<float>{r.getRight()-145*s,r.getBottom()-30*s,125*s,20*s},juce::Justification::centredRight,false);

}

void LinkedDrawer::resized()
{
    layoutKnobs (true);
}
