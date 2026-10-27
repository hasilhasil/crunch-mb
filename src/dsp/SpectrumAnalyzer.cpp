#include "SpectrumAnalyzer.h"

void SpectrumAnalyzer::prepare (double sampleRate, int fftSize)
{
    sampleRate_ = sampleRate;
    fftSize_    = juce::nextPowerOfTwo (fftSize);

    const int order = juce::roundToInt (std::log2 ((float) fftSize_));
    fft_ = std::make_unique<juce::dsp::FFT> (order);

    window_.assign ((size_t) fftSize_, 0.0f);
    for (int n = 0; n < fftSize_; ++n)
        window_[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * n / (fftSize_ - 1)));

    fftBuf_.assign ((size_t) fftSize_, {});
    fftTmp_.assign ((size_t) fftSize_, {});

    binLevelsDb_.assign ((size_t) (fftSize_ / 2 + 1), -140.0f);
    prevLevelsDb_.assign ((size_t) (fftSize_ / 2 + 1), -140.0f);
    hopPeakMag_.assign ((size_t) (fftSize_ / 2 + 1), 0.0f);

    hop_ = juce::jmax (1, (int) std::lround (sampleRate_ / 60.0));
    lutPoints_ = -1;

    // 环形 FIFO：容量 = 完整窗口 + hop + 单块余量（稳态零分配）
    fifoCap_ = fftSize_ + hop_ * 4 + kFifoExtra;
    fifo_.assign ((size_t) fifoCap_, 0.0f);
    fifoRead_ = fifoWrite_ = fifoAvail_ = 0;

    reset();
}

void SpectrumAnalyzer::reset()
{
    fifoRead_ = fifoWrite_ = fifoAvail_ = 0;
    hasFrame_ = false;
    fadeIn_ = 0.0f;
    lastTilt_ = tilt_;
    std::fill (binLevelsDb_.begin(), binLevelsDb_.end(), -140.0f);
    std::fill (prevLevelsDb_.begin(), prevLevelsDb_.end(), -140.0f);
    std::fill (hopPeakMag_.begin(), hopPeakMag_.end(), 0.0f);
    frameCounter_ = 0;
    lastFrameDeltaDb_ = 0.0f;
}

void SpectrumAnalyzer::setTilt (float dbPerOct)
{
    tilt_ = dbPerOct;
}

void SpectrumAnalyzer::pushSamples (const float* mono, int numSamples)
{
    if (mono == nullptr || numSamples <= 0 || fft_ == nullptr)
        return;
    fifoPush (mono, numSamples);          // 满则丢弃最新样本（分析线程卡死时保命）
}

bool SpectrumAnalyzer::processPendingFrame()
{
    if (fft_ == nullptr || fifoAvail_ < fftSize_)
        return false;

    // 窗口 = FIFO 中最旧的 fftSize_ 个样本（跨回绕点时拷到线性缓冲）
    windowLinear_.resize ((size_t) fftSize_);
    int r = fifoRead_;
    for (int i = 0; i < fftSize_; ++i)
    {
        windowLinear_[(size_t) i] = fifo_[(size_t) r];
        if (++r >= fifoCap_) r = 0;
    }
    processFrame (windowLinear_.data());

    // 一个 hop + 把积压 surplus 分 4 份泄掉，使缓冲收敛到目标水位
    const int target  = fftSize_ + hop_;
    const int surplus = fifoAvail_ - target;
    const int advance = hop_ + (surplus > 0 ? surplus / 4 : 0);
    fifoAdvance (advance);
    return true;
}

void SpectrumAnalyzer::processFrame (const float* data)
{
    for (int n = 0; n < fftSize_; ++n)
        fftBuf_[(size_t) n] = std::complex<float> (data[n] * window_[(size_t) n], 0.0f);

    fft_->perform (fftBuf_.data(), fftTmp_.data(), false);

    const float norm = 4.0f / (float) fftSize_;
    const float refFreq = 1000.0f;

    const auto nowMs = juce::Time::getMillisecondCounter();
    const double dt = (lastFrameMs_ == 0)
                        ? 1.0 / 60.0
                        : juce::jlimit (0.001, 0.100, (double) (nowMs - lastFrameMs_) / 1000.0);
    lastFrameMs_ = nowMs;

    // 单级一阶指数衰落（仅在此处执行一次，getLevels 不再重复时间滤波）：
    //   · Attack ：瞬时捕获峰值（60fps 下无滞后跟手）
    //   · Release：k_release = 1 - exp(-dt / tauReleaseSec)，三档 τ = 100 / 200 / 400 ms
    const double tauReleaseSec = juce::jmax (1.0e-3, releaseMs_.load() * 0.001);
    const float  kRelease      = (float) (1.0 - std::exp (-dt / tauReleaseSec));

    // B1：起始软启动——编辑器刚打开时按 180ms 时间常数平滑升起；超过 0.60 后直接到位避免 Pre/Post 跨阈值分离
    constexpr double kFadeTauSec = 0.180;
    if (! hasFrame_)
        fadeIn_ = 0.0f;
    else
    {
        fadeIn_ = juce::jmin (1.0f, fadeIn_ + (float) (1.0 - std::exp (-dt / kFadeTauSec)));
        if (fadeIn_ > 0.60f)
            fadeIn_ = 1.0f;
    }

    const bool frozen = frozen_.load();
    float maxDelta = 0.0f;

    for (int k = 0; k <= fftSize_ / 2; ++k)
    {
        const float mag = std::abs (fftTmp_[(size_t) k]) * norm + 1.0e-9f;
        float db = 20.0f * std::log10 (mag);

        const float freq = (float) k * (float) sampleRate_ / (float) fftSize_;
        if (freq > 1.0f)
            db += tilt_ * std::log2 (freq / refFreq);

        float& current = binLevelsDb_[(size_t) k];
        const float prev = hasFrame_ ? prevLevelsDb_[(size_t) k] : db;

        if (frozen)
            current = juce::jmax (prev, db);          // Freeze：只升不降 = 历史最大包络
        else if (frameCounter_ <= 1 || db >= prev)
            current = db;                             // 首帧跳过未发布的冷启动瞬态 + 瞬态即时捕获
        else
            current = prev + (db - prev) * kRelease;  // 单级一阶指数衰落（100 / 200 / 400 ms）

        prevLevelsDb_[(size_t) k] = current;
        maxDelta = juce::jmax (maxDelta, std::abs (current - prev));
    }

    hasFrame_ = true;
    ++frameCounter_;
    lastFrameDeltaDb_ = maxDelta;
}

void SpectrumAnalyzer::rebuildLookup (int numPoints)
{
    const double fMin = 10.0;
    const double fMax = juce::jmin (22100.0, sampleRate_ * 0.5);   // A5+：显示上限 22100 Hz
    const double binHz = sampleRate_ / (double) fftSize_;

    lutK0_.resize ((size_t) numPoints);
    lutK1_.resize ((size_t) numPoints);
    lutFreq_.resize ((size_t) numPoints);
    lutFrac_.resize ((size_t) numPoints);
    lutAvg_.resize ((size_t) numPoints);

    const double step = std::pow (fMax / fMin, 1.0 / (double) juce::jmax (1, numPoints - 1));

    for (int i = 0; i < numPoints; ++i)
    {
        const double t = (double) i / (double) juce::jmax (1, numPoints - 1);
        const double freq = fMin * std::pow (fMax / fMin, t);

        const double lo = (freq / step) / binHz;
        const double hi = (freq * step) / binHz;

        if (hi - lo < 1.0)
        {
            const double pos = freq / binHz;
            const int k = juce::jlimit (0, fftSize_ / 2, (int) std::floor (pos));

            lutK0_[(size_t) i]   = k;
            lutK1_[(size_t) i]   = juce::jlimit (0, fftSize_ / 2, k + 1);
            lutFrac_[(size_t) i] = (float) (pos - std::floor (pos));
            lutAvg_[(size_t) i]  = 0;
        }
        else
        {
            const int k0 = juce::jlimit (0, fftSize_ / 2, (int) std::floor (lo));
            int k1 = juce::jlimit (0, fftSize_ / 2, (int) std::ceil (hi));
            if (k1 <= k0)
                k1 = juce::jmin (fftSize_ / 2, k0 + 1);

            lutK0_[(size_t) i]   = k0;
            lutK1_[(size_t) i]   = k1;
            lutFrac_[(size_t) i] = 0.0f;
            lutAvg_[(size_t) i]  = 1;
        }

        lutFreq_[(size_t) i] = (float) freq;
    }

    lutPoints_ = numPoints;
    lutSampleRate_ = sampleRate_;
    lutFftSize_ = fftSize_;
}

void SpectrumAnalyzer::getLevels (juce::Array<float>& freqs, juce::Array<float>& levelsDb, int numPoints)
{
    freqs.clearQuick();
    levelsDb.clearQuick();

    if (numPoints <= 0 || fft_ == nullptr || binLevelsDb_.empty())
        return;

    if (numPoints != lutPoints_ || sampleRate_ != lutSampleRate_ || fftSize_ != lutFftSize_)
        rebuildLookup (numPoints);

    freqs.ensureStorageAllocated (numPoints);
    levelsDb.ensureStorageAllocated (numPoints);

    displayScratchDb_.resize ((size_t) numPoints);
    const bool frozen = frozen_.load();

    for (int i = 0; i < numPoints; ++i)
    {
        const size_t si = (size_t) i;
        const int k0 = lutK0_[si];
        const int k1 = lutK1_[si];

        float db;

        if (lutAvg_[si] != 0)
        {
            double energy = 0.0;
            int count = 0;
            for (int k = k0; k <= k1; ++k)
            {
                energy += std::pow (10.0, (double) binLevelsDb_[(size_t) k] / 10.0);
                ++count;
            }
            db = (float) (10.0 * std::log10 (juce::jmax (1.0e-20, energy / (double) juce::jmax (1, count))));
        }
        else
        {
            const float a = binLevelsDb_[(size_t) k0];
            const float b = binLevelsDb_[(size_t) k1];
            db = a * (1.0f - lutFrac_[si]) + b * lutFrac_[si];
        }

        displayScratchDb_[si] = db;
    }

    // 3 点空间核平滑（0.25 / 0.50 / 0.25），消除相邻对数频率采样点之间的细碎锯齿
    const float kFade = juce::jlimit (0.0f, 1.0f, fadeIn_);
    for (int i = 0; i < numPoints; ++i)
    {
        float db = displayScratchDb_[(size_t) i];
        if (! frozen && i > 0 && i + 1 < numPoints)
        {
            db = 0.25f * displayScratchDb_[(size_t) (i - 1)]
               + 0.50f * displayScratchDb_[(size_t) i]
               + 0.25f * displayScratchDb_[(size_t) (i + 1)];
        }

        // B1 起始软启动：fadeIn_ 从 0 → 1，显示值从底部（−100dB）按时间常数升起
        db = -100.0f + (db + 100.0f) * kFade;

        freqs.add (lutFreq_[(size_t) i]);
        levelsDb.add (db);
    }
}
