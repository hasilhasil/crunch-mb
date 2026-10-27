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
        return id == Param::ID::enabled
            || id == Param::ID::xover1 || id == Param::ID::xover2 || id == Param::ID::xover3
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

    loadGlobalUiPrefs();
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
    pushAnalyzerSettingsToEngine();

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
    {
        const juce::SpinLock::ScopedLockType sl (dspLock_);
        dsp_.reset();
    }
    inputPeak_.store  (0.0f, std::memory_order_relaxed);
    outputPeak_.store (0.0f, std::memory_order_relaxed);
    for (auto& g : bandGainDb_)
        g.store (0.0f, std::memory_order_relaxed);
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
    // setLatencySamples() 与 preTapRing_ 均以 **1× 宿主采样率样本数** 为单位。
    // dsp_.getCrossoverLatencySamples() 与 getOversamplingLatencySamples() 均已返回 1× 样本数，
    // lookahead 同样按 1× sampleRate_ 计算（不可再乘 osFactor）。
    // 当插件总开关 enabled 关闭时，processBlock 直通 0 延迟干声，PDC 上报同步归 0。
    const bool en = boolParam (apvts.getRawParameterValue (Param::ID::enabled));
    const int laSamples1x = (int) std::lround (
        Param::lookaheadIndexToMs (choiceIndex (apvts.getRawParameterValue (Param::ID::lookahead)))
        * 0.001 * sampleRate_);
    const int lat = en
        ? (dsp_.getCrossoverLatencySamples() + juce::jmax (0, laSamples1x)
           + dsp_.getOversamplingLatencySamples())
        : 0;
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
    pushAnalyzerSettingsToEngine();
    uiWakeFlag_.store (true, std::memory_order_release);
}

void CrunchMBAudioProcessor::pushAnalyzerSettingsToEngine() noexcept
{

    spectrumEngine_.setResolution (analyzerSettings_.fftSize);
    spectrumEngine_.setReleaseMs (SpectrumAnalyzer::speedToReleaseMs (analyzerSettings_.speed));
    spectrumEngine_.setTilt (analyzerSettings_.tilt);
    spectrumEngine_.setFrozen (analyzerSettings_.frozen);
}

namespace
{
    bool gGlobalUiPrefsEnabled = true;
}

void CrunchMBAudioProcessor::setGlobalUiPrefsEnabledForTest (bool enabled) noexcept
{
    gGlobalUiPrefsEnabled = enabled;
}

juce::File CrunchMBAudioProcessor::globalUiPrefsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Kilo")
        .getChildFile ("CrunchMB")
        .getChildFile ("ui_preferences.xml");
}

void CrunchMBAudioProcessor::loadGlobalUiPrefs() noexcept
{
    if (! gGlobalUiPrefsEnabled)
        return;
    const auto file = globalUiPrefsFile();
    if (! file.existsAsFile())
        return;
    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName ("CrunchMBUiPrefs"))
        {
            UiPackSettings up;
            up.themeId   = juce::jlimit (0, 1, xml->getIntAttribute ("uiThemePack",   0));
            up.paletteId = juce::jlimit (0, 5, xml->getIntAttribute ("uiPalettePack", 0));
            up.layoutId  = juce::jlimit (0, 1, xml->getIntAttribute ("uiLayoutPack",  0));
            up.localeId  = juce::jlimit (0, 1, xml->getIntAttribute ("uiLocalePack",  0));
            uiPackSettings_ = up;
        }
    }
}

void CrunchMBAudioProcessor::saveGlobalUiPrefs() const noexcept
{
    if (! gGlobalUiPrefsEnabled)
        return;
    const auto file = globalUiPrefsFile();
    file.getParentDirectory().createDirectory();
    juce::XmlElement xml ("CrunchMBUiPrefs");
    xml.setAttribute ("uiThemePack",   uiPackSettings_.themeId);
    xml.setAttribute ("uiPalettePack", uiPackSettings_.paletteId);
    xml.setAttribute ("uiLayoutPack",  uiPackSettings_.layoutId);
    xml.setAttribute ("uiLocalePack",  uiPackSettings_.localeId);
    xml.writeTo (file);
}

void CrunchMBAudioProcessor::setUiPackSettings (const UiPackSettings& s, bool persistGlobal) noexcept
{
    uiPackSettings_ = s;
    if (persistGlobal)
        saveGlobalUiPrefs();
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
    // historyCounter_ 保持单调递增（仅写槽位时取模），避免每 4096 块回绕归 1 清空历史
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

    // R35（N4）：带 id 的精确通知，让 UI 只刷新对应的参数副本。
    // ⚠️ 只在**消息线程**调用回调：宿主做 sample-accurate 自动化时 setValue
    //    可能发生在音频线程，那里绝不能碰 juce::Component，也不该构造
    //    juce::String 拷贝。音频线程路径（或编辑器未挂载回调时）置 uiWakeFlag_，
    //    由 UI 定时器做一次全量同步（见 DisplayView::syncUiFromParams）。
    if (uiWakeCallback_ != nullptr && juce::MessageManager::existsAndIsCurrentThread())
        uiWakeCallback_ (parameterID);
    else
        uiWakeFlag_.store (true, std::memory_order_release);
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

    // FL Studio 等宿主在未接线槽位可能禁用 mainIn：此时直接清零输出缓冲与峰值状态返回
    if (mainInCh <= 0 || buffer.getNumChannels() <= 0 || numSamples <= 0)
    {
        buffer.clear();
        inputPeak_.store  (0.0f, std::memory_order_relaxed);
        outputPeak_.store (0.0f, std::memory_order_relaxed);
        for (auto& g : bandGainDb_)
            g.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // 使用主总线通道数（而非包含侧链的 buffer.getNumChannels()）收敛 safeCh，
    // 防止 Mono 主总线 + Stereo 侧链（总 3 通道）时把侧链左声道（ch 1）误当主输出右声道处理。
    const int mainBusCh = juce::jmax (mainInCh, mainOutCh);
    const int safeCh    = juce::jlimit (1, 2, juce::jmin (mainBusCh, buffer.getNumChannels()));
    const int tapN      = juce::jmin (numSamples, (int) monoPre_.size());

    // ---- Enabled 旁通（TECH_SPEC §5.1）：关时直通主进→主出，同时更新 IN/OUT 峰值并归零压缩增益 ----
    if (! boolParam (apvts.getRawParameterValue (Param::ID::enabled)))
    {
        if (mainInCh == 1 && safeCh == 2)                    // 单声道进 → 双通同源
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1),
                                               buffer.getReadPointer (0), numSamples);
        for (int ch = safeCh; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        float pk = 0.0f;
        for (int i = 0; i < tapN; ++i)
        {
            const float m = safeCh > 1
                ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i))
                : buffer.getSample (0, i);
            monoPre_[(size_t) i]  = m;
            monoPost_[(size_t) i] = m;
            pk = juce::jmax (pk, std::abs (m));
        }
        inputPeak_.store  (pk, std::memory_order_relaxed);
        outputPeak_.store (pk, std::memory_order_relaxed);
        for (auto& g : bandGainDb_)
            g.store (0.0f, std::memory_order_relaxed);
        if (tapN > 0)
            spectrumEngine_.pushSamples (monoPre_.data(), monoPost_.data(), tapN);
        return;
    }

    if (mainInCh == 1 && safeCh == 2)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (1),
                                           buffer.getReadPointer (0), numSamples);

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
        xml->setAttribute ("uiThemePack",      uiPackSettings_.themeId);
        xml->setAttribute ("uiPalettePack",    uiPackSettings_.paletteId);
        xml->setAttribute ("uiLayoutPack",     uiPackSettings_.layoutId);
        xml->setAttribute ("uiLocalePack",     uiPackSettings_.localeId);
        xml->setAttribute ("uiPresetIndex",    currentPresetIndex_);
        xml->setAttribute ("uiAbSlot",         activeAbSlot_);
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
            if (xml->hasAttribute ("uiThemePack"))
            {
                UiPackSettings up;
                up.themeId   = juce::jlimit (0, 1, xml->getIntAttribute ("uiThemePack", 0));
                up.paletteId = juce::jlimit (0, 5, xml->getIntAttribute ("uiPalettePack", 0));
                up.layoutId  = juce::jlimit (0, 1, xml->getIntAttribute ("uiLayoutPack", 0));
                up.localeId  = juce::jlimit (0, 1, xml->getIntAttribute ("uiLocalePack", 0));
                uiPackSettings_ = up;
            }
            if (xml->hasAttribute ("uiPresetIndex"))
                currentPresetIndex_ = juce::jlimit (0, kNumFactoryPresets - 1, xml->getIntAttribute ("uiPresetIndex", 0));
            if (xml->hasAttribute ("uiAbSlot"))
                activeAbSlot_ = juce::jlimit (0, 1, xml->getIntAttribute ("uiAbSlot", 0));

            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            reapplyStructureAndLatency();
        }
}

void CrunchMBAudioProcessor::setAbSlot (bool useSlotB)
{
    const int target = useSlotB ? 1 : 0;
    if (target == activeAbSlot_)
        return;

    // 保存当前槽位的完整参数快照
    abSlotState_[activeAbSlot_]       = apvts.copyState();
    abSlotInitialized_[activeAbSlot_] = true;

    // 若目标槽尚未初始化，先以当前状态初始化（便于直接在此基础上微调对比）
    if (! abSlotInitialized_[target] || ! abSlotState_[target].isValid())
    {
        abSlotState_[target]       = abSlotState_[activeAbSlot_].createCopy();
        abSlotInitialized_[target] = true;
    }

    activeAbSlot_ = target;
    apvts.replaceState (abSlotState_[activeAbSlot_].createCopy());
    reapplyStructureAndLatency();
    if (uiWakeCallback_ != nullptr && juce::MessageManager::existsAndIsCurrentThread())
        uiWakeCallback_ ({});
}

void CrunchMBAudioProcessor::toggleAbSlot()
{
    setAbSlot (activeAbSlot_ == 0);
}

void CrunchMBAudioProcessor::applyFactoryPreset (int index)
{
    const int idx = juce::jlimit (0, kNumFactoryPresets - 1, index);
    currentPresetIndex_ = idx;

    auto& api = getBackend();
    auto setBand = [&] (int b, float th, float rng, float rat, float atk, float rel,
                        float mk, float mix, float knee, int det = 0, int dyn = 0)
    {
        api.setParam (Param::bandThreshold (b), th);
        api.setParam (Param::bandRange     (b), rng);
        api.setParam (Param::bandRatio     (b), rat);
        api.setParam (Param::bandAttack    (b), atk);
        api.setParam (Param::bandRelease   (b), rel);
        api.setParam (Param::bandMakeup    (b), mk);
        api.setParam (Param::bandMix       (b), mix);
        api.setParam (Param::bandKnee      (b), knee);
        api.setParam (Param::bandDetector  (b), (float) det);
        api.setParam (Param::bandDynamics  (b), (float) dyn);
        api.setParam (Param::bandSCFilter  (b), 0.0f);
        api.setParam (Param::bandSolo      (b), 0.0f);
        api.setParam (Param::bandMute      (b), 0.0f);
        api.setParam (Param::bandDelta     (b), 0.0f);
        api.setParam (Param::bandExt       (b), 0.0f);
    };

    // 先复位全局默认参数
    api.setParam (Param::ID::enabled,       1.0f);
    api.setParam (Param::ID::phaseMode,     0.0f); // Natural
    api.setParam (Param::ID::linearQuality, 1.0f); // Medium
    api.setParam (Param::ID::xoverSlope,    1.0f); // 24 dB/oct
    api.setCrossovers (200.0f, 1000.0f, 6000.0f);
    api.setParam (Param::ID::inputGain,     0.0f);
    api.setParam (Param::ID::outputGain,    0.0f);
    api.setParam (Param::ID::lookahead,     0.0f);
    api.setParam (Param::ID::oversampling,  0.0f);
    api.setParam (Param::ID::globalMix,     100.0f);
    api.setParam (Param::ID::autoGain,      0.0f);
    api.setParam (Param::ID::monitorMode,   0.0f);

    switch (idx)
    {
        case 0: // Default
            for (int b = 0; b < 4; ++b)
                setBand (b, -20.0f, -6.0f, 4.0f, 10.0f, 100.0f, 0.0f, 100.0f, 6.0f, 0, 0);
            break;

        case 1: // Gentle Bus Glue
            api.setParam (Param::ID::xoverSlope, 0.0f); // 12 dB/oct
            api.setCrossovers (160.0f, 900.0f, 5200.0f);
            setBand (0, -18.0f, -4.0f, 2.0f, 25.0f, 180.0f, 1.0f, 100.0f, 12.0f, 1, 0);
            setBand (1, -20.0f, -3.5f, 2.0f, 18.0f, 140.0f, 0.8f, 100.0f, 12.0f, 1, 0);
            setBand (2, -21.0f, -3.5f, 2.2f, 14.0f, 120.0f, 0.8f, 100.0f, 12.0f, 1, 0);
            setBand (3, -22.0f, -3.0f, 2.0f, 10.0f, 100.0f, 1.2f, 100.0f, 12.0f, 1, 0);
            break;

        case 2: // Transparent Leveler
            api.setParam (Param::ID::phaseMode, 2.0f); // Linear Phase
            api.setCrossovers (180.0f, 1100.0f, 6500.0f);
            for (int b = 0; b < 4; ++b)
                setBand (b, -24.0f, -5.0f, 2.5f, 15.0f, 160.0f, 1.5f, 100.0f, 18.0f, 1, 0);
            break;

        case 3: // Mastering Warmth & Air
            api.setParam (Param::ID::phaseMode, 2.0f); // Linear Phase
            api.setCrossovers (120.0f, 850.0f, 7500.0f);
            setBand (0, -16.0f, -4.5f, 3.0f, 30.0f, 220.0f, 1.5f, 100.0f, 10.0f, 1, 0);
            setBand (1, -20.0f, -3.0f, 2.0f, 20.0f, 150.0f, 0.8f, 100.0f, 12.0f, 1, 0);
            setBand (2, -22.0f, -3.5f, 2.2f, 12.0f, 120.0f, 0.5f, 100.0f, 12.0f, 1, 0);
            setBand (3, -24.0f, -3.0f, 2.0f,  8.0f,  90.0f, 2.2f, 100.0f, 15.0f, 1, 0);
            break;

        case 4: // Linear Phase Master
            api.setParam (Param::ID::phaseMode,     2.0f);
            api.setParam (Param::ID::linearQuality, 2.0f); // High
            api.setCrossovers (140.0f, 1000.0f, 6000.0f);
            setBand (0, -18.0f, -5.0f, 3.2f, 22.0f, 180.0f, 1.2f, 100.0f,  9.0f, 1, 0);
            setBand (1, -20.0f, -4.0f, 2.5f, 16.0f, 140.0f, 1.0f, 100.0f, 10.0f, 1, 0);
            setBand (2, -21.0f, -4.0f, 2.5f, 12.0f, 110.0f, 1.0f, 100.0f, 10.0f, 1, 0);
            setBand (3, -22.0f, -4.5f, 2.8f,  6.0f,  85.0f, 1.5f, 100.0f, 10.0f, 0, 0);
            break;

        case 5: // Punchy Loudness
            api.setParam (Param::ID::lookahead,  1.0f); // 1 ms
            api.setParam (Param::ID::globalMix,  85.0f);
            api.setCrossovers (150.0f, 950.0f, 5500.0f);
            setBand (0, -16.0f, -8.0f, 4.5f, 35.0f, 120.0f, 2.5f, 100.0f, 6.0f, 0, 0);
            setBand (1, -18.0f, -6.0f, 3.5f, 20.0f, 100.0f, 1.8f, 100.0f, 6.0f, 0, 0);
            setBand (2, -19.0f, -6.0f, 3.5f, 12.0f,  90.0f, 2.0f, 100.0f, 6.0f, 0, 0);
            setBand (3, -20.0f, -6.5f, 4.0f,  5.0f,  75.0f, 2.4f, 100.0f, 6.0f, 0, 0);
            break;

        case 6: // Drum Bus Smash
            api.setParam (Param::ID::xoverSlope, 2.0f); // 36 dB/oct
            api.setParam (Param::ID::globalMix,  75.0f);
            api.setCrossovers (130.0f, 800.0f, 4800.0f);
            setBand (0, -15.0f, -10.0f, 6.0f, 28.0f,  95.0f, 3.2f, 100.0f, 4.0f, 0, 0);
            setBand (1, -18.0f,  -8.0f, 4.5f, 15.0f,  85.0f, 1.5f, 100.0f, 6.0f, 0, 0);
            setBand (2, -18.0f,  -7.0f, 4.0f,  8.0f,  70.0f, 2.0f, 100.0f, 6.0f, 0, 0);
            setBand (3, -20.0f,  -6.0f, 3.5f,  3.0f,  60.0f, 2.5f, 100.0f, 6.0f, 0, 0);
            break;

        case 7: // Tight Sub & Kick
            api.setParam (Param::ID::xoverSlope, 3.0f); // 48 dB/oct
            api.setCrossovers (95.0f, 450.0f, 3800.0f);
            setBand (0, -14.0f, -12.0f, 8.0f, 12.0f,  80.0f, 2.8f, 100.0f, 3.0f, 0, 0);
            setBand (1, -18.0f,  -6.0f, 3.5f, 18.0f, 110.0f, 0.5f, 100.0f, 6.0f, 0, 0);
            setBand (2, -22.0f,  -3.0f, 2.0f, 12.0f, 100.0f, 0.0f,  80.0f, 8.0f, 0, 0);
            setBand (3, -24.0f,  -2.0f, 1.8f,  8.0f,  90.0f, 0.5f,  80.0f, 8.0f, 0, 0);
            break;

        case 8: // Crisp Transient Snap (Expand)
            api.setCrossovers (160.0f, 900.0f, 4500.0f);
            setBand (0, -18.0f, +4.0f, 2.5f,  2.0f,  90.0f, 0.5f, 100.0f, 6.0f, 0, 1);
            setBand (1, -20.0f, -3.0f, 2.2f, 15.0f, 110.0f, 0.0f, 100.0f, 8.0f, 0, 0);
            setBand (2, -20.0f, +4.5f, 2.8f,  1.5f,  75.0f, 1.0f, 100.0f, 6.0f, 0, 1);
            setBand (3, -22.0f, +6.0f, 3.2f,  0.8f,  60.0f, 1.8f, 100.0f, 6.0f, 0, 1);
            break;

        case 9: // Vocal Presence & De-Ess
            api.setCrossovers (180.0f, 1200.0f, 5800.0f);
            setBand (0, -22.0f,  -6.0f, 3.0f, 18.0f, 130.0f, -1.0f, 100.0f,  8.0f, 1, 0);
            setBand (1, -20.0f,  -4.5f, 2.8f, 12.0f, 110.0f,  1.2f, 100.0f, 10.0f, 1, 0);
            setBand (2, -18.0f,  -5.0f, 3.2f,  6.0f,  85.0f,  2.2f, 100.0f,  8.0f, 0, 0);
            setBand (3, -26.0f, -10.0f, 6.5f,  0.5f,  45.0f, -0.5f, 100.0f,  4.0f, 0, 0);
            break;

        case 10: // Acoustic Guitar Polish
            api.setCrossovers (170.0f, 750.0f, 5000.0f);
            setBand (0, -19.0f, -7.5f, 4.0f, 10.0f, 110.0f, -0.8f, 100.0f,  6.0f, 1, 0);
            setBand (1, -21.0f, -5.0f, 3.0f, 14.0f, 120.0f,  0.4f, 100.0f,  9.0f, 1, 0);
            setBand (2, -22.0f, -3.5f, 2.2f, 10.0f,  95.0f,  1.0f, 100.0f, 10.0f, 1, 0);
            setBand (3, -24.0f, -4.0f, 2.5f,  4.0f,  75.0f,  2.0f, 100.0f, 10.0f, 0, 0);
            break;

        case 11: // Upward Detail Lift
        default:
            api.setCrossovers (200.0f, 1000.0f, 6000.0f);
            setBand (0, -28.0f, +4.5f, 2.5f, 12.0f, 150.0f, 0.5f, 100.0f, 12.0f, 1, 0);
            setBand (1, -30.0f, +5.5f, 2.8f, 10.0f, 130.0f, 0.8f, 100.0f, 12.0f, 1, 0);
            setBand (2, -30.0f, +6.0f, 3.0f,  8.0f, 110.0f, 1.2f, 100.0f, 12.0f, 1, 0);
            setBand (3, -32.0f, +6.5f, 3.0f,  5.0f,  95.0f, 1.5f, 100.0f, 12.0f, 1, 0);
            break;
    }

    abSlotState_[activeAbSlot_]       = apvts.copyState();
    abSlotInitialized_[activeAbSlot_] = true;
    reapplyStructureAndLatency();
    if (uiWakeCallback_ != nullptr && juce::MessageManager::existsAndIsCurrentThread())
        uiWakeCallback_ ({});
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
