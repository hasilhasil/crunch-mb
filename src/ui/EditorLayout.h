#pragma once
#include <JuceHeader.h>
#include "core/LayoutContract.h"
#include "packs/layouts/BuiltInLayouts.h"

// ============================================================
// EditorLayout — 命名槽位几何计算器（驱动于 LayoutTokens 布局包）
//   · 默认单参数构造使用 LayoutId::Standard，与既有断言 100% 逐像素兼容
//   · 传入 LayoutTokens（如 Sculpted）时自动提供内凹频谱视窗与四包切换卡片槽位
// ============================================================
struct EditorLayout
{
    static constexpr int defaultWidth = 1200, defaultHeight = 780;
    static constexpr int minWidth = 920, minHeight = 560;
    float width, height, scale;
    UiPack::LayoutTokens tokens;

    explicit EditorLayout (juce::Rectangle<int> bounds,
                           const UiPack::LayoutTokens& layoutTokens = UiPack::buildLayoutPack (UiPack::LayoutId::Standard))
        : width ((float) bounds.getWidth()),
          height ((float) bounds.getHeight()),
          scale (juce::jlimit (0.90f, 2.0f, juce::jmin (width / 1040.0f, height / 780.0f))),
          tokens (layoutTokens)
    {}

    float topHeight() const    { return tokens.topBarHeight * scale; }
    float globalHeight() const { return tokens.globalBarHeight * scale; }
    float globalTop() const    { return height - globalHeight(); }
    float meterWidth() const   { return tokens.meterWidth * scale; }
    float plotRight() const    { return width - meterWidth(); }

    /** 中央频谱与动态曲线的屏幕槽位（Sculpted 布局下内缩为圆角内凹槽） */
    juce::Rectangle<float> plotWellRect() const
    {
        const float ix = tokens.plotInsetX * scale;
        const float iy = tokens.plotInsetY * scale;
        return { ix, topHeight() + iy,
                 juce::jmax (10.0f, plotRight() - ix * 2.0f),
                 juce::jmax (10.0f, (globalTop() - topHeight()) - iy * 2.0f) };
    }

    juce::Rectangle<float> power() const        { return { 145 * scale, 8 * scale, 30 * scale, 30 * scale }; }
    juce::Rectangle<float> theme() const        { return { plotRight() + 7 * scale, 8 * scale, 30 * scale, 30 * scale }; }
    juce::Rectangle<float> range() const        { return { plotRight() - 89 * scale, 8 * scale, 80 * scale, 30 * scale }; }
    juce::Rectangle<float> linkedButton() const { return { range().getX() - 169 * scale, 8 * scale, 160 * scale, 30 * scale }; }

    /** 顶栏预设选择窗口槽位（与右侧 A/B 对比按钮成组居中于电源键与 LINKED CONTROL 之间） */
    juce::Rectangle<float> presetSelector() const
    {
        const float left  = power().getRight() + 14.0f * scale;
        const float right = linkedButton().getX() - 14.0f * scale;
        const float presW = 228.0f * scale;
        const float gap   = 10.0f  * scale;
        const float abW   = 66.0f  * scale;
        const float total = presW + gap + abW;
        const float start = juce::jmax (left, left + (right - left - total) * 0.5f);
        return { start, 8.0f * scale, presW, 30.0f * scale };
    }

    /** 顶栏 A/B 对比按钮槽位（紧邻预设选择窗口右侧，仅含 A / B 双字母图标） */
    juce::Rectangle<float> abCompareButton() const
    {
        const auto p = presetSelector();
        return { p.getRight() + 10.0f * scale, 8.0f * scale, 66.0f * scale, 30.0f * scale };
    }

    juce::Rectangle<float> globalCell (int index) const
    {
        const float weights[8] = { 1.50f, 1.25f, 1.27f, 1.28f, 1.40f, 0.90f, 1.0f, 0.74f };
        const float pad = 10 * scale, gap = 6 * scale;
        const float unit = (width - 2 * pad - 7 * gap) / 9.34f;
        float x = pad;
        for (int i = 0; i < index; ++i)
            x += weights[i] * unit + gap;
        return { x, globalTop() + 10 * scale, weights[index] * unit, 34 * scale };
    }

    juce::Rectangle<float> linkedPanel() const
    {
        const float w = juce::jmin (820 * scale, plotRight() - 24 * scale), h = 230 * scale;
        return { (plotRight() - w) * 0.5f, globalTop() - h - 42 * scale, w, h };
    }

    /** 右上角设置按钮展开的唯一新版「四包正交组合 + MIDI Learn」菜单卡片（已移除旧 4 行主题菜单） */
    juce::Rectangle<float> packSwitcherCard() const
    {
        const float w = 340.0f * scale;
        const float h = 380.0f * scale;
        const float x = juce::jmax (10.0f * scale, width - w - 8.0f * scale);
        const float y = topHeight() + 4.0f * scale;
        return { x, y, w, h };
    }

    /**
     * 四包切换卡片内部按钮槽位：
     *   section 0 = Preset（2 项：0=Neumorphism Studio, 1=Classic Pro Dark）
     *   section 1 = Theme Pack（2 项：0=Classic, 1=Neumorphism）
     *   section 2 = Palette Pack（6 项：0..5 = Blue/Red/Cream/NeuLight/NeuTeal/NeuDark，3列×2行）
     *   section 3 = Layout Pack（2 项：0=Standard, 1=Sculpted）
     *   section 4 = Locale Pack（2 项：0=en-US, 1=zh-CN）
     *   section 5 = Rollback（1 项：0=Rollback Last Swap）
     *   section 6 = MIDI Learn（1 项：0=MIDI Learn Toggle）
     */
    juce::Rectangle<float> packOptionRect (int section, int optionIndex) const
    {
        const auto c = packSwitcherCard();
        const float pad = 12.0f * scale;
        const float innerW = c.getWidth() - pad * 2.0f;
        const float x0 = c.getX() + pad;
        const float y0 = c.getY();
        const float btnH = 24.0f * scale;

        if (section == 0)
        {
            const float cellW = (innerW - 6.0f * scale) * 0.5f;
            return { x0 + optionIndex * (cellW + 6.0f * scale), y0 + 36.0f * scale, cellW, 26.0f * scale };
        }
        if (section == 1)
        {
            const float cellW = (innerW - 6.0f * scale) * 0.5f;
            return { x0 + optionIndex * (cellW + 6.0f * scale), y0 + 82.0f * scale, cellW, btnH };
        }
        if (section == 2)
        {
            const int col = optionIndex % 3;
            const int row = optionIndex / 3;
            const float cellW = (innerW - 12.0f * scale) / 3.0f;
            return { x0 + col * (cellW + 6.0f * scale),
                     y0 + (128.0f + row * 28.0f) * scale,
                     cellW, btnH };
        }
        if (section == 3)
        {
            const float cellW = (innerW - 6.0f * scale) * 0.5f;
            return { x0 + optionIndex * (cellW + 6.0f * scale), y0 + 204.0f * scale, cellW, btnH };
        }
        if (section == 4)
        {
            const float cellW = (innerW - 6.0f * scale) * 0.5f;
            return { x0 + optionIndex * (cellW + 6.0f * scale), y0 + 250.0f * scale, cellW, btnH };
        }
        if (section == 5)
            return { x0, y0 + 294.0f * scale, innerW, 28.0f * scale };

        // section == 6: MIDI Learn
        return { x0, y0 + 332.0f * scale, innerW, 28.0f * scale };
    }
};
