#pragma once

#include <JuceHeader.h>
#include <array>

// ============================================================
// PaletteContract — 配色包（Palette Pack）语义颜色槽位契约
//
// 架构纪律（对齐 natalia-cli packages/plugins/ui/next/src/theme/palettes.ts）：
//   · 所有配色包实现完全相同的 PaletteTokens 语义槽位集合（等集契约）
//   · 组件与 Theme 包、NeuPainter 严禁硬编码颜色字面量，一律引用本契约槽位
//   · 同时提供新拟物双阴影槽位（shadowDark / shadowLight / insetDark / insetLight）
//     与经典界面槽位，使任意 PalettePack 与任意 ThemePack 可正交混搭、原子替换
// ============================================================

namespace UiPack
{
    enum class PaletteId : int
    {
        Blue     = 0,
        Red      = 1,
        Cream    = 2,
        NeuLight = 3,   // neumorphism-style-pack 主配色：#e0e5ec + #6d5dfc 电光紫
        NeuTeal  = 4,   // neumorphism-style-pack 辅配色：#e0e5ec + #4ecdc4 青绿
        NeuDark  = 5    // 暗色新拟物：深灰软塑底板 + 双阴影 + #7c6dff
    };

    inline constexpr int kNumPalettes = 6;

    struct PaletteTokens
    {
        juce::String id;
        juce::String name;

        // ---- 基础与表面槽位 ----
        juce::Colour bg;            // --neu-bg：主画布/机身底色
        juce::Colour bgLight;       // --neu-bg-light：次级浅亮表面（#f0f0f3）
        juce::Colour panel;         // --neu-secondary：侧栏/表头底板（#d1d9e6）
        juce::Colour surface;       // 实心卡片表面
        juce::Colour surfaceA;      // 顶/底栏半透明表面
        juce::Colour popA;          // 浮层面板表面
        juce::Colour track;         // 轨道/凹槽底色
        juce::Colour knob;          // 旋钮帽基色

        // ---- 文本与网格槽位 ----
        juce::Colour text;          // --neu-text：主文本
        juce::Colour dim;           // --neu-text-muted：次级/静默文本
        juce::Colour grid;          // 次网格线
        juce::Colour gridZ;         // 0 dB 基准线 / 细描边

        // ---- 频谱与电平槽位 ----
        juce::Colour meterBg;       // 电平表凹槽底色
        juce::Colour inFill;        // Pre 频谱 / 输入电平填充
        juce::Colour outLevel;      // 输出电平峰值 / 亮部

        // ---- 强调色与状态槽位（来自 neumorphism-tokens.json accent-1..4）----
        juce::Colour accent;        // --neu-accent / accent-1：主强调色（#6d5dfc 等）
        juce::Colour accent2;       // accent-2：珊瑚红（#ff6b6b，告警/副强调）
        juce::Colour accent3;       // accent-3：青绿（#4ecdc4，辅助曲线/状态）
        juce::Colour accent4;       // accent-4：暖金（#ffe66d，旋钮旋转态暖光）
        juce::Colour onAccent;      // 强调色表面上的文字色
        juce::Colour halo;          // 环境遮罩 / 光晕

        // ---- 新拟物双重阴影语义槽位（供 ThemePack 几何表达式引用）----
        juce::Colour shadowDark;    // --neu-shadow-dark（右下正偏移暗影，#b8bcc2）
        juce::Colour shadowLight;   // --neu-shadow-light（左上负偏移高光，#ffffff）
        juce::Colour insetDark;     // 内凹左上暗影
        juce::Colour insetLight;    // 内凹右下高光

        bool light = false;         // 是否为浅色系底板
    };
} // namespace UiPack
