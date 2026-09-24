#pragma once

#include <JuceHeader.h>

#include <memory>

class CrunchMBAudioProcessor;
class DisplayView;

// ============================================================
// CrunchMBAudioProcessorEditor — 外壳（Phase 4.2 起）
// 托管 DisplayView（UI_SPEC v1.8 Layer 1 显示区）；
// Phase 5 追加 Layer 2/3（全局条控件 / Popover / LINKED 抽屉）。
// ============================================================

class CrunchMBAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit CrunchMBAudioProcessorEditor (CrunchMBAudioProcessor&);
    ~CrunchMBAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CrunchMBAudioProcessor& proc;
    std::unique_ptr<DisplayView> display;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrunchMBAudioProcessorEditor)
};
