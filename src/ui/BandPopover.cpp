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
    if (expertLayer_ != nullptr)
        expertLayer_->repaint();
    repaint();
}

void BandPopover::applyUiSnapshot (const UiPack::UiSnapshot& snap)
{
    pal_          = snap.palette;
    themeTokens_  = snap.theme;
    layoutTokens_ = snap.layout;
    locale_       = snap.locale;
    scLow_.setPalette (pal_);
    scHigh_.setPalette (pal_);
    scLow_.setThemeTokens (themeTokens_);
    scHigh_.setThemeTokens (themeTokens_);
    scLow_.setLabel (tr ("pop.highPass"));
    scHigh_.setLabel (tr ("pop.lowPass"));

    static const char* kKeys[kPopKnobs] = {
        "knob.threshold", "knob.range", "knob.attack", "knob.release", "knob.output", "knob.mix"
    };
    for (int k = 0; k < kPopKnobs; ++k)
    {
        if (auto& kb = knobs_[(size_t) k])
        {
            kb->setPalette (pal_);
            kb->setThemeTokens (themeTokens_);
            kb->setLabel (tr (kKeys[k]));
        }
    }
    if (expertLayer_ != nullptr)
        expertLayer_->repaint();
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
    if (editingSlider_ >= 0)
        closeSliderEdit (false);
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
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    const bool vis = expertOpen_ && (open_ || a >= 0.004f);
    for(int i=0;i<2;++i){auto& field=i==0?scLow_:scHigh_;field.setScale(s);
        field.setBounds(juce::Rectangle<float>(left,box.getY()+(32+48*i)*s,box.getRight()-left-12*s,38*s).toNearestInt());
        field.setAlpha (a);
        field.setVisible (vis);}
}

// ---------------- 开合 / 动画 ----------------
void BandPopover::setOpen (bool open)
{
    if (open == open_)
        return;
    if (! open && editingSlider_ >= 0)
        closeSliderEdit (true);
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
    if (expertLayer_ != nullptr)
        expertLayer_->repaint();
    repaint();
}

bool BandPopover::advance (double nowMs)
{
    const float prevAnim = anim_;
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
    if (std::abs (anim_ - prevAnim) > 1.0e-5f)
    {
        if (expertLayer_ != nullptr)
            expertLayer_->repaint();
        repaint();
    }
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
        static const char* kLabelKeys[kPopKnobs] = {
            "knob.threshold", "knob.range", "knob.attack", "knob.release", "knob.output", "knob.mix"
        };
        kb->setPalette (pal_);
        kb->setThemeTokens (themeTokens_);
        kb->setLabel (tr (kLabelKeys[k]));
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

bool BandPopover::openSliderEdit (int k)
{
    if (! open_ || (k != 0 && k != 1))
        return false;
    endSliderDrag();
    editingSlider_      = k;
    sliderEditText_.clear();
    sliderEditCursorOn_ = true;
    sliderLastBlinkMs_  = juce::Time::getMillisecondCounter();
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    repaint();
    return true;
}

void BandPopover::closeSliderEdit (bool commit)
{
    if (editingSlider_ < 0)
        return;
    const int k = editingSlider_;
    if (commit)
    {
        juce::String t = sliderEditText_.trim().toLowerCase();
        if (k == 0 && t.containsChar (':'))
            t = t.upToFirstOccurrenceOf (":", false, false).trim();
        if (t.endsWith ("db"))
            t = t.dropLastCharacters (2).trim();

        if (t.isNotEmpty())
        {
            const auto bytes = t.toStdString();
            char* endPtr = nullptr;
            const double parsed = std::strtod (bytes.c_str(), &endPtr);
            if (endPtr == bytes.c_str() + bytes.size() && std::isfinite (parsed))
            {
                const int b = juce::jmax (0, band_);
                if (k == 0)
                {
                    const float ratio = juce::jlimit (1.0f, 20.0f, (float) parsed);
                    if (auto* p = proc_.apvts.getParameter (Param::bandRatio (b))) p->beginChangeGesture();
                    applySlider (0, (double) ratio);
                    if (auto* p = proc_.apvts.getParameter (Param::bandRatio (b))) p->endChangeGesture();
                }
                else if (k == 1)
                {
                    const float kneeDb = juce::jlimit (0.0f, 48.0f, (float) parsed);
                    const juce::String kid = Param::bandKnee (b);
                    if (auto* p = proc_.apvts.getParameter (kid)) p->beginChangeGesture();
                    applySlider (1, (double) (kneeDb / 48.0f));
                    if (auto* p = proc_.apvts.getParameter (kid)) p->endChangeGesture();
                }
            }
        }
    }
    editingSlider_ = -1;
    sliderEditText_.clear();
    repaint();
}

bool BandPopover::handleSliderEditKey (const juce::KeyPress& key)
{
    if (editingSlider_ < 0)
        return false;
    if (key == juce::KeyPress::escapeKey) { closeSliderEdit (false); return true; }
    if (key == juce::KeyPress::returnKey) { closeSliderEdit (true);  return true; }
    if (key == juce::KeyPress::backspaceKey)
    {
        sliderEditText_ = sliderEditText_.substring (0, juce::jmax (0, sliderEditText_.length() - 1));
        repaint();
        return true;
    }
    const juce::juce_wchar ch = key.getTextCharacter();
    const juce::juce_wchar c  = (ch != 0) ? ch : (juce::juce_wchar) key.getKeyCode();
    if ((c >= '0' && c <= '9') || c == '.' || (editingSlider_ == 0 && c == ':'))
    {
        if (sliderEditText_.length() < 8)
            sliderEditText_ += juce::String::charToString (c);
        repaint();
        return true;
    }
    return false;
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

    if (editingSlider_ >= 0)
    {
        if (sliderLastBlinkMs_ == 0)
            sliderLastBlinkMs_ = nowMs;
        if ((int) (nowMs - sliderLastBlinkMs_) >= (int) Anim::kBlinkMs)
        {
            sliderEditCursorOn_ = ! sliderEditCursorOn_;
            sliderLastBlinkMs_  = nowMs;
            repaint();
        }
    }
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
    if (! open_ && anim_ < 0.004f)
        return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    const auto r = popoverRect();
    const float scale = EditorLayout (hostBounds_, layoutTokens_).scale;

    if (themeTokens_.isNeumorphic)
    {
        // 新拟物浮层面板：8px/16px 双重阴影 + rounded-2xl + border-0
        UiPack::NeuPainter::drawRaisedRect (g, r, themeTokens_.radiusLg * scale,
                                            pal_, themeTokens_.shadowCard, scale, a, false);
    }
    else
    {
        const float corner = r.getWidth() * (12.0f / 472.0f);
        struct Layer { float dy; float alpha; float expand; };
        const Layer layers[3] = {
            { 2.0f,  0.30f, 0.0f  },
            { 6.0f,  0.20f, 3.0f  },
            { 14.0f, 0.14f, 7.0f  },
        };
        for (const auto& L : layers)
        {
            const float k = r.getHeight() / 200.0f;
            auto shadow = r.translated (0.0f, L.dy * k).expanded (L.expand * k);
            g.setColour (pal_.halo.withMultipliedAlpha (L.alpha * 1.4f * a));
            g.fillRoundedRectangle (shadow, corner + L.expand * k);
        }

        g.setColour (pal_.popA.withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, corner);
        g.setColour (pal_.accent.withAlpha (0.5f * a));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    // ---- 顶栏：段名 + 频率范围 + ✕ ----
    {
        const float s = scale;
        const juce::Rectangle<float> bolt(r.getX(),r.getY(),8*s,0);
        static const char* kNameKeys[4] = { "band.low", "band.lowMid", "band.highMid", "band.high" };
        g.setColour (pal_.text.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (MBTheme::uiMedium(), 12.0f * s));
        g.drawText (tr (kNameKeys[juce::jlimit (0, 3, band_)]) + "  "
                    + (freqText_.isNotEmpty() ? freqText_
                                              : fmtHzShort (bandHzLow_) + " - " + fmtHzShort (bandHzHigh_)),
                    juce::Rectangle<float> (bolt.getRight() + r.getWidth() * (8.0f / 472.0f),
                                            r.getY() + 7.0f * s,
                                            r.getWidth() * (300.0f / 472.0f),
                                            24.0f * s),
                    juce::Justification::centredLeft);

        const auto closeBtn = closeRect();
        const float cs = closeBtn.getWidth();
        UiPack::NeuPainter::drawButton (g, closeBtn, pal_, themeTokens_, false,
                                        closeBtn.contains (lastMousePos_), s, a,
                                        r.getWidth() * (6.0f / 472.0f));
        {
            const float cx2 = closeBtn.getCentreX(), cy2 = closeBtn.getCentreY();
            const float rr  = cs * 0.24f;
            g.setColour (pal_.text.withMultipliedAlpha (a));
            g.drawLine (cx2 - rr, cy2 - rr, cx2 + rr, cy2 + rr, juce::jmax (1.2f, cs * 0.09f));
            g.drawLine (cx2 + rr, cy2 - rr, cx2 - rr, cy2 + rr, juce::jmax (1.2f, cs * 0.09f));
        }

        // 分隔线
        g.setColour (pal_.grid.withMultipliedAlpha (a));
        g.fillRect (r.getX() + r.getWidth() * (10.0f / 472.0f),
                    r.getY() + 36.0f * s,
                    r.getWidth() - r.getWidth() * (20.0f / 472.0f), 1.0f);
    }

    // ---- Phase C-6：Dynamics 分段按钮（Compress / Expand）----
    {
        const auto seg = dynamicsSegRect();
        const float halfW = seg.getWidth() * 0.5f;
        const juce::String labels[2] = { tr ("pop.compress"), tr ("pop.expand") };
        if (themeTokens_.isNeumorphic)
        {
            UiPack::NeuPainter::drawInsetRect (g, seg, seg.getHeight() * 0.35f, pal_,
                                               themeTokens_.insetButton, scale, a, true);
            for (int i = 0; i < 2; ++i)
            {
                const bool on = ((i == 1) == dynamicsExpand_);
                auto cell = juce::Rectangle<float> (seg.getX() + halfW * (float) i, seg.getY(),
                                                    halfW, seg.getHeight()).reduced (2.0f * scale);
                if (on)
                {
                    UiPack::NeuPainter::drawRaisedRect (g, cell, cell.getHeight() * 0.32f,
                                                        pal_, themeTokens_.shadowBadge, scale, a, true);
                    g.setColour (pal_.onAccent.withAlpha (0.96f * a));
                }
                else
                {
                    g.setColour (pal_.dim.withMultipliedAlpha (a));
                }
                g.setFont (MBTheme::font (on ? MBTheme::uiMedium() : MBTheme::uiRegular(), 12.0f * scale));
                g.drawText (labels[i], cell, juce::Justification::centred);
            }
        }
        else
        {
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
                    g.setColour (pal_.onAccent.withAlpha (0.95f * a));
                }
                else
                {
                    g.setColour (pal_.dim.withMultipliedAlpha (a));
                }
                g.setFont (MBTheme::font (on ? MBTheme::uiMedium() : MBTheme::uiRegular(), 12.0f * scale));
                g.drawText (labels[i], cell, juce::Justification::centred);
            }
            g.setColour (pal_.gridZ.withMultipliedAlpha (a));
            g.drawRoundedRectangle (seg, seg.getHeight() * 0.28f, 1.0f);
        }
    }

    // Approved two-row footer: Compress/Expand + Ratio, Knee + Sidechain.
    for(int k=0;k<3;++k) {
        const auto row=popSliderRect(k);
        const bool hot=(sliderDrag_==k)||row.contains(lastMousePos_);
        g.setFont(MBTheme::font(MBTheme::uiMedium(),12*scale));
        g.setColour(pal_.dim.withMultipliedAlpha(a));
        g.drawText(k==0?tr("pop.ratio"):k==1?tr("pop.knee"):tr("pop.sidechain"),
                   row.withWidth((k==2?80:46)*scale),juce::Justification::centredLeft);
        if(k==2) {
            const float x=row.getX()+90*scale,w=(row.getWidth()-90*scale)*.5f;
            for(int j=0;j<2;++j) {
                const auto cell=juce::Rectangle<float>(x+j*w,row.getY(),w-4*scale,row.getHeight());
                const bool on=(sliders_[2]>.5)==(j==1);
                UiPack::NeuPainter::drawButton(g,cell,pal_,themeTokens_,on,cell.contains(lastMousePos_),scale,a,5*scale);
                g.setColour((on?pal_.accent:pal_.dim).withMultipliedAlpha(a));
                g.drawText(j==0?tr("pop.int"):tr("pop.ext"),cell,juce::Justification::centred);
            }
            continue;
        }
        const float fraction=juce::jlimit(0.0f,1.0f,k==0?float((sliders_[0]-1)/19):float(sliders_[1]));
        const auto track=popSliderTrackRect(k);
        if(themeTokens_.isNeumorphic)
        {
            UiPack::NeuPainter::drawInsetRect(g,track.expanded(0,1.5f*scale),3*scale,pal_,themeTokens_.insetButton,scale,a,true);
            g.setColour(pal_.accent.withMultipliedAlpha(a));
            g.fillRoundedRectangle(track.withWidth(juce::jmax(4*scale,track.getWidth()*fraction)),2*scale);
            const juce::Rectangle<float> thumb(track.getX()+track.getWidth()*fraction-6*scale,row.getCentreY()-6*scale,12*scale,12*scale);
            UiPack::NeuPainter::drawRaisedCircle(g,thumb,pal_,hot?themeTokens_.shadowHover:themeTokens_.shadowBadge,scale);
        }
        else
        {
            g.setColour(pal_.track.withMultipliedAlpha(a));g.fillRoundedRectangle(track,2*scale);
            g.setColour(pal_.accent.withMultipliedAlpha(a));g.fillRoundedRectangle(track.withWidth(juce::jmax(4*scale,track.getWidth()*fraction)),2*scale);
            g.setColour((hot?pal_.onAccent:pal_.knob).withMultipliedAlpha(a));
            g.fillEllipse(track.getX()+track.getWidth()*fraction-5*scale,row.getCentreY()-5*scale,10*scale,10*scale);
        }
        const auto value=k==0?juce::String(sliders_[0],1)+" : 1":juce::String(sliders_[1]*48,1)+" dB";
        const auto valBox = row.withLeft (row.getRight() - 68.0f * scale);
        if (editingSlider_ == k)
        {
            const auto editBox = valBox.reduced (0.0f, 1.0f * scale);
            if (themeTokens_.isNeumorphic)
            {
                UiPack::NeuPainter::drawInsetRect (g, editBox, 6.0f * scale, pal_, themeTokens_.insetFocus, scale, a, true);
                g.setColour (pal_.accent.withMultipliedAlpha (0.5f * a));
                g.drawRoundedRectangle (editBox, 6.0f * scale, 1.0f * scale);
            }
            else
            {
                g.setColour (pal_.surface.withAlpha (0.97f * a));
                g.fillRoundedRectangle (editBox, 5.0f * scale);
                g.setColour (pal_.accent.withMultipliedAlpha (a));
                g.drawRoundedRectangle (editBox, 5.0f * scale, 1.2f * scale);
            }
            g.setColour (pal_.text.withMultipliedAlpha (a));
            g.setFont (MBTheme::font (MBTheme::regular(), 12.0f * scale));
            g.drawText (sliderEditText_ + (sliderEditCursorOn_ ? "|" : " "),
                        editBox, juce::Justification::centred, false);
        }
        else
        {
            g.setFont(MBTheme::font(MBTheme::regular(),12*scale));g.setColour(pal_.text.withMultipliedAlpha(a));
            g.drawText(value,row.withLeft(row.getRight()-64*scale),juce::Justification::centredRight);
        }
    }

    // ---- EXPERT 右缘竖排标签（Phase C-5，Pro-MB 对标）----
    const auto er = expertRect();
    {
        UiPack::NeuPainter::drawButton (g, er, pal_, themeTokens_, expertOpen_,
                                        er.contains (lastMousePos_), scale, a, er.getWidth() * 0.3f);
        g.setColour (expertOpen_ ? pal_.accent.withAlpha (0.9f * a)
                                 : pal_.dim.withMultipliedAlpha (a));
        g.setFont (MBTheme::font (expertOpen_ ? MBTheme::uiMedium() : MBTheme::uiRegular(), 12.0f * scale));

        const juce::String expLabel = tr ("pop.expert");
        const juce::String txt = expLabel + (expertOpen_ ? juce::String::fromUTF8 (" \xE2\x96\xBC")
                                                         : juce::String::fromUTF8 (" \xE2\x96\xB6"));
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
}

// R35（C-b）：EXPERT 展开层——由 ExpertLayer 子组件调用，合成在旋钮之上。
void BandPopover::paintExpertLayer(juce::Graphics& g)
{
    if((!open_&&anim_<.004f)||!expertOpen_||anim_<.004f)return;
    const float a = juce::jlimit (0.0f, 1.0f, anim_);
    const auto box=expertContentRect();const float s=EditorLayout(hostBounds_,layoutTokens_).scale;
    if(themeTokens_.isNeumorphic)
    {
        UiPack::NeuPainter::drawInsetRect(g,box,themeTokens_.radiusMd*s,pal_,themeTokens_.insetWell,s,a,true);
    }
    else
    {
        g.setColour(pal_.surface.withAlpha(.80f*a));g.fillRoundedRectangle(box,7*s);
        g.setColour(pal_.gridZ.withMultipliedAlpha(a));g.drawRoundedRectangle(box,7*s,s);
    }
    const juce::String titles[]={tr("pop.detector"),tr("pop.lookahead"),tr("pop.scFiltering")};
    const juce::String options[3][3]={
        {tr("pop.peak"),tr("pop.rms"),{}},
        {tr("pop.off"),"1 ms","3 ms"},
        {tr("pop.band"),tr("pop.free"),{}}
    };
    const int selected[]={sliders_[3]>.5?1:0,(int)std::lround(sliders_[4]*2),getParamNorm("band"+juce::String(juce::jmax(0,band_))+"SCFilter")>.5f?1:0};
    for(int row=0;row<3;++row){g.setFont(MBTheme::font(MBTheme::uiMedium(),13*s));g.setColour(pal_.dim.withMultipliedAlpha(a));
        g.drawText(titles[row],juce::Rectangle<float>(box.getX()+12*s,box.getY()+(12+row*40)*s,130*s,30*s),juce::Justification::centredLeft);
        for(int i=0;i<(row==1?3:2);++i){const auto r=expertOptionRect((ExpertRow)(row+1),i);const bool on=selected[row]==i;
            UiPack::NeuPainter::drawButton(g,r,pal_,themeTokens_,on,r.contains(lastMousePos_),s,a,4*s);
            g.setColour((on?(themeTokens_.isNeumorphic?pal_.accent:pal_.text):pal_.dim).withMultipliedAlpha(a));
            g.setFont(MBTheme::font(on?MBTheme::uiMedium():MBTheme::uiRegular(),14*s));
            g.drawText(options[row][i],r,juce::Justification::centred,false);}}
    const float left=box.getX()+box.getWidth()*.58f;
    g.setColour(pal_.dim.withMultipliedAlpha(a));g.setFont(MBTheme::font(MBTheme::uiMedium(),12*s));
    g.drawText(tr("pop.freqFreeMode"),juce::Rectangle<float>(left,box.getY()+8*s,box.getRight()-left-12*s,22*s),juce::Justification::centredLeft);
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

