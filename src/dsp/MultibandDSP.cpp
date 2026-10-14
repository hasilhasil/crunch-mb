#include "MultibandDSP.h"

void MultibandDSP::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    maxBlock_   = juce::jmax (16, maxBlockSize);
    numCh_      = juce::jlimit (1, 2, numChannels);

    // 2026-10-14：过采样。整条链在 procRate_ = sampleRate_ × osFactor_ 下运行。
    osFactor_  = juce::jlimit (1, 4, struct_.oversampling);
    procRate_  = sampleRate_ * (double) osFactor_;
    procBlock_ = maxBlock_ * osFactor_;

    crossover_.prepare   (procRate_, procBlock_, numCh_);
    scCrossover_.prepare (procRate_, procBlock_, numCh_);

    for (auto& c : comps_)
        c.prepare (procRate_);
    for (auto& f : scFilters_)
        f.prepare (procRate_);

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            bandStore_[b][ch].assign ((size_t) procBlock_, 0.0f);
            scStore_[b][ch].assign   ((size_t) procBlock_, 0.0f);
        }
        gStore_[b][0].assign ((size_t) procBlock_, 0.0f);
        gStore_[b][1].assign ((size_t) procBlock_, 0.0f);
    }
    for (int ch = 0; ch < 2; ++ch)
        outStore_[ch].assign ((size_t) procBlock_, 0.0f);

    // Lookahead 容量：3ms @ (192kHz × 4) = 2304 样本（留余量），标量切换零分配
    laMax_ = (int) std::ceil (0.003 * procRate_) + 16;
    for (int b = 0; b < Param::kNumBands; ++b)
        for (int ch = 0; ch < 2; ++ch)
            laBuf_[b][ch].assign ((size_t) laMax_, 0.0f);

    // 平滑系数用真实处理采样率（过采样下墙钟时间不变）
    mixSmoothCoeff_ = (float) std::exp (-1.0 / (0.010 * procRate_));   // Mix 10ms
    gateCoeff_      = (float) std::exp (-1.0 / (0.005 * procRate_));   // 门 5ms

    // 过采样器（上/下采样 staging 缓冲按 1× 块长；侧链 ZOH 缓冲按 procBlock_）
    osIn_.setSize (2, maxBlock_, false, false, false);
    scHold_.setSize (2, procBlock_, false, false, false);
    rebuildOversampler (osFactor_);

    setStructure (struct_);
    reset();
}

void MultibandDSP::rebuildOversampler (int factor) noexcept
{
    osFactor_ = juce::jlimit (1, 4, factor);
    procRate_ = sampleRate_ * (double) osFactor_;
    procBlock_ = maxBlock_ * osFactor_;

    if (osFactor_ == 1)
    {
        oversampler_.reset();
        return;
    }
    // factor 是指数：1 → 2×，2 → 4×。useIntegerLatency = true 便于整数延迟上报。
    const int exponent = (osFactor_ == 2) ? 1 : 2;
    oversampler_ = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh_, (size_t) exponent,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);
    oversampler_->initProcessing ((size_t) maxBlock_);
    oversampler_->reset();
}

void MultibandDSP::reset()
{
    crossover_.reset();
    scCrossover_.reset();
    for (auto& c : comps_)
        c.reset();
    for (auto& f : scFilters_)
        f.reset();
    if (oversampler_ != nullptr)
        oversampler_->reset();

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
    const bool osChanged = (juce::jlimit (1, 4, s.oversampling) != osFactor_);

    struct_ = s;
    Param::clampCrossovers (struct_.xover1, struct_.xover2, struct_.xover3);
    struct_.oversampling = juce::jlimit (1, 4, s.oversampling);

    if (osChanged)
    {
        // 结构性变化：整条链的采样率/块长都变，必须重新 prepare 所有子模块
        const int keepFactor = struct_.oversampling;
        prepare (sampleRate_, maxBlock_, numCh_);          // 内部按 struct_.oversampling 重建
        struct_.oversampling = keepFactor;                 // prepare 里 setStructure 会再进一次
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
    return crossover_.getLatencySamples() + laSamples_ + getOversamplingLatencySamples();
}

int MultibandDSP::getCrossoverLatencySamples() const noexcept
{
    return crossover_.getLatencySamples();
}

int MultibandDSP::getOversamplingLatencySamples() const noexcept
{
    return (oversampler_ != nullptr) ? (int) std::lround (oversampler_->getLatencyInSamples()) : 0;
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

    // lookahead 以**墙钟时间**为准：过采样下换算成 procRate_ 下的样本数
    laSamples_ = (int) std::lround (p.lookaheadMs * 0.001 * procRate_);
    laSamples_ = juce::jlimit (0, laMax_, laSamples_);

    for (int b = 0; b < Param::kNumBands; ++b)
    {
        const auto& bp = p.bands[(size_t) b];
        comps_[b].setParameters (bp.thresholdDb, bp.ratio, bp.attackMs, bp.releaseMs,
                                 bp.rangeDb, bp.kneeDb, bp.useRms);
        comps_[b].setStereoLink (juce::jlimit (0.0f, 1.0f, bp.stereoLink * 0.01f));   // 2026-10-14
        scFilters_[(size_t) b].setParameters (bp.scLowHz, bp.scHighHz, bp.scFree);     // 2026-10-14
        makeupLin_[b]  = juce::Decibels::decibelsToGain (bp.makeupDb);
        mixLin_[b]     = juce::jlimit (0.0f, 1.0f, bp.mixPct * 0.01f);
        extEnabled_[b] = bp.ext;
    }
    computeGateTargets (p);
}

void MultibandDSP::process (float* const* channels, int numSamples,
                            const float* const* sidechain, int sidechainChannels) noexcept
{
    // 2026-10-14：过采样包装。整条链跑在 procRate_ 下，这里只负责上/下采样。
    if (osFactor_ > 1 && oversampler_ != nullptr && numSamples <= maxBlock_)
    {
        const int chans = juce::jmin (numCh_, 2);

        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::copy (osIn_.getWritePointer (ch), channels[ch], numSamples);

        const juce::dsp::AudioBlock<const float> inBlock (osIn_);
        auto up = oversampler_->processSamplesUp (inBlock);   // chans × (numSamples × factor)

        // 侧链同样按倍数扩展（每样本重复 factor 次 = 零阶保持，供 EXT 检测用）。
        // 缓冲在 prepare() 预分配，音频线程零分配（TECH_SPEC §4）。
        const float* scUp[2] = { nullptr, nullptr };
        int scUpCh = 0;
        if (sidechain != nullptr && sidechainChannels > 0)
        {
            bool anyExt = false;
            for (int b = 0; b < Param::kNumBands; ++b)
                anyExt = anyExt || extEnabled_[b];
            if (anyExt)
            {
                const int upN = (int) up.getNumSamples();
                const int scCh = juce::jmin (2, sidechainChannels);
                for (int ch = 0; ch < scCh; ++ch)
                {
                    float* dst = scHold_.getWritePointer (ch);
                    for (int i = 0; i < upN; ++i)
                        dst[i] = sidechain[ch][i / osFactor_];
                }
                scUpCh = scCh;
                for (int ch = 0; ch < scCh; ++ch)
                    scUp[ch] = scHold_.getReadPointer (ch);
            }
        }

        float* upPtr[2] = { up.getChannelPointer (0),
                            chans > 1 ? up.getChannelPointer (1) : up.getChannelPointer (0) };
        processChain (upPtr, (int) up.getNumSamples(), scUpCh > 0 ? scUp : nullptr, scUpCh);

        oversampler_->processSamplesDown (up);                // 就地：up 变回 1× 块长
        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::copy (channels[ch], up.getChannelPointer (ch), numSamples);
        return;
    }

    processChain (channels, numSamples, sidechain, sidechainChannels);
}

void MultibandDSP::processChain (float* const* channels, int numSamples,
                                 const float* const* sidechain, int sidechainChannels) noexcept
{
    const int chans = juce::jmin (numCh_, 2);

    for (int done = 0; done < numSamples; done += procBlock_)
    {
        const int n = juce::jmin (procBlock_, numSamples - done);

        float* chPtr[2] = { nullptr, nullptr };
        for (int ch = 0; ch < chans; ++ch)
            chPtr[ch] = channels[ch] + done;

        // 1) 输入增益
        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::multiply (chPtr[ch], inGain_, n);

        // 2) 主分频
        const float* inC[2]  = { chPtr[0], chans > 1 ? chPtr[1] : nullptr };
        float* bL[2]  = { bandStore_[0][0].data(), bandStore_[0][1].data() };
        float* bLM[2] = { bandStore_[1][0].data(), bandStore_[1][1].data() };
        float* bHM[2] = { bandStore_[2][0].data(), bandStore_[2][1].data() };
        float* bH[2]  = { bandStore_[3][0].data(), bandStore_[3][1].data() };
        crossover_.process (inC, bL, bLM, bHM, bH, chans, n);

        // 3) 侧链分频（仅当有段启用 EXT 且侧链总线有信号）
        bool scReady = false;
        if (sidechain != nullptr && sidechainChannels > 0)
        {
            bool anyExt = false;
            for (int b = 0; b < Param::kNumBands; ++b)
                anyExt = anyExt || extEnabled_[b];

            if (anyExt)
            {
                const float* scIn[2] = { sidechain[0],
                                         sidechainChannels > 1 ? sidechain[1] : sidechain[0] };
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
                    // lookahead 环形延迟：**始终写环（预热）**，读位置 = 写位置 − laSamples_。
                    // 2026-09-23 修复：此前 laSamples_==0 时整段跳过，环内停留 zeros；
                    // 前瞻切入瞬间读出未预热数据 → 3ms 静音 gap 爆音（用户实测 click）。
                    // 现在环内始终是最近 laMax_ 样本的真实历史，切入即正确的延时移位。
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
                    bandStore_[b][1][i] = ar;   // 2026-10-14：mono 时 [1] 与 [0] 同源，跳过冗余写
                gL[i] = gl * mk;               // 2026-10-14：L/R 独立增益 × makeup
                gR[i] = gr * mk;
            }
        }

        // 5) 求和（Minimal 中性 = 校正结构；其余/门激活 = 逐段带门混合）
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

                if (struct_.mode == Param::PhaseMode::Minimal && ! gatesActive_)
                {
                    // 校正结构：out = dry·m̄ + Σ band·(g_i − m̄)，dry = Σband
                    float dry = 0.0f, gTot[Param::kNumBands];
                    for (int b = 0; b < Param::kNumBands; ++b)
                    {
                        const float d = bandStore_[b][ch][i];
                        dry += d;
                        gTot[b] = (1.0f - mArr[b]) + mArr[b] * gStore_[b][ch][i];
                    }
                    const float mBar = 0.25f * (gTot[0] + gTot[1] + gTot[2] + gTot[3]);
                    float acc = dry * mBar;
                    for (int b = 0; b < Param::kNumBands; ++b)
                        acc += bandStore_[b][ch][i] * (gTot[b] - mBar);
                    out[i] = acc;
                }
                else
                {
                    float acc = 0.0f;
                    for (int b = 0; b < Param::kNumBands; ++b)
                    {
                        const float d = bandStore_[b][ch][i];
                        const float gmk = gStore_[b][ch][i];
                        const float signal   = d * ((1.0f - mArr[b]) + mArr[b] * gmk);
                        // TECH_SPEC §3.7：Delta 作用在 Mix 之后 —— wet−dry = d·m·(gmk−1)。
                        // 2026-10-14 修复：此前漏乘 mArr[b]，Mix<100% 时 Delta 监听音量
                        // 偏大 1/mix 倍（Mix=50% → 2 倍，Mix=20% → 5 倍）。
                        const float deltaSig = d * mArr[b] * (gmk - 1.0f);
                        acc += signal * gArr[b] + deltaSig * dArr[b];
                    }
                    out[i] = acc;
                }
            }
        }

        // 6) 输出增益 + 写回
        for (int ch = 0; ch < chans; ++ch)
        {
            juce::FloatVectorOperations::multiply (outStore_[ch].data(), outGain_, n);
            juce::FloatVectorOperations::copy (chPtr[ch], outStore_[ch].data(), n);
        }
    }
}
