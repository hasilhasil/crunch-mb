#include "OutputPanel.h"
#include "EditorLayout.h"

OutputPanel::OutputPanel()
{
    setInterceptsMouseClicks(false,true);
    const char* labels[]={"INPUT GAIN","OUTPUT GAIN","MIX"};
    for(int i=0;i<3;++i){auto& knob=knobs_[(size_t)i];
        knob.setRange(i==2?0:-36,i==2?200:36,.1);knob.setDefaultValue(i==2?100:0);
        knob.setValue(i==2?100:0,false);knob.setLabel(labels[i]);knob.setValueSuffix(i==2?"%":"dB");knob.setDecimals(1);
        if(i!=2){knob.setBipolar(true);knob.setBipolarOrigin(0);}
        knob.setSelectionFeedbackEnabled(true);
        knob.onValueChange=[this,i](double v){if(i==0)inputGainDb_=(float)v;else if(i==1)outputGainDb_=(float)v;else globalMixPct_=(float)v;
            if(onSettingsChanged)onSettingsChanged();};
        knob.onDragStart=[this,i]{if(onGesture)onGesture(i,true);};
        knob.onDragEnd=[this,i]{if(onGesture)onGesture(i,false);};
        addChildComponent(knob);
    }
    monitorBox_.addItemList({"Output","Input","Band 1","Band 2","Band 3","Band 4","Gain Reduction"},1);
    monitorBox_.setLookAndFeel(&menuLookAndFeel_);monitorBox_.setSelectedId(1,juce::dontSendNotification);
    monitorBox_.onChange=[this]{monitorIdx_=monitorBox_.getSelectedId()-1;if(onSettingsChanged)onSettingsChanged();};
    addChildComponent(monitorBox_);setTheme(MBTheme::Theme::Blue);
}
OutputPanel::~OutputPanel(){monitorBox_.setLookAndFeel(nullptr);}
juce::Rectangle<float> OutputPanel::panelRect() const
{
    const EditorLayout l(hostBounds_);const float s=l.scale,w=440*s,h=316*s;
    return {l.plotRight()-w-12*s,l.globalTop()-h-12*s,w,h};
}
juce::Rectangle<float> OutputPanel::toggleRect(int index) const
{
    const auto r=panelRect();const float s=EditorLayout(hostBounds_).scale;
    return {r.getX()+16*s+index*208*s,r.getY()+207*s,200*s,34*s};
}
void OutputPanel::setHostBounds(juce::Rectangle<int> bounds){hostBounds_=bounds;layoutControls();repaint();}
void OutputPanel::resized(){layoutControls();}
void OutputPanel::setTheme(MBTheme::Theme theme)
{
    pal_=MBTheme::palette(theme);for(auto& knob:knobs_)knob.setPalette(pal_);
    monitorBox_.setColour(juce::ComboBox::backgroundColourId,pal_.track);
    monitorBox_.setColour(juce::ComboBox::textColourId,pal_.text);
    monitorBox_.setColour(juce::ComboBox::outlineColourId,pal_.gridZ);
    menuLookAndFeel_.setColour(juce::PopupMenu::backgroundColourId,pal_.surface);
    menuLookAndFeel_.setColour(juce::PopupMenu::textColourId,pal_.text);
    menuLookAndFeel_.setColour(juce::PopupMenu::highlightedBackgroundColourId,pal_.accent);
    repaint();
}
void OutputPanel::applyUiSnapshot(const UiPack::UiSnapshot& snap)
{
    pal_          = snap.palette;
    themeTokens_  = snap.theme;
    layoutTokens_ = snap.layout;
    locale_       = snap.locale;
    const char* labelKeys[3] = { "knob.inputGain", "knob.outputGain", "knob.mix" };
    for (int i = 0; i < 3; ++i)
    {
        knobs_[(size_t) i].setPalette (pal_);
        knobs_[(size_t) i].setThemeTokens (themeTokens_);
        knobs_[(size_t) i].setLabel (tr (labelKeys[i]));
    }
    monitorBox_.setColour(juce::ComboBox::backgroundColourId,pal_.track);
    monitorBox_.setColour(juce::ComboBox::textColourId,pal_.text);
    monitorBox_.setColour(juce::ComboBox::outlineColourId,themeTokens_.isNeumorphic?pal_.bg.withAlpha(0.0f):pal_.gridZ);
    menuLookAndFeel_.setColour(juce::PopupMenu::backgroundColourId,pal_.surface);
    menuLookAndFeel_.setColour(juce::PopupMenu::textColourId,pal_.text);
    menuLookAndFeel_.setColour(juce::PopupMenu::highlightedBackgroundColourId,pal_.accent);
    repaint();
}
void OutputPanel::layoutControls()
{
    const auto r=panelRect();const float s=EditorLayout(hostBounds_).scale,cell=(r.getWidth()-32*s)/3;
    for(int i=0;i<3;++i){auto& knob=knobs_[(size_t)i];knob.setKnobDiameter(76*s);
        knob.setUiTextSizes(14*s,16*s);knob.setBounds(juce::Rectangle<float>(r.getX()+16*s+i*cell,r.getY()+49*s,cell,150*s).toNearestInt());knob.setVisible(open_);knob.setAlpha(anim_);}
    menuLookAndFeel_.scale=s;monitorBox_.setBounds(juce::Rectangle<float>(r.getX()+132*s,r.getY()+254*s,r.getWidth()-148*s,36*s).toNearestInt());
    monitorBox_.setVisible(open_);monitorBox_.setAlpha(anim_);
}
void OutputPanel::syncControls()
{
    knobs_[0].setValue(inputGainDb_,false);knobs_[1].setValue(outputGainDb_,false);knobs_[2].setValue(globalMixPct_,false);
    monitorBox_.setSelectedId(monitorIdx_+1,juce::dontSendNotification);
}
bool OutputPanel::clickButton()
{
    open_=sticky_=!open_;syncControls();layoutControls();
    tween_.start(juce::Time::getMillisecondCounter(),open_?1.0f:0.0f,Anim::kFadeMs,anim_);animTarget_=open_?1:0;return true;
}
void OutputPanel::clickOutside()
{
    if(!open_)return;open_=sticky_=false;layoutControls();
    tween_.start(juce::Time::getMillisecondCounter(),0,Anim::kFadeMs,anim_);animTarget_=0;
}
bool OutputPanel::advance(double now)
{
    anim_=tween_.value(now);if(std::abs(anim_-animTarget_)<Anim::kSnapEps){anim_=animTarget_;tween_.snapTo(animTarget_);}
    bool busy=!tween_.done(now);
    for(auto& knob:knobs_){knob.setAlpha(anim_);knob.advanceStates(now);knob.setEditCursorBlink((juce::uint32)now);busy|=knob.isAnimating();}
    monitorBox_.setAlpha(anim_);
    return busy;
}
bool OutputPanel::hitTestRow(juce::Point<float> p,int& row) const
{
    if(!open_)return false;for(int i=0;i<2;++i)if(toggleRect(i).contains(p)){row=i+3;return true;}return false;
}
void OutputPanel::clickRow(int row)
{
    if(row==3)globalBypass_=!globalBypass_;else if(row==4)autoGain_=!autoGain_;else return;
    if(onSettingsChanged)onSettingsChanged();repaint();
}
void OutputPanel::paint(juce::Graphics& g)
{
    if(!open_||anim_<.004f)return;const auto r=panelRect();const float s=EditorLayout(hostBounds_,layoutTokens_).scale,a=anim_;
    if(themeTokens_.isNeumorphic)
    {
        UiPack::NeuPainter::drawRaisedRect(g,r,themeTokens_.radiusLg*s,pal_,themeTokens_.shadowCard,s,a,false);
    }
    else
    {
        g.setColour(pal_.halo.withMultipliedAlpha(.38f*a));g.fillRoundedRectangle(r.translated(0,5*s).expanded(3*s),12*s);
        g.setColour(pal_.popA.withMultipliedAlpha(a));g.fillRoundedRectangle(r,12*s);g.setColour(pal_.gridZ);g.drawRoundedRectangle(r,12*s,s);
    }
    g.setColour(pal_.text);g.setFont(MBTheme::font(MBTheme::uiMedium(),16*s));
    g.drawText(tr("output.title"),r.withTrimmedLeft(18*s).withHeight(42*s),juce::Justification::centredLeft);
    g.setColour(pal_.gridZ.withMultipliedAlpha(themeTokens_.isNeumorphic?0.35f:1.0f));
    g.drawLine(r.getX()+16*s,r.getY()+42*s,r.getRight()-16*s,r.getY()+42*s,s);
    for(int i=0;i<2;++i){auto cell=toggleRect(i);bool on=i==0?globalBypass_:autoGain_;
        UiPack::NeuPainter::drawButton(g,cell,pal_,themeTokens_,on,false,s,a,5*s);
        g.setColour(on?pal_.accent:pal_.dim);g.setFont(MBTheme::font(MBTheme::uiMedium(),14*s));
        g.drawText(i==0?tr("output.globalBypass"):tr("output.autoGain"),cell.withTrimmedLeft(12*s).withTrimmedRight(40*s),juce::Justification::centredLeft);
        g.drawText(on?tr("output.on"):tr("output.off"),cell.withTrimmedLeft(cell.getWidth()-40*s),juce::Justification::centred);}
    g.setColour(pal_.dim);g.setFont(MBTheme::font(MBTheme::uiRegular(),14*s));
    g.drawText(tr("output.monitor"),juce::Rectangle<float>(r.getX()+20*s,r.getY()+254*s,100*s,36*s),juce::Justification::centredLeft);
}
