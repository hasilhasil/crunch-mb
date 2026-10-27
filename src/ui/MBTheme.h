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

    // 全局统一字体：Google Sans Flex（Regular / Medium / SemiBold / Bold）
    inline juce::Typeface::Ptr regular()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::GoogleSansFlexRegular_ttf,
                                                     BinaryData::GoogleSansFlexRegular_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr medium()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::GoogleSansFlexMedium_ttf,
                                                     BinaryData::GoogleSansFlexMedium_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr semiBold()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::GoogleSansFlexSemiBold_ttf,
                                                     BinaryData::GoogleSansFlexSemiBold_ttfSize);
        return face;
    }
    inline juce::Typeface::Ptr bold()
    {
        static const juce::Typeface::Ptr face =
            juce::Typeface::createSystemTypefaceFor (BinaryData::GoogleSansFlexBold_ttf,
                                                     BinaryData::GoogleSansFlexBold_ttfSize);
        return face;
    }

    inline juce::Typeface::Ptr uiRegular()
    {
        return regular();
    }
    inline juce::Typeface::Ptr uiMedium()
    {
        return medium();
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
        float h = height * textScaleRef();
        juce::Typeface::Ptr primary = (face != nullptr) ? face : regular();
        int effectiveStyle = style;

        if (cjkModeRef())
        {
            // 中文模式（zh-CN）视觉字面与字重校准（保持 Google Sans Flex 主字体，不显式改用微软雅黑）：
            //   1. 汉字方块字面（~90% em）比西文大写（~71% em）偏大，统一按 0.92x 收敛并设 11.2f 清晰度下限；
            //   2. 收敛过度跳跃的粗体样式，使各面板标签与按钮的笔画胖瘦匀称统一。
            h = juce::jmax (11.2f, h * 0.92f);
            if (primary == bold() || primary == semiBold())
                primary = medium();
            effectiveStyle &= ~juce::Font::bold;
        }
        else
        {
            h = juce::jmax (6.0f, h);
        }

        if (primary != nullptr)
            return juce::Font (juce::FontOptions (h, effectiveStyle).withTypeface (primary).withFallbackEnabled (true));
        return juce::Font (juce::FontOptions (h, effectiveStyle).withFallbackEnabled (true));
    }

    /** 左上角 CRUNCH MB 品牌标题专用字体：不受语言包缩放或字重校准影响，始终保持原样 */
    inline juce::Font brandTitleFont (float height)
    {
        const float h = juce::jmax (6.0f, height * textScaleRef());
        const auto primary = uiMedium();
        if (primary != nullptr)
            return juce::Font (juce::FontOptions (h, juce::Font::plain).withTypeface (primary).withFallbackEnabled (true));
        return juce::Font (juce::FontOptions (h, juce::Font::plain));
    }
} // namespace MBTheme
