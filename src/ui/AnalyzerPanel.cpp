#include "AnalyzerPanel.h"
#include "../PluginProcessor.h"
#include "EditorLayout.h"

// ============================================================
// AnalyzerPanel 实现
// ============================================================

namespace
{
    constexpr float kDesignW = 920.0f;
    constexpr float kDesignH = 620.0f;
    constexpr float kGlobalBarH = 32.0f;
    constexpr float kRowH = 20.0f;      // 每行高度（设计稿像素）
    constexpr int   kNumRows = 14;      // Pre/Post/4 Res/3 Speed/4 Tilt/Freeze
}

juce::Rectangle<float> AnalyzerPanel::panelRect() const
{

    const EditorLayout l(hostBounds_);const float s=l.scale,w=440*s,h=348*s;
    return {l.plotRight()-w-12*s,l.globalTop()-h-12*s,w,h};

}

void AnalyzerPanel::setHostBounds (juce::Rectangle<int> hostBounds)
{
    if (hostBounds_ == hostBounds)
        return;
    hostBounds_ = hostBounds;
    repaint();
}

// ---------------- 状态机（Phase D：Pro-MB sticky 语义）----------------
// R36（B2）：hoverButton() 已删除。用户实测"鼠标移到检测区域就展开"会误触，
//   底栏按钮改为**纯点击**展开（clickButton 的 sticky 语义不变）。
//   保留空实现会被误认为还有悬停开合路径，直接去掉。

bool AnalyzerPanel::clickButton()
{
    if (! sticky_)
    {
        // 第一次点击：变 sticky 并保持打开
        sticky_ = true;
        open_ = true;
    }
    else
    {
        // 已 sticky：再点 = 收起并解除 sticky
        sticky_ = false;
        open_ = false;
    }
    tween_.start (juce::Time::getMillisecondCounter(), open_ ? 1.0f : 0.0f,
                  Anim::kFadeMs, anim_);
    animTarget_ = open_ ? 1.0f : 0.0f;
    return true;
}

void AnalyzerPanel::clickOutside()
{
    if (! open_)
        return;
    open_ = false;
    sticky_ = false;                 // 点外面 = 完全收起（连 sticky 一起解除）
    tween_.start (juce::Time::getMillisecondCounter(), 0.0f, Anim::kFadeMs, anim_);
    animTarget_ = 0.0f;
}

bool AnalyzerPanel::advance (double nowMs)
{
    anim_ = tween_.value (nowMs);
    if (std::abs (anim_ - animTarget_) < Anim::kSnapEps)
    {
        anim_ = animTarget_;
        tween_.snapTo (animTarget_);
    }
    return ! tween_.done (nowMs);
}

// ---------------- 行布局与点击 ----------------
void AnalyzerPanel::layoutRows() {}

juce::Rectangle<float> AnalyzerPanel::optionRect(int row) const
{
    const auto r=panelRect();const float s=EditorLayout(hostBounds_).scale;
    int index=0,count=1;float y=298,h=32;
    if(row<2){index=row;count=2;y=46;}
    else if(row<6){index=row-2;count=4;y=116;h=38;}
    else if(row<9){index=row-6;count=3;y=186;}
    else if(row<13){index=row-9;count=4;y=250;}
    const float cell=(r.getWidth()-32*s)/count;
    return {r.getX()+16*s+index*cell,r.getY()+y*s,cell-4*s,h*s};
}
bool AnalyzerPanel::hitTestRow(juce::Point<float> p,int& rowOut) const
{
    if(!open_)return false;
    for(int i=0;i<14;++i)if(optionRect(i).contains(p)){rowOut=i;return true;}
    return false;
}

void AnalyzerPanel::clickRow (int row)
{
    switch (row)
    {
        case 0:  preOn_  = ! preOn_;  break;
        case 1:  postOn_ = ! postOn_; break;
        case 2:  fftSize_ = 1024; break;
        case 3:  fftSize_ = 2048; break;
        case 4:  fftSize_ = 4096; break;
        case 5:  fftSize_ = 8192; break;
        case 6:  speed_ = 0; break;
        case 7:  speed_ = 1; break;
        case 8:  speed_ = 2; break;
        case 9:  tilt_ = 0.0f; break;
        case 10: tilt_ = 1.5f; break;
        case 11: tilt_ = 3.0f; break;
        case 12: tilt_ = 4.5f; break;
        case 13: frozen_ = ! frozen_; break;
        default: break;
    }
    if (onSettingsChanged)
        onSettingsChanged();
    repaint();
}

// ---------------- 绘制 ----------------
void AnalyzerPanel::paint(juce::Graphics& g)
{
    if(!open_||anim_<.004f)return;
    const auto r=panelRect();const float s=EditorLayout(hostBounds_,layoutTokens_).scale,a=anim_;
    if(themeTokens_.isNeumorphic)
    {
        UiPack::NeuPainter::drawRaisedRect(g,r,themeTokens_.radiusLg*s,pal_,themeTokens_.shadowCard,s,a,false);
    }
    else
    {
        g.setColour(pal_.halo.withMultipliedAlpha(.38f*a));g.fillRoundedRectangle(r.translated(0,5*s).expanded(3*s),12*s);
        g.setColour(pal_.popA.withMultipliedAlpha(a));g.fillRoundedRectangle(r,12*s);
        g.setColour(pal_.gridZ);g.drawRoundedRectangle(r,12*s,s);
    }
    g.setColour(pal_.text);g.setFont(MBTheme::font(MBTheme::uiMedium(),16*s));
    g.drawText(tr("analyzer.title"),r.withTrimmedLeft(18*s).withHeight(40*s),juce::Justification::centredLeft);
    const char* headingKeys[]={"analyzer.resolution","analyzer.speed","analyzer.tilt"};const float ys[]={90,162,226};
    for(int i=0;i<3;++i){g.setFont(MBTheme::font(MBTheme::uiMedium(),13*s));g.setColour(pal_.dim);
        g.drawText(tr(headingKeys[i]),juce::Rectangle<float>(r.getX()+18*s,r.getY()+ys[i]*s,r.getWidth()-36*s,22*s),juce::Justification::centredLeft);}
    const bool on[]={preOn_,postOn_,fftSize_==1024,fftSize_==2048,fftSize_==4096,fftSize_==8192,
        speed_==0,speed_==1,speed_==2,std::abs(tilt_)<.01f,std::abs(tilt_-1.5f)<.01f,std::abs(tilt_-3)<.01f,std::abs(tilt_-4.5f)<.01f,frozen_};
    const juce::String labels[]={
        tr("analyzer.pre"),tr("analyzer.post"),
        tr("analyzer.low1024"),tr("analyzer.med2048"),tr("analyzer.high4096"),tr("analyzer.max8192"),
        tr("analyzer.fast"),tr("analyzer.medium"),tr("analyzer.slow"),
        tr("analyzer.off"),"1.5","3.0","4.5",tr("analyzer.freeze")
    };
    for(int i=0;i<14;++i){auto cell=optionRect(i);
        UiPack::NeuPainter::drawButton(g,cell,pal_,themeTokens_,on[i],false,s,a,5*s);
        g.setColour(on[i]?(themeTokens_.isNeumorphic?pal_.accent:pal_.text):pal_.dim);
        g.setFont(MBTheme::font(on[i]?MBTheme::uiMedium():MBTheme::uiRegular(),(i>=2&&i<6?13.0f:14.0f)*s));
        g.drawText(labels[i],cell.reduced(3*s,0),juce::Justification::centred,false);}
}
