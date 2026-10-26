#pragma once

#include <JuceHeader.h>
#include <array>
#include <unordered_map>
#include <vector>

// ============================================================
// LocaleRuntime — 语言包契约与运行时（对齐 natalia-cli LocaleRuntime）
//   · 每个语言包提供 { id, code, label, dictionary }
//   · 严格要求所有 SHIPPED_LANGUAGE_PACKS 的键集合 100% 对齐且非空
//   · 支持回退链（当前语言 -> en-US -> 键名本身）
// ============================================================

namespace UiPack
{
    enum class LocaleId : int
    {
        EnUS = 0,   // 默认英文（保持原像素/宽度断言兼容）
        ZhCN = 1    // 简体中文（自动启用 CJK 字体回退链）
    };

    inline constexpr int kNumLocales = 2;

    struct LocalePack
    {
        LocaleId     id = LocaleId::EnUS;
        juce::String code;      // "en-US" / "zh-CN"
        juce::String label;     // "English" / "简体中文"
        bool         isCjk = false;
        std::unordered_map<juce::String, juce::String> entries;

        juce::String get (const juce::String& key) const
        {
            const auto it = entries.find (key);
            if (it != entries.end() && it->second.isNotEmpty())
                return it->second;
            return key;
        }
    };
} // namespace UiPack
