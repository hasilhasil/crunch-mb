#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>
#include "core/UiComposition.h"

// ============================================================
// MBTheme — 桥接门面（委托给 src/ui/core/ 四包正交架构）
//   · Palette 统一复用 UiPack::PaletteTokens（零重复定义）
//   · Theme 枚举覆盖 6 套内置配色包（Blue/Red/Cream/NeuLight/NeuTeal/NeuDark）
//   · 字体出口支持 CJK 语言包自动回退（zh-CN 下自动挂载系统微软雅黑等无衬线字体，杜绝方框乱码）
// ============================================================
namespace MBTheme
{
    enum class Theme : int
    {
        Blue     = (int) UiPack::PaletteId::Blue,
        Red      = (int) UiPack::PaletteId::Red,
        Cream    = (int) UiPack::PaletteId::Cream,
        NeuLight = (int) UiPack::PaletteId::NeuLight,
        NeuTeal  = (int) UiPack::PaletteId::NeuTeal,
        NeuDark  = (int) UiPack::PaletteId::NeuDark
    };

    using Palette = UiPack::PaletteTokens;

    inline Palette palette (Theme t)
    {
        return UiPack::buildPalettePack ((UiPack::PaletteId) t);
    }

    // Poppins 字重（BinaryData）
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

    inline juce::Typeface::Ptr uiRegular()
    {
        static const auto face = juce::Typeface::createSystemTypefaceFor (
            BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr uiMedium()
    {
        static const auto face = juce::Typeface::createSystemTypefaceFor (
            BinaryData::BarlowMedium_ttf, BinaryData::BarlowMedium_ttfSize);
        return face;
    }

    // ---- CJK 字体回退链（zh-CN 语言包激活时启用，防 Poppins/Barlow 缺中文字形）----
    inline juce::Typeface::Ptr cjkTypeface (bool boldWeight)
    {
        static const juce::Typeface::Ptr cjkReg = [] () -> juce::Typeface::Ptr
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (const char* cand : { "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", "SimHei" })
                if (names.contains (cand))
                    return juce::Font (juce::FontOptions (cand, 14.0f, juce::Font::plain)).getTypefacePtr();
            return nullptr;
        }();
        static const juce::Typeface::Ptr cjkBold = [] () -> juce::Typeface::Ptr
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (const char* cand : { "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", "SimHei" })
                if (names.contains (cand))
                    return juce::Font (juce::FontOptions (cand, 14.0f, juce::Font::bold)).getTypefacePtr();
            return nullptr;
        }();
        return boldWeight ? (cjkBold != nullptr ? cjkBold : cjkReg) : cjkReg;
    }

    inline bool& cjkModeRef() noexcept { static bool cjk = false; return cjk; }
    inline void  setCjkMode (bool on) noexcept { cjkModeRef() = on; }
    inline bool  isCjkMode() noexcept { return cjkModeRef(); }

    inline constexpr float kUiTextScale = 1.0f;
    inline float& textScaleRef() { static float s = 1.0f; return s; }
    inline void  setTextScale (float s) { textScaleRef() = juce::jlimit (0.5f, 3.0f, s); }
    inline float textScale() { return textScaleRef(); }

    inline juce::Font font (juce::Typeface::Ptr face, float height, int style = juce::Font::plain)
    {
        const float h = juce::jmax (6.0f, height * textScaleRef());
        if (cjkModeRef())
        {
            const bool wantBold = (style & juce::Font::bold) != 0
                               || face == medium() || face == semiBold()
                               || face == bold()   || face == uiMedium();
            if (auto cjk = cjkTypeface (wantBold))
                return juce::Font (juce::FontOptions (h, style).withTypeface (cjk));
        }
        if (face != nullptr)
            return juce::Font (juce::FontOptions (h, style).withTypeface (face));
        return juce::Font (juce::FontOptions (h, style));
    }
} // namespace MBTheme
