#pragma once

#include <JuceHeader.h>

#include <BinaryData.h>

// ============================================================
// MBTheme — 三主题色板（mockup v9 CSS 变量逐值对应，UI_SPEC §9）
//   Blue / Red / Cream；统一 accent 色（D14：无每段独立色）
// ============================================================
namespace MBTheme
{
    enum class Theme { Blue = 0, Red = 1, Cream = 2 };

    struct Palette
    {
        juce::Colour bg, panel, surface, surfaceA, popA, track, knob, text, dim,
                     grid, gridZ, meterBg, inFill, outLevel, accent, halo;
        bool light = false;
    };

    inline juce::Colour col (juce::uint32 argb) { return juce::Colour (argb); }

    // mockup v9 CSS rgba() → 0xAARRGGBB（⚠️ 必须 8 位；多写一位会被截断成不透明）
    inline juce::Colour rgba (int r, int g, int b, double a)
    {
        return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b,
                             (juce::uint8) juce::jlimit (0, 255, (int) std::lround (a * 255.0)));
    }

    inline Palette palette (Theme t)
    {
        Palette p;
        switch (t)
        {
            case Theme::Red:
                p.bg = col (0xff101214); p.panel = col (0xff1a1d20); p.surface = col (0xff22262b);
                p.surfaceA = rgba (34, 38, 43, 0.84); p.popA = rgba (34, 38, 43, 0.62);
                p.track = col (0xff2a2e33); p.knob = col (0xfff2efe8); p.text = col (0xffd8d8d8);
                p.dim = col (0xff9aa0a6); p.grid = col (0xff22262b); p.gridZ = col (0xff3c4249);
                p.meterBg = col (0xff16181b); p.inFill = col (0xff6a7076); p.outLevel = col (0xffe8eaed);
                p.accent = col (0xffe05b5b); p.halo = rgba (0, 0, 0, 0.65); p.light = false;
                break;
            case Theme::Cream:
                p.bg = col (0xfff6f0e7); p.panel = col (0xffefe8db); p.surface = col (0xfffffdf8);
                p.surfaceA = rgba (255, 253, 248, 0.86); p.popA = rgba (255, 253, 248, 0.68);
                p.track = col (0xffded4c4); p.knob = col (0xffffffff); p.text = col (0xff463f38);
                p.dim = col (0xff8d8375); p.grid = col (0xffe7dfcf); p.gridZ = col (0xffc8bdac);
                p.meterBg = col (0xfff2ebdf); p.inFill = col (0xffb5aa98); p.outLevel = col (0xff703c2b);
                p.accent = col (0xffe07856); p.halo = rgba (255, 255, 255, 0.70); p.light = true;
                break;
            case Theme::Blue:
            default:
                p.bg = col (0xff101214); p.panel = col (0xff1a1d20); p.surface = col (0xff22262b);
                p.surfaceA = rgba (34, 38, 43, 0.84); p.popA = rgba (34, 38, 43, 0.62);
                p.track = col (0xff2a2e33); p.knob = col (0xfff2efe8); p.text = col (0xffd8d8d8);
                p.dim = col (0xff9aa0a6); p.grid = col (0xff22262b); p.gridZ = col (0xff3c4249);
                p.meterBg = col (0xff16181b); p.inFill = col (0xff6a7076); p.outLevel = col (0xffe8eaed);
                p.accent = col (0xff2f9dff); p.halo = rgba (0, 0, 0, 0.65); p.light = false;
                break;
        }
        return p;
    }

    // Poppins 字重（BinaryData，来自兄弟项目 Fonts 目录）
    // ⚠️ 必须 static 缓存：createSystemTypefaceFor 每次都新建 Typeface 并驻留
    //   JUCE 的 TypefaceCache——早期每帧调 12+ 次，宿主里内存持续爬升（2026-09-23 实测）。
    inline juce::Typeface::Ptr regular()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf,
                                                     BinaryData::PoppinsRegular_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr medium()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf,
                                                     BinaryData::PoppinsMedium_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr semiBold()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsSemiBold_ttf,
                                                     BinaryData::PoppinsSemiBold_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr bold()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf,
                                                     BinaryData::PoppinsBold_ttfSize);
        return face;
    }

    inline juce::Font font (juce::Typeface::Ptr face, float height, int style = juce::Font::plain)
    {
        // JUCE 8：FontOptions(Typeface::Ptr) 只收类型指针，高度/样式经 withXxx 链式附加
        if (face != nullptr)
            return juce::Font (juce::FontOptions (height, style).withTypeface (face));
        return juce::Font (juce::FontOptions (height, style));
    }
} // namespace MBTheme
