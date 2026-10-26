#pragma once

#include "PaletteContract.h"
#include "ThemeContract.h"

// ============================================================
// NeuPainter — 新拟物（Neumorphism）双阴影立体表面绘制原语
//
// 严格执行 neumorphism-SKILL.md 的物理规则：
//   1. 固定左上光源（Fixed Illuminant）：
//      负偏移 (-X, -Y) 恒用 pal.shadowLight，正偏移 (+X, +Y) 恒用 pal.shadowDark
//   2. Hover Shadowing（悬停遮光）：
//      hovered 时使用 theme.shadowHover（偏移缩小），绝不放大阴影、绝不 translate 位移
//   3. Extrude to Intrude（外凸转内凹）：
//      pressed / active 时从外凸双阴影直接切换为内凹双阴影（inset），位置保持原位
//   4. Focus Relaxation（聚焦松弛）：
//      输入框 focused 时使用 theme.insetFocus（内凹深度从 5px 减至 2px）
//   5. 零颜色字面量：所有色彩均从 PaletteTokens 语义槽位读取
// ============================================================

namespace UiPack
{
    struct NeuPainter
    {
        /** 绘制外凸（Extruded / Raised）圆角矩形：右下暗影 + 左上高光 + 同色系底面 */
        static void drawRaisedRect (juce::Graphics& g,
                                    juce::Rectangle<float> rect,
                                    float cornerRadius,
                                    const PaletteTokens& pal,
                                    const DualShadowSpec& spec,
                                    float scale = 1.0f,
                                    float opacity = 1.0f,
                                    bool fillAccent = false)
        {
            const float d    = spec.offset * scale;
            const float blur = spec.blur   * scale;
            const int   passes = 4;

            if (d > 0.1f && opacity > 0.01f)
            {
                for (int i = passes; i >= 1; --i)
                {
                    const float t      = (float) i / (float) passes;
                    const float expand = blur * 0.36f * t;
                    const float shift  = d * (0.45f + 0.55f * t);
                    const float weight = (1.0f - (t - 0.25f) * 0.65f) / (float) passes;

                    // 左上高光（-shift, -shift）
                    if (spec.lightAlpha > 0.001f)
                    {
                        g.setColour (pal.shadowLight.withMultipliedAlpha (spec.lightAlpha * weight * opacity));
                        g.fillRoundedRectangle (rect.translated (-shift, -shift).expanded (expand),
                                                cornerRadius + expand * 0.6f);
                    }
                    // 右下暗影（+shift, +shift）
                    if (spec.darkAlpha > 0.001f)
                    {
                        g.setColour (pal.shadowDark.withMultipliedAlpha (spec.darkAlpha * weight * opacity));
                        g.fillRoundedRectangle (rect.translated (shift, shift).expanded (expand),
                                                cornerRadius + expand * 0.6f);
                    }
                }
            }

            // 本体填充（与背景同色系，或 Accent 按钮）
            g.setColour ((fillAccent ? pal.accent : pal.bg).withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (rect, cornerRadius);

            // 135° 柔和塑形微光泽（左上 bgLight -> 右下 bg）
            if (! fillAccent)
            {
                juce::ColourGradient sheen (pal.bgLight.withMultipliedAlpha (0.38f * opacity),
                                            rect.getX(), rect.getY(),
                                            pal.bg.withMultipliedAlpha (0.0f),
                                            rect.getRight(), rect.getBottom(), false);
                g.setGradientFill (sheen);
                g.fillRoundedRectangle (rect, cornerRadius);
            }
        }

        /** 绘制内凹（Intruded / Inset）圆角矩形：裁剪内壁左上暗影 + 右下亮高光 */
        static void drawInsetRect (juce::Graphics& g,
                                   juce::Rectangle<float> rect,
                                   float cornerRadius,
                                   const PaletteTokens& pal,
                                   const DualShadowSpec& spec,
                                   float scale = 1.0f,
                                   float opacity = 1.0f,
                                   bool useWellTint = false)
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (rect, cornerRadius);
            g.reduceClipRegion (clip);

            // 凹槽底面（同色系或微沉底色）
            const juce::Colour base = useWellTint
                ? pal.bg.interpolatedWith (pal.track, 0.28f)
                : pal.bg;
            g.setColour (base.withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (rect, cornerRadius);

            const float d    = juce::jmax (1.0f, spec.offset * scale);
            const float blur = juce::jmax (2.0f, spec.blur   * scale);
            const int   passes = 5;

            // 数学精确的 CSS inset 阴影：
            //   将外扩 strokeW*0.5 的圆角框向 (+shift,+shift) 平移后描边，
            //   其左/上内壁精确探入 rect 内部 shift 像素，而右/下内壁远离 rect 零侵入；
            //   向 (-shift,-shift) 平移则精确照亮右/下内壁。
            for (int i = 1; i <= passes; ++i)
            {
                const float t       = (float) i / (float) passes;
                const float shift   = d * t;
                const float strokeW = blur * 1.35f + shift * 2.0f;
                const float w       = (1.15f - 0.65f * t) / (float) passes;
                const auto  outer   = rect.expanded (strokeW * 0.5f);

                if (spec.darkAlpha > 0.001f)
                {
                    g.setColour (pal.insetDark.withMultipliedAlpha (spec.darkAlpha * w * opacity));
                    g.drawRoundedRectangle (outer.translated (shift, shift),
                                            cornerRadius + strokeW * 0.35f, strokeW);
                }
                if (spec.lightAlpha > 0.001f)
                {
                    g.setColour (pal.insetLight.withMultipliedAlpha (spec.lightAlpha * w * opacity));
                    g.drawRoundedRectangle (outer.translated (-shift, -shift),
                                            cornerRadius + strokeW * 0.35f, strokeW);
                }
            }
        }

        /** 绘制按钮/胶囊（自动处理 Raised -> Hover Shadowing -> Pressed/Active Inset） */
        static void drawButton (juce::Graphics& g,
                                juce::Rectangle<float> rect,
                                const PaletteTokens& pal,
                                const ThemeTokens& theme,
                                bool activeOrPressed,
                                bool hovered,
                                float scale = 1.0f,
                                float opacity = 1.0f,
                                float customRadius = -1.0f)
        {
            const float radius = (customRadius >= 0.0f) ? customRadius : (theme.radiusMd * scale);
            if (! theme.isNeumorphic)
            {
                g.setColour ((activeOrPressed ? pal.accent.withAlpha (0.24f) : pal.track)
                             .withMultipliedAlpha (opacity));
                g.fillRoundedRectangle (rect, radius);
                if (theme.borderWidth > 0.0f)
                {
                    g.setColour ((activeOrPressed ? pal.accent : pal.gridZ).withMultipliedAlpha (opacity));
                    g.drawRoundedRectangle (rect, radius, theme.borderWidth * scale);
                }
                return;
            }

            // Neumorphism 按钮物理：
            //   · activeOrPressed -> Extrude to Intrude（直接转为内凹 insetButton，零位移）
            //   · hovered         -> Hover Shadowing（外阴影从 shadowButton 缩小到 shadowHover）
            //   · default         -> 外凸 shadowButton
            if (activeOrPressed)
            {
                drawInsetRect (g, rect, radius, pal, theme.insetButton, scale, opacity, false);
                g.setColour (pal.accent.withMultipliedAlpha (0.14f * opacity));
                g.fillRoundedRectangle (rect.reduced (1.5f * scale), juce::jmax (2.0f, radius - 1.5f * scale));
            }
            else
            {
                const auto& spec = hovered ? theme.shadowHover : theme.shadowButton;
                drawRaisedRect (g, rect, radius, pal, spec, scale, opacity, false);
            }
        }

        /** 绘制圆形外凸表面（用于 neu-circle 旋钮帽与圆点） */
        static void drawRaisedCircle (juce::Graphics& g,
                                      juce::Rectangle<float> circleBounds,
                                      const PaletteTokens& pal,
                                      const DualShadowSpec& spec,
                                      float scale = 1.0f)
        {
            const float r = circleBounds.getWidth() * 0.5f;
            drawRaisedRect (g, circleBounds, r, pal, spec, scale, 1.0f, false);
        }

        /** 绘制圆形内凹槽（用于旋钮外环槽） */
        static void drawInsetCircle (juce::Graphics& g,
                                     juce::Rectangle<float> circleBounds,
                                     const PaletteTokens& pal,
                                     const DualShadowSpec& spec,
                                     float scale = 1.0f)
        {
            const float r = circleBounds.getWidth() * 0.5f;
            drawInsetRect (g, circleBounds, r, pal, spec, scale, 1.0f, true);
        }
    };
} // namespace UiPack
