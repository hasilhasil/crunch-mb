#pragma once

#include "../../core/LocaleRuntime.h"

// ============================================================
// BuiltInLocales — 内置双语语言包（en-US / zh-CN，键集 100% 对齐）
// ============================================================

namespace UiPack
{
    inline const LocalePack& buildLocalePack (LocaleId id)
    {
        static const LocalePack enPack = [] {
            LocalePack p;
            p.id    = LocaleId::EnUS;
            p.code  = "en-US";
            p.label = "English";
            p.isCjk = false;
            p.entries = {
                { "band.low",             "LOW" },
                { "band.lowMid",          "LOW MID" },
                { "band.highMid",         "HIGH MID" },
                { "band.high",            "HIGH" },
                { "top.linkedControl",    "LINKED CONTROL" },
                { "top.autoRange",        "Auto" },
                { "global.phase",         "PHASE" },
                { "global.slope",         "SLOPE" },
                { "global.quality",       "QUALITY" },
                { "global.lookahead",     "LOOKAHEAD" },
                { "global.oversampling",  "OVERSAMPLING" },
                { "global.analyzer",      "Analyzer" },
                { "global.outPrefix",     "OUT " },
                { "global.bypass",        "BYPASS" },
                { "knob.threshold",       "THRESHOLD" },
                { "knob.range",           "RANGE" },
                { "knob.attack",          "ATTACK" },
                { "knob.release",         "RELEASE" },
                { "knob.output",          "OUTPUT" },
                { "knob.makeup",          "MAKEUP" },
                { "knob.mix",             "MIX" },
                { "knob.inputGain",       "INPUT GAIN" },
                { "knob.outputGain",      "OUTPUT GAIN" },
                { "pop.compress",         "COMPRESS" },
                { "pop.expand",           "EXPAND" },
                { "pop.ratio",            "RATIO" },
                { "pop.knee",             "KNEE" },
                { "pop.sidechain",        "SIDECHAIN" },
                { "pop.int",              "INT" },
                { "pop.ext",              "EXT" },
                { "pop.expert",           "EXPERT" },
                { "pop.detector",         "DETECTOR" },
                { "pop.lookahead",        "LOOKAHEAD" },
                { "pop.scFiltering",      "SC FILTERING" },
                { "pop.freqFreeMode",     "FREQUENCY (FREE MODE)" },
                { "pop.highPass",         "HIGH-PASS" },
                { "pop.lowPass",          "LOW-PASS" },
                { "pop.peak",             "Peak" },
                { "pop.rms",              "RMS" },
                { "pop.off",              "Off" },
                { "pop.band",             "Band" },
                { "pop.free",             "Free" },
                { "linked.title",         "LINKED CONTROL" },
                { "linked.relativeFooter","THRESHOLD / ATTACK / RELEASE / MAKEUP / RANGE  |  RELATIVE OFFSET" },
                { "linked.mixFooter",     "MIX  |  ALL BANDS" },
                { "analyzer.title",       "ANALYZER" },
                { "analyzer.resolution",  "RESOLUTION" },
                { "analyzer.speed",       "SPEED" },
                { "analyzer.tilt",        "TILT (dB/oct)" },
                { "analyzer.pre",         "Pre spectrum" },
                { "analyzer.post",        "Post spectrum" },
                { "analyzer.low1024",     "Low 1024" },
                { "analyzer.med2048",     "Medium 2048" },
                { "analyzer.high4096",    "High 4096" },
                { "analyzer.max8192",     "Max 8192" },
                { "analyzer.fast",        "Fast" },
                { "analyzer.medium",      "Medium" },
                { "analyzer.slow",        "Slow" },
                { "analyzer.off",         "Off" },
                { "analyzer.freeze",      "Freeze" },
                { "output.title",         "OUTPUT" },
                { "output.globalBypass",  "Global Bypass" },
                { "output.autoGain",      "Auto Gain" },
                { "output.monitor",       "Monitor" },
                { "output.on",            "ON" },
                { "output.off",           "OFF" },
                { "freq.hint",            "Hz  (Enter confirm / Esc cancel)" },
                { "pack.title",           "UI COMPOSITION PACKS" },
                { "pack.presetNeu",       "Preset: Neumorphism Studio" },
                { "pack.presetClassic",   "Preset: Classic Pro Dark" },
                { "pack.theme",           "THEME" },
                { "pack.palette",         "PALETTE" },
                { "pack.layout",          "LAYOUT" },
                { "pack.locale",          "LANGUAGE" },
                { "pack.rollback",        "Rollback Last Swap" },
                { "pack.theme.classic",   "Classic" },
                { "pack.theme.neu",       "Neumorphism" },
                { "pack.layout.standard", "Standard" },
                { "pack.layout.sculpted", "Sculpted" },
                { "pack.midiLearnOn",     "MIDI Learn: On" },
                { "pack.midiLearnOff",    "MIDI Learn: Off" }
            };
            return p;
        }();

        static const LocalePack zhPack = [] {
            LocalePack p;
            p.id    = LocaleId::ZhCN;
            p.code  = "zh-CN";
            p.label = juce::String::fromUTF8 ("\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87");
            p.isCjk = true;
            auto u8 = [] (const char* s) { return juce::String::fromUTF8 (s); };
            p.entries = {
                { "band.low",             u8 ("低频") },
                { "band.lowMid",          u8 ("中低频") },
                { "band.highMid",         u8 ("中高频") },
                { "band.high",            u8 ("高频") },
                { "top.linkedControl",    u8 ("四段联动控制") },
                { "top.autoRange",        u8 ("自动") },
                { "global.phase",         u8 ("相位") },
                { "global.slope",         u8 ("斜率") },
                { "global.quality",       u8 ("质量") },
                { "global.lookahead",     u8 ("前瞻") },
                { "global.oversampling",  u8 ("过采样") },
                { "global.analyzer",      u8 ("频谱仪") },
                { "global.outPrefix",     u8 ("输出 ") },
                { "global.bypass",        u8 ("旁通") },
                { "knob.threshold",       u8 ("阈值") },
                { "knob.range",           u8 ("范围") },
                { "knob.attack",          u8 ("起音") },
                { "knob.release",         u8 ("释音") },
                { "knob.output",          u8 ("输出增益") },
                { "knob.makeup",          u8 ("补偿增益") },
                { "knob.mix",             u8 ("干湿比") },
                { "knob.inputGain",       u8 ("输入增益") },
                { "knob.outputGain",      u8 ("输出增益") },
                { "pop.compress",         u8 ("压缩") },
                { "pop.expand",           u8 ("扩展") },
                { "pop.ratio",            u8 ("比率") },
                { "pop.knee",             u8 ("拐点") },
                { "pop.sidechain",        u8 ("侧链源") },
                { "pop.int",              u8 ("内部") },
                { "pop.ext",              u8 ("外部") },
                { "pop.expert",           u8 ("专家") },
                { "pop.detector",         u8 ("检测器") },
                { "pop.lookahead",        u8 ("全局前瞻") },
                { "pop.scFiltering",      u8 ("侧链滤波") },
                { "pop.freqFreeMode",     u8 ("自由滤波范围 (FREE)") },
                { "pop.highPass",         u8 ("高通截止") },
                { "pop.lowPass",          u8 ("低通截止") },
                { "pop.peak",             u8 ("峰值") },
                { "pop.rms",              u8 ("均方根") },
                { "pop.off",              u8 ("关") },
                { "pop.band",             u8 ("本段") },
                { "pop.free",             u8 ("自由") },
                { "linked.title",         u8 ("四段联动控制") },
                { "linked.relativeFooter",u8 ("阈值 / 起音 / 释音 / 补偿 / 范围  |  相对偏移") },
                { "linked.mixFooter",     u8 ("干湿比  |  全频段统一") },
                { "analyzer.title",       u8 ("频谱分析仪") },
                { "analyzer.resolution",  u8 ("FFT 分辨率") },
                { "analyzer.speed",       u8 ("衰减速度") },
                { "analyzer.tilt",        u8 ("频谱倾斜 (dB/oct)") },
                { "analyzer.pre",         u8 ("处理前频谱") },
                { "analyzer.post",        u8 ("处理后频谱") },
                { "analyzer.low1024",     u8 ("低 1024") },
                { "analyzer.med2048",     u8 ("中 2048") },
                { "analyzer.high4096",    u8 ("高 4096") },
                { "analyzer.max8192",     u8 ("极高 8192") },
                { "analyzer.fast",        u8 ("快") },
                { "analyzer.medium",      u8 ("中") },
                { "analyzer.slow",        u8 ("慢") },
                { "analyzer.off",         u8 ("关") },
                { "analyzer.freeze",      u8 ("冻结画面") },
                { "output.title",         u8 ("全局输出与监听") },
                { "output.globalBypass",  u8 ("全局旁通") },
                { "output.autoGain",      u8 ("自动响度补偿") },
                { "output.monitor",       u8 ("监听信号") },
                { "output.on",            u8 ("开") },
                { "output.off",           u8 ("关") },
                { "freq.hint",            u8 ("Hz  (Enter 确认 / Esc 取消)") },
                { "pack.title",           u8 ("四包正交 UI 组合中心") },
                { "pack.presetNeu",       u8 ("一键新拟物：Neumorphism 浅色") },
                { "pack.presetClassic",   u8 ("一键经典：Classic 深色") },
                { "pack.theme",           u8 ("主题包") },
                { "pack.palette",         u8 ("配色包") },
                { "pack.layout",          u8 ("布局包") },
                { "pack.locale",          u8 ("语言包") },
                { "pack.rollback",        u8 ("原子回滚上一次组合") },
                { "pack.theme.classic",   u8 ("经典平面") },
                { "pack.theme.neu",       u8 ("新拟物") },
                { "pack.layout.standard", u8 ("标准全出血") },
                { "pack.layout.sculpted", u8 ("新拟物内凹槽") },
                { "pack.midiLearnOn",     u8 ("MIDI 学习: 开") },
                { "pack.midiLearnOff",    u8 ("MIDI 学习: 关") }
            };
            return p;
        }();

        return (id == LocaleId::ZhCN) ? zhPack : enPack;
    }

    inline const std::array<LocaleId, kNumLocales>& shippedLocaleIds() noexcept
    {
        static const std::array<LocaleId, kNumLocales> ids = {
            LocaleId::EnUS, LocaleId::ZhCN
        };
        return ids;
    }
} // namespace UiPack
