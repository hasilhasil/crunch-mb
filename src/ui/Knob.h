// ============================================================
// Crunch MB — Knob（Phase 5.1 旋钮家族控件）
//   UI_SPEC §6.4：暖灰环 + accent 弧 + 近白帽 + 深色指针
//   交互（§6.4 + Pro-MB 官方 Knobs 帮助）：
//     · 垂直拖动：速度比例（慢拖 = 精细，FabFilter 同款逻辑）
//     · 双击：内嵌文本输入（自绘，不用 juce::TextEditor——宿主里会弹独立窗）
//     · 滚轮：按 interval 步进
//     · Shift：微调（×0.1）
//     · Ctrl+click：复位默认值
// ============================================================
#pragma once
#include <JuceHeader.h>
#include "MBTheme.h"

class Knob : public juce::Component
{
public:
    Knob() = default;

    // ---- 配置 ----
    void setRange (double minValue, double maxValue, double interval = 0.0);
    void setDefaultValue (double d)            { default_ = juce::jlimit (min_, max_, d); }
    void setLabel (juce::String text)          { label_ = std::move (text); repaint(); }
    void setValueSuffix (juce::String s)       { suffix_ = std::move (s); repaint(); }
    void setDecimals (int d)                   { decimals_ = d; repaint(); }
    void setKnobDiameter (float px)            { knobDiameter_ = px; repaint(); }
    void setShowLabel (bool s)                 { showLabel_ = s; repaint(); }
    void setBipolar (bool b)                   { bipolar_ = b; repaint(); }   // 中点起弧（LINKED 偏移用）
    void setValuePrefix (juce::String p)       { valuePrefix_ = std::move (p); repaint(); }
    void setLinkedMark (bool b)                { linkedMark_ = b; repaint(); }   // §5.2 ⊞ 联动标识
    void setPalette (MBTheme::Palette p)       { pal_ = p; repaint(); }

    // ---- 值 ----
    void setValue (double v, bool sendNotification = true);
    double getValue() const                    { return value_; }
    double getNormalisedValue() const          { return (value_ - min_) / (max_ - min_); }

    /** 值变化回调（拖拽/滚轮/输入/复位）*/
    std::function<void (double)> onValueChange;
    /** 开始/结束拖拽（宿主自动化用）*/
    std::function<void()> onDragStart, onDragEnd;

    void paint (juce::Graphics&) override;
    void resized() override;
    // 测试钩子
    void mouseDownForTest (float downX, float downY, juce::ModifierKeys mods,
                           float x, float y)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        mouseDown (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     nullptr, nullptr, juce::Time(),
                                     juce::Point<float> (downX, downY), juce::Time(), 1, false));
    }
    void mouseDragForTest (float downX, float downY, juce::ModifierKeys mods,
                           float x, float y)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        mouseDrag (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     nullptr, nullptr, juce::Time(),
                                     juce::Point<float> (downX, downY), juce::Time(), 1, true));
    }
    void mouseUpForTest (float downX, float downY, juce::ModifierKeys mods,
                         float x, float y)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        mouseUp (juce::MouseEvent (src, juce::Point<float> (x, y), mods,
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   nullptr, nullptr, juce::Time(),
                                   juce::Point<float> (downX, downY), juce::Time(), 1, false));
    }
    void mouseWheelForTest (float x, float y, float dy)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        juce::MouseWheelDetails w; w.deltaY = dy; w.isReversed = false;
        mouseWheelMove (juce::MouseEvent (src, juce::Point<float> (x, y), juce::ModifierKeys(),
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          nullptr, nullptr, juce::Time(),
                                          juce::Point<float> (x, y), juce::Time(), 1, false), w);
    }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void commitValue (double v);
    void openEditor();
    void closeEditor (bool apply);
    float valueToAngle (double v) const;      // 弧度
    juce::Rectangle<float> knobBounds() const;

    double min_ = 0.0, max_ = 1.0, interval_ = 0.0;
    double value_ = 0.0, default_ = 0.0;
    double dragStartValue_ = 0.0;
    int    decimals_ = 1;
    float  knobDiameter_ = 48.0f;
    bool   showLabel_ = true;
    bool   bipolar_ = false;
    bool   linkedMark_ = false;                    // §5.2：⊞ 联动标识
    bool   dragging_ = false;
    bool   shiftFine_ = false;
    float  slowAccum_ = 0.0f;                  // 慢拖累积（速度比例）

    juce::String label_, suffix_, valuePrefix_;
    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);

    // 内嵌编辑（自绘）
    bool editing_ = false;
    juce::String editText_;
    bool editCursorOn_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};
