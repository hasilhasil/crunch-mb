#include "LoudnessMeter.h"

// ============================================================
// LoudnessMeter 实现
//
// K 加权系数取自 ITU-R BS.1770-4（48 kHz 标称值，按采样率换算）。
// 这里用与采样率无关的 RBJ 配方现算，避免为每个采样率存表。
// ============================================================

void LoudnessMeter::prepare (double sampleRate)
{
    sampleRate_ = juce::jmax (8000.0, sampleRate);
    designFilters (sampleRate_);
    reset();
}

void LoudnessMeter::reset() noexcept
{
    shelf_.reset();
    hpf_.reset();
    sumSq_ = 0.0;
    count_ = 0;
    wPos_ = 0;
    wFill_ = 0;
    blockMs_.fill (0.0);
    blockN_.fill (0);
    loudnessDb_.store (-100.0f, std::memory_order_relaxed);
}

void LoudnessMeter::designFilters (double sampleRate) noexcept
{
    const double fs = sampleRate;
    const double pi = juce::MathConstants<double>::pi;

    // ---- 第一级：high-shelf，+4 dB @ 高频（BS.1770 预滤波）----
    // 截止 1681.9 Hz，增益 +4 dB，Q = 0.7071（Butterworth）
    {
        const double f0 = 1681.9;
        const double gainDb = 4.0;
        const double Q = 0.7071;
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * f0 / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * Q);

        const double b0 =    A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * std::sqrt (A) * alpha);
        const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
        const double b2 =    A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * std::sqrt (A) * alpha);
        const double a0 =        (A + 1.0) - (A - 1.0) * cw + 2.0 * std::sqrt (A) * alpha;
        const double a1 =     2.0 * ((A - 1.0) - (A + 1.0) * cw);
        const double a2 =        (A + 1.0) - (A - 1.0) * cw - 2.0 * std::sqrt (A) * alpha;

        const double inv = 1.0 / a0;
        shelf_.b0 = (float) (b0 * inv);
        shelf_.b1 = (float) (b1 * inv);
        shelf_.b2 = (float) (b2 * inv);
        shelf_.a1 = (float) (a1 * inv);
        shelf_.a2 = (float) (a2 * inv);
    }

    // ---- 第二级：high-pass，RLB 权重（约 38 Hz @ 48k，Q = 0.5）----
    {
        const double f0 = 38.135;
        const double Q = 0.5;
        const double w0 = 2.0 * pi * f0 / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * Q);

        const double b0 =  (1.0 + cw) * 0.5;
        const double b1 = -(1.0 + cw);
        const double b2 =  (1.0 + cw) * 0.5;
        const double a0 =   1.0 + alpha;
        const double a1 =  -2.0 * cw;
        const double a2 =   1.0 - alpha;

        const double inv = 1.0 / a0;
        hpf_.b0 = (float) (b0 * inv);
        hpf_.b1 = (float) (b1 * inv);
        hpf_.b2 = (float) (b2 * inv);
        hpf_.a1 = (float) (a1 * inv);
        hpf_.a2 = (float) (a2 * inv);
    }
}

void LoudnessMeter::processSample (float& x) noexcept
{
    float y = shelf_.process (x);
    y = hpf_.process (y);
    x = y;
    sumSq_ += (double) y * (double) y;
    ++count_;
}

void LoudnessMeter::endBlock (int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    // 本块均方（按本块样本数归一，与窗内其它块可比）
    const double blockMs = (count_ > 0) ? (sumSq_ / (double) count_) : 0.0;

    blockMs_[(size_t) wPos_] = blockMs;
    blockN_[(size_t) wPos_]  = numSamples;
    wPos_ = (wPos_ + 1) % kWindowBlocks;
    if (wFill_ < kWindowBlocks) ++wFill_;

    // 窗内总均方 = Σ(块均方 × 块样本数) / Σ块样本数
    double acc = 0.0;
    int    nAcc = 0;
    for (int i = 0; i < wFill_; ++i)
    {
        acc  += blockMs_[(size_t) i] * (double) blockN_[(size_t) i];
        nAcc += blockN_[(size_t) i];
    }
    const double meanSq = (nAcc > 0) ? (acc / (double) nAcc) : 0.0;

    // BS.1770：L = -0.691 + 10·log10(meanSquare)（单声道近似）
    constexpr double kOffset = -0.691;
    const float db = (meanSq > 1.0e-20)
                   ? (float) (kOffset + 10.0 * std::log10 (meanSq))
                   : -100.0f;
    loudnessDb_.store (db, std::memory_order_relaxed);

    sumSq_ = 0.0;
    count_ = 0;
}
