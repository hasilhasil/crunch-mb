#pragma once

#include "../../core/ThemeContract.h"
#include <array>

// ============================================================
// BuiltInThemes — 内置 Theme 包（Classic vs Neumorphism）
//   严格遵循零颜色字面量守卫：此处仅包含几何、圆角、阴影偏移与动效常数
// ============================================================

namespace UiPack
{
    inline ThemeTokens buildThemePack (ThemeId id)
    {
        ThemeTokens t;
        switch (id)
        {
            case ThemeId::Neumorphism:
                // 严格对齐 D:\Aquama\下载\neumorphism-style-pack\neumorphism-SKILL.md & globals.css：
                //   · border-0（borderWidth = 0）
                //   · rounded-xl (12px) / rounded-2xl (16px)
                //   · 固定左上高光 (-X,-Y) / 右下暗影 (+X,+Y)
                //   · Hover Shadowing：hover 阴影小于常态按钮阴影（3.5px < 5.5px）
                //   · Extrude to Intrude：按下/激活直接切换为 inset 4px/8px，无 translate 位移
                //   · Input Focus：聚焦时内阴影从 5px 收缩至 2px
                //   · Smooth Molding：300ms 过渡
                t.id              = "neumorphism";
                t.name            = "Neumorphism";
                t.isNeumorphic    = true;
                t.radiusSm        = 8.0f;
                t.radiusMd        = 12.0f;
                t.radiusLg        = 16.0f;
                t.radiusXl        = 20.0f;
                t.radiusPill      = 999.0f;
                t.space1          = 4.0f;
                t.space2          = 8.0f;
                t.space3          = 12.0f;
                t.space4          = 16.0f;
                t.space6          = 24.0f;
                t.borderWidth     = 0.0f;
                t.cardBorderAlpha = 0.0f;
                t.surfaceSheen    = 0.05f;
                t.shadowBadge     = { 3.0f,  6.0f, 0.72f, 0.85f, false };
                t.shadowButton    = { 5.0f, 10.0f, 0.78f, 0.90f, false };
                t.shadowHover     = { 3.0f,  6.0f, 0.70f, 0.85f, false };
                t.shadowCard      = { 8.0f, 16.0f, 0.82f, 0.92f, false };
                t.insetButton     = { 4.0f,  8.0f, 0.80f, 0.88f, true  };
                t.insetInput      = { 5.0f, 10.0f, 0.82f, 0.90f, true  };
                t.insetFocus      = { 2.0f,  4.0f, 0.65f, 0.80f, true  };
                t.insetWell       = { 7.0f, 14.0f, 0.82f, 0.90f, true  };
                t.motionFastMs    = 200.0;
                t.motionBaseMs    = 300.0;
                break;

            case ThemeId::Classic:
            default:
                t.id              = "classic";
                t.name            = "Classic Flat";
                t.isNeumorphic    = false;
                t.radiusSm        = 4.0f;
                t.radiusMd        = 6.0f;
                t.radiusLg        = 12.0f;
                t.radiusXl        = 16.0f;
                t.radiusPill      = 999.0f;
                t.space1          = 4.0f;
                t.space2          = 8.0f;
                t.space3          = 12.0f;
                t.space4          = 16.0f;
                t.space6          = 24.0f;
                t.borderWidth     = 1.0f;
                t.cardBorderAlpha = 0.55f;
                t.surfaceSheen    = 0.0f;
                t.shadowBadge     = { 0.0f,  0.0f, 0.0f,  0.0f,  false };
                t.shadowButton    = { 0.0f,  0.0f, 0.0f,  0.0f,  false };
                t.shadowHover     = { 0.0f,  0.0f, 0.0f,  0.0f,  false };
                t.shadowCard      = { 6.0f, 14.0f, 0.35f, 0.0f,  false };
                t.insetButton     = { 0.0f,  0.0f, 0.0f,  0.0f,  true  };
                t.insetInput      = { 0.0f,  0.0f, 0.0f,  0.0f,  true  };
                t.insetFocus      = { 0.0f,  0.0f, 0.0f,  0.0f,  true  };
                t.insetWell       = { 0.0f,  0.0f, 0.0f,  0.0f,  true  };
                t.motionFastMs    = 160.0;
                t.motionBaseMs    = 220.0;
                break;
        }
        return t;
    }

    inline const std::array<ThemeId, kNumThemes>& shippedThemeIds() noexcept
    {
        static const std::array<ThemeId, kNumThemes> ids = {
            ThemeId::Classic, ThemeId::Neumorphism
        };
        return ids;
    }
} // namespace UiPack
