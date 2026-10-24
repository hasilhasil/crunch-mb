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
    // Poppins 作为默认无衬线（验证 FONT_DIR 引用 + BinaryData 链路）
    if (auto face = MBTheme::regular())
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface (face);

    // R36（B4）：全局字号倍率。用户实测"整体按钮和 UI 都太小，不方便阅读"。
    //   放在编辑器构造里而不是 DisplayView 里，是为了让离屏测试保持 1.0
    //   ——否则 808+ 条像素/几何断言会跟着字号一起漂。
    MBTheme::setTextScale (MBTheme::kUiTextScale);

    display = std::make_unique<DisplayView> (proc);
    addAndMakeVisible (*display);

    setResizable (true, true);
    setResizeLimits (EditorLayout::minWidth, EditorLayout::minHeight, 2080, 1560);
    setSize (EditorLayout::defaultWidth, EditorLayout::defaultHeight);

    // ⚠️ setSize 之后立即给 display 真实 bounds：若只靠 resized() 回调，
    // 宿主不及时调用时 display 的 size 为 0 → plotHeightPx() 退化 → 曲线比例错乱（#9）
    display->setBounds (getLocalBounds());

    // ⚠️ 懒启动必须在这里激活：早期版本只在析构里 setEditorActive(false)，
    // 导致分析线程从未启动 → 双层频谱完全不显示（2026-09-23 用户实测 B1）。
    display->setActive (true);
}

CrunchMBAudioProcessorEditor::~CrunchMBAudioProcessorEditor()
{
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
