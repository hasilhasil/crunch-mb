#pragma once

#include <JuceHeader.h>

#include <atomic>

#include "Parameters.h"
#include "dsp/MultibandDSP.h"
#include "dsp/SpectrumEngine.h"
#include "api/MBBackend.h"

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

    // ---------- R32：UI 唤醒标志 ----------
    // Phase E-2 空闲停表后，UI 唯一的参数检测点（timerCallback）不再运行，
    // 宿主自动化改参数时画面永久停在旧值。parameterChanged 置位、
    // DisplayView::paint() 消费（唤醒定时器 + 重绘）。
    bool consumeUiWakeFlag() noexcept
    {
        return uiWakeFlag_.exchange (false, std::memory_order_acq_rel);
    }
    /** R32：参数变化时由编辑器注册的回调，用于立即重绘，不必等下一个巡检周期。
     *  R35（N4）：签名带 **parameterID** —— UI 据此精确定位要刷新哪个副本
     *  （popover / LINKED 抽屉 / Output 面板各自持有参数快照，此前只在"打开时"
     *   同步一次；宿主自动化或别的入口改参数后显示会停在旧值，
     *   Output 面板的 Auto-Gain / Monitor 行甚至会"显示与真实相反"）。
     *  ⚠️ 只有**消息线程**才会调用它；音频线程自动化只置 uiWakeFlag_，
     *     由 UI 定时器兜底做全量同步（绝不在音频线程碰组件）。
     *  ⚠️ UI 侧必须在销毁时调用 setUiWakeCallback(nullptr)，
     *     否则回调会指向已释放的组件（use-after-free）。 */
    void setUiWakeCallback (std::function<void (const juce::String&)> cb)
    {
        uiWakeCallback_ = std::move (cb);
    }
    /** R35 测试钩子：回调是否已注册（编辑器存活期间应为 true）。 */
    bool hasUiWakeCallbackForTest() const { return uiWakeCallback_ != nullptr; }

    // ---------- R35（N4）：Analyzer 设置持久化 ----------
    // 这 6 项是**显示设置**，不该进 APVTS 参数表（不需要宿主自动化，
    // 也会把参数数从 86 推到 92）。改为随工程状态一起存成 ValueTree 属性，
    // 否则关开编辑器/重载工程后 Pre/Post/Resolution/Speed/Tilt/Freeze 全部回到默认。
    struct AnalyzerSettings
    {
        bool  preOn   = true;
        bool  postOn  = true;
        int   fftSize = 2048;
        int   speed   = 1;        // 0=fast 1=medium 2=slow
        float tilt    = 0.0f;
        bool  frozen  = false;
    };
    const AnalyzerSettings& getAnalyzerSettings() const noexcept { return analyzerSettings_; }
    void setAnalyzerSettings (const AnalyzerSettings& s) noexcept { analyzerSettings_ = s; }

    // ---------- 四包 UI 组合持久化（默认保持原深色界面：Classic + Blue + Standard + EnUS） ----------
    struct UiPackSettings
    {
        int themeId   = 0;   // 0=Classic, 1=Neumorphism
        int paletteId = 0;   // 0=Blue, 1=Red, 2=Cream, 3=NeuLight, 4=NeuTeal, 5=NeuDark
        int layoutId  = 0;   // 0=Standard, 1=Sculpted
        int localeId  = 0;   // 0=en-US, 1=zh-CN
    };
    const UiPackSettings& getUiPackSettings() const noexcept { return uiPackSettings_; }
    void setUiPackSettings (const UiPackSettings& s) noexcept { uiPackSettings_ = s; }

    // ---------- 编辑器开关（懒启动频谱分析，Phase 4.1） ----------
    void setEditorActive (bool active) noexcept;

    /** 测试钩子：按物理值直接设参数（不走 UI）。 */
    void setParamPhysicalForTest (const juce::String& id, float physical);

    // ---------- 参数状态（62 参数，全 UI/API 经此访问） ----------
    juce::AudioProcessorValueTreeState apvts;

    // 显示/API 数据面（Phase 4/6 起由 UI/facade 只读访问）
    MultibandDSP& getDSP() noexcept { return dsp_; }

    /** Phase：统一后端入口（TECH_SPEC §8 / PLAN Phase 6）。
     *  UI 与外部调用方都经此访问后端，不直触 APVTS/DSP 内部。
     *  惰性创建：没用到就不构造（facade 只包引用，成本极低）。 */
    MBBackend& getBackend();

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

    // ---- Phase：电平历史环形缓冲（API readHistory / readMultibandHistory 用）----
    // 音频线程每块写一点（input/output dB），消息线程按需读取。
    // SPSC 无锁：写侧只推 counter（release），读侧先取 counter（acquire）再读数据。
    // 单个 float 的读在本平台原子，故读侧即使与写侧并发也只会读到"稍旧"的点，
    // 不会撕裂——metering 数据可接受。
    static constexpr int kHistoryCapacity = 4096;
    /** 音频线程：推入一点（inputDb / outputDb）。零分配零锁。 */
    void pushHistoryPoint (float inputDb, float outputDb) noexcept;
    /** 读侧：取最近 points 个点（按时间正序）。 */
    void readHistoryPoints (int points, std::vector<float>& inputDb,
                            std::vector<float>& outputDb,
                            unsigned& counter, int& pointsWritten) const;

    /** 供 setStateInformation / MBBackend::loadState 在替换 ValueTree 后同步刷新 DSP 结构、延迟与 UI。 */
    void reapplyStructureAndLatency();

    /** 将持久化或更新后的 AnalyzerSettings 同步下发到 SpectrumEngine。 */
    void pushAnalyzerSettingsToEngine() noexcept;

private:
    void applyStructure();                    // 消息线程：分频/FIR 设计
    void updateLatency() noexcept;            // 消息线程：setLatencySamples
    void readScalars (MultibandDSP::ScalarParams& sp) const noexcept;  // 音频线程
    void pushDisplayTaps (juce::AudioBuffer<float>& buffer,
                          const float* monoPre, const float* monoPost,
                          int numSamples) noexcept;                  // 音频线程

    juce::SpinLock dspLock_;                  // 保护结构切换/prepare 与 processBlock 的并发访问
    MultibandDSP dsp_;
    SpectrumEngine spectrumEngine_;           // 后台分析线程 + 无锁三缓冲
    std::unique_ptr<MBBackend> backend_;      // Phase：统一后端 facade（惰性创建）

    // R32：UI 唤醒标志（parameterChanged 置位 / DisplayView::paint 消费）
    std::atomic<bool> uiWakeFlag_ { false };
    std::function<void (const juce::String&)> uiWakeCallback_;
    // R35（N4）：Analyzer 显示设置（随状态持久化，见 getStateInformation）
    AnalyzerSettings analyzerSettings_;
    UiPackSettings   uiPackSettings_;

    // 电平表（音频线程写原子，UI 做弹道）
    std::atomic<float> inputPeak_  { 0.0f };
    std::atomic<float> outputPeak_ { 0.0f };
    std::array<std::atomic<float>, Param::kNumBands> bandGainDb_ {};

    // Phase：电平历史环形缓冲（SPSC）
    std::array<std::atomic<float>, kHistoryCapacity> historyIn_  {};
    std::array<std::atomic<float>, kHistoryCapacity> historyOut_ {};
    std::atomic<unsigned> historyCounter_ { 0 };

    // 频谱 pre 抽头延迟环（Linear 模式输出晚 L 样本 → pre 延迟 L 使两层对齐）
    std::vector<float> preTapRing_;
    int preTapPos_ = 0;
    std::atomic<int> preTapLatency_ { 0 };
    std::vector<float> monoPre_, monoPost_;   // 预分配单声道抽头缓冲

    double sampleRate_ = 48000.0;             // prepareToPlay 缓存（勿依赖 getSampleRate：
                                              // 部分宿主/测试路径未调 setPlayConfigDetails）
    int lastLatency_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrunchMBAudioProcessor)
};
