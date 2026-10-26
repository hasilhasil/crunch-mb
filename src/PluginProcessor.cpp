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
    // Phase（R19c）：本体委托给 Param::choiceIndexFromValue——该换算原先在
    // processor 与 DisplayView 各有一份（见 §8.2 R19b 的同型问题）。
    inline int choiceIndex (const std::atomic<float>* raw) noexcept
    {
        return Param::choiceIndexFromValue (raw->load());
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
    // R32：给**全部**参数注册监听器，而不是只注册结构参数。
    // 原先只注册 8 个结构参数 → parameterChanged 对 Threshold/Makeup/Mix 等
    // 永不触发 → UI 唤醒回调收不到 → Phase E-2 空闲降频后宿主自动化改这些
    // 参数时画面不更新（用户实测"UI 卡住完全不刷新"）。
    for (const auto& id : Param::allIds())
        apvts.addParameterListener (id, this);
}

CrunchMBAudioProcessor::~CrunchMBAudioProcessor()
{
    for (const auto& id : Param::allIds())
        apvts.removeParameterListener (id, this);
}

void CrunchMBAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    {
        const juce::SpinLock::ScopedLockType sl (dspLock_);
        dsp_.prepare (sampleRate_, samplesPerBlock, 2);
    }

    // ---- Phase 4.1 频谱引擎 ----
    // ⚠️ 不要在这里 setUIActive(false)：宿主每次开始播放/切设备都会调 prepareToPlay，
    // 会把分析线程停掉 → 用户看到的"频谱消失且不动，必须关开编辑器才好"（2026-09-23）。
    // 编辑器开关状态由 setEditorActive() 单独管理，prepareToPlay 不碰它。
    spectrumEngine_.setSampleRate (sampleRate_);

    // pre 抽头延迟环：容量 = 最大分频延迟(6144) + 最大 lookahead(3ms @ 1x)
    //   + 过采样滤波器延迟余量(256) + 块余量。
    const int laMax = (int) std::ceil (0.003 * sampleRate_ * 4.0);
    const int ringSize = 6144 + laMax + 256 + juce::jmax (samplesPerBlock, 4096);
    preTapRing_.assign ((size_t) ringSize, 0.0f);
    preTapPos_ = 0;
    preTapLatency_.store (0, std::memory_order_relaxed);
    monoPre_.assign ((size_t) juce::jmax (4096, samplesPerBlock), 0.0f);
    monoPost_.assign ((size_t) juce::jmax (4096, samplesPerBlock), 0.0f);

    // R31 & MISC-03 修复：只在此处调用一次 applyStructure() + updateLatency()（消除原重复调用）
    applyStructure();
    updateLatency();
}

void CrunchMBAudioProcessor::releaseResources()
{
    const juce::SpinLock::ScopedLockType sl (dspLock_);
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

    const juce::SpinLock::ScopedLockType sl (dspLock_);
    dsp_.setStructure (sp);
}

void CrunchMBAudioProcessor::updateLatency() noexcept
{
    // 修复 BUG-02：setLatencySamples() 与 preTapRing_ 均以 **1× 宿主采样率样本数** 为单位。
    // dsp_.getCrossoverLatencySamples() 与 getOversamplingLatencySamples() 均已返回 1× 样本数，
    // lookahead 同样按 1× sampleRate_ 计算（不可再乘 osFactor）。
    const int laSamples1x = (int) std::lround (
        Param::lookaheadIndexToMs (choiceIndex (apvts.getRawParameterValue (Param::ID::lookahead)))
        * 0.001 * sampleRate_);
    const int lat = dsp_.getCrossoverLatencySamples() + juce::jmax (0, laSamples1x)
                  + dsp_.getOversamplingLatencySamples();
    preTapLatency_.store (lat, std::memory_order_release);
    if (lat != lastLatency_)
    {
        setLatencySamples (lat);
        lastLatency_ = lat;
    }
}

void CrunchMBAudioProcessor::reapplyStructureAndLatency()
{
    applyStructure();
    updateLatency();
    uiWakeFlag_.store (true, std::memory_order_release);
}

MBBackend& CrunchMBAudioProcessor::getBackend()
{
    if (backend_ == nullptr)
    {
        backend_ = std::make_unique<MBBackend> (*this);
        backend_->buildParamCache();
    }
    return *backend_;
}

void CrunchMBAudioProcessor::setEditorActive (bool active) noexcept
{
    spectrumEngine_.setUIActive (active);
}

void CrunchMBAudioProcessor::setParamPhysicalForTest (const juce::String& id, float physical)
{
    if (auto* p = apvts.getParameter (id))
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            p->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (physical));
        else
            p->setValueNotifyingHost (physical);
    }
}

// ---------------- Phase：电平历史环形缓冲 ----------------
void CrunchMBAudioProcessor::pushHistoryPoint (float inputDb, float outputDb) noexcept
{
    const unsigned cnt = historyCounter_.load (std::memory_order_relaxed);
    const unsigned idx = cnt % (unsigned) kHistoryCapacity;
    historyIn_[(size_t) idx].store (inputDb,  std::memory_order_relaxed);
    historyOut_[(size_t) idx].store (outputDb, std::memory_order_relaxed);
    // 修复 P1-10：historyCounter_ 保持单调递增（仅写槽位时取模），避免每 4096 块回绕归 1 清空历史
    historyCounter_.store (cnt + 1u, std::memory_order_release);
}

void CrunchMBAudioProcessor::readHistoryPoints (int points, std::vector<float>& inputDb,
                                                std::vector<float>& outputDb,
                                                unsigned& counter, int& pointsWritten) const
{
    inputDb.clear();
    outputDb.clear();
    counter = 0;
    pointsWritten = 0;

    const unsigned cnt = historyCounter_.load (std::memory_order_acquire);
    if (cnt == 0)
        return;

    const int want = juce::jlimit (1, kHistoryCapacity, points);
    const int avail = (int) juce::jmin ((unsigned) kHistoryCapacity, cnt);
    const int n = juce::jmin (want, avail);

    // 取最近 n 个点（时间正序：旧 → 新）
    for (int i = 0; i < n; ++i)
    {
        // 最旧的点索引 = (cnt - n + i) mod capacity
        const unsigned raw = cnt - (unsigned) n + (unsigned) i;
        const unsigned slot = raw % (unsigned) kHistoryCapacity;
        inputDb.push_back  (historyIn_[(size_t) slot].load (std::memory_order_relaxed));
        outputDb.push_back (historyOut_[(size_t) slot].load (std::memory_order_relaxed));
    }
    counter = cnt;
    pointsWritten = n;
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

    // R32：任何参数变化都通知 UI"该醒了"。
    // Phase E-2 空闲降频后 UI 唯一的检测点也是定时器，这里置原子标志兜底。
    uiWakeFlag_.store (true, std::memory_order_release);

    // R35（N4）：带 id 的精确通知，让 UI 只刷新对应的参数副本。
    // ⚠️ 只在**消息线程**调用回调：宿主做 sample-accurate 自动化时 setValue
    //    可能发生在音频线程，那里绝不能碰 juce::Component，也不该构造
    //    juce::String 拷贝。音频线程路径只置上面的 uiWakeFlag_，
    //    由 UI 定时器做一次全量同步（见 DisplayView::syncUiFromParams）。
    if (uiWakeCallback_ != nullptr && juce::MessageManager::existsAndIsCurrentThread())
        uiWakeCallback_ (parameterID);
}

void CrunchMBAudioProcessor::readScalars (MultibandDSP::ScalarParams& sp) const noexcept
{
    sp.inputGainDb  = apvts.getRawParameterValue (Param::ID::inputGain)->load();
    sp.outputGainDb = apvts.getRawParameterValue (Param::ID::outputGain)->load();
    sp.globalMixPct = apvts.getRawParameterValue (Param::ID::globalMix)->load();
    // Auto-Gain（Future MB 对标）。Bool 参数实际值 0/1。
    sp.autoGain = apvts.getRawParameterValue (Param::ID::autoGain)->load() >= 0.5f;
    // Monitor（Future MB 对标）。Choice 参数实际值 = 选项索引。
    sp.monitorMode = Param::choiceIndexFromValue (
        apvts.getRawParameterValue (Param::ID::monitorMode)->load());
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

    // Phase：电平历史一点（每块一个点，SPSC 环形缓冲）
    pushHistoryPoint (juce::Decibels::gainToDecibels (inPeak,  -120.0f),
                      juce::Decibels::gainToDecibels (outPeak, -120.0f));

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

    // FL Studio 等宿主在未接线槽位可能禁用 mainIn：此时直接清零输出缓冲返回
    if (mainInCh <= 0 || buffer.getNumChannels() <= 0 || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    // ---- Enabled 旁通（TECH_SPEC §5.1）：关时直通主进→主出 ----
    if (! boolParam (apvts.getRawParameterValue (Param::ID::enabled)))
    {
        if (mainInCh == 1 && mainOutCh == 2 && buffer.getNumChannels() >= 2)   // 单声道进 → 双通同源
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1),
                                               buffer.getReadPointer (0), numSamples);
        for (int ch = juce::jmax (mainInCh, mainOutCh); ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
        return;
    }

    // 修复 P0-4：使用主总线通道数（而非包含侧链的 buffer.getNumChannels()）收敛 safeCh，
    // 防止 Mono 主总线 + Stereo 侧链（总 3 通道）时把侧链左声道（ch 1）误当主输出右声道处理。
    const int mainBusCh = juce::jmax (mainInCh, mainOutCh);
    const int safeCh    = juce::jlimit (1, 2, juce::jmin (mainBusCh, buffer.getNumChannels()));
    if (mainInCh == 1 && safeCh == 2)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (1),
                                           buffer.getReadPointer (0), numSamples);

    const int tapN = juce::jmin (numSamples, (int) monoPre_.size());

    // ---- 侧链总线（输入 bus 1；禁用/无信号时 fallback INT） ----
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

    // ---- Phase 4.1：采集 pre 抽头（单声道，经延迟环对齐 post） ----
    if (tapN > 0 && ! preTapRing_.empty())
    {
        const int chans = safeCh;
        const int ringCap = (int) preTapRing_.size();
        const int tapLat  = juce::jlimit (0, ringCap - 1,
                                          preTapLatency_.load (std::memory_order_acquire));
        for (int i = 0; i < tapN; ++i)
        {
            const float m = chans > 1
                ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i))
                : buffer.getSample (0, i);
            // 写入延迟环，读出 L 样本前的值 → pre/post 两层时间对齐
            preTapRing_[(size_t) preTapPos_] = m;
            int rp = preTapPos_ - tapLat;
            if (rp < 0) rp += ringCap;
            monoPre_[(size_t) i] = preTapRing_[(size_t) rp];
            if (++preTapPos_ >= ringCap) preTapPos_ = 0;
        }
    }

    // ---- 主通道（单声道输入 → 双通同源）----
    float* chPtr[2] = { nullptr, nullptr };
    chPtr[0] = buffer.getWritePointer (0);
    chPtr[1] = safeCh > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    // ---- 标量参数（APVTS 原子读，TECH_SPEC §4.2）→ DSP ----
    MultibandDSP::ScalarParams sp;
    readScalars (sp);
    {
        const juce::SpinLock::ScopedLockType sl (dspLock_);
        dsp_.updateScalars (sp);
        dsp_.process (chPtr, numSamples, scCh > 0 ? scPtr : nullptr, scCh, safeCh);
    }

    // ---- 清理非主通道残留（输入侧多余通道，防陈旧数据） ----
    for (int ch = safeCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- Phase 4.1：采集 post 抽头并推送频谱/电平 ----
    if (tapN > 0)
    {
        const int chans = safeCh;
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
    {
        // R35（N14）：Analyzer 的 6 项显示设置不是 APVTS 参数，
        // 这里作为 XML 属性一起存。否则关开编辑器 / 宿主重载工程后
        // Pre/Post/Resolution/Speed/Tilt/Freeze 全部回到默认。
        xml->setAttribute ("uiAnalyzerPre",    analyzerSettings_.preOn ? 1 : 0);
        xml->setAttribute ("uiAnalyzerPost",   analyzerSettings_.postOn ? 1 : 0);
        xml->setAttribute ("uiAnalyzerFft",    analyzerSettings_.fftSize);
        xml->setAttribute ("uiAnalyzerSpeed",  analyzerSettings_.speed);
        xml->setAttribute ("uiAnalyzerTilt",   (double) analyzerSettings_.tilt);
        xml->setAttribute ("uiAnalyzerFrozen", analyzerSettings_.frozen ? 1 : 0);
        copyXmlToBinary (*xml, destData);
    }
}

void CrunchMBAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            // R35（N14）：先取回显示设置（缺失时保留当前值，兼容旧工程）。
            if (xml->hasAttribute ("uiAnalyzerPre"))
            {
                AnalyzerSettings s;
                s.preOn   = xml->getIntAttribute ("uiAnalyzerPre", 1) != 0;
                s.postOn  = xml->getIntAttribute ("uiAnalyzerPost", 1) != 0;
                s.fftSize = juce::jlimit (1024, 8192, xml->getIntAttribute ("uiAnalyzerFft", 2048));
                s.speed   = juce::jlimit (0, 2, xml->getIntAttribute ("uiAnalyzerSpeed", 1));
                s.tilt    = (float) xml->getDoubleAttribute ("uiAnalyzerTilt", 0.0);
                s.frozen  = xml->getIntAttribute ("uiAnalyzerFrozen", 0) != 0;
                analyzerSettings_ = s;
            }

            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            reapplyStructureAndLatency();
        }
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
