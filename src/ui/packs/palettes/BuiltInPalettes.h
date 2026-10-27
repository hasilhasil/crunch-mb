#pragma once

#include "../../core/PaletteContract.h"

// ============================================================
// BuiltInPalettes — 内置 6 套可原子替换配色包
//   · Blue / Red / Cream（保留经典三套配色，并补齐双阴影语义槽位）
//   · NeuLight / NeuTeal / NeuDark（来自 neumorphism-style-pack）
// ============================================================

namespace UiPack
{
    namespace detail
    {
        inline juce::Colour hex (juce::uint32 argb) noexcept { return juce::Colour (argb); }
        inline juce::Colour rgba (int r, int g, int b, double a) noexcept
        {
            return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b,
                                 (juce::uint8) juce::jlimit (0, 255, (int) std::lround (a * 255.0)));
        }
    }

    inline PaletteTokens buildPalettePack (PaletteId id)
    {
        using detail::hex;
        using detail::rgba;
        PaletteTokens p;

        switch (id)
        {
            case PaletteId::Red:
                p.id          = "red";
                p.name        = "Classic Red";
                p.bg          = hex (0xff101214);
                p.bgLight     = hex (0xff181b1f);
                p.panel       = hex (0xff1a1d20);
                p.surface     = hex (0xff22262b);
                p.surfaceA    = rgba (34, 38, 43, 0.84);
                p.popA        = rgba (34, 38, 43, 0.62);
                p.track       = hex (0xff2a2e33);
                p.knob        = hex (0xfff2efe8);
                p.text        = hex (0xffd8d8d8);
                p.dim         = hex (0xff9aa0a6);
                p.grid        = hex (0xff22262b);
                p.gridZ       = hex (0xff3c4249);
                p.meterBg     = hex (0xff16181b);
                p.inFill      = hex (0xff6a7076);
                p.outLevel    = hex (0xffe8eaed);
                p.accent      = hex (0xffe05b5b);
                p.accent2     = hex (0xffff6b6b);
                p.accent3     = hex (0xff4ecdc4);
                p.accent4     = hex (0xffffc86b);
                p.onAccent    = hex (0xffffffff);
                p.halo        = rgba (0, 0, 0, 0.65);
                p.shadowDark  = rgba (0, 0, 0, 0.62);
                p.shadowLight = rgba (255, 255, 255, 0.07);
                p.insetDark   = rgba (0, 0, 0, 0.68);
                p.insetLight  = rgba (255, 255, 255, 0.08);
                p.light       = false;
                break;

            case PaletteId::Cream:
                p.id          = "cream";
                p.name        = "Classic Cream";
                p.bg          = hex (0xfff6f0e7);
                p.bgLight     = hex (0xfffffdf8);
                p.panel       = hex (0xffefe8db);
                p.surface     = hex (0xfffffdf8);
                p.surfaceA    = rgba (255, 253, 248, 0.86);
                p.popA        = rgba (255, 253, 248, 0.68);
                p.track       = hex (0xffded4c4);
                p.knob        = hex (0xffffffff);
                p.text        = hex (0xff463f38);
                p.dim         = hex (0xff8d8375);
                p.grid        = hex (0xffe7dfcf);
                p.gridZ       = hex (0xffc8bdac);
                p.meterBg     = hex (0xfff2ebdf);
                p.inFill      = hex (0xffb5aa98);
                p.outLevel    = hex (0xff703c2b);
                p.accent      = hex (0xffe07856);
                p.accent2     = hex (0xffff6b6b);
                p.accent3     = hex (0xff4ecdc4);
                p.accent4     = hex (0xffffc86b);
                p.onAccent    = hex (0xffffffff);
                p.halo        = rgba (255, 255, 255, 0.70);
                p.shadowDark  = rgba (186, 174, 156, 0.72);
                p.shadowLight = rgba (255, 255, 255, 0.92);
                p.insetDark   = rgba (176, 164, 146, 0.75);
                p.insetLight  = rgba (255, 255, 255, 0.90);
                p.light       = true;
                break;

            case PaletteId::NeuLight:
                // 严格对齐 neumorphism-globals.css & neumorphism-tokens.json：
                //   --neu-bg: #e0e5ec, --neu-bg-light: #f0f0f3, secondary: #d1d9e6
                //   --neu-shadow-dark: #b8bcc2, --neu-shadow-light: #ffffff
                //   --neu-accent: #6d5dfc, --neu-text: #333333, --neu-text-muted: #6b7280
                p.id          = "neu-light";
                p.name        = "Neumorphism Violet";
                p.bg          = hex (0xffe0e5ec);
                p.bgLight     = hex (0xfff0f0f3);
                p.panel       = hex (0xffd1d9e6);
                p.surface     = hex (0xffe0e5ec);
                p.surfaceA    = rgba (224, 229, 236, 0.86);
                p.popA        = rgba (224, 229, 236, 0.68);
                p.track       = hex (0xffd1d9e6);
                p.knob        = hex (0xffe0e5ec);
                p.text        = hex (0xff333333);
                p.dim         = hex (0xff6b7280);
                p.grid        = hex (0xffcfd6e0);
                p.gridZ       = hex (0xffb8bcc2);
                p.meterBg     = hex (0xffd5dce6);
                p.inFill      = hex (0xff8b95a5);
                p.outLevel    = hex (0xff4ecdc4);
                p.accent      = hex (0xff6d5dfc);
                p.accent2     = hex (0xffff6b6b);
                p.accent3     = hex (0xff4ecdc4);
                p.accent4     = hex (0xffffb84d);
                p.onAccent    = hex (0xffffffff);
                p.halo        = rgba (184, 188, 194, 0.55);
                p.shadowDark  = hex (0xffb8bcc2);
                p.shadowLight = hex (0xffffffff);
                p.insetDark   = hex (0xffb8bec7);
                p.insetLight  = hex (0xffffffff);
                p.light       = true;
                break;

            case PaletteId::NeuTeal:
                // neumorphism-tokens.json accent-3 (#4ecdc4 / 深青对比增强) + #e0e5ec 底色
                p.id          = "neu-teal";
                p.name        = "Neumorphism Teal";
                p.bg          = hex (0xffe0e5ec);
                p.bgLight     = hex (0xfff0f0f3);
                p.panel       = hex (0xffd1d9e6);
                p.surface     = hex (0xffe0e5ec);
                p.surfaceA    = rgba (224, 229, 236, 0.86);
                p.popA        = rgba (224, 229, 236, 0.68);
                p.track       = hex (0xffd1d9e6);
                p.knob        = hex (0xffe0e5ec);
                p.text        = hex (0xff2b3440);
                p.dim         = hex (0xff64748b);
                p.grid        = hex (0xffcfd6e0);
                p.gridZ       = hex (0xffb8bcc2);
                p.meterBg     = hex (0xffd5dce6);
                p.inFill      = hex (0xff8b95a5);
                p.outLevel    = hex (0xff6d5dfc);
                p.accent      = hex (0xff14b8a6);
                p.accent2     = hex (0xffff6b6b);
                p.accent3     = hex (0xff4ecdc4);
                p.accent4     = hex (0xffffb84d);
                p.onAccent    = hex (0xffffffff);
                p.halo        = rgba (184, 188, 194, 0.55);
                p.shadowDark  = hex (0xffb8bec7);
                p.shadowLight = hex (0xffffffff);
                p.insetDark   = hex (0xffb8bec7);
                p.insetLight  = hex (0xffffffff);
                p.light       = true;
                break;

            case PaletteId::NeuDark:
                // 暗色新拟物（对齐 natalia-cli darkPalette 双阴影层次）
                p.id          = "neu-dark";
                p.name        = "Neumorphism Dark";
                p.bg          = hex (0xff1e2229);
                p.bgLight     = hex (0xff262b34);
                p.panel       = hex (0xff181b21);
                p.surface     = hex (0xff1e2229);
                p.surfaceA    = rgba (30, 34, 41, 0.84);
                p.popA        = rgba (30, 34, 41, 0.62);
                p.track       = hex (0xff171a20);
                p.knob        = hex (0xff222730);
                p.text        = hex (0xffe2e8f0);
                p.dim         = hex (0xff94a3b8);
                p.grid        = hex (0xff262b34);
                p.gridZ       = hex (0xff363d4a);
                p.meterBg     = hex (0xff16191f);
                p.inFill      = hex (0xff64748b);
                p.outLevel    = hex (0xff4ecdc4);
                p.accent      = hex (0xff7c6dff);
                p.accent2     = hex (0xffff6b6b);
                p.accent3     = hex (0xff4ecdc4);
                p.accent4     = hex (0xffffc86b);
                p.onAccent    = hex (0xffffffff);
                p.halo        = rgba (0, 0, 0, 0.65);
                p.shadowDark  = hex (0xff111418);
                p.shadowLight = hex (0xff262c36);
                p.insetDark   = hex (0xff101317);
                p.insetLight  = hex (0xff252b35);
                p.light       = false;
                break;

            case PaletteId::Blue:
            default:
                p.id          = "blue";
                p.name        = "Classic Blue";
                p.bg          = hex (0xff101214);
                p.bgLight     = hex (0xff181b1f);
                p.panel       = hex (0xff1a1d20);
                p.surface     = hex (0xff22262b);
                p.surfaceA    = rgba (34, 38, 43, 0.84);
                p.popA        = rgba (34, 38, 43, 0.62);
                p.track       = hex (0xff2a2e33);
                p.knob        = hex (0xfff2efe8);
                p.text        = hex (0xffd8d8d8);
                p.dim         = hex (0xff9aa0a6);
                p.grid        = hex (0xff22262b);
                p.gridZ       = hex (0xff3c4249);
                p.meterBg     = hex (0xff16181b);
                p.inFill      = hex (0xff6a7076);
                p.outLevel    = hex (0xffe8eaed);
                p.accent      = hex (0xff2f9dff);
                p.accent2     = hex (0xffff6b6b);
                p.accent3     = hex (0xff4ecdc4);
                p.accent4     = hex (0xffffc86b);
                p.onAccent    = hex (0xffffffff);
                p.halo        = rgba (0, 0, 0, 0.65);
                p.shadowDark  = rgba (0, 0, 0, 0.62);
                p.shadowLight = rgba (255, 255, 255, 0.07);
                p.insetDark   = rgba (0, 0, 0, 0.68);
                p.insetLight  = rgba (255, 255, 255, 0.08);
                p.light       = false;
                break;
        }
        return p;
    }

    inline const std::array<PaletteId, kNumPalettes>& shippedPaletteIds() noexcept
    {
        static const std::array<PaletteId, kNumPalettes> ids = {
            PaletteId::Blue, PaletteId::Red, PaletteId::Cream,
            PaletteId::NeuLight, PaletteId::NeuTeal, PaletteId::NeuDark
        };
        return ids;
    }
} // namespace UiPack
