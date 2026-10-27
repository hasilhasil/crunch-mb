#pragma once

#include <JuceHeader.h>

#include <complex>
#include <vector>

// ============================================================
// SpectrumAnalyzer — FFT 频谱分析器（Crunch EQ 移植，PLAN Phase 4.1）
// 帧生产由外部驱动（processPendingFrame），使分析帧率与宿主块大小解耦。
// 与 EQ 版差异：tilt 默认 0（MB 的双层频谱不加 Pink 倾斜，UI_SPEC §4.4）。
// ============================================================
class SpectrumAnalyzer
{
public:
    void prepare (double sampleRate, int fftSize);
    void reset();
    void pushSamples (const float* mono, int numSamples);
    bool processPendingFrame();
    void setTilt (float dbPerOct);
    // 频谱分析器 Speed 三档一阶指数衰减时间常数 τ（ms）：
    //   · Fast = 100 ms, Medium = 200 ms（默认）, Slow = 400 ms
    //   · 单级一阶指数平滑（仅在 processFrame 执行一次，消除二阶串联滞后与顶部悬停）
    static constexpr double kSpeedTauMs[3] = { 100.0, 200.0, 400.0 };   // Fast / Medium / Slow
    static constexpr double speedToReleaseMs (int speedIdx) noexcept
    {
        const int idx = (speedIdx < 0) ? 0 : (speedIdx > 2 ? 2 : speedIdx);
        return kSpeedTauMs[idx];
    }

    void setReleaseMs (double ms) noexcept { releaseMs_.store (juce::jmax (1.0, ms)); }
    void setFrozen (bool f)        noexcept { frozen_.store (f); }
    bool isFrozen() const          noexcept { return frozen_.load(); }
    int  getFftSize() const noexcept { return fftSize_; }   // SpectrumEngine 饥饿检测用
    void getLevels (juce::Array<float>& freqs, juce::Array<float>& levelsDb, int numPoints);

    juce::uint32 getFrameCounter() const noexcept { return frameCounter_; }
    float getLastFrameDeltaDb() const noexcept    { return lastFrameDeltaDb_; }

    // 诊断/测试钩子：直接读仓级 dB（避开 fadeIn 与对数轴插值，便于验证平滑公式）
    int   getNumBins() const noexcept { return (int) binLevelsDb_.size(); }
    float getBinLevelDb (int bin) const
    {
        return binLevelsDb_[(size_t) juce::jlimit (0, getNumBins() - 1, bin)];
    }

private:
    void processFrame (const float* data);
    void rebuildLookup (int numPoints);

    double sampleRate_ = 48000.0;
    int    fftSize_    = 4096;
    float  tilt_       = 0.0f;      // MB：不加倾斜（EQ 版默认 4.5）

    // FIFO 改环形缓冲：物理索引 read/write ∈ [0,cap)，水位 avail ∈ [0,cap]
    // （只存物理索引 + 独立水位计数，杜绝"绝对计数回绕"类错误；
    //  早期 vector insert/erase 版每块一次 O(n) 搬移且 capacity 不收缩）
    static constexpr int kFifoExtra = 4096;        // 容纳单个宿主块 + 余量
    std::vector<float> fifo_;
    int fifoRead_ = 0, fifoWrite_ = 0, fifoAvail_ = 0, fifoCap_ = 0;

    inline void fifoAdvance (int n) noexcept
    {
        n = juce::jmin (n, fifoAvail_);
        fifoRead_ = (fifoRead_ + n) % fifoCap_;
        fifoAvail_ -= n;
    }
    inline void fifoPush (const float* src, int n) noexcept
    {
        n = juce::jmin (n, fifoCap_ - fifoAvail_);     // 满则丢弃最新样本（保命）
        for (int i = 0; i < n; ++i)
        {
            fifo_[(size_t) fifoWrite_] = src[i];
            if (++fifoWrite_ >= fifoCap_) fifoWrite_ = 0;
        }
        fifoAvail_ += n;
    }
    int    hop_            = 512;

    std::vector<float> windowLinear_;   // 跨回绕点时的线性窗口缓冲（容量 = fftSize_）

    std::vector<float> window_;
    std::vector<std::complex<float>> fftBuf_;
    std::vector<std::complex<float>> fftTmp_;
    std::unique_ptr<juce::dsp::FFT> fft_;

    std::vector<float> binLevelsDb_;
    std::vector<float> prevLevelsDb_;
    std::vector<float> hopPeakMag_;
    std::vector<float> displayScratchDb_;
    bool hasFrame_ = false;
    float lastTilt_ = 0.0f;

    static constexpr double kReleaseMs = kSpeedTauMs[1];         // 默认 = Medium (200 ms)
    std::atomic<double> releaseMs_ { kReleaseMs };
    std::atomic<bool>  frozen_    { false };     // Pro-MB Freeze
    float fadeIn_ = 0.0f;      // B1：起始软启动进度 0→1（每帧按 1−exp(−dt/τ) 推进）
    juce::uint32 lastFrameMs_ = 0;

    juce::uint32 frameCounter_ = 0;
    float lastFrameDeltaDb_ = 0.0f;

    std::vector<int>   lutK0_, lutK1_;
    std::vector<float> lutFreq_, lutFrac_;
    std::vector<uint8_t> lutAvg_;
    int    lutPoints_ = -1;
    double lutSampleRate_ = 0.0;
    int    lutFftSize_ = 0;
};
