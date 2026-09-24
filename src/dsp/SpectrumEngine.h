#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <vector>

#include "SpectrumAnalyzer.h"

// ============================================================
// SpectrumEngine — 双层频谱引擎（Crunch EQ 移植，PLAN Phase 4.1）
//
// 线程模型（解决"频谱卡但电平表顺"的块大小耦合）：
//   · 音频线程只写 SPSC 环形缓冲——无锁、无分配、永不阻塞
//   · 后台线程以固定节奏排空环形缓冲喂分析器，每个显示 tick 至多发布
//     ONE 帧，经无锁三缓冲交付
//   · UI 线程从不跑 FFT，只取最新发布点做显示整形
//
// MB 与 EQ 版差异：
//   · 去 analyzerMode（MB 恒双层：Pre + Post，UI_SPEC §4.4）
//   · 保留 setTilt（默认 0 = 不加倾斜），保留懒启动（编辑器开关）
// ============================================================
class SpectrumEngine : public juce::Thread
{
public:
    static constexpr int kDisplayPoints = 512;
    static constexpr juce::uint32 kFrameIntervalMs = 16;

    SpectrumEngine();
    ~SpectrumEngine() override;

    // processor prepareToPlay / UI 线程
    void setSampleRate (double rate)  noexcept { sampleRate_.store (rate); }
    void setTilt (float dbPerOct)     noexcept { tilt_.store (dbPerOct); }
    void setUIActive (bool isActive)  noexcept;
    // 彻底停线程（Editor 析构时调；之后 setUIActive(true) 可复活）
    void suspend() noexcept;

    // 音频线程：单声道 pre/post，等长
    void pushSamples (const float* pre, const float* post, int numSamples) noexcept;

    // UI 线程：最新发布点；有新帧返回 true
    bool getLatest (std::array<float, kDisplayPoints>& pre,
                    std::array<float, kDisplayPoints>& post);

    // UI 线程：对数频率轴（只需取一次）
    bool getFrequencies (std::vector<float>& freqs);

private:
    void run() override;
    void analyseTick();
    void publishDecayFrame (double dtSec, double tauSec);   // #8：无信号保持+衰减帧
    static void drain (juce::AbstractFifo& fifo, std::vector<float>& ring, SpectrumAnalyzer& analyzer);

    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<float>  tilt_      { 0.0f };
    std::atomic<bool>   uiActive_  { false };
    std::atomic<bool>   running_   { true };   // false = 线程挂起（长睡眠，不耗调度）
    juce::uint32 lastHeartbeatMs_ = 0;         // #8：衰减帧时间步进基准
    juce::uint32 starvedSinceMs_  = 0;         // #8：饥饿起始（保持期计时）
    juce::uint32 lastDataMs_      = 0;         // #8：最后一次收到音频数据的时刻
    int  lastReadyCount_ = -1;                 // #8：上次观察到的 ring 数据量

    juce::AbstractFifo fifoPre_  { 1 << 16 };
    juce::AbstractFifo fifoPost_ { 1 << 16 };
    std::vector<float> ringPre_, ringPost_;

    SpectrumAnalyzer analyzerPre_, analyzerPost_;
    double currentRate_ = 0.0;
    juce::uint32 lastFrameMs_ = 0;

    struct Snapshot { std::array<float, kDisplayPoints> pre {}, post {}; };
    std::array<Snapshot, 3> slots_;
    std::atomic<int> publishedSlot_ { -1 };
    std::atomic<juce::uint32> publishVersion_ { 0 };   // #8：同 slot 内容更新也可见
    int writeSlot_ = 0;
    std::atomic<int> uiSlot_ { -1 };
    std::atomic<juce::uint32> uiVersion_ { 0 };

    std::vector<float> freqs_;
    juce::Array<float> scratchFreqs_, scratchPre_, scratchPost_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumEngine)
};
