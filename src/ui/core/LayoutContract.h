#pragma once

#include <JuceHeader.h>
#include <array>

// ============================================================
// LayoutContract — 布局包（Layout Pack）命名槽位与几何契约
//
// 架构纪律（对齐 natalia-cli UiShellSlots + LayoutProfile）：
//   · 布局包定义顶栏、频谱视窗、电平表栏、底栏与浮层卡片的几何比例与内边距
//   · 组件只向 Layout 询问命名槽位 Rect，不再自己硬编码父窗口偏移
//   · Standard 保持与原 1200×780 全出血布局逐像素兼容；
//     Sculpted 为新拟物双阴影提供呼吸边距与内凹频谱视窗（Inset Display Well）
// ============================================================

namespace UiPack
{
    enum class LayoutId : int
    {
        Standard = 0,   // 默认全出血频谱布局
        Sculpted = 1    // 新拟物雕塑内凹视窗布局（留出双阴影呼吸边距）
    };

    inline constexpr int kNumLayouts = 2;

    struct LayoutTokens
    {
        juce::String id;
        juce::String name;

        float topBarHeight     = 46.0f;
        float globalBarHeight  = 54.0f;
        float meterWidth       = 44.0f;
        float plotInsetX       = 0.0f;    // 频谱视窗左右内缩边距（px @ scale=1.0）
        float plotInsetY       = 0.0f;    // 频谱视窗上下内缩边距（px @ scale=1.0）
        float plotCornerRadius = 0.0f;    // 频谱内凹视窗圆角
        bool  sunkenPlotWell   = false;   // 是否把中央频谱绘制为 neu-pressed 内凹屏幕槽
    };
} // namespace UiPack
