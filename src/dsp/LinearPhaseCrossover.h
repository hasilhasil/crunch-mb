#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <complex>
#include <vector>

#include "../Parameters.h"

// ============================================================
// Ring<T> — 固定容量环形缓冲（音频线程零分配）
// 容量在构造/resetWithCapacity 时一次分配；push/popTo/读只搬数据。
// FIFO 语义：writePos_ 追 readPos_，可用容量 = capacity_-1。
// ============================================================
template <typename T>
class Ring
{
public:
    Ring() = default;
    explicit Ring (size_t capacity) { resetWithCapacity (capacity); }

    void resetWithCapacity (size_t capacity)
    {
        capacity_ = capacity + 1;              // 多留一格区分空/满
        buf_.assign (capacity_, T {});
        readPos_ = writePos_ = 0;
    }

    /** 当前元素个数（0 .. capacity_-1）。空 = 读写位置重合；满 = 差 capacity_-1。 */
    size_t size() const noexcept { return (writePos_ + capacity_ - readPos_) % capacity_; }
    bool   empty() const noexcept { return readPos_ == writePos_; }
    size_t capacity() const noexcept { return capacity_ - 1; }

    void clear() noexcept { readPos_ = writePos_ = 0; }

    /** 追加 n 个样本（调用方保证不超容量）。 */
    void push (const T* src, size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
        {
            buf_[writePos_] = src[i];
            writePos_ = (writePos_ + 1) % capacity_;
        }
    }

    /** 取出前 n 个样本到 dst，并前移读位置。 */
    void popTo (T* dst, size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
        {
            dst[i] = buf_[readPos_];
            readPos_ = (readPos_ + 1) % capacity_;
        }
    }

    /** 读第 i 个样本（不前移读位置）。i 须 < size()。 */
    T operator[] (size_t i) const noexcept
    {
        return buf_[(readPos_ + i) % capacity_];
    }

    /** 用 n 个零值预填环形缓冲（reset 时预填 blockSize 个 0，消除固定延迟环 dly_ 的可变块长漂移）。 */
    void fillZeros (size_t n) noexcept
    {
        readPos_ = 0;
        writePos_ = juce::jmin (n, capacity_ - 1);
        for (size_t i = 0; i < writePos_; ++i)
            buf_[i] = T {};
    }

    /** 只前移读位置 n 格（数据已在别处消费过时用，避免多余搬运）。 */
    void advanceRead (size_t n) noexcept
    {
        readPos_ = (readPos_ + n) % capacity_;
    }

private:
    std::vector<T> buf_;
    size_t capacity_ = 1, readPos_ = 0, writePos_ = 0;
};

// ============================================================
// FirFilter — overlap-save FIR 引擎
// 架构逐项复用 EQ LinearPhaseFilter（FIFO 块卷积 + 尾部折叠）；
// RT 加固：FIFO 用固定容量 Ring<float>（构造时一次分配），
//          热路径零分配零锁（TECH_SPEC §4.1）。
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
        fft_       = std::make_unique<juce::dsp::FFT> (order);
        designFft_ = std::make_unique<juce::dsp::FFT> (order);

        fftIn_.assign  ((size_t) fftSize_, {});
        fftOut_.assign ((size_t) fftSize_, {});
        designIn_.assign ((size_t) fftSize_, {});
        irSpectrumSlots_[0].assign ((size_t) fftSize_, {});
        irSpectrumSlots_[1].assign ((size_t) fftSize_, {});
        activeIrSlot_.store (0, std::memory_order_relaxed);
        writeIrSlot_ = 1;

        // 固定容量 FIFO（**音频线程零分配**）
        // 在 reset() 中向 outFifo_ 预填 blockSize_ 个 0，使任意固定或可变块长 s 下：
        //   · 恒满足 inFifo.size() + outFifo.size() == blockSize_（卷积后恒 ≥ s，永不欠载）
        //   · 真实延迟精确恒等于 blockSize_ + (L−1)/2（彻底消除原 dly_ 在 s != maxBlockSize 时的漂移）
        const size_t fifoCap = (size_t) blockSize_ * 2 + (size_t) juce::jmax (4096, maxBlockSize) + 64;
        inFifo_.assign  ((size_t) numCh_, Ring<float> (fifoCap));
        outFifo_.assign ((size_t) numCh_, Ring<float> (fifoCap));
        overlap_.assign ((size_t) numCh_, std::vector<float> ((size_t) blockSize_, 0.0f));
        outTmp_.assign ((size_t) blockSize_, 0.0f);

        irLength_ = 0;
        latency_  = 0;
        reset();
    }

    // 非实时（消息线程）：使用独立的 designFft_ / designIn_ 写入未激活的频域核槽位，
    // 再原子切换 activeIrSlot_，避免与音频线程 processOneChannel 的 fftIn_ / irSpectrum 竞争。
    void setImpulseResponse (const float* ir, int length)
    {
        if (designFft_ == nullptr)
            return;

        const int len = juce::jmin (length, blockSize_ + 1);
        std::fill (designIn_.begin(), designIn_.end(), std::complex<float> {});
        for (int i = 0; i < len; ++i)
            designIn_[(size_t) i] = std::complex<float> (ir[(size_t) i], 0.0f);

        const int slot = writeIrSlot_ & 1;
        designFft_->perform (designIn_.data(), irSpectrumSlots_[(size_t) slot].data(), false);

        irLength_ = len;
        latency_  = blockSize_ + (len - 1) / 2;    // pad + (L−1)/2（断言④）
        activeIrSlot_.store (slot, std::memory_order_release);
        writeIrSlot_ = slot ^ 1;
    }

    void reset()
    {
        for (auto& fifo : inFifo_)  fifo.clear();
        for (auto& fifo : outFifo_) fifo.fillZeros ((size_t) blockSize_);
        for (auto& ov   : overlap_) std::fill (ov.begin(), ov.end(), 0.0f);
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

        // 分片推进（防止极端超大块超出单次 FIFO 容量）
        int pos = 0;
        while (pos < numSamples)
        {
            const int chunk = juce::jmin (blockSize_, numSamples - pos);
            inFifo.push (input + pos, (size_t) chunk);

            const auto& irSpec = irSpectrumSlots_[(size_t) activeIrSlot_.load (std::memory_order_acquire)];

            while ((int) inFifo.size() >= blockSize_)
            {
                for (int i = 0; i < blockSize_; ++i)
                    fftIn_[(size_t) i] = std::complex<float> (inFifo[(size_t) i], 0.0f);
                for (int i = blockSize_; i < fftSize_; ++i)
                    fftIn_[(size_t) i] = std::complex<float> {};

                inFifo.advanceRead ((size_t) blockSize_);   // 只前移读位置，不搬数据

                fft_->perform (fftIn_.data(), fftOut_.data(), false);
                for (int i = 0; i < fftSize_; ++i)
                    fftOut_[(size_t) i] *= irSpec[(size_t) i];
                fft_->perform (fftOut_.data(), fftIn_.data(), true);

                // 输出块先落到预分配的 outTmp_ 再入环（避免边算边改环）
                for (int i = 0; i < blockSize_; ++i)
                    outTmp_[(size_t) i] = fftIn_[(size_t) i].real() + ov[(size_t) i];
                outFifo.push (outTmp_.data(), (size_t) blockSize_);

                for (int i = 0; i < blockSize_; ++i)
                    ov[(size_t) i] = (i < irLength_ - 1) ? fftIn_[(size_t) (blockSize_ + i)].real() : 0.0f;
            }

            const int have = (int) outFifo.size();
            if (have >= chunk)
            {
                outFifo.popTo (output + pos, (size_t) chunk);
            }
            else
            {
                outFifo.popTo (output + pos, (size_t) have);
                for (int i = have; i < chunk; ++i)
                    output[pos + i] = 0.0f;
                outFifo.clear();
            }
            pos += chunk;
        }
    }

    double sampleRate_ = 48000.0;
    int    fftSize_    = 4096;
    int    blockSize_  = 2048;
    int    irLength_   = 0;
    int    latency_    = 0;
    int    numCh_      = 2;

    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<juce::dsp::FFT> designFft_;
    std::array<std::vector<std::complex<float>>, 2> irSpectrumSlots_;
    std::atomic<int> activeIrSlot_ { 0 };
    int writeIrSlot_ = 1;
    std::vector<std::complex<float>> designIn_;
    std::vector<Ring<float>> inFifo_, outFifo_;          // 固定容量 FIFO（音频线程零分配）
    std::vector<std::vector<float>> overlap_;
    std::vector<float> outTmp_;                          // 输出块暂存（prepare 预分配）
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
