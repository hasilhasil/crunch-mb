#include "BandCompressor.h"

namespace
{
    // RMS 检测器包络窗口（固定 10ms；非参数）
    constexpr double kRmsWindowSec = 0.010;
}

void BandCompressor::prepare (double sampleRate)
{
    sampleRate_ = juce::jmax (1.0, sampleRate);
    reset();
}

void BandCompressor::reset()
{
    smoothedGainDb_ = 0.0f;
    lastGain_       = 1.0f;
    rmsEnvelope_    = 0.0f;
    lastDetectorDb_ = -100.0f;
}

void BandCompressor::setParameters (float thresholdDb, float ratio, float attackMs, float releaseMs,
                                    float rangeDb, bool softKnee, bool useRmsDetector) noexcept
{
    thresholdDb_ = thresholdDb;
    ratio_       = juce::jmax (1.0f, ratio);
    rangeDb_     = rangeDb;
    kneeWidthDb_ = softKnee ? 12.0f : 0.0f;   // 软拐点 = 12dB 二次插值（TECH_SPEC §3.3）
    useRms_      = useRmsDetector;

    const auto coeffFor = [this] (double ms)
    {
        const double tauSec = juce::jmax (0.0001, ms * 0.001);
        return (float) std::exp (-1.0 / (tauSec * sampleRate_));
    };
    attackCoeff_  = coeffFor ((double) attackMs);
    releaseCoeff_ = coeffFor ((double) releaseMs);
    rmsCoeff_     = coeffFor (kRmsWindowSec * 1000.0);
}

float BandCompressor::computeTargetGainDb (float levelDb) const noexcept
{
    const float over = levelDb - thresholdDb_;
    const float slope = 1.0f / ratio_ - 1.0f;      // ≤ 0
    const float w = kneeWidthDb_;

    float gainDb;
    if (w > 0.0f)
    {
        // 软拐点：±w/2 区间二次插值（标准 soft-knee 传输曲线）
        if (over <= -0.5f * w)       gainDb = 0.0f;
        else if (over >= 0.5f * w)   gainDb = slope * over;
        else
        {
            const float t = over + 0.5f * w;
            gainDb = slope * t * t / (2.0f * w);
        }
    }
    else
    {
        gainDb = over > 0.0f ? slope * over : 0.0f;
    }

    // Range 钳制（Pro-MB 同功能）：|Range| = 增益变化幅度上限，符号 = 方向。
    // 本插件为压缩器，增益变化方向恒为负：Range ≤ 0 时限制 |GR|；Range > 0
    // 时仅允许正向变化（预留），压缩增益被钳到 0。
    const float lo = juce::jmin (rangeDb_, 0.0f);
    const float hi = juce::jmax (rangeDb_, 0.0f);
    return juce::jlimit (lo, hi, gainDb);
}

float BandCompressor::getStaticGainDb (float levelDb) const noexcept
{
    return computeTargetGainDb (levelDb);
}

float BandCompressor::processGain (float left, float right) noexcept
{
    const float peak = juce::jmax (std::abs (left), std::abs (right));

    float level = peak;
    if (useRms_)
    {
        rmsEnvelope_ = rmsEnvelope_ * rmsCoeff_ + peak * peak * (1.0f - rmsCoeff_);
        level = std::sqrt (rmsEnvelope_);
    }

    lastDetectorDb_ = juce::Decibels::gainToDecibels (level, -100.0f);
    const float target = computeTargetGainDb (lastDetectorDb_);

    // dB 域单极点平滑：目标更压缩时 attack，否则 release（63%@tau）
    const float coeff = (target < smoothedGainDb_) ? attackCoeff_ : releaseCoeff_;
    smoothedGainDb_ += (target - smoothedGainDb_) * (1.0f - coeff);

    lastGain_ = juce::Decibels::decibelsToGain (smoothedGainDb_);
    return lastGain_;
}

void BandCompressor::processSample (float& left, float& right) noexcept
{
    const float g = processGain (left, right);
    left  *= g;
    right *= g;
}
