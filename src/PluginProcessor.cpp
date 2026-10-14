#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================
// CrunchMBAudioProcessor 实现
// ============================================================

namespace
{
    // ⚠️ JUCE 8 语义：apvts.getRawParameterValue() 返回 getRawDenormalisedValue()
    // （反归一化值）。对 AudioParameterChoice 即为**索引本身**（0/1/2…），
    // 对 Bool 为 0/1，对 Float 为物理值——都不是 0..1 归一化值。
    // （2026-09-23 修复：此前误按归一化值 ×(n−1) 换算，导致默认 Medium→High、
    //   Slope24→Slope48，Linear 延迟翻倍 3072→6144。）
    inline int choiceIndex (const std::atomic<float>* raw) noexcept
    {
        return (int) juce::roundToInt (raw->load());
    }
    inline bool boolParam (const std::atomic<float>* raw) noexcept
    {
        return raw->load() >= 0.5f;
    }
    inline bool isStructuralParam (const juce::String& id) noexcept
    {
        return id == Param::ID::xover1 || id == Param::ID::xover2 || id == Param::ID::xover3
            || id == Param::ID::xoverSlope || id == Param::ID::phaseMode
            || id == Param::ID::linearQuality || id == Param::ID::lookahead
            || id == Param::ID::oversampling;      // 2026-10-14
    }
}

CrunchMBAudioProcessor::CrunchMBAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                              .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                              .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", Param::createLayout())
{
    // 结构/延迟参数监听（消息线程）——FIR 设计禁上音频线程（PLAN L5）
    for (const char* id : { Param::ID::xover1, Param::ID::xover2, Param::ID::xover3,
                            Param::ID::xoverSlope, Param::ID::phaseMode,
                            Param::ID::linearQuality, Param::ID::lookahead,
                            Param::ID::oversampling })
        apvts.addParameterListener (id, this);
}

CrunchMBAudioProcessor::~CrunchMBAudioProcessor()
{
    for (const char* id : { Param::ID::xover1, Param::ID::xover2, Param::ID::xover3,
                            Param::ID::xoverSlope, Param::ID::phaseMode,
                            Param::ID::linearQuality, Param::ID::lookahead,
                            Param::ID::oversampling })
        apvts.removeParameterListener (id, this);
}

void CrunchMBAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    dsp_.prepare (sampleRate_, samplesPerBlock, 2);

    // ---- Phase 4.1 频谱引擎 ----
    // ⚠️ 不要在这里 setUIActive(false)：宿主每次开始播放/切设备都会调 prepareToPlay，
    // 会把分析线程停掉 → 用户看到的"频谱消失且不动，必须关开编辑器才好"（2026-09-23）。
    // 编辑器开关状态由 setEditorActive() 单独管理，prepareToPlay 不碰它。
    spectrumEngine_.setSampleRate (sampleRate_);

    // pre 抽头延迟环：容量 = 最大分频延迟(6144) + 最大 lookahead(3ms @ 4× = 576)
    //   + 过采样滤波器延迟余量(256) + 块余量。2026-10-14：块容量下限取 4096——
    //   宿主偶发超大块（>prepare 时的 samplesPerBlock）时不会让频谱/电平表静默假死。
    const int laMax = (int) std::ceil (0.003 * sampleRate_ * 4.0);
    const int ringSize = 6144 + laMax + 256 + juce::jmax (samplesPerBlock, 4096);
    preTapRing_.assign ((size_t) ringSize, 0.0f);
    preTapPos_ = 0;
    preTapLatency_ = 0;
    monoPre_.assign ((size_t) juce::jmax (4096, samplesPerBlock), 0.0f);
    monoPost_.assign ((size_t) juce::jmax (4096, samplesPerBlock), 0.0f);

    applyStructure();
    updateLatency();
}

void CrunchMBAudioProcessor::releaseResources()
{
    dsp_.reset();
}

bool CrunchMBAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 默认布局 = 立体声主进/主出 + 侧链（TECH_SPEC §1）；
    // 协商宽容化（2026-09-23，FL Studio 实测）：FL 在效果器未接线时会先以
    // "主输入禁用"或单声道布局实例化，严格拒绝会导致 FL 报
    // "problem opening the plugin ... unknown reason"（优雅失败、无崩溃）。
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo()
        && mainOut != juce::AudioChannelSet::mono())
        return false;

    if (! (mainIn == mainOut || mainIn.isDisabled()))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! (sc.isDisabled()
               || sc == juce::AudioChannelSet::stereo()
               || sc == juce::AudioChannelSet::mono()))
            return false;
    }
    return true;
}

void CrunchMBAudioProcessor::applyStructure()
{
    MultibandDSP::StructureParams sp;
    sp.xover1  = apvts.getRawParameterValue (Param::ID::xover1)->load();
    sp.xover2  = apvts.getRawParameterValue (Param::ID::xover2)->load();
    sp.xover3  = apvts.getRawParameterValue (Param::ID::xover3)->load();
    sp.slopeDb = Param::slopeIndexToDb (choiceIndex (apvts.getRawParameterValue (Param::ID::xoverSlope)));
    sp.mode    = (Param::PhaseMode)     choiceIndex (apvts.getRawParameterValue (Param::ID::phaseMode));
    sp.quality = (Param::LinearQuality) choiceIndex (apvts.getRawParameterValue (Param::ID::linearQuality));
    sp.oversampling = Param::oversamplingIndexToFactor (
        choiceIndex (apvts.getRawParameterValue (Param::ID::oversampling)));   // 2026-10-14
    dsp_.setStructure (sp);
}

void CrunchMBAudioProcessor::updateLatency() noexcept
{
    // 延迟 = 分频延迟 + lookahead + 过采样。lookahead 部分必须从参数直接算：
    // dsp_ 的 laSamples_ 是音频线程状态（updateScalars 写入），切换瞬间
    // 消息线程可能读到旧值——2026-09-23 用户实测"切 1ms/3ms 延迟不变"即此因。
    // 2026-10-14：lookahead 与过采样延迟都按 procRate_ = sampleRate_ × factor 换算。
    const int osFactor = dsp_.getOversamplingFactor();
    const int laSamples = (int) std::lround (
        Param::lookaheadIndexToMs (choiceIndex (apvts.getRawParameterValue (Param::ID::lookahead)))
        * 0.001 * sampleRate_ * (double) osFactor);
    const int lat = dsp_.getCrossoverLatencySamples() + juce::jmax (0, laSamples)
                  + dsp_.getOversamplingLatencySamples();
    if (lat != lastLatency_)
    {
        setLatencySamples (lat);
        lastLatency_ = lat;
        preTapLatency_ = lat;      // 频谱 pre 抽头对齐用（音频线程读）
    }
}

void CrunchMBAudioProcessor::setEditorActive (bool active) noexcept
{
    spectrumEngine_.setUIActive (active);
}

void CrunchMBAudioProcessor::handleAsyncUpdate()
{
    applyStructure();      // 消息线程：含 Linear FIR 设计
    updateLatency();
}

void CrunchMBAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (isStructuralParam (parameterID))
        triggerAsyncUpdate();          // 合并到下一消息循环
}

void CrunchMBAudioProcessor::readScalars (MultibandDSP::ScalarParams& sp) const noexcept
{
    sp.inputGainDb  = apvts.getRawParameterValue (Param::ID::inputGain)->load();
    sp.outputGainDb = apvts.getRawParameterValue (Param::ID::outputGain)->load();
    sp.lookaheadMs  = (int) juce::roundToInt (Param::lookaheadIndexToMs (
        choiceIndex (apvts.getRawParameterValue (Param::ID::lookahead))));

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        auto& bp = sp.bands[(size_t) b];
        bp.thresholdDb = apvts.getRawParameterValue (Param::bandThreshold (b))->load();
        bp.rangeDb     = apvts.getRawParameterValue (Param::bandRange (b))->load();
        bp.ratio       = apvts.getRawParameterValue (Param::bandRatio (b))->load();
        bp.attackMs    = apvts.getRawParameterValue (Param::bandAttack (b))->load();
        bp.releaseMs   = apvts.getRawParameterValue (Param::bandRelease (b))->load();
        bp.makeupDb    = apvts.getRawParameterValue (Param::bandMakeup (b))->load();
        bp.mixPct      = apvts.getRawParameterValue (Param::bandMix (b))->load();
        bp.kneeDb      = apvts.getRawParameterValue (Param::bandKnee (b))->load();   // 2026-10-14：连续 dB
        // Phase C-6：动态模式 Compress/Expand
        bp.dynamics    = choiceIndex (apvts.getRawParameterValue (Param::bandDynamics (b)));
        // 2026-10-14：侧链滤波 + 立体声链接
        bp.scFree      = choiceIndex (apvts.getRawParameterValue (Param::bandSCFilter (b))) == (int) Param::SCFilter::Free;
        bp.scLowHz     = apvts.getRawParameterValue (Param::bandSCLow  (b))->load();
        bp.scHighHz    = apvts.getRawParameterValue (Param::bandSCHigh (b))->load();
        bp.stereoLink  = apvts.getRawParameterValue (Param::bandStereoLink (b))->load();
        bp.useRms      = choiceIndex (apvts.getRawParameterValue (Param::bandDetector (b))) == (int) Param::Detector::RMS;
        bp.solo        = boolParam (apvts.getRawParameterValue (Param::bandSolo (b)));
        bp.mute        = boolParam (apvts.getRawParameterValue (Param::bandMute (b)));
        bp.delta       = boolParam (apvts.getRawParameterValue (Param::bandDelta (b)));
        bp.ext         = boolParam (apvts.getRawParameterValue (Param::bandExt (b)));
    }
}

void CrunchMBAudioProcessor::pushDisplayTaps (juce::AudioBuffer<float>& buffer,
                                              const float* monoPre, const float* monoPost,
                                              int numSamples) noexcept
{
    // 输入/输出峰值（音频线程原子写，UI 做弹道）
    float inPeak = 0.0f, outPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        inPeak  = juce::jmax (inPeak,  std::abs (monoPre[(size_t) i]));
        outPeak = juce::jmax (outPeak, std::abs (monoPost[(size_t) i]));
    }
    inputPeak_.store (inPeak, std::memory_order_relaxed);
    outputPeak_.store (outPeak, std::memory_order_relaxed);

    // 段平滑增益（仿 EQ 曲线数据源）
    for (int b = 0; b < Param::kNumBands; ++b)
        bandGainDb_[(size_t) b].store (dsp_.getBandGainDb (b), std::memory_order_relaxed);

    // 频谱：pre 抽头经延迟环对齐 post（Linear 模式输出晚 L 样本）
    spectrumEngine_.pushSamples (monoPre, monoPost, numSamples);
    (void) buffer;
}

void CrunchMBAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // TECH_SPEC §4.4

    const int numSamples  = buffer.getNumSamples();
    const int mainInCh    = getMainBusNumInputChannels();
    const int mainOutCh   = getMainBusNumOutputChannels();

    // ---- Enabled 旁通（TECH_SPEC §5.1）：关时直通主进→主出 ----
    if (! boolParam (apvts.getRawParameterValue (Param::ID::enabled)))
    {
        if (mainInCh == 1 && mainOutCh == 2)                    // 单声道进 → 双通同源
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1),
                                               buffer.getReadPointer (0), numSamples);
        for (int ch = juce::jmax (mainInCh, mainOutCh); ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
        return;
    }

    // ---- Phase 4.1：采集 pre 抽头（单声道，经延迟环对齐 post） ----
    // 2026-10-14：块大于抽头缓冲时按容量截断（而非整块跳过）——此前超大块会让
    // 频谱与电平表静默冻结。截断只影响显示精度，不影响音频输出。
    const int tapN = juce::jmin (numSamples, (int) monoPre_.size());
    if (tapN > 0)
    {
        const int chans = juce::jmin (2, mainInCh);
        for (int i = 0; i < tapN; ++i)
        {
            const float m = chans > 1
                ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i))
                : buffer.getSample (0, i);
            // 写入延迟环，读出 L 样本前的值 → pre/post 两层时间对齐
            preTapRing_[(size_t) preTapPos_] = m;
            int rp = preTapPos_ - preTapLatency_;
            if (rp < 0) rp += (int) preTapRing_.size();
            monoPre_[(size_t) i] = preTapRing_[(size_t) rp];
            if (++preTapPos_ >= (int) preTapRing_.size()) preTapPos_ = 0;
        }
    }

    // ---- 侧链总线（输入 bus 1；禁用/无信号时 fallback INT） ----
    // ⚠️ 2026-09-23 修复：此前误用 getBusCount(false)/getBus(false,1)（输出总线），
    // 侧链输入永远取不到 → EXT 恒 fallback INT。
    const float* scPtr[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
    {
        auto* scBus = getBus (true, 1);
        if (scBus != nullptr && scBus->isEnabled())
        {
            const auto scBuf = getBusBuffer (buffer, true, 1);
            if (scBuf.getNumChannels() > 0 && scBuf.getNumSamples() >= numSamples)
            {
                scCh = juce::jmin (2, scBuf.getNumChannels());
                for (int ch = 0; ch < scCh; ++ch)
                    scPtr[ch] = scBuf.getReadPointer (ch);
            }
        }
    }

    // ---- 主通道（单声道输入 → 双通同源） ----
    float* chPtr[2] = { nullptr, nullptr };
    chPtr[0] = buffer.getWritePointer (0);
    chPtr[1] = mainInCh > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    // ---- 标量参数（APVTS 原子读，TECH_SPEC §4.2）→ DSP ----
    // ⚠️ 2026-09-23 修复：此前只 readScalars 而未 updateScalars，导致全部
    // 标量参数（阈值/比率/Mix/监听门/侧链 EXT/前瞻/输入输出增益）不生效。
    MultibandDSP::ScalarParams sp;
    readScalars (sp);
    dsp_.updateScalars (sp);
    dsp_.process (chPtr, numSamples, scCh > 0 ? scPtr : nullptr, scCh);

    // ---- 清理非主通道残留（输入侧多余通道，防陈旧数据） ----
    for (int ch = juce::jmax (mainInCh, mainOutCh); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- Phase 4.1：采集 post 抽头并推送频谱/电平 ----
    if (tapN > 0)
    {
        const int chans = juce::jmin (2, mainInCh);
        for (int i = 0; i < tapN; ++i)
            monoPost_[(size_t) i] = chans > 1
                ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i))
                : buffer.getSample (0, i);
        pushDisplayTaps (buffer, monoPre_.data(), monoPost_.data(), tapN);
    }
}

void CrunchMBAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CrunchMBAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));   // 监听器会触发结构重应用
}

juce::AudioProcessorEditor* CrunchMBAudioProcessor::createEditor()
{
    return new CrunchMBAudioProcessorEditor (*this);
}

// 入口工厂
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrunchMBAudioProcessor();
}
