#pragma once
#include <JuceHeader.h>
#include <cstdlib>
#include "MBTheme.h"

// A continuous parameter readout: click selects, drag adjusts, double-click edits.
class ValueField : public juce::Component
{
public:
    std::function<void(double)> onValueChange;
    std::function<void()> onDragStart, onDragEnd;
    ValueField()
    {
        setWantsKeyboardFocus(true);
        addChildComponent(editor_);
        editor_.setMultiLine(false);
        editor_.setJustification(juce::Justification::centred);
        editor_.onReturnKey = [this] { finishEdit(true); };
        editor_.onEscapeKey = [this] { finishEdit(false); };
        editor_.onFocusLost = [this] { if(editing_) finishEdit(true); };
    }
    void configure(juce::String label, juce::String unit, double low, double high, int decimals, bool logarithmic=false)
    { label_=label; unit_=unit; low_=low; high_=high; decimals_=decimals; logarithmic_=logarithmic; }
    void setValue(double value) { value_=juce::jlimit(low_,high_,value); repaint(); }
    double getValue() const { return value_; }
    void setPalette(MBTheme::Palette palette) { pal_=palette; resized();repaint(); }
    void setScale(float scale) { if(std::abs(scale_-scale)<.0001f)return;scale_=scale;resized();repaint(); }
    bool applyText(const juce::String& input)
    {
        auto text=input.trim().toLowerCase(); double multiplier=1;
        if(text.endsWith("khz")){text=text.dropLastCharacters(3).trim();multiplier=1000;}
        else if(text.endsWithChar('k')){text=text.dropLastCharacters(1).trim();multiplier=1000;}
        else if(unit_.isNotEmpty()&&text.endsWith(unit_.toLowerCase())) text=text.dropLastCharacters(unit_.length()).trim();
        const auto bytes=text.toStdString(); char* end=nullptr;
        const double value=std::strtod(bytes.c_str(),&end)*multiplier;
        if(bytes.empty()||end!=bytes.c_str()+bytes.size()||!std::isfinite(value)) return false;
        if(onDragStart)onDragStart(); commit(value); if(onDragEnd)onDragEnd(); return true;
    }
    void paint(juce::Graphics& g) override
    {
        const auto r=getLocalBounds().toFloat(); auto value=valueRect();
        g.setFont(MBTheme::font(MBTheme::uiRegular(),13*scale_));g.setColour(pal_.dim);
        g.drawText(label_,r.withWidth(r.getWidth()*.49f),juce::Justification::centredLeft,false);
        const bool selected=hasKeyboardFocus(true);
        g.setColour(selected?pal_.accent.withAlpha(.16f):pal_.track);g.fillRoundedRectangle(value,5*scale_);
        g.setColour(selected?pal_.accent:pal_.gridZ);g.drawRoundedRectangle(value,5*scale_,scale_);
        g.setFont(MBTheme::font(MBTheme::uiMedium(),15*scale_));g.setColour(pal_.text);
        if(!editing_)g.drawText(juce::String(value_,decimals_)+" "+unit_,value.reduced(5*scale_,0),juce::Justification::centred,false);
    }
    void resized() override
    {
        editor_.setBounds(valueRect().reduced(2*scale_).toNearestInt());
        editor_.setFont(MBTheme::font(MBTheme::uiMedium(),15*scale_));
        editor_.setColour(juce::TextEditor::backgroundColourId,pal_.surface);
        editor_.setColour(juce::TextEditor::textColourId,pal_.text);
        editor_.setColour(juce::TextEditor::highlightColourId,pal_.accent.withAlpha(.35f));
        editor_.setColour(juce::TextEditor::outlineColourId,juce::Colours::transparentBlack);
        editor_.setColour(juce::TextEditor::focusedOutlineColourId,pal_.accent);
    }
    void mouseDown(const juce::MouseEvent&) override
    { grabKeyboardFocus();start_=value_;if(onDragStart)onDragStart();gesture_=true;repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const double delta=-e.getDistanceFromDragStartY()/220.0*(e.mods.isShiftDown()?.1:1.0);
        if(logarithmic_)commit(start_*std::pow(high_/low_,delta));else commit(start_+(high_-low_)*delta);
    }
    void mouseUp(const juce::MouseEvent&) override { endGesture(); }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        endGesture();editing_=true;editor_.setText(juce::String(value_,decimals_),false);
        editor_.setVisible(true);editor_.grabKeyboardFocus();editor_.selectAll();repaint();
    }
    void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w) override
    {
        if(w.deltaY==0||editing_)return;
        if(onDragStart)onDragStart();
        const double step=(w.deltaY>0?1:-1)*(e.mods.isShiftDown()?.001:.01);
        commit(logarithmic_?value_*std::pow(high_/low_,step):value_+(high_-low_)*step);
        if(onDragEnd)onDragEnd();
    }
    void focusGained(juce::Component::FocusChangeType) override { repaint(); }
    void focusLost(juce::Component::FocusChangeType) override { repaint(); }
private:
    juce::Rectangle<float> valueRect() const { auto r=getLocalBounds().toFloat();return r.withTrimmedLeft(r.getWidth()*.50f).reduced(0,2*scale_); }
    void commit(double value) { setValue(value);if(onValueChange)onValueChange(value_); }
    void endGesture(){if(gesture_&&onDragEnd)onDragEnd();gesture_=false;}
    void finishEdit(bool apply)
    {
        if(!editing_)return;
        if(apply&&!applyText(editor_.getText())){editor_.setColour(juce::TextEditor::outlineColourId,juce::Colours::red);return;}
        editing_=false;editor_.setVisible(false);repaint();
    }
    MBTheme::Palette pal_=MBTheme::palette(MBTheme::Theme::Blue);
    juce::String label_,unit_;juce::TextEditor editor_;
    double value_=0,low_=0,high_=1,start_=0;int decimals_=1;float scale_=1;
    bool logarithmic_=false,editing_=false,gesture_=false;
};
