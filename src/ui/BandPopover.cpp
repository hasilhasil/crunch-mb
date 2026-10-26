#include "BandPopover.h"
#include "../PluginProcessor.h"
#include "EditorLayout.h"

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
    scLow_.configure("HIGH-PASS","Hz",20,2000,0,true);
    scHigh_.configure("LOW-PASS","Hz",500,20000,0,true);
    for(int i=0;i<2;++i){auto& field=i==0?scLow_:scHigh_;const juce::String suffix=i==0?"SCLow":"SCHigh";
        field.onValueChange=[this,suffix](double v){setParamPhysical("band"+juce::String(juce::jmax(0,band_))+suffix,(float)v);};
        field.onDragStart=[this,suffix]{if(auto* p=proc_.apvts.getParameter("band"+juce::String(juce::jmax(0,band_))+suffix))p->beginChangeGesture();};
        field.onDragEnd=[this,suffix]{if(auto* p=proc_.apvts.getParameter("band"+juce::String(juce::jmax(0,band_))+suffix))p->endChangeGesture();};}

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
    scLow_.setPalette(pal_);scHigh_.setPalette(pal_);
    for (auto& kb : knobs_)
        if (kb != nullptr) kb->setPalette (pal_);
    repaint();
}

void BandPopover::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    if (expertLayer_ != nullptr)
        expertLayer_->setBounds (getLocalBounds());
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

// R36（B6）：该段的真实频率范围（Hz）。由 DisplayView 在分频点变化时推送。
// 顶部 "LOW 20 - 200" 一类文本必须在拖动分频点时实时更新——
// 原先这里塞的是像素坐标（还只在打开面板时算一次），数字既不真实也不刷新。
void BandPopover::setBandFreqRange (float hzLow, float hzHigh)
{
    if (std::abs (hzLow - bandHzLow_) < 1.0e-3f && std::abs (hzHigh - bandHzHigh_) < 1.0e-3f
        && freqText_.isNotEmpty())
        return;
    bandHzLow_  = hzLow;
    bandHzHigh_ = hzHigh;
    freqText_   = fmtHzShort (hzLow) + " - " + fmtHzShort (hzHigh);
    repaint();
}

// ---------------- 几何 ----------------
juce::Rectangle<float> BandPopover::popoverRect() const
{

    const EditorLayout l(hostBounds_);const float s=l.scale;
    const float w=juce::jmin(760*s,l.plotRight()-24*s),h=(expertOpen_?430.0f:280.0f)*s;
    const float x=juce::jlimit(12*s,juce::jmax(12*s,l.plotRight()-w-12*s),(bandLeftX_+bandRightX_)*.5f-w*.5f);
    return {x,l.globalTop()-h-48*s,w,h};

}

juce::Rectangle<float> BandPopover::popKnobRect (int k) const
{

    const auto r=popoverRect();const float s=EditorLayout(hostBounds_).scale,cell=(r.getWidth()-58*s)/kPopKnobs;
    return {r.getX()+16*s+k*cell,r.getY()+36*s,cell,137*s};

}

juce::Rectangle<float> BandPopover::popSliderRect (int k) const
{

    const auto r=popoverRect();const float s=EditorLayout(hostBounds_).scale;
    const float leftW=(r.getWidth()-54*s)/2.15f;
    const float rightX=r.getX()+leftW+38*s;
    if(k==1)return {r.getX()+16*s,r.getY()+222*s,leftW,28*s};
    return {rightX,r.getY()+(k==0?180:222)*s,r.getRight()-rightX-16*s,28*s};

}

juce::Rectangle<float> BandPopover::popSliderTrackRect (int k) const
{
    const auto row = popSliderRect (k);
    const float s = EditorLayout (hostBounds_).scale;
    return { row.getX() + 50*s, row.getCentreY() - 2*s,
             juce::jmax (1.0f, row.getWidth() - 118*s), 4*s };
}

juce::Rectangle<float> BandPopover::expertRect() const
{

    const auto r=popoverRect();const float s=EditorLayout(hostBounds_).scale;
    return {r.getRight()-30*s,r.getY()+46*s,22*s,122*s};

}

juce::Rectangle<float> BandPopover::expertContentRect() const
{

    const auto r=popoverRect();const float s=EditorLayout(hostBounds_).scale;
    return {r.getX()+16*s,r.getY()+272*s,r.getWidth()-32*s,142*s};

}

// ---------------- Phase C-6：Dynamics 分段按钮 ----------------
// Pro-MB 的 segmented control：两段等宽，选中段用 accent 底 + 白字，
// 未选中段透明底 + dim 字。点击翻转 Compress/Expand。
juce::Rectangle<float> BandPopover::dynamicsSegRect() const
{

    const auto r=popoverRect();const float s=EditorLayout(hostBounds_).scale;
    return {r.getX()+16*s,r.getY()+180*s,(r.getWidth()-54*s)/2.15f,28*s};

}

bool BandPopover::hitTestDynamicsSeg (juce::Point<float> p) const
{
    return open_ && dynamicsSegRect().contains (p);
}

bool BandPopover::clickDynamicsSeg(bool expand)
{
    dynamicsExpand_ = expand;
    const int b = (band_ >= 0) ? band_ : 0;
    setParamNorm ("band" + juce::String (b) + "Dynamics", dynamicsExpand_ ? 1.0f : 0.0f);
    repaint();
    return true;
}

juce::Rectangle<float> BandPopover::expertOptionRect(ExpertRow row,int option) const
{
    const auto box=expertContentRect();const float s=EditorLayout(hostBounds_).scale;
    const int index=(int)row-1,count=row==ExpertRow::Lookahead?3:2;
    const float left=box.getX()+145*s,cell=(box.getWidth()*.56f-151*s)/count;
    return {left+option*cell,box.getY()+12*s+index*40*s,cell-4*s,30*s};
}
BandPopover::ExpertRow BandPopover::hitTestExpertRow(juce::Point<float> p) const
{
    if(!open_||!expertOpen_)return ExpertRow::None;
    for(auto row:{ExpertRow::Detector,ExpertRow::Lookahead,ExpertRow::Filtering})
        for(int i=0;i<(row==ExpertRow::Lookahead?3:2);++i)if(expertOptionRect(row,i).contains(p))return row;
    return ExpertRow::None;
}
void BandPopover::selectExpertOption(ExpertRow row,int option)
{
    const auto prefix="band"+juce::String(juce::jmax(0,band_));
    if(row==ExpertRow::Detector)applySlider(3,juce::jlimit(0,1,option));
    else if(row==ExpertRow::Lookahead){const int index=juce::jlimit(0,2,option);setParamPhysical("lookahead",(float)index);sliders_[4]=index*.5f;}
    else if(row==ExpertRow::Filtering)setParamPhysical(prefix+"SCFilter",(float)juce::jlimit(0,1,option));
    if(expertLayer_)expertLayer_->repaint();repaint();
}
bool BandPopover::selectExpertAt(juce::Point<float> p)
{
    if(!open_||!expertOpen_)return false;
    for(auto row:{ExpertRow::Detector,ExpertRow::Lookahead,ExpertRow::Filtering})
        for(int i=0;i<(row==ExpertRow::Lookahead?3:2);++i)if(expertOptionRect(row,i).contains(p)){selectExpertOption(row,i);return true;}
    return false;
}
// Kept for existing callers; continuous values are edited by ValueField.
void BandPopover::clickExpertRow(ExpertRow row)
{
    if(row==ExpertRow::Detector)selectExpertOption(row,sliders_[3]>.5?0:1);
    if(row==ExpertRow::Lookahead)selectExpertOption(row,((int)std::lround(sliders_[4]*2)+1)%3);
    if(row==ExpertRow::Filtering)selectExpertOption(row,getParamNorm("band"+juce::String(juce::jmax(0,band_))+"SCFilter")>.5f?0:1);
}
void BandPopover::layoutExpertFields()
{
    const auto box=expertContentRect();const float s=EditorLayout(hostBounds_).scale;
    const float left=box.getX()+box.getWidth()*.58f;
    for(int i=0;i<2;++i){auto& field=i==0?scLow_:scHigh_;field.setScale(s);
        field.setBounds(juce::Rectangle<float>(left,box.getY()+(32+48*i)*s,box.getRight()-left-12*s,38*s).toNearestInt());
        field.setVisible(open_&&expertOpen_);}
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
    if (expertLayer_ != nullptr && expertOpen_)
        expertLayer_->repaint();       // R35（C-b）：覆盖层随动画重绘
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
    // ⚠️ R36：这里的范围/中心必须与 Parameters.cpp 的声明**逐字一致**，
    //   否则旋钮位置与宿主自动化车道的位置会互相矛盾（见 Knob::setSkew 注释）。
    struct Cfg { double mn, mx, iv, def, skewCentre; int dec; const char* suf; const char* label; bool bipolar; };
    const Cfg cfgs[kPopKnobs] = {
        { -60.0,    0.0, 0.1, -20.0,   0.0, 1, "dB", "THRESHOLD", false },
        { -30.0,   30.0, 0.1,  -6.0,   0.0, 1, "dB", "RANGE",     true  },
        {   0.1,  200.0, 0.1,  10.0,  10.0, 1, "ms", "ATTACK",    false },
        {  10.0, 1000.0, 0.5, 100.0, 100.0, 1, "ms", "RELEASE",   false },
        { -36.0,   36.0, 0.1,   0.0,   0.0, 1, "dB", "OUTPUT",    true  },   // B5：= Makeup
        {   0.0,  100.0, 1.0, 100.0,   0.0, 0, "%",  "MIX",       false },
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
        // R36（K7）：Attack / Release 参数用的是 setSkewForCentre，
        //   旋钮必须用同一个 centre 复算刻度，否则默认值会落在行程 5% 处。
        if (cfgs[k].skewCentre > 0.0)
            kb->setSkew (cfgs[k].skewCentre);
        // R36（K6）：双极弧（Range / Output）从 0 起画，不是从最小值起画。
        if (cfgs[k].bipolar)
        {
            kb->setBipolar (true);
            kb->setBipolarOrigin (0.0);
        }
        const int kk = k;
        kb->onValueChange = [this, kk] (double v) { applyPopoverKnob (kk, v); };
        kb->onDragStart = [this, kk]
        {
            static const char* kIds[kPopKnobs] = { "Threshold", "Range", "Attack", "Release", "Makeup", "Mix" };
            const int b = (band_ >= 0) ? band_ : 0;
            if (auto* p = proc_.apvts.getParameter ("band" + juce::String (b) + kIds[juce::jlimit (0, kPopKnobs - 1, kk)]))
                p->beginChangeGesture();
        };
        kb->onDragEnd = [this, kk]
        {
            static const char* kIds[kPopKnobs] = { "Threshold", "Range", "Attack", "Release", "Makeup", "Mix" };
            const int b = (band_ >= 0) ? band_ : 0;
            if (auto* p = proc_.apvts.getParameter ("band" + juce::String (b) + kIds[juce::jlimit (0, kPopKnobs - 1, kk)]))
                p->endChangeGesture();
        };
        addAndMakeVisible (kb);
        kb->setVisible (false);
        knobs_[(size_t) k].reset (kb);
    }
    knobsReady_ = true;

    // R35（C-b）：EXPERT 覆盖层必须在旋钮**之后**创建/置顶，
    // 否则它会被 5 个 Knob 盖住（子组件按 z 序合成）。
    if (expertLayer_ == nullptr)
    {
        expertLayer_ = std::make_unique<ExpertLayer> (*this);
        addAndMakeVisible (*expertLayer_);
        expertLayer_->addChildComponent(scLow_);expertLayer_->addChildComponent(scHigh_);
        scLow_.setPalette(pal_);scHigh_.setPalette(pal_);
    }
    expertLayer_->setBounds (getLocalBounds());
    expertLayer_->toFront (false);
}

void BandPopover::layoutKnobs (bool force)
{
    layoutExpertFields();
    if (! knobsReady_)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    for (int k = 0; k < kPopKnobs; ++k)
    {
        if (knobs_[(size_t) k] == nullptr)
            continue;
        const auto rc = popKnobRect (k);
        auto* kb = knobs_[(size_t) k].get();
        if (kb == nullptr)
            continue;
        // R35（D）：旋钮直径随面板高度缩放（原先写死 38px，窗口放大后
        // 旋钮在一大片空槽里显得很小）。上下限防止极端尺寸下失真。
        // Knob 没有 diameter getter，用成员缓存，避免每帧 setKnobDiameter
        // （它会 invalidateBackLayer + repaint，代价不小）。
        const float dia = 72.0f * EditorLayout(hostBounds_).scale;
        if (std::abs (knobDiameters_[(size_t) k] - dia) > 0.5f)
        {
            knobDiameters_[(size_t) k] = dia;
            kb->setKnobDiameter (dia);
        }
        // R34（A）：去掉纵向滑入，只保留淡入。
        // 原先这里是 `rc.getY() + (1.0f - a) * rc.getHeight()`——把旋钮下移最多
        // 一整个旋钮行高（默认 74px），而 paint() 只对**面板**做 alpha 淡入、
        // 不做位移，于是动画期间旋钮跑到面板下缘之外滑上来，
        // 用户实测"弹出二级菜单时里面的旋钮跟菜单窗口不同步"。
        // 现在位置恒等于最终槽位，透明度跟随面板 anim_ 一起淡入淡出。
        const int nx = (int) rc.getX(), ny = (int) rc.getY();
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
        "Threshold", "Range", "Attack", "Release", "Makeup", "Mix"   // B5：Makeup = OUTPUT
    };
    setParamPhysical ("band" + juce::String (b) + ids[juce::jlimit (0, kPopKnobs - 1, k)], (float) v);
    syncFromParams();
}

// ---------------- 滑杆 ----------------
void BandPopover::beginSliderDrag (int k, float mouseX)
{
    if (sliderDrag_ >= 0)
        endSliderDrag();
    sliderDrag_ = k;
    sliderDragX0_ = mouseX;
    // Ratio is stored as a physical 1..20 value; the drag baseline must be 0..1.
    // Using the raw ratio here clamped every drag to 20:1, including left drags.
    const double value = sliders_[(size_t) juce::jlimit (0, 4, k)];
    sliderDragNorm0_ = k == 0 ? (value - 1.0) / 19.0 : value;
    if (k == 0 || k == 1)
    {
        const int b = (band_ >= 0) ? band_ : 0;
        const juce::String id = (k == 0) ? Param::bandRatio (b) : Param::bandKnee (b);
        if (auto* p = proc_.apvts.getParameter (id))
            p->beginChangeGesture();
    }
}

void BandPopover::dragSlider (int k, float mouseX)
{
    if (sliderDrag_ != k)
        return;
    const float w = popSliderTrackRect (k).getWidth();
    float frac = (float) sliderDragNorm0_ + (mouseX - sliderDragX0_) / w;
    frac = juce::jlimit (0.0f, 1.0f, frac);

    if (k == 0)
        applySlider (0, 1.0 + (double) frac * 19.0);      // ratio 1..20
    else
        applySlider (k, (double) frac);                    // knee 0..48 dB（归一化）
}

void BandPopover::endSliderDrag()
{
    if (sliderDrag_ == 0 || sliderDrag_ == 1)
    {
        const int b = (band_ >= 0) ? band_ : 0;
        const juce::String id = (sliderDrag_ == 0) ? Param::bandRatio (b) : Param::bandKnee (b);
        if (auto* p = proc_.apvts.getParameter (id))
            p->endChangeGesture();
    }
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
    scLow_.setValue(getParamPhysical("band"+juce::String(b)+"SCLow"));
    scHigh_.setValue(getParamPhysical("band"+juce::String(b)+"SCHigh"));
    static const juce::String ids[kPopKnobs] = {
        "Threshold", "Range", "Attack", "Release", "Makeup", "Mix"   // B5：Makeup = OUTPUT
    };
    for (int k = 0; k < kPopKnobs; ++k)
    {
        const juce::String id = "band" + juce::String (b) + ids[k];
        values_[(size_t) k] = getParamPhysical (id);
        // R36（K7）：归一化必须走**参数自己的** NormalisableRange，
        //   不能按 min/max 线性除——Attack/Release 是对数刻度。
        normValues_[(size_t) k] = juce::jlimit (0.0f, 1.0f, getParamNorm (id));
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

// R35（N4）：按 parameterID 精确刷新。
// 返回 true 表示这个 id 属于本 popover（无论是否被 band_ 过滤），
// 让调用方知道"已处理"，避免再做无谓的全量同步。
//
// 为什么要按 id 而不是每帧全量：宿主一次自动化 sweep 会连发几十个回调，
// 全量 syncFromParams() 每次都读 9 个参数 + 5 次 setValue（会 repaint）。
// 按 id 过滤后只有真正受影响的控件会动。
bool BandPopover::syncParam (const juce::String& paramId)
{
    if (band_ < 0)
        return false;
    const juce::String prefix = "band" + juce::String (band_);
    if (! paramId.startsWith (prefix))
    {
        // 全局的 lookahead 也属于本面板（EXPERT 行 + sliders_[4]）
        if(paramId==Param::ID::lookahead){sliders_[4]=getParamPhysical(paramId)*.5f;return true;}
        return false;
    }

    const juce::String suffix = paramId.substring (prefix.length());

    static const char* kKnobSuffix[kPopKnobs] = {
        "Threshold", "Range", "Attack", "Release", "Makeup", "Mix"   // B5：Makeup = OUTPUT
    };
    for (int k = 0; k < kPopKnobs; ++k)
    {
        if (suffix != kKnobSuffix[k])
            continue;
        const double v = getParamPhysical (paramId);
        values_[(size_t) k] = v;
        // R36（K7）：归一化走参数自己的 NormalisableRange（对数刻度别再线性除）
        normValues_[(size_t) k] = juce::jlimit (0.0f, 1.0f, getParamNorm (paramId));
        // ⚠️ sendNotification=false：这是"参数 → 控件"的回灌，
        //    再触发 onValueChange 会把值又写回参数（无限回环 + 与宿主打架）。
        if (knobs_[(size_t) k] != nullptr)
            knobs_[(size_t) k]->setValue (v, false);
        return true;
    }

    if (suffix == "Ratio")   { sliders_[0] = getParamPhysical (paramId);          return true; }
    if (suffix == "Knee")    { sliders_[1] = getParamNorm (paramId);              return true; }
    if (suffix == "Ext")     { sliders_[2] = getParamNorm (paramId);              return true; }
    if (suffix == "Detector"){ sliders_[3] = getParamNorm (paramId);              return true; }
    if (suffix == "Dynamics")
    {
        dynamicsExpand_ = getParamNorm (paramId) > 0.5f;
        return true;
    }
    if(suffix=="SCLow"){scLow_.setValue(getParamPhysical(paramId));return true;}
    if(suffix=="SCHigh"){scHigh_.setValue(getParamPhysical(paramId));return true;}
    if(suffix=="SCFilter")return true;        // EXPERT 行直接读参数绘制，无需缓存

    return false;
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
    return open_ && closeRect().contains(p);
}

juce::Rectangle<float> BandPopover::closeRect() const
{
    const auto r = popoverRect();
    const float s = 26.0f * EditorLayout(hostBounds_).scale;
    return {r.getRight() - s - 12.0f * EditorLayout(hostBounds_).scale,
            r.getY() + 5.0f * EditorLayout(hostBounds_).scale, s, s};
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
        const float s=EditorLayout(hostBounds_).scale;
        const juce::Rectangle<float> bolt(r.getX(),r.getY(),8*s,0);
        static const char* kNames[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };
        g.setColour (pal_.text);
        g.setFont (MBTheme::font (MBTheme::uiMedium(), 12.0f * EditorLayout(hostBounds_).scale));
        // R36（B6）：频率范围用 setBandFreqRange 推下来的真实 Hz 文本，
        //   不再把像素坐标当 Hz 打印、也不再只在打开时算一次。
        g.drawText (juce::String (kNames[juce::jlimit (0, 3, band_)]) + "  "
                    + (freqText_.isNotEmpty() ? freqText_
                                              : fmtHzShort (bandHzLow_) + " - " + fmtHzShort (bandHzHigh_)),
                    juce::Rectangle<float> (bolt.getRight() + r.getWidth() * (8.0f / 472.0f),
                                            r.getY() + 7.0f * EditorLayout(hostBounds_).scale,
                                            r.getWidth() * (300.0f / 472.0f),
                                            24.0f * EditorLayout(hostBounds_).scale),
                    juce::Justification::centredLeft);

        const auto closeBtn = closeRect();
        const float cs = closeBtn.getWidth();
        g.setColour (pal_.track);
        g.fillRoundedRectangle (closeBtn, r.getWidth() * (6.0f / 472.0f));
        // R36：原来这里画的是 "✕"（U+2715）——Poppins 里没有这个字形，
        //   实际渲染成两个方框（快照 03_popover_2x 右上也看到过同样的问题）。
        //   改成两条线画出来的叉，字体缺字也不影响。
        {
            const float cx2 = closeBtn.getCentreX(), cy2 = closeBtn.getCentreY();
            const float rr  = cs * 0.24f;
            g.setColour (pal_.text);
            g.drawLine (cx2 - rr, cy2 - rr, cx2 + rr, cy2 + rr, juce::jmax (1.2f, cs * 0.09f));
            g.drawLine (cx2 + rr, cy2 - rr, cx2 - rr, cy2 + rr, juce::jmax (1.2f, cs * 0.09f));
        }

        // 分隔线
        g.setColour (pal_.grid);
        g.fillRect (r.getX() + r.getWidth() * (10.0f / 472.0f),
                    r.getY() + 36.0f * EditorLayout(hostBounds_).scale,
                    r.getWidth() - r.getWidth() * (20.0f / 472.0f), 1.0f);
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
            g.setFont (MBTheme::font (on ? MBTheme::uiMedium() : MBTheme::uiRegular(), 12.0f * EditorLayout(hostBounds_).scale));
            g.drawText (labels[i], cell, juce::Justification::centred);
        }
        // 外框（分段控件的容器感）
        g.setColour (pal_.gridZ.withMultipliedAlpha (a));
        g.drawRoundedRectangle (seg, seg.getHeight() * 0.28f, 1.0f);
    }

    // Approved two-row footer: Compress/Expand + Ratio, Knee + Sidechain.
    const float scale=EditorLayout(hostBounds_).scale;
    for(int k=0;k<3;++k) {
        const auto row=popSliderRect(k);
        const bool hot=(sliderDrag_==k)||row.contains(lastMousePos_);
        g.setFont(MBTheme::font(MBTheme::uiMedium(),12*scale));
        g.setColour(pal_.dim.withMultipliedAlpha(a));
        g.drawText(k==0?"RATIO":k==1?"KNEE":"SIDECHAIN",row.withWidth((k==2?80:46)*scale),juce::Justification::centredLeft);
        if(k==2) {
            const float x=row.getX()+90*scale,w=(row.getWidth()-90*scale)*.5f;
            for(int j=0;j<2;++j) {
                const auto cell=juce::Rectangle<float>(x+j*w,row.getY(),w-4*scale,row.getHeight());
                const bool on=(sliders_[2]>.5)==(j==1);
                g.setColour((on?pal_.accent.withAlpha(.2f):pal_.track).withMultipliedAlpha(a));
                g.fillRoundedRectangle(cell,5*scale);
                g.setColour((on?pal_.accent:pal_.dim).withMultipliedAlpha(a));
                g.drawText(j==0?"INT":"EXT",cell,juce::Justification::centred);
            }
            continue;
        }
        const float fraction=juce::jlimit(0.0f,1.0f,k==0?float((sliders_[0]-1)/19):float(sliders_[1]));
        const auto track=popSliderTrackRect(k);
        g.setColour(pal_.track.withMultipliedAlpha(a));g.fillRoundedRectangle(track,2*scale);
        g.setColour(pal_.accent.withMultipliedAlpha(a));g.fillRoundedRectangle(track.withWidth(juce::jmax(4*scale,track.getWidth()*fraction)),2*scale);
        g.setColour((hot?juce::Colours::white:pal_.knob).withMultipliedAlpha(a));
        g.fillEllipse(track.getX()+track.getWidth()*fraction-5*scale,row.getCentreY()-5*scale,10*scale,10*scale);
        const auto value=k==0?juce::String(sliders_[0],1)+" : 1":juce::String(sliders_[1]*48,1)+" dB";
        g.setFont(MBTheme::font(MBTheme::regular(),12*scale));g.setColour(pal_.text.withMultipliedAlpha(a));
        g.drawText(value,row.withLeft(row.getRight()-64*scale),juce::Justification::centredRight);
    }

    // ---- EXPERT 右缘竖排标签（Phase C-5，Pro-MB 对标）----
    const auto er = expertRect();
    {
        // 竖排底带：贴右缘的一条窄带，比面板底色略亮以区分
        g.setColour (pal_.track.withAlpha (0.75f * a));
        g.fillRoundedRectangle (er, er.getWidth() * 0.3f);
        g.setColour (expertOpen_ ? pal_.accent.withAlpha (0.9f * a)
                                 : pal_.dim.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (expertOpen_ ? MBTheme::uiMedium() : MBTheme::uiRegular(), 12.0f * EditorLayout(hostBounds_).scale));

        // R36（B4）：原来逐字符往下排（"E X P E R T"竖着一列），观感很差。
        //   改为把「EXPERT ▸」整体旋转 −90°，沿右缘竖着读。
        const juce::String txt = expertOpen_ ? juce::String::fromUTF8 ("EXPERT \xE2\x96\xBC")
                                             : juce::String::fromUTF8 ("EXPERT \xE2\x96\xB6");
        g.saveState();
        const float ecx = er.getCentreX(), ecy = er.getCentreY();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, ecx, ecy));
        g.drawText (txt,
                    juce::Rectangle<float> (ecx - er.getHeight() * 0.5f,
                                            ecy - er.getWidth() * 0.5f,
                                            er.getHeight(), er.getWidth()),
                    juce::Justification::centred);
        g.restoreState();
    }

    // ---- EXPERT 展开内容已移到 ExpertLayer 子组件（R35/C-b）----
    // 原因：子组件永远画在父 paint() 之后，EXPERT 框若留在这里会被
    // 5 个 Knob 盖住右半（旋钮 3/4 压在 SC HIGH-PASS / SC LOW-PASS 数值列上）。
}

// R35（C-b）：EXPERT 展开层——由 ExpertLayer 子组件调用，合成在旋钮之上。
void BandPopover::paintExpertLayer(juce::Graphics& g)
{
    if(!open_||!expertOpen_||anim_<.004f)return;
    const auto box=expertContentRect();const float s=EditorLayout(hostBounds_).scale;
    g.setColour(pal_.surface.withAlpha(.80f*anim_));g.fillRoundedRectangle(box,7*s);
    g.setColour(pal_.gridZ);g.drawRoundedRectangle(box,7*s,s);
    const char* titles[]={"DETECTOR","LOOKAHEAD","SC FILTERING"};
    const char* options[3][3]={{"Peak","RMS",""},{"Off","1 ms","3 ms"},{"Band","Free",""}};
    const int selected[]={sliders_[3]>.5?1:0,(int)std::lround(sliders_[4]*2),getParamNorm("band"+juce::String(juce::jmax(0,band_))+"SCFilter")>.5f?1:0};
    for(int row=0;row<3;++row){g.setFont(MBTheme::font(MBTheme::uiMedium(),13*s));g.setColour(pal_.dim);
        g.drawText(titles[row],juce::Rectangle<float>(box.getX()+12*s,box.getY()+(12+row*40)*s,130*s,30*s),juce::Justification::centredLeft);
        for(int i=0;i<(row==1?3:2);++i){const auto r=expertOptionRect((ExpertRow)(row+1),i);const bool on=selected[row]==i;
            g.setColour(on?pal_.accent.withAlpha(.2f):pal_.track);g.fillRoundedRectangle(r,4*s);
            g.setColour(on?pal_.accent:pal_.gridZ);g.drawRoundedRectangle(r,4*s,s);
            g.setColour(on?pal_.text:pal_.dim);g.setFont(MBTheme::font(MBTheme::uiRegular(),14*s));g.drawText(options[row][i],r,juce::Justification::centred,false);}}
    const float left=box.getX()+box.getWidth()*.58f;
    g.setColour(pal_.dim);g.setFont(MBTheme::font(MBTheme::uiMedium(),12*s));
    g.drawText("FREQUENCY (FREE MODE)",juce::Rectangle<float>(left,box.getY()+8*s,box.getRight()-left-12*s,22*s),juce::Justification::centredLeft);
}

void BandPopover::resized()
{
    if (expertLayer_ != nullptr)
        expertLayer_->setBounds (getLocalBounds());
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

