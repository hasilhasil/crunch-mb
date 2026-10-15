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
    for (int ch = 0; ch < 2; ++ch)
        dryStore_[ch].assign ((size_t) procBlock_, 0.0f);

    // Lookahead 容量：3ms @ (192kHz × 4) = 2304 样本（留余量），标量切换零分配
    laMax_ = (int) std::ceil (0.003 * procRate_) + 16;
    for (int b = 0; b < Param::kNumBands; ++b)
        for (int ch = 0; ch < 2; ++ch)
            laBuf_[b][ch].assign ((size_t) laMax_, 0.0f);

    // 平滑系数用真实处理采样率（过采样下墙钟时间不变）
    mixSmoothCoeff_ = (float) std::exp (-1.0 / (0.010 * procRate_));   // Mix 10ms
    gateCoeff_      = (float) std::exp (-1.0 / (0.005 * procRate_));   // 门 5ms

    // Auto-Gain：响度窗 400ms；补偿平滑 300ms（够快能跟上调参，又不至于
    // 让增益本身成为可闻的抽动感）
    loudPre_.prepare (procRate_);
    loudPost_.prepare (procRate_);
    loudPre_.setWindowSec (0.400);
    loudPost_.setWindowSec (0.400);
    autoGainCoeff_  = (float) std::exp (-1.0 / (0.300 * procRate_));
    autoGainDb_     = 0.0f;
    autoGainMaxDb_  = 24.0f;

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
    // 关闭 → 完全不参与处理，且把补偿量平滑归零（避免"开过再关"留下残余增益）。
    if (! p.autoGain)
    {
        autoGainEnabled_ = false;
        autoGainDb_ = 0.0f;
    }
    else
    {
        autoGainEnabled_ = true;
    }

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
    if (osFactor_ > 1 && oversampler_ != nullptr && numSamples <= maxBlock_)
    {
        const int chans = useCh;

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

        // Phase（R13 修复）：processSamplesDown 的参数必须是 **1× 块大小**的输出块，
        // 不是过采样后的 up。JUCE 用它反推内部缓冲需求：
        //     currentNumSamples = outputBlock.getNumSamples()
        //     for (n = 0; n < stages.size() - 1; ++n) currentNumSamples *= factor;
        // 传过采样后的 up（2048）时，4x（2 级）会算出 2048×2 = 4096，
        // 而第一级内部缓冲只按 initProcessing(512) → 1024 分配 → **越界写**。
        // 2x 只有 1 级，循环不执行，碰巧不出错——这正是它一直没被发现的原因。
        // 现在用 osIn_ 的前 numSamples 样本作输出目标（osIn_ 容量 = maxBlock_ ≥ numSamples）。
        juce::dsp::AudioBlock<float> downFull (osIn_);
        auto downBlock = downFull.getSubBlock (0, (size_t) numSamples);
        oversampler_->processSamplesDown (downBlock);           // 降采样回 1× 块长
        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::copy (channels[ch],
                                               downBlock.getChannelPointer (ch), numSamples);
        return;
    }

    processChain (channels, numSamples, sidechain, sidechainChannels, useCh);
}

void MultibandDSP::processChain (float* const* channels, int numSamples,
                                 const float* const* sidechain, int sidechainChannels,
                                 int useChannels) noexcept
{
    const int chans = juce::jlimit (1, 2, useChannels);

    for (int done = 0; done < numSamples; done += procBlock_)
    {
        const int n = juce::jmin (procBlock_, numSamples - done);

        float* chPtr[2] = { nullptr, nullptr };
        for (int ch = 0; ch < chans; ++ch)
            chPtr[ch] = channels[ch] + done;
        // R31：mono 时把 chPtr[1] 回退到 ch0（而非留 nullptr）。
        //     下游（分频器、段求和、Auto-Gain）都按 2 通道寻址，
        //     nullptr 会被解引用 → 0xC0000005。
        if (chPtr[1] == nullptr)
            chPtr[1] = chPtr[0];

        // 1) 输入增益
        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::multiply (chPtr[ch], inGain_, n);

        // 1b) Auto-Gain：测**干声**响度（输入增益后、分频前 = 用户听到的参考）
        if (autoGainEnabled_)
        {
            for (int ch = 0; ch < chans; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    float s = chPtr[ch][i];
                    loudPre_.processSample (s);
                }
            loudPre_.endBlock (n * chans);
        }

        // 2) 主分频
        // ⚠️ R31 修复：mono（numCh_==1）时 inC[1] 原先传 nullptr，
        //    而 CrossoverBank::process 用 jmin(numChannels, numCh_) 定 chans
        //    （CrossoverBank 自己的 numCh_ 由 prepare 时的通道数决定 = 2），
        //    于是它解引用 input[1] == nullptr → 0xC0000005。
        //    宿主把单轨人声/吉他放到 mono 总线时第一块就崩。
        //    修法与 process() 第 309 行的 upPtr 一致：mono 时回退到 ch0。
        const float* inC[2]  = { chPtr[0], chPtr[1] != nullptr ? chPtr[1] : chPtr[0] };
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
        // Phase：全局 Mix 需要干声参考——在输入增益之后、分频之前存一份。
        for (int ch = 0; ch < chans; ++ch)
            juce::FloatVectorOperations::copy (dryStore_[(size_t) ch].data(),
                                               chPtr[ch], n);

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

                // ---- Phase：全局 Mix（Pro-MB 对标）----
                // 100% = 纯处理声；<100% = 与干声混合（减弱效果）；
                // >100% = 并行压缩提响（干声按 (mix−1) 叠加）。
                // 干声参考在输入增益后、分频前已存（dryStore_），
                // 保证 Mix=0 时输出与输入完全一致（含输入增益）。
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
                {
                    float s = outStore_[(size_t) ch][(size_t) i];
                    loudPost_.processSample (s);
                }
            loudPost_.endBlock (n * chans);

            // 补偿 = 干声响度 − 湿声响度（使输出响度回到输入水平）
            const float targetDb = loudPre_.getLoudnessDb();
            const float wetDb    = loudPost_.getLoudnessDb();
            float want = 0.0f;
            if (targetDb > -90.0f && wetDb > -90.0f)
                want = juce::jlimit (-autoGainMaxDb_, autoGainMaxDb_, targetDb - wetDb);

            // dB 域平滑（300ms）。
            // ⚠️ 系数必须按**本块样本数**推进：autoGainCoeff_ 是每样本系数，
            //    而这里每个 processChain 迭代只调一次（块级推进）。
            //    直接用每样本系数等于把 300ms 拖成 300ms×procBlock_（实测：
            //    220 块只建立 0.06 dB，几乎无补偿）。
            const float blkCoeff = std::pow (autoGainCoeff_, (float) n);
            autoGainDb_ += (want - autoGainDb_) * (1.0f - blkCoeff);
            const float g = juce::Decibels::decibelsToGain (autoGainDb_);
            for (int ch = 0; ch < chans; ++ch)
                juce::FloatVectorOperations::multiply (outStore_[(size_t) ch].data(), g, n);
        }

        // 6) 输出增益 + 写回
        for (int ch = 0; ch < chans; ++ch)
        {
            juce::FloatVectorOperations::multiply (outStore_[ch].data(), outGain_, n);
            juce::FloatVectorOperations::copy (chPtr[ch], outStore_[ch].data(), n);
        }

        // 7) Monitor（Future MB 对标）：用选中信号**替换**输出。
        //    ⚠️ monitorMode_ == 0（Output）时整段跳过——默认行为逐样本不变。
        //    所有源信号都在本块内现成（bandStore_ / dryStore_ / gStore_），
        //    无额外缓冲、无分配。
        if (monitorMode_ != 0)
        {
            const int m = monitorMode_;
            for (int ch = 0; ch < chans; ++ch)
            {
                float* out = chPtr[ch];
                if (m == 1)                       // Input = 干声
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
                    // GR 作为可听信号：把该段增益的"衰减量"写进输出。
                    // gStore_ 存的是 g×makeup（线性），GR ≈ 1 − g。
                    // 取四段平均后整体衰减 20dB，避免过响。
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
