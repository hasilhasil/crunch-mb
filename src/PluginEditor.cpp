#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/DisplayView.h"
#include "ui/EditorLayout.h"

// ============================================================
// CrunchMBAudioProcessorEditor — 外壳（Phase 4.2）
//   · 托管 DisplayView（UI_SPEC v1.8 Layer 1 显示区）
//   · 加载 Poppins 四字重
//   · 编辑器开关 → 频谱引擎懒启动
// ============================================================

CrunchMBAudioProcessorEditor::CrunchMBAudioProcessorEditor (CrunchMBAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // 修复 MISC-01：使用编辑器私有 LookAndFeel 实例设置默认字体，避免污染宿主同进程其它 JUCE 插件
    if (auto face = MBTheme::regular())
        editorLookAndFeel_.setDefaultSansSerifTypeface (face);
    setLookAndFeel (&editorLookAndFeel_);

    MBTheme::setTextScale (MBTheme::kUiTextScale);

    display = std::make_unique<DisplayView> (proc);
    addAndMakeVisible (*display);

    setResizable (true, true);
    setResizeLimits (EditorLayout::minWidth, EditorLayout::minHeight, 2080, 1560);
    setSize (EditorLayout::defaultWidth, EditorLayout::defaultHeight);

    display->setBounds (getLocalBounds());
    display->setActive (true);
}

CrunchMBAudioProcessorEditor::~CrunchMBAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    proc.setEditorActive (false);     // 停频谱分析线程（懒启动配对）
}

void CrunchMBAudioProcessorEditor::resized()
{
    if (display)
        display->setBounds (getLocalBounds());
}

void CrunchMBAudioProcessorEditor::paint (juce::Graphics& g)
{
    // 显示区由 DisplayView 全权绘制；此处仅兜底背景
    g.fillAll (juce::Colour (0xff101214));
}
