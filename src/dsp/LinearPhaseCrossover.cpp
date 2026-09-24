#include "LinearPhaseCrossover.h"

void LinearPhaseCrossover::prepare (double sampleRate, int maxBlockSize, int numChannels,
                                    Param::LinearQuality quality)
{
    sampleRate_ = sampleRate;
    numCh_      = juce::jmax (1, numChannels);
    quality_    = quality;

    const int N = fftSizeForQuality (quality);
    for (auto& f : filters_)
        f.prepare (N, maxBlockSize, numCh_);
    designValid_ = false;          // 新 FFT 尺寸 → 核需重装
}

void LinearPhaseCrossover::reset()
{
    for (auto& f : filters_)
        f.reset();
}

void LinearPhaseCrossover::setParameters (float xover1, float xover2, float xover3, int slopeDb)
{
    if (designValid_ && xover1 == lastX1_ && xover2 == lastX2_
        && xover3 == lastX3_ && slopeDb == lastSlope_)
        return;                                     // 无变化：跳过 FFT 重设计

    std::vector<float> kL, kLM, kHM, kH;
    int L = 0, D = 0;
    designBandKernels (sampleRate_, xover1, xover2, xover3, slopeDb,
                       fftSizeForQuality (quality_), kL, kLM, kHM, kH, L, D);

    filters_[0].setImpulseResponse (kL.data(),  L);
    filters_[1].setImpulseResponse (kLM.data(), L);
    filters_[2].setImpulseResponse (kHM.data(), L);
    filters_[3].setImpulseResponse (kH.data(),  L);

    lastX1_ = xover1; lastX2_ = xover2; lastX3_ = xover3;
    lastSlope_ = slopeDb;
    designValid_ = true;
}

void LinearPhaseCrossover::process (const float* const* input,
                                    float* const* bandL, float* const* bandLM,
                                    float* const* bandHM, float* const* bandH,
                                    int numChannels, int numSamples) noexcept
{
    filters_[0].process (input, bandL,  numChannels, numSamples);
    filters_[1].process (input, bandLM, numChannels, numSamples);
    filters_[2].process (input, bandHM, numChannels, numSamples);
    filters_[3].process (input, bandH,  numChannels, numSamples);
}

void LinearPhaseCrossover::designBandKernels (double sampleRate,
                                              float xover1, float xover2, float xover3,
                                              int slopeDb, int fftSize,
                                              std::vector<float>& kL, std::vector<float>& kLM,
                                              std::vector<float>& kHM, std::vector<float>& kH,
                                              int& outLength, int& outDelay)
{
    const int N = juce::nextPowerOfTwo (fftSize);
    const int L = N / 2 + 1;              // = blockSize + 1（FirFilter 上限）
    const int D = (L - 1) / 2;            // = N/4（核对称中心）
    const double bwOrder = (double) juce::jmax (1, slopeDb / 6);   // 12→2 … 48→8

    juce::dsp::FFT fft (juce::roundToInt (std::log2 ((float) N)));
    std::vector<std::complex<float>> spec ((size_t) N), taps ((size_t) N);
    std::vector<float> l1 ((size_t) L), l2 ((size_t) L), l3 ((size_t) L);

    // 频采样设计 LP 核：Butterworth 幅度 × 线性相位 e^{-jωD}，截断至 L 对称抽头，
    // 按抽头和归一化（Σ|H| 归一化：DC=1，且与 FFT 缩放约定无关）。
    const auto designLP = [&] (float fc, std::vector<float>& out)
    {
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f  = (double) k * sampleRate / (double) N;
            const double m  = 1.0 / std::sqrt (1.0 + std::pow (f / (double) fc, 2.0 * bwOrder));
            const double ph = -juce::MathConstants<double>::twoPi * (double) (k * D) / (double) N;
            spec[(size_t) k] = { (float) (m * std::cos (ph)), (float) (m * std::sin (ph)) };
        }
        for (int k = N / 2 + 1; k < N; ++k)
            spec[(size_t) k] = std::conj (spec[(size_t) (N - k)]);

        fft.perform (spec.data(), taps.data(), true);      // IDFT → 时域核

        double sum = 0.0;
        for (int i = 0; i < L; ++i)
            sum += (double) taps[(size_t) i].real();
        const float norm = (float) (1.0 / (std::abs (sum) > 1.0e-12 ? sum : 1.0));
        for (int i = 0; i < L; ++i)
            out[(size_t) i] = taps[(size_t) i].real() * norm;
    };

    designLP (xover1, l1);
    designLP (xover2, l2);
    designLP (xover3, l3);

    // 互补差分构造 ⇒ Σ 段核 = δ_D 精确（恒等求和 → 纯延迟）
    kL.assign ((size_t) L, 0.0f);
    kLM.assign ((size_t) L, 0.0f);
    kHM.assign ((size_t) L, 0.0f);
    kH.assign ((size_t) L, 0.0f);
    for (int i = 0; i < L; ++i)
    {
        kL[(size_t) i]  = l1[(size_t) i];
        kLM[(size_t) i] = l2[(size_t) i] - l1[(size_t) i];
        kHM[(size_t) i] = l3[(size_t) i] - l2[(size_t) i];
        kH[(size_t) i]  = (i == D ? 1.0f : 0.0f) - l3[(size_t) i];
    }
    outLength = L;
    outDelay  = D;
}
