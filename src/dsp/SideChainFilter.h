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
// 阶数：HP/LP 各 4 阶 Linkwitz-Riley（2 节二阶 Butterworth 级联 = 24 dB/oct）。
// 音频线程热路径零分配零锁（TECH_SPEC §4）。
// ============================================================
class SideChainFilter
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = juce::jmax (1.0, sampleRate);
        coeffsValid_ = false;
        reset();
        if (free_)
            applyCoefficients();
    }

    void reset()
    {
        for (auto& f : hp_) f.reset();
        for (auto& f : lp_) f.reset();
    }

    /** isFree=false 时直通（Band 模式）；增加脏值缓存避免每块重复重算 16 次双二阶系数 */
    void setParameters (float lowHz, float highHz, bool isFree) noexcept
    {
        const bool freqChanged = (lowHz != lowHz_) || (highHz != highHz_);
        lowHz_  = lowHz;
        highHz_ = highHz;
        free_   = isFree;
        if (! free_)
        {
            if (freqChanged)
                coeffsValid_ = false;
            return;
        }
        if (! coeffsValid_ || freqChanged)
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
    static constexpr int kSections = 2;   // 2 节 2 阶 Butterworth 级联 = 4 阶 LR4（24 dB/oct）

    void applyCoefficients() noexcept
    {
        const double sr = sampleRate_;
        // 钳到 Nyquist 之内，避免 fc >= fs/2 时系数爆炸
        const float nyq = (float) (sr * 0.5);
        float lo = juce::jlimit (10.0f, nyq * 0.90f, lowHz_);
        float hi = juce::jlimit (20.0f, nyq * 0.98f, highHz_);

        // Phase（可用性修复）：low > high 时 HP 与 LP 的阻带相交，
        // 通带几乎为空 → 检测器看到近静音 → **该段永远不触发**。
        // 参数范围允许这种组合（low 20-2000、high 500-20000），
        // 用户完全可能设出 low=2000/high=500。
        // Pro-MB 的 Free 模式保证 low <= high；这里改为取中点后各让一半，
        // 保证 [lo, hi] 是非空区间且仍以用户意图的中值为中心。
        if (lo > hi)
        {
            const float mid = 0.5f * (lo + hi);
            const float halfWidth = 0.25f * juce::jmax (1.0f, mid);   // 1/4 音程宽的可用通带
            lo = juce::jmax (10.0f, mid - halfWidth);
            hi = juce::jmin (nyq * 0.98f, mid + halfWidth);
        }

        const float q  = 0.70710678f;      // Butterworth
        for (int i = 0; i < kSections; ++i)
        {
            hp_[(size_t) i].setCoefficients (BiquadFilter::designHighPass (lo, q, (float) sr));
            lp_[(size_t) i].setCoefficients (BiquadFilter::designLowPass  (hi, q, (float) sr));
        }
        coeffsValid_ = true;
    }

    double sampleRate_ = 48000.0;
    // Phase：初值与 Parameters.cpp 的注册默认值对齐（100 / 5000），
    // 避免 setParameters 尚未被调用时滤波器用在错误频率上。
    float  lowHz_  = 100.0f;
    float  highHz_ = 5000.0f;
    bool   free_   = false;
    bool   coeffsValid_ = false;

    BiquadFilter hp_[kSections];
    BiquadFilter lp_[kSections];
};
