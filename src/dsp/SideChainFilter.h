#pragma once

#include <JuceHeader.h>

#include "BiquadFilter.h"

// ============================================================
// SideChainFilter — 侧链检测信号滤波（Pro-MB "Free" 模式对标，2026-10-14 新增）
//
// 作用位置：**只在检测通路上**（DETECTOR 之前），不影响音频输出。
//   · Band 模式（默认）：不滤波，该段由自身频段信号触发（分频器已限带）
//   · Free 模式：HP(lowHz) + LP(highHz)，让该段触发于完全不同的频率范围
//     （Pro-MB 官方："trigger on a completely different frequency range"）
//
// 阶数：HP/LP 各 2 阶（2 个 RBJ biquad 级联 = 12 dB/oct），与分频器 12 dB 档一致。
// 音频线程热路径零分配零锁（TECH_SPEC §4）。
// ============================================================
class SideChainFilter
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = juce::jmax (1.0, sampleRate);
        reset();
        applyCoefficients();
    }

    void reset()
    {
        for (auto& f : hp_) f.reset();
        for (auto& f : lp_) f.reset();
    }

    /** isFree=false 时直通（Band 模式）；系数变化不 reset（避免自动化爆音） */
    void setParameters (float lowHz, float highHz, bool isFree) noexcept
    {
        lowHz_  = lowHz;
        highHz_ = highHz;
        free_   = isFree;
        applyCoefficients();
    }

    /** 就地滤波检测信号（free_ == false 时零开销直返） */
    void process (float& left, float& right) noexcept
    {
        if (! free_)
            return;
        for (int i = 0; i < kSections; ++i)
        {
            left  = hp_[(size_t) i].processSample (left);
            right = hp_[(size_t) i].processSample (right);
        }
        for (int i = 0; i < kSections; ++i)
        {
            left  = lp_[(size_t) i].processSample (left);
            right = lp_[(size_t) i].processSample (right);
        }
    }

    bool isFree() const noexcept { return free_; }

private:
    static constexpr int kSections = 2;   // 2 阶 = 12 dB/oct

    void applyCoefficients() noexcept
    {
        const double sr = sampleRate_;
        // 钳到 Nyquist 之内，避免 fc >= fs/2 时系数爆炸
        const float lo = juce::jlimit (10.0f, (float) (sr * 0.45), lowHz_);
        const float hi = juce::jlimit (20.0f, (float) (sr * 0.49), highHz_);
        const float q  = 0.70710678f;      // Butterworth
        for (int i = 0; i < kSections; ++i)
        {
            hp_[(size_t) i].setCoefficients (BiquadFilter::designHighCut (lo, q, (float) sr));
            lp_[(size_t) i].setCoefficients (BiquadFilter::designLowCut  (hi, q, (float) sr));
        }
    }

    double sampleRate_ = 48000.0;
    float  lowHz_  = 20.0f;
    float  highHz_ = 20000.0f;
    bool   free_   = false;

    BiquadFilter hp_[kSections];
    BiquadFilter lp_[kSections];
};
