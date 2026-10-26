#pragma once

#include "../../core/LayoutContract.h"

// ============================================================
// BuiltInLayouts — 内置 2 套可原子替换布局包
// ============================================================

namespace UiPack
{
    inline LayoutTokens buildLayoutPack (LayoutId id)
    {
        LayoutTokens l;
        switch (id)
        {
            case LayoutId::Sculpted:
                l.id               = "sculpted";
                l.name             = "Sculpted Well";
                l.topBarHeight     = 46.0f;
                l.globalBarHeight  = 54.0f;
                l.meterWidth       = 44.0f;
                l.plotInsetX       = 8.0f;
                l.plotInsetY       = 6.0f;
                l.plotCornerRadius = 16.0f;
                l.sunkenPlotWell   = true;
                break;

            case LayoutId::Standard:
            default:
                l.id               = "standard";
                l.name             = "Standard Full-Bleed";
                l.topBarHeight     = 46.0f;
                l.globalBarHeight  = 54.0f;
                l.meterWidth       = 44.0f;
                l.plotInsetX       = 0.0f;
                l.plotInsetY       = 0.0f;
                l.plotCornerRadius = 0.0f;
                l.sunkenPlotWell   = false;
                break;
        }
        return l;
    }

    inline const std::array<LayoutId, kNumLayouts>& shippedLayoutIds() noexcept
    {
        static const std::array<LayoutId, kNumLayouts> ids = {
            LayoutId::Standard, LayoutId::Sculpted
        };
        return ids;
    }
} // namespace UiPack
