#pragma once

#include <JuceHeader.h>

#include <complex>
#include <vector>

#include "../Parameters.h"

// ============================================================
// FirFilter — overlap-save FIR 引擎
// 架构逐项复用 EQ LinearPhaseFilter（FIFO 块卷积 + 尾部折叠）；
// RT 加固：prepare() 预分配 FIFO 容量，热路径 insert/erase 零分配（TECH_SPEC §4.1）。
// 延迟 = blockSize + (L−1)/2（pad + 群延迟，PLAN §5 结论③：≈0.75×FFT）。
// ============================================================
class FirFilter
{
public:
    void prepare (int fftSize, int maxBlockSize, int numChannels)
    {
        fftSize_   = juce::nextPowerOfTwo (fftSize);
        blockSize_ = fftSize_ / 2;
        numCh_     = juce::jmax (1, numChannels);

        const int order = juce::roundToInt (std::log2 ((float) fftSize_));
        fft_ = std::make_unique<juce::dsp::FFT> (order);

        fftIn_.assign  ((size_t) fftSize_, {});
        fftOut_.assign ((size_t) fftSize_, {});
        irSpectrum_.assign ((size_t) fftSize_, {});

        inFifo_.assign  ((size_t) numCh_, {});
        outFifo_.assign ((size_t) numCh_, {});
        overlap_.assign ((size_t) numCh_, std::vector<float> ((size_t) blockSize_, 0.0f));

        const size_t cap = (size_t) blockSize_ + (size_t) juce::jmax (1, maxBlockSize) + 8;
        for (auto& f : inFifo_)  f.reserve (cap);
        for (auto& f : outFifo_) f.reserve (cap);

        // 固定输出延迟环（callSize 样本）：FIFO 块卷积的真实延迟 = (B−s)+D，
        // 随宿主调用块大小 s 漂移；补一个 s 样本延迟环后总延迟恒 = B + D
        // = pad + (L−1)/2（断言④），与调用块大小无关。
        callSize_ = juce::jmax (1, maxBlockSize);
        dly_.assign ((size_t) numCh_, std::vector<float> ((size_t) callSize_, 0.0f));
        dlyPos_.assign ((size_t) numCh_, 0);

        irLength_ = 0;
        latency_  = 0;
    }

    // 非实时（消息线程）：IR 变更不 reset（保持在途音频连续——EQ 同款结论）
    void setImpulseResponse (const float* ir, int length)
    {
        if (fft_ == nullptr)
            return;

        irLength_ = juce::jmin (length, blockSize_ + 1);
        std::fill (fftIn_.begin(), fftIn_.end(), std::complex<float> {});
        for (int i = 0; i < irLength_; ++i)
            fftIn_[(size_t) i] = std::complex<float> (ir[(size_t) i], 0.0f);

        fft_->perform (fftIn_.data(), irSpectrum_.data(), false);

        latency_ = blockSize_ + (irLength_ - 1) / 2;    // pad + (L−1)/2（断言④）
    }

    void reset()
    {
        for (auto& fifo : inFifo_)  fifo.clear();
        for (auto& fifo : outFifo_) fifo.clear();
        for (auto& ov   : overlap_) std::fill (ov.begin(), ov.end(), 0.0f);
        for (auto& d    : dly_)     std::fill (d.begin(), d.end(), 0.0f);
        std::fill (dlyPos_.begin(), dlyPos_.end(), 0);
    }

    void process (const float* const* input, float* const* output, int numChannels, int numSamples) noexcept
    {
        if (fft_ == nullptr || irLength_ == 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::clear (output[ch], numSamples);
            return;
        }

        const int chans = juce::jmin (numChannels, numCh_);
        for (int ch = 0; ch < chans; ++ch)
            processOneChannel (ch, input[ch], output[ch], numSamples);
        for (int ch = chans; ch < numChannels; ++ch)
            juce::FloatVectorOperations::clear (output[ch], numSamples);
    }

    int  getLatencySamples() const noexcept { return latency_; }
    bool ready() const noexcept             { return irLength_ > 0 && fft_ != nullptr; }

private:
    void processOneChannel (int channel, const float* input, float* output, int numSamples) noexcept
    {
        auto& inFifo  = inFifo_[(size_t) channel];
        auto& outFifo = outFifo_[(size_t) channel];
        auto& ov      = overlap_[(size_t) channel];

        inFifo.insert (inFifo.end(), input, input + numSamples);

        while ((int) inFifo.size() >= blockSize_)
        {
            for (int i = 0; i < blockSize_; ++i)
                fftIn_[(size_t) i] = std::complex<float> (inFifo[(size_t) i], 0.0f);
            for (int i = blockSize_; i < fftSize_; ++i)
                fftIn_[(size_t) i] = std::complex<float> {};

            inFifo.erase (inFifo.begin(), inFifo.begin() + blockSize_);

            fft_->perform (fftIn_.data(), fftOut_.data(), false);
            for (int i = 0; i < fftSize_; ++i)
                fftOut_[(size_t) i] *= irSpectrum_[(size_t) i];
            fft_->perform (fftOut_.data(), fftIn_.data(), true);

            for (int i = 0; i < blockSize_; ++i)
                outFifo.push_back (fftIn_[(size_t) i].real() + ov[(size_t) i]);

            for (int i = 0; i < blockSize_; ++i)
                ov[(size_t) i] = (i < irLength_ - 1) ? fftIn_[(size_t) (blockSize_ + i)].real() : 0.0f;
        }

        const int have = (int) outFifo.size();
        if (have >= numSamples)
        {
            std::copy (outFifo.begin(), outFifo.begin() + numSamples, output);
            outFifo.erase (outFifo.begin(), outFifo.begin() + numSamples);
        }
        else
        {
            for (int i = 0; i < have; ++i)
                output[i] = outFifo[(size_t) i];
            for (int i = have; i < numSamples; ++i)
                output[i] = 0.0f;
            outFifo.clear();
        }

        // 固定延迟环（callSize 样本）→ 总延迟恒 = blockSize + (L−1)/2
        auto& dly = dly_[(size_t) channel];
        int pos = dlyPos_[(size_t) channel];
        for (int i = 0; i < numSamples; ++i)
        {
            const float y = output[i];
            output[i] = dly[(size_t) pos];
            dly[(size_t) pos] = y;
            if (++pos == callSize_) pos = 0;
        }
        dlyPos_[(size_t) channel] = pos;
    }

    double sampleRate_ = 48000.0;
    int    fftSize_    = 4096;
    int    blockSize_  = 2048;
    int    irLength_   = 0;
    int    latency_    = 0;
    int    numCh_      = 2;
    int    callSize_   = 512;

    std::vector<std::vector<float>> dly_;     // 固定输出延迟环（callSize 样本/通道）
    std::vector<int> dlyPos_;

    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<std::complex<float>> irSpectrum_;
    std::vector<std::vector<float>>  inFifo_, outFifo_, overlap_;
    std::vector<std::complex<float>> fftIn_, fftOut_;
};

// ============================================================
// LinearPhaseCrossover — 线性相位 FIR 四段交叉分频器（复用 LinearPhaseFilter 架构）
//
// 核构造（恒等保证）：先设计同延迟 LP 核 l1/l2/l3（Butterworth 幅度、频采样、
// Σ|H| 归一化），再令：
//     kL = l1,  kLM = l2 − l1,  kHM = l3 − l2,  kH = δ_D − l3
// ⇒ Σk = δ_D **精确成立**（断言③：求和 = 纯延迟，群延迟恒定 = D）。
// 延迟 = blockSize + (L−1)/2 = 3N/4（N = FFT 尺寸，断言④）。
// ⚠️ designBandKernels 含 FFT 设计——消息线程调用（PLAN L5，勿上音频线程）。
// ============================================================
class LinearPhaseCrossover
{
public:
    // 质量档 → FFT 尺寸（延迟 = 3N/4 = 1536 / 3072 / 6144 samples）
    static int fftSizeForQuality (Param::LinearQuality q)
    {
        switch (q)
        {
            case Param::LinearQuality::Low:    return 2048;
            case Param::LinearQuality::High:   return 8192;
            case Param::LinearQuality::Medium:
            default:                           return 4096;
        }
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels, Param::LinearQuality quality);
    void reset();

    // 非实时（含 FFT 核设计）：安装 4 段 FIR 核
    void setParameters (float xover1, float x1xover2, float xover3, int slopeDb);

    void process (const float* const* input,
                  float* const* bandL, float* const* bandLM,
                  float* const* bandHM, float* const* bandH,
                  int numChannels, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return filters_[0].getLatencySamples(); }
    bool ready() const noexcept            { return filters_[0].ready(); }

    // 核设计（纯函数，测试直用）——Σk = δ_D 精确
    static void designBandKernels (double sampleRate,
                                   float xover1, float xover2, float xover3,
                                   int slopeDb, int fftSize,
                                   std::vector<float>& kL, std::vector<float>& kLM,
                                   std::vector<float>& kHM, std::vector<float>& kH,
                                   int& outLength, int& outDelay);

private:
    FirFilter filters_[4];   // 0=L 1=LM 2=HM 3=H（各含立体声）
    double sampleRate_ = 48000.0;
    int    numCh_ = 2;
    Param::LinearQuality quality_ = Param::LinearQuality::Medium;

    // 设计变更检测：参数不变则跳过 FFT 重设计（自动化/异步更新防抖）
    float lastX1_ = 0.0f, lastX2_ = 0.0f, lastX3_ = 0.0f;
    int   lastSlope_ = 0;
    bool  designValid_ = false;
};
