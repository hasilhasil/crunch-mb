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
    smoothedGainDbR_ = 0.0f;
    lastGain_       = 1.0f;
    rmsEnvelope_    = 0.0f;
    rmsEnvelopeR_   = 0.0f;
    lastDetectorDb_ = -100.0f;
    lastDetectorDbR_ = -100.0f;
}

void BandCompressor::setParameters (float thresholdDb, float ratio, float attackMs, float releaseMs,
                                    float rangeDb, float kneeDb, bool useRmsDetector) noexcept
{
    thresholdDb_ = thresholdDb;
    ratio_       = juce::jmax (1.0f, ratio);
    rangeDb_     = rangeDb;
    // 2026-10-14：膝宽改为连续 0–48 dB（Pro-MB 对标）。0 = 硬膝，其余 = 二次插值软膝。
    kneeWidthDb_ = juce::jmax (0.0f, kneeDb);
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

    // Range 符号决定方向（Pro-MB / Future MB 语义）：
    //   Range < 0 → 向下压缩：阈值以上衰减（经典 compressor）
    //   Range > 0 → 向上压缩：阈值以下抬升（从噪声底压缩动态范围）
    //
    // Phase C-6：DynamicsMode（Pro-MB Compress/Expand 分段按钮对标）
    //   Compress → 以上逻辑（朝阈值方向压）
    //   Expand   → 反向：阈值以上**抬升**、阈值以下**衰减**（扩大动态范围），
    //              用于修复被过度压缩的音轨、或做"门限扩展"式的清晰化。
    //              数学上就是把 Compress 的增益曲线取负号（幅度按 Range 钳制）。
    const bool upward = rangeDb_ > 0.0f;
    const bool expand = (dynamicsMode_ == DynamicsMode::Expand);
    const float lim = std::abs (rangeDb_);          // |Range| = 增益变化幅度上限
    const float sign = expand ? -1.0f : 1.0f;       // Expand = Compress 曲线取负

    float gainDb = 0.0f;

    if (upward)
    {
        // u = 低于阈值多少（>0 表示低于阈值）
        const float u = -over;
        if (w > 0.0f)
        {
            if (u >= 0.5f * w)       gainDb = -slope * u;                 // 正：上抬
            else if (u <= -0.5f * w) gainDb = 0.0f;                       // 阈值以上不处理
            else
            {
                const float t = u + 0.5f * w;
                gainDb = -slope * t * t / (2.0f * w);                     // 膝区二次（连续）
            }
        }
        else
        {
            gainDb = (u > 0.0f) ? (-slope * u) : 0.0f;
        }
    }
    else
    {
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
    }

    gainDb *= sign;                     // Phase C-6：Expand 模式取负

    // |增益变化| ≤ |Range|
    return juce::jlimit (-lim, lim, gainDb);
}

float BandCompressor::getStaticGainDb (float levelDb) const noexcept
{
    return computeTargetGainDb (levelDb);
}

void BandCompressor::processGain (float left, float right, float& gainL, float& gainR) noexcept
{
    // 2026-10-14：立体声链接（Pro-MB 对标）。
    //   link = 1.0 → 两声被**较响者**驱动，增益相同（原行为 max(|L|,|R|)）
    //   link = 0.0 → 左右各自独立压缩
    // 实现：两声电平先按 link 向 max 混合，再各跑一条包络。
    //   link=1 时 detL == detR == max(levelL, levelR) → 两条包络输入相同 → 增益相同
    //   link=0 时 detL = levelL, detR = levelR → 完全独立
    const float peakL = std::abs (left);
    const float peakR = std::abs (right);

    float levelL = peakL, levelR = peakR;
    if (useRms_)
    {
        // Phase：link=1 时两个 RMS 包络也必须同步，否则后面的 detL/detR
        // 会因包络历史不同而分叉，立体声镜像依旧不严格。
        if (stereoLink_ >= 0.999f)
        {
            const float p = juce::jmax (peakL, peakR);   // 较响者驱动
            rmsEnvelope_  = rmsEnvelope_  * rmsCoeff_ + p * p * (1.0f - rmsCoeff_);
            rmsEnvelopeR_ = rmsEnvelope_;
        }
        else
        {
            rmsEnvelope_  = rmsEnvelope_  * rmsCoeff_ + peakL * peakL * (1.0f - rmsCoeff_);
            rmsEnvelopeR_ = rmsEnvelopeR_ * rmsCoeff_ + peakR * peakR * (1.0f - rmsCoeff_);
        }
        levelL = std::sqrt (rmsEnvelope_);
        levelR = std::sqrt (rmsEnvelopeR_);
    }

    const float loudest = juce::jmax (levelL, levelR);
    const float detL = levelL + stereoLink_ * (loudest - levelL);
    const float detR = levelR + stereoLink_ * (loudest - levelR);

    lastDetectorDb_  = juce::Decibels::gainToDecibels (detL, -100.0f);
    lastDetectorDbR_ = juce::Decibels::gainToDecibels (detR, -100.0f);

    const float targetL = computeTargetGainDb (lastDetectorDb_);
    const float targetR = computeTargetGainDb (lastDetectorDbR_);

    // Phase（立体声镜像修复）：link=1 时两声被较响者驱动，targetL == targetR。
    // 但原来的系数选择比较的是**各自独立的平滑状态**：
    //     coeffL = (targetL < smoothedGainDb_)
    //     coeffR = (targetR < smoothedGainDbR_)
    // 一旦左右历史不同（例如 link 刚从 0 调到 1，或参数刚变更），
    // 就会出现"左走 attack、右走 release"——同一时刻两条包络用不同时间常数，
    // 立体声镜像不再严格匹配（可闻的轻微宽度抖动）。
    // 修法：link ≥ 0.999 时共用同一组系数与同一状态，保证镜像一致；
    //       link 更低时保留原有独立行为（用户明确要左右分离）。
    const bool fullyLinked = stereoLink_ >= 0.999f;

    if (fullyLinked)
    {
        const float coeff = (targetL < smoothedGainDb_) ? attackCoeff_ : releaseCoeff_;
        smoothedGainDb_  += (targetL - smoothedGainDb_)  * (1.0f - coeff);
        smoothedGainDbR_  = smoothedGainDb_;      // 强制镜像一致
    }
    else
    {
        // dB 域单极点平滑：目标更压缩时 attack，否则 release（63%@tau）
        const float coeffL = (targetL < smoothedGainDb_)  ? attackCoeff_ : releaseCoeff_;
        const float coeffR = (targetR < smoothedGainDbR_) ? attackCoeff_ : releaseCoeff_;
        smoothedGainDb_  += (targetL - smoothedGainDb_)  * (1.0f - coeffL);
        smoothedGainDbR_ += (targetR - smoothedGainDbR_) * (1.0f - coeffR);
    }

    gainL = juce::Decibels::decibelsToGain (smoothedGainDb_);
    gainR = juce::Decibels::decibelsToGain (smoothedGainDbR_);
    lastGain_ = gainL;          // 兼容：UI/测试读左声道
}

void BandCompressor::processSample (float& left, float& right) noexcept
{
    float gl = 1.0f, gr = 1.0f;
    processGain (left, right, gl, gr);
    left  *= gl;
    right *= gr;
}
