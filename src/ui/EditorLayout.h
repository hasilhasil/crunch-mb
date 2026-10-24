#pragma once
#include <JuceHeader.h>

// Shared geometry for painting and hit testing. Extra width/height belongs to the
// spectrum; controls grow uniformly and retain a readable minimum size.
struct EditorLayout
{
    static constexpr int defaultWidth = 1200, defaultHeight = 780;
    static constexpr int minWidth = 920, minHeight = 560;
    float width, height, scale;
    explicit EditorLayout (juce::Rectangle<int> bounds)
        : width ((float) bounds.getWidth()), height ((float) bounds.getHeight()),
          scale (juce::jlimit (0.90f, 2.0f, juce::jmin (width / 1040.0f, height / 780.0f))) {}
    float topHeight() const { return 46.0f * scale; }
    float globalHeight() const { return 54.0f * scale; }
    float globalTop() const { return height - globalHeight(); }
    float meterWidth() const { return 44.0f * scale; }
    float plotRight() const { return width - meterWidth(); }
    juce::Rectangle<float> power() const { return { 145*scale, 8*scale, 30*scale, 30*scale }; }
    juce::Rectangle<float> theme() const { return { plotRight()+7*scale, 8*scale, 30*scale, 30*scale }; }
    juce::Rectangle<float> range() const { return { plotRight()-89*scale, 8*scale, 80*scale, 30*scale }; }
    juce::Rectangle<float> linkedButton() const { return { range().getX()-169*scale, 8*scale, 160*scale, 30*scale }; }
    juce::Rectangle<float> bandTab (int band) const
    {
        const float left = 200*scale;
        const float available = linkedButton().getX() - left - 20*scale;
        const float cell = juce::jmin (83*scale, available / 4);
        const float start = left + (available-4*cell)*0.5f;
        return { start + band*cell, 8*scale, cell-3*scale, 30*scale };
    }
    juce::Rectangle<float> globalCell (int index) const
    {
        // Full labels and longest supported state fit at 920 px without scrolling.
        const float weights[8] = { 1.50f,1.25f,1.27f,1.28f,1.40f,.90f,1.0f,.74f };
        const float pad = 10*scale, gap = 6*scale;
        const float unit = (width-2*pad-7*gap)/9.34f;
        float x = pad;
        for (int i=0; i<index; ++i) x += weights[i]*unit+gap;
        return { x, globalTop()+10*scale, weights[index]*unit, 34*scale };
    }
    juce::Rectangle<float> linkedPanel() const
    {
        const float w = juce::jmin (820*scale, plotRight()-24*scale), h = 230*scale;
        return { (plotRight()-w)*.5f, globalTop()-h-42*scale, w, h };
    }
};
