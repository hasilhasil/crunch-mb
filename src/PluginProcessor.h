#pragma once

#include <JuceHeader.h>

#include <atomic>

#include "Parameters.h"
#include "dsp/MultibandDSP.h"
#include "dsp/SpectrumEngine.h"

// ============================================================
// CrunchMBAudioProcessor — APVTS + 总线（含侧链）+ 状态序列化 + DSP 编排
// 线程模型（TECH_SPEC §4 / PLAN L5）：
//   · processBlock      —— 音频线程：只读 APVTS 原子值 → updateScalars → process
//   · parameterChanged  —— 消息线程：结构/延迟参数变化 → triggerAsyncUpdate
//   · handleAsyncUpdate —— 消息线程：applyStructure（含 FIR 设计）+ setLatencySamples
// ============================================================

class CrunchMBAudioProcessor : public juce::AudioProcessor,
                               public juce::AsyncUpdater,
                               public juce::AudioProcessorValueTreeState::Listener
{
public:
    CrunchMBAudioProcessor();
    ~CrunchMBAudioProcessor() override;

    // ---------- AudioProcessor ----------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "Crunch MB"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------- AsyncUpdater（消息线程） ----------
    void handleAsyncUpdate() override;

    // ---------- APVTS::Listener（消息线程） ----------
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // ---------- 编辑器开关（懒启动频谱分析，Phase 4.1） ----------
    void setEditorActive (bool active) noexcept;

    // ---------- 参数状态（62 参数，全 UI/API 经此访问） ----------
    juce::AudioProcessorValueTreeState apvts;

    // 显示/API 数据面（Phase 4/6 起由 UI/facade 只读访问）
    MultibandDSP& getDSP() noexcept { return dsp_; }

    // ---- Phase 4.1 显示数据（音频线程写 / UI 线程读，全无锁） ----
    SpectrumEngine& getSpectrumEngine() noexcept { return spectrumEngine_; }
    float getInputPeak() const noexcept  { return inputPeak_.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak_.load (std::memory_order_relaxed); }
    // 段平滑增益（dB）——仿 EQ 动态曲线用；音频线程每块写一次
    float getBandGainDb (int band) const noexcept
    {
        return bandGainDb_[(size_t) juce::jlimit (0, Param::kNumBands - 1, band)]
            .load (std::memory_order_relaxed);
    }

private:
    void applyStructure();                    // 消息线程：分频/FIR 设计
    void updateLatency() noexcept;            // 消息线程：setLatencySamples
    void readScalars (MultibandDSP::ScalarParams& sp) const noexcept;  // 音频线程
    void pushDisplayTaps (juce::AudioBuffer<float>& buffer,
                          const float* monoPre, const float* monoPost,
                          int numSamples) noexcept;                  // 音频线程

    MultibandDSP dsp_;
    SpectrumEngine spectrumEngine_;           // 后台分析线程 + 无锁三缓冲

    // 电平表（音频线程写原子，UI 做弹道）
    std::atomic<float> inputPeak_  { 0.0f };
    std::atomic<float> outputPeak_ { 0.0f };
    std::array<std::atomic<float>, Param::kNumBands> bandGainDb_ {};

    // 频谱 pre 抽头延迟环（Linear 模式输出晚 L 样本 → pre 延迟 L 使两层对齐）
    std::vector<float> preTapRing_;
    int preTapPos_ = 0;
    int preTapLatency_ = 0;
    std::vector<float> monoPre_, monoPost_;   // 预分配单声道抽头缓冲

    double sampleRate_ = 48000.0;             // prepareToPlay 缓存（勿依赖 getSampleRate：
                                              // 部分宿主/测试路径未调 setPlayConfigDetails）
    int lastLatency_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrunchMBAudioProcessor)
};
