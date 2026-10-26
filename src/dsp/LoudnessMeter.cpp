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
    for (auto& f : shelf_) f.reset();
    for (auto& f : hpf_)   f.reset();
    numActiveCh_ = 1;
    sumSq_ = 0.0;
    count_ = 0;
    bucketSq_ = 0.0;
    bucketSamples_ = 0;
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
        for (auto& s : shelf_)
        {
            s.b0 = (float) (b0 * inv);
            s.b1 = (float) (b1 * inv);
            s.b2 = (float) (b2 * inv);
            s.a1 = (float) (a1 * inv);
            s.a2 = (float) (a2 * inv);
        }
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
        for (auto& h : hpf_)
        {
            h.b0 = (float) (b0 * inv);
            h.b1 = (float) (b1 * inv);
            h.b2 = (float) (b2 * inv);
            h.a1 = (float) (a1 * inv);
            h.a2 = (float) (a2 * inv);
        }
    }
}

void LoudnessMeter::processSample (int channel, float x) noexcept
{
    const size_t ch = (size_t) juce::jlimit (0, 1, channel);
    if ((int) ch + 1 > numActiveCh_)
        numActiveCh_ = (int) ch + 1;
    float y = shelf_[ch].process (x);
    y = hpf_[ch].process (y);
    sumSq_ += (double) y * (double) y;
    ++count_;
}

void LoudnessMeter::processSample (float& x) noexcept
{
    float y = shelf_[0].process (x);
    y = hpf_[0].process (y);
    x = y;
    sumSq_ += (double) y * (double) y;
    ++count_;
}

void LoudnessMeter::endBlock (int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int nSamples = (count_ > 0) ? count_ : numSamples;
    bucketSq_      += sumSq_;
    bucketSamples_ += nSamples;
    sumSq_ = 0.0;
    count_ = 0;

    // 最小桶粒度约 5ms（乘活跃通道数），防止 FL Studio 极小块（如 1~16 样本）撑爆 256 桶环形窗
    const int chScale = juce::jmax (1, numActiveCh_);
    const int minBucket = juce::jmax (1, (int) std::lround (0.005 * sampleRate_)) * chScale;
    const int targetSamples = juce::jmax (1, (int) std::lround (windowSec_ * sampleRate_)) * chScale;

    if (bucketSamples_ >= minBucket)
    {
        blockMs_[(size_t) wPos_] = bucketSq_ / (double) bucketSamples_;
        blockN_[(size_t) wPos_]  = bucketSamples_;
        wPos_ = (wPos_ + 1) % kWindowBlocks;
        if (wFill_ < kWindowBlocks) ++wFill_;
        bucketSq_ = 0.0;
        bucketSamples_ = 0;
    }

    // 从最新桶向前回溯累加，严格截取最近 windowSec_（默认 400ms）对应的样本数
    double acc  = bucketSq_;
    int    nAcc = bucketSamples_;
    for (int step = 0; step < wFill_ && nAcc < targetSamples; ++step)
    {
        const int idx = (wPos_ - 1 - step + kWindowBlocks) % kWindowBlocks;
        const int bn  = blockN_[(size_t) idx];
        if (bn <= 0)
            continue;
        const int take = juce::jmin (bn, targetSamples - nAcc);
        acc  += blockMs_[(size_t) idx] * (double) take;
        nAcc += take;
    }
    const double meanSq = (nAcc > 0) ? (acc / (double) nAcc) : 0.0;

    // BS.1770：L = -0.691 + 10·log10(meanSquare)
    constexpr double kOffset = -0.691;
    const float db = (meanSq > 1.0e-20)
                   ? (float) (kOffset + 10.0 * std::log10 (meanSq))
                   : -100.0f;
    loudnessDb_.store (db, std::memory_order_relaxed);
    numActiveCh_ = 1;
}
