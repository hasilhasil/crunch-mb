#include "MultibandDSP.h"

void MultibandDSP::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    maxBlock_   = juce::jmax (16, maxBlockSize);
    numCh_      = juce::jlimit (1, 2, numChannels);

    // 预分配按最大 4x 过采样容量一次性分配工作缓冲与 2x/4x 过采样器，
    // 运行时切换 Oversampling (Off/2x/4x) 不再重分配 vector 或重建 Oversampling 实例（修复 BUG-01）。
    const int maxProcBlock = maxBlock_ * 4;

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            bandStore_[b][ch].assign ((size_t) maxProcBlock, 0.0f);
            scStore_[b][ch].assign   ((size_t) maxProcBlock, 0.0f);
        }
        gStore_[b][0].assign ((size_t) maxProcBlock, 0.0f);
        gStore_[b][1].assign ((size_t) maxProcBlock, 0.0f);
    }
    for (int ch = 0; ch < 2; ++ch)
        outStore_[ch].assign ((size_t) maxProcBlock, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
        dryStore_[ch].assign ((size_t) maxProcBlock, 0.0f);

    // Lookahead 容量：按 4x 最大处理采样率预分配，标量/过采样切换零分配
    laMax_ = (int) std::ceil (0.003 * sampleRate_ * 4.0) + 16;
    for (int b = 0; b < Param::kNumBands; ++b)
        for (int ch = 0; ch < 2; ++ch)
            laBuf_[b][ch].assign ((size_t) laMax_, 0.0f);

    // 过采样器（上/下采样 staging 缓冲按 1× 块长并清零；侧链 ZOH 缓冲按最大 4× 块长）
    osIn_.setSize (2, maxBlock_, false, true, false);
    scHold_.setSize (2, maxProcBlock, false, true, false);

    oversampler2x_ = std::make_unique<juce::dsp::Oversampling<float>> (
        2u, 1u, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler2x_->initProcessing ((size_t) maxBlock_);
    oversampler2x_->reset();

    oversampler4x_ = std::make_unique<juce::dsp::Oversampling<float>> (
        2u, 2u, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler4x_->initProcessing ((size_t) maxBlock_);
    oversampler4x_->reset();

    configureForOversampling (struct_.oversampling);
    setStructure (struct_);
    reset();
}

void MultibandDSP::configureForOversampling (int factor) noexcept
{
    osFactor_  = (factor == 2) ? 2 : (factor >= 4) ? 4 : 1;
    procRate_  = sampleRate_ * (double) osFactor_;
    procBlock_ = maxBlock_ * osFactor_;

    crossover_.prepare   (procRate_, procBlock_, numCh_);
    scCrossover_.prepare (procRate_, procBlock_, numCh_);

    for (auto& c : comps_)
        c.prepare (procRate_);
    for (auto& f : scFilters_)
        f.prepare (procRate_);

    // 平滑系数用真实处理采样率（过采样下墙钟时间不变）
    mixSmoothCoeff_ = (float) std::exp (-1.0 / (0.010 * procRate_));   // Mix 10ms
    gateCoeff_      = (float) std::exp (-1.0 / (0.005 * procRate_));   // 门 5ms

    // Auto-Gain：响度窗 400ms；补偿平滑 300ms
    loudPre_.prepare (procRate_);
    loudPost_.prepare (procRate_);
    loudPre_.setWindowSec (0.400);
    loudPost_.setWindowSec (0.400);
    autoGainCoeff_  = (float) std::exp (-1.0 / (0.300 * procRate_));
    autoGainDb_     = 0.0f;
    autoGainMaxDb_  = 24.0f;

    if (oversampler2x_ != nullptr) oversampler2x_->reset();
    if (oversampler4x_ != nullptr) oversampler4x_->reset();
}

void MultibandDSP::reset()
{
    crossover_.reset();
    scCrossover_.reset();
    for (auto& c : comps_)
        c.reset();
    for (auto& f : scFilters_)
        f.reset();
    if (oversampler2x_ != nullptr)
        oversampler2x_->reset();
    if (oversampler4x_ != nullptr)
        oversampler4x_->reset();

    loudPre_.reset();
    loudPost_.reset();
    autoGainDb_ = 0.0f;

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (laBuf_[b][ch].begin(), laBuf_[b][ch].end(), 0.0f);
            laPos_[b][ch] = 0;
        }
        gateSm_[b]   = gateT_[b];
        deltaSm_[b]  = deltaT_[b];
        mixSmooth_[b] = mixLin_[b];
    }
}

void MultibandDSP::setStructure (const StructureParams& s)
{
    const int wantOs = (s.oversampling == 2) ? 2 : (s.oversampling >= 4) ? 4 : 1;
    const bool osChanged = (wantOs != osFactor_);

    struct_ = s;
    Param::clampCrossovers (struct_.xover1, struct_.xover2, struct_.xover3);
    struct_.oversampling = wantOs;

    if (osChanged)
    {
        // 采样率倍数变化：更新子模块采样率（工作缓冲与过采样器已在 prepare 预分配，零重分配）
        configureForOversampling (wantOs);
        crossover_.setParameters  (struct_.xover1, struct_.xover2, struct_.xover3,
                                   struct_.slopeDb, struct_.mode, struct_.quality);
        scCrossover_.setParameters (struct_.xover1, struct_.xover2, struct_.xover3,
                                    struct_.slopeDb, Param::PhaseMode::Natural,
                                    Param::LinearQuality::Medium);
        reset();
        return;
    }

    crossover_.setParameters  (struct_.xover1, struct_.xover2, struct_.xover3,
                               struct_.slopeDb, struct_.mode, struct_.quality);
    // 侧链恒 IIR（DESIGN §6.3）：Natural 形状 + 同斜率/交叉点
    scCrossover_.setParameters (struct_.xover1, struct_.xover2, struct_.xover3,
                                struct_.slopeDb, Param::PhaseMode::Natural,
                                Param::LinearQuality::Medium);
}

int MultibandDSP::getLatencySamples() const noexcept
{
    // 修复 BUG-02：统一返回 1× 宿主采样率下的样本数（crossover 与 lookahead 运行在 procRate_ 下，需除以 osFactor_）
    const int os = juce::jmax (1, osFactor_);
    return (crossover_.getLatencySamples() + laSamples_) / os + getOversamplingLatencySamples();
}

int MultibandDSP::getCrossoverLatencySamples() const noexcept
{
    const int os = juce::jmax (1, osFactor_);
    return crossover_.getLatencySamples() / os;
}

int MultibandDSP::getOversamplingLatencySamples() const noexcept
{
    auto* os = activeOversampler();
    return (os != nullptr) ? (int) std::lround (os->getLatencyInSamples()) : 0;
}

float MultibandDSP::getBandGainDb (int band) const noexcept
{
    const int b = juce::jlimit (0, Param::kNumBands - 1, band);
    return comps_[b].getSmoothedGainDb();
}

float MultibandDSP::getBandDetectorDb (int band) const noexcept
{
    // Phase C-3：读音频线程写的检测电平。float 读在本平台是原子的，
    // 且偶发撕裂只影响一帧的可视化，不影响音频（与 getBandGainDb 同策略）。
    const int b = juce::jlimit (0, Param::kNumBands - 1, band);
    return comps_[b].getLastDetectorDb();
}

float MultibandDSP::getBandGateSmoothed (int band) const noexcept
{
    const int b = juce::jlimit (0, Param::kNumBands - 1, band);
    return gateSm_[b];
}

float MultibandDSP::getBandStaticGainDb (int band, float levelDb) const
{
    const int b = juce::jlimit (0, Param::kNumBands - 1, band);
    return comps_[b].getStaticGainDb (levelDb);
}

std::array<float, Param::kNumBands> MultibandDSP::getBandGainReductionDb() const
{
    // GR = 该段实际增益的衰减量。bandGainDb_ 已含 Range 钳制与 Makeup，
    // 取其负值得"衰减了多少 dB"（正数）。压缩时为负增益 → GR 为正。
    std::array<float, Param::kNumBands> gr {};
    for (int b = 0; b < Param::kNumBands; ++b)
        gr[(size_t) b] = -getBandGainDb (b);
    return gr;
}

void MultibandDSP::computeGateTargets (const ScalarParams& p) noexcept
{
    bool anyDelta = false, anySolo = false, anyMute = false;
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        anyDelta = anyDelta || p.bands[(size_t) b].delta;
        anySolo  = anySolo  || p.bands[(size_t) b].solo;
        anyMute  = anyMute  || p.bands[(size_t) b].mute;
    }
    gatesActive_ = anyDelta || anySolo || anyMute;

    // 优先级：Delta > Solo > Mute（TECH_SPEC §3.7）
    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const auto& bp = p.bands[(size_t) b];
        if (anyDelta)      { gateT_[b] = 0.0f; deltaT_[b] = bp.delta ? 1.0f : 0.0f; }
        else if (anySolo)  { gateT_[b] = bp.solo ? 1.0f : 0.0f; deltaT_[b] = 0.0f; }
        else               { gateT_[b] = bp.mute ? 0.0f : 1.0f; deltaT_[b] = 0.0f; }
    }
}

void MultibandDSP::updateScalars (const ScalarParams& p) noexcept
{
    inGain_  = juce::Decibels::decibelsToGain (p.inputGainDb);
    outGain_ = juce::Decibels::decibelsToGain (p.outputGainDb);
    // Phase：全局 Mix 0-200% → 线性比（100% = 1.0）
    globalMixLin_ = juce::jlimit (0.0f, 2.0f, p.globalMixPct * 0.01f);

    // ---- Auto-Gain（Future MB 对标，默认关闭）----
    // 关闭 → 停止响度测量，并在 processChain 中将残余 autoGainDb_ 平滑归零（避免跳变爆音）。
    autoGainEnabled_ = p.autoGain;

    // ---- Monitor（Future MB 对标）----
    monitorMode_ = juce::jlimit (0, 6, p.monitorMode);

    // lookahead 以**墙钟时间**为准：过采样下换算成 procRate_ 下的样本数
    laSamples_ = (int) std::lround (p.lookaheadMs * 0.001 * procRate_);
    laSamples_ = juce::jlimit (0, laMax_, laSamples_);

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const auto& bp = p.bands[(size_t) b];
        comps_[b].setParameters (bp.thresholdDb, bp.ratio, bp.attackMs, bp.releaseMs,
                                 bp.rangeDb, bp.kneeDb, bp.useRms);
        // Phase C-6：动态模式（Compress / Expand）
        comps_[b].setDynamicsMode (bp.dynamics == 1 ? DynamicsMode::Expand
                                                    : DynamicsMode::Compress);
        comps_[b].setStereoLink (juce::jlimit (0.0f, 1.0f, bp.stereoLink * 0.01f));   // 2026-10-14
        scFilters_[(size_t) b].setParameters (bp.scLowHz, bp.scHighHz, bp.scFree);     // 2026-10-14
        makeupLin_[b]  = juce::Decibels::decibelsToGain (bp.makeupDb);
        mixLin_[b]     = juce::jlimit (0.0f, 1.0f, bp.mixPct * 0.01f);
        extEnabled_[b] = bp.ext;
    }
    computeGateTargets (p);
}

void MultibandDSP::process (float* const* channels, int numSamples,
                            const float* const* sidechain, int sidechainChannels,
                            int numChannels) noexcept
{
    // R31：本次实际使用的通道数。调用方按 buffer 实况传入（宿主报的 mainInCh
    // 可能与 buffer 不一致——实测 mono 场景 mainInCh=2 而 buffer 只有 1 通道）。
    const int useCh = juce::jlimit (1, 2, numChannels);

    // 2026-10-14：过采样包装。整条链跑在 procRate_ 下，这里只负责上/下采样。
    //
    // ⚠️ 这里的 `numSamples <= maxBlock_` 守卫**不能**改成音频线程分块循环。
    // JUCE 的 `dsp::Oversampling` 内部按 initProcessing() 时给的块大小预分配
    // FIR 状态；本次实测：改成"按 maxBlock_ 分块、每块传子块"后，
    // processSamplesUp 返回的块大小与本次处理的 n 不符，copy 回 channels 时
    // 多写 (maxBlock_ − n) 个样本 → 堆损坏（0xC0000374 / 0xC0000005）。
    // 若将来要支持"宿主给出超过 maxBlock_ 的块"，正确位置是在
    // prepareToPlay 里把 maxBlock_ 一开始就设得足够大，而不是在音频线程分片。
    auto* os = activeOversampler();
    if (osFactor_ > 1 && os != nullptr)
    {
        const int chans = useCh;
        bool anyExt = false;
        for (int b = 0; b < Param::kNumBands; ++b)
            anyExt = anyExt || extEnabled_[b];

        // 修复 P0-2 & LOGIC-03：
        // 1) 上采样与降采样均严格传入 .getSubBlock(0, chunkN)，消除 numSamples < maxBlock_ 时
        //    上采样整块尾部陈旧垃圾及侧链越界读取（0xC0000005）；
        // 2) 当 numSamples > maxBlock_ 时按 maxBlock_ 切片依次通过过采样器，绝不回退到 1× 采样率。
        for (int base = 0; base < numSamples; base += maxBlock_)
        {
            const int chunkN = juce::jmin (maxBlock_, numSamples - base);
            if (chunkN <= 0)
                break;

            for (int ch = 0; ch < chans; ++ch)
                juce::FloatVectorOperations::copy (osIn_.getWritePointer (ch),
                                                   channels[ch] + base, chunkN);
            if (chans == 1)
                juce::FloatVectorOperations::copy (osIn_.getWritePointer (1),
                                                   channels[0] + base, chunkN);

            const juce::dsp::AudioBlock<const float> inFull (osIn_);
            const auto inBlock = inFull.getSubBlock (0, (size_t) chunkN);
            auto up = os->processSamplesUp (inBlock);   // 精确 = chans × (chunkN × osFactor_)

            const float* scUp[2] = { nullptr, nullptr };
            int scUpCh = 0;
            if (sidechain != nullptr && sidechainChannels > 0 && anyExt)
            {
                const int upN = (int) up.getNumSamples();
                const int scCh = juce::jmin (2, sidechainChannels);
                for (int ch = 0; ch < scCh; ++ch)
                {
                    float* dst = scHold_.getWritePointer (ch);
                    const float* src = sidechain[ch] + base;
                    for (int i = 0; i < upN; ++i)
                        dst[i] = src[i / osFactor_];
                }
                scUpCh = scCh;
                for (int ch = 0; ch < scCh; ++ch)
                    scUp[ch] = scHold_.getReadPointer (ch);
            }

            float* upPtr[2] = { up.getChannelPointer (0),
                                chans > 1 ? up.getChannelPointer (1) : up.getChannelPointer (0) };
            processChain (upPtr, (int) up.getNumSamples(),
                          scUpCh > 0 ? scUp : nullptr, scUpCh, chans);

            juce::dsp::AudioBlock<float> downFull (osIn_);
            auto downBlock = downFull.getSubBlock (0, (size_t) chunkN);
            os->processSamplesDown (downBlock);
            for (int ch = 0; ch < chans; ++ch)
                juce::FloatVectorOperations::copy (channels[ch] + base,
                                                   downBlock.getChannelPointer (ch), chunkN);
        }
        return;
    }

    processChain (channels, numSamples, sidechain, sidechainChannels, useCh);
}

void MultibandDSP::processChain (float* const* channels, int numSamples,
                                 const float* const* sidechain, int sidechainChannels,
                                 int useChannels) noexcept
{
    const int chans = juce::jlimit (1, 2, useChannels);
    const int stepBlock = (int) outStore_[0].size();

    for (int done = 0; done < numSamples; done += stepBlock)
    {
        const int n = juce::jmin (stepBlock, numSamples - done);

        float* chPtr[2] = { nullptr, nullptr };
        for (int ch = 0; ch < chans; ++ch)
            chPtr[ch] = channels[ch] + done;
        if (chPtr[1] == nullptr)
            chPtr[1] = chPtr[0];

        // 1) 输入增益
        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::multiply (chPtr[ch], inGain_, n);

        // 1b) Auto-Gain：测**干声**响度（各通道独立滤波器状态，修复 BUG-03）
        if (autoGainEnabled_)
        {
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i)
                    loudPre_.processSample (ch, chPtr[ch][i]);
            loudPre_.endBlock (n * chans);
        }

        // 2) 主分频
        const float* inC[2]  = { chPtr[0], chPtr[1] != nullptr ? chPtr[1] : chPtr[0] };
        float* bL[2]  = { bandStore_[0][0].data(), bandStore_[0][1].data() };
        float* bLM[2] = { bandStore_[1][0].data(), bandStore_[1][1].data() };
        float* bHM[2] = { bandStore_[2][0].data(), bandStore_[2][1].data() };
        float* bH[2]  = { bandStore_[3][0].data(), bandStore_[3][1].data() };
        crossover_.process (inC, bL, bLM, bHM, bH, chans, n);

        // 3) 侧链分频（仅当有段启用 EXT 且侧链总线有信号；补齐 + done 偏移修复 LOGIC-03）
        bool scReady = false;
        if (sidechain != nullptr && sidechainChannels > 0)
        {
            bool anyExt = false;
            for (int b = 0; b < Param::kNumBands; ++b)
                anyExt = anyExt || extEnabled_[b];

            if (anyExt)
            {
                const float* scIn[2] = { sidechain[0] + done,
                                         (sidechainChannels > 1 ? sidechain[1] : sidechain[0]) + done };
                float* sL[2]  = { scStore_[0][0].data(), scStore_[0][1].data() };
                float* sLM[2] = { scStore_[1][0].data(), scStore_[1][1].data() };
                float* sHM[2] = { scStore_[2][0].data(), scStore_[2][1].data() };
                float* sH[2]  = { scStore_[3][0].data(), scStore_[3][1].data() };
                scCrossover_.process (scIn, sL, sLM, sHM, sH,
                                      juce::jmin (2, sidechainChannels), n);
                scReady = true;
            }
        }

        // 4) 每段：检测（INT/EXT）→ 侧链滤波 → 增益 → lookahead 延迟音频 → g·makeup
        for (int b = 0; b < Param::kNumBands; ++b)
        {
            const float* l = bandStore_[b][0].data();
            const float* r = chans > 1 ? bandStore_[b][1].data() : bandStore_[b][0].data();
            float* gL = gStore_[b][0].data();
            float* gR = gStore_[b][1].data();
            const float mk = makeupLin_[b];
            const bool useExt = extEnabled_[b] && scReady;
            const float* sl = useExt ? scStore_[b][0].data() : nullptr;
            const float* sr = useExt ? (chans > 1 ? scStore_[b][1].data() : scStore_[b][0].data()) : nullptr;
            auto& scf = scFilters_[(size_t) b];

            for (int i = 0; i < n; ++i)
            {
                // 检测信号：EXT 取侧链同频段，否则取本段音频；Free 模式下再过 HP/LP
                float dl = useExt ? sl[i] : l[i];
                float dr = useExt ? sr[i] : r[i];
                scf.process (dl, dr);                       // 2026-10-14：侧链检测滤波（Band 模式直通）

                float gl = 1.0f, gr = 1.0f;
                comps_[b].processGain (dl, dr, gl, gr);      // 2026-10-14：L/R 独立 + 立体声链接

                float al = l[i], ar = r[i];
                {
                    auto& bl  = laBuf_[b][0];
                    auto& br  = laBuf_[b][1];
                    int&  pw  = laPos_[b][0];
                    int&  prw = laPos_[b][1];
                    if (laSamples_ > 0)
                    {
                        int rl = pw  - laSamples_;
                        int rr = prw - laSamples_;
                        if (rl < 0) rl += laMax_;
                        if (rr < 0) rr += laMax_;
                        al = bl[(size_t) rl];
                        ar = br[(size_t) rr];
                    }
                    bl[(size_t) pw]  = l[i];
                    br[(size_t) prw] = r[i];
                    if (++pw  >= laMax_) pw  = 0;
                    if (++prw >= laMax_) prw = 0;
                }
                bandStore_[b][0][i] = al;
                if (chans > 1)
                    bandStore_[b][1][i] = ar;
                gL[i] = gl * mk;
                gR[i] = gr * mk;
            }
        }

        // 4b) 修复 BUG-04 / P1-6：干声参考（dryStore_）取自分频后、Lookahead 延迟后的四段未压缩信号之和。
        //     这使干声参考天然具备与湿声完全一致的 Linear Phase FIR 延迟、Lookahead 延迟以及
        //     LR4/LR8 分频全通相位，彻底消除 globalMix != 100% 时的梳状滤波/回声及 Monitor=Input 的 PDC 偏移。
        for (int ch = 0; ch < chans; ++ch)
        {
            float* dry = dryStore_[(size_t) ch].data();
            const float* b0 = bandStore_[0][ch].data();
            const float* b1 = bandStore_[1][ch].data();
            const float* b2 = bandStore_[2][ch].data();
            const float* b3 = bandStore_[3][ch].data();
            for (int i = 0; i < n; ++i)
                dry[i] = b0[i] + b1[i] + b2[i] + b3[i];
        }

        // 5) 求和（各段带门与 Delta 混合）
        // ⚠️ 平滑推进必须在通道循环外——stereo 时每通道推一遍会使时间常数减半（2026-09-23 修复）
        for (int ch = 0; ch < chans; ++ch)
        {
            float* out = outStore_[ch].data();
            for (int i = 0; i < n; ++i)
            {
                // Mix / 门平滑（每样本只推进一次，与通道数无关）
                if (ch == 0)
                {
                    for (int b = 0; b < Param::kNumBands; ++b)
                    {
                        mixSmooth_[b] += (mixLin_[b] - mixSmooth_[b]) * (1.0f - mixSmoothCoeff_);
                        gateSm_[b]    += (gateT_[b]   - gateSm_[b])    * (1.0f - gateCoeff_);
                        deltaSm_[b]   += (deltaT_[b]  - deltaSm_[b])   * (1.0f - gateCoeff_);
                    }
                }
                const float mArr[Param::kNumBands] = { mixSmooth_[0], mixSmooth_[1], mixSmooth_[2], mixSmooth_[3] };
                const float gArr[Param::kNumBands] = { gateSm_[0],  gateSm_[1],  gateSm_[2],  gateSm_[3]  };
                const float dArr[Param::kNumBands] = { deltaSm_[0], deltaSm_[1], deltaSm_[2], deltaSm_[3] };

                float acc = 0.0f;
                for (int b = 0; b < Param::kNumBands; ++b)
                {
                    const float d = bandStore_[b][ch][i];
                    const float gmk = gStore_[b][ch][i];
                    const float signal   = d * ((1.0f - mArr[b]) + mArr[b] * gmk);
                    const float deltaSig = d * mArr[b] * (gmk - 1.0f);
                    acc += signal * gArr[b] + deltaSig * dArr[b];
                }
                out[i] = acc;

                // ---- Phase：全局 Mix（Pro-MB 对标）----
                // 100% = 纯处理声；<100% = 与同相延迟对齐干声混合；>100% = 并行压缩提响
                if (globalMixLin_ != 1.0f)
                {
                    const float wet = out[i];
                    const float dryRef = dryStore_[(size_t) ch][i];
                    out[i] = globalMixLin_ <= 1.0f
                           ? (wet * globalMixLin_ + dryRef * (1.0f - globalMixLin_))
                           : (wet + dryRef * (globalMixLin_ - 1.0f));
                }
            }
        }

        // 5b) Auto-Gain：测**湿声**响度并施加补偿（全局 Mix 之后、输出增益之前）
        if (autoGainEnabled_)
        {
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i)
                    loudPost_.processSample (ch, outStore_[(size_t) ch][(size_t) i]);
            loudPost_.endBlock (n * chans);

            // 补偿 = 干声响度 − 湿声响度（使输出响度回到输入水平）
            const float targetDb = loudPre_.getLoudnessDb();
            const float wetDb    = loudPost_.getLoudnessDb();
            float want = 0.0f;
            if (targetDb > -90.0f && wetDb > -90.0f)
                want = juce::jlimit (-autoGainMaxDb_, autoGainMaxDb_, targetDb - wetDb);

            const float blkCoeff = std::pow (autoGainCoeff_, (float) n);
            autoGainDb_ += (want - autoGainDb_) * (1.0f - blkCoeff);
            const float g = juce::Decibels::decibelsToGain (autoGainDb_);
            for (int ch = 0; ch < chans; ++ch)
                juce::FloatVectorOperations::multiply (outStore_[(size_t) ch].data(), g, n);
        }
        else if (std::abs (autoGainDb_) > 1.0e-4f)
        {
            // 关闭 Auto-Gain 时平滑释放回 0 dB，避免块边界跳变爆音
            const float releaseCoeff = (float) std::exp (-1.0 / (0.025 * procRate_));
            for (int i = 0; i < n; ++i)
            {
                autoGainDb_ *= releaseCoeff;
                const float g = juce::Decibels::decibelsToGain (autoGainDb_);
                for (int ch = 0; ch < chans; ++ch)
                    outStore_[(size_t) ch][(size_t) i] *= g;
            }
            if (std::abs (autoGainDb_) <= 1.0e-4f)
                autoGainDb_ = 0.0f;
        }

        // 6) 输出增益 + 写回
        for (int ch = 0; ch < chans; ++ch)
        {
            juce::FloatVectorOperations::multiply (outStore_[ch].data(), outGain_, n);
            juce::FloatVectorOperations::copy (chPtr[ch], outStore_[ch].data(), n);
        }

        // 7) Monitor（Future MB 对标）：用选中信号**替换**输出。
        if (monitorMode_ != 0)
        {
            const int m = monitorMode_;
            for (int ch = 0; ch < chans; ++ch)
            {
                float* out = chPtr[ch];
                if (m == 1)                       // Input = 相位/延迟对齐的未压缩干声
                {
                    juce::FloatVectorOperations::copy (out, dryStore_[(size_t) ch].data(), n);
                }
                else if (m >= 2 && m <= 5)        // Band 1-4 = 该段信号
                {
                    const int b = m - 2;
                    juce::FloatVectorOperations::copy (out, bandStore_[(size_t) b][(size_t) ch].data(), n);
                    juce::FloatVectorOperations::multiply (out, outGain_, n);
                }
                else                              // 6 = Gain Reduction
                {
                    for (int i = 0; i < n; ++i)
                    {
                        float gSum = 0.0f;
                        for (int b = 0; b < Param::kNumBands; ++b)
                            gSum += gStore_[b][(size_t) ch][(size_t) i];
                        const float gAvg = 0.25f * gSum;
                        const float gr = 1.0f - gAvg;             // 0..1（正数=有衰减）
                        out[i] = juce::jlimit (-1.0f, 1.0f, gr * 0.1f);
                    }
                }
            }
        }
    }
}
