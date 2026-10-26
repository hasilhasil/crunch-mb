#include "CrossoverBank.h"

namespace
{
    // Butterworth Q_k = 1 / (2 sin((2k−1)π / (2N)))，k = 1..N/2
    // 校验：N=2 → 0.7071；N=4 → 1.3066 / 0.5412；N=6 → 1.9319 / 0.7071 / 0.5176
    double butterworthQ (int order, int k)
    {
        const double arg = (2.0 * (double) k - 1.0) * juce::MathConstants<double>::pi
                           / (2.0 * (double) order);
        return 1.0 / (2.0 * std::sin (arg));
    }
}

void CrossoverBank::Split::setCoefficients (float fc, int slopeDb, bool lrMode, double sampleRate)
{
    // 恒等重建 vs LR 树（2026-09-23 用户决策修订：**分段干净优先**——避免切换
    // 相位模式后用户重调设置）：
    //   · 4/8 阶（24/48 dB/oct）→ LR 树（LP/HP 独立链）：分段干净
    //     （50Hz→band0 90%+ @T10b），代价是内侧交叉点求和涟漪
    //     （LR4 0.489dB / LR8 1.729dB 实测，断言① 容差 ±0.6/2.0dB）
    //   · 2/6 阶（12/36 dB/oct）→ 互补对（hi := x − lo）：LR 不存在于奇数阶，
    //     逐样本求和精确；HP 通道有相位感知泄漏（BW 在 fc 下方 4 八度相位已
    //     −37.7° → x−lo 残留 30% 功率）
    // lrMode 参数保留（历史签名）：两种 IIR 模式现共用同一拓扑选择。
    (void) lrMode;
    const int order = juce::jmax (2, slopeDb / 6);          // 12→2, 24→4, 36→6, 48→8
    const bool useLR = (order == 4 || order == 8);

    if (useLR)
    {
        complement_ = false;
        const int bwOrder = order / 2;                       // LR 形状 = (BW_bwOrder)²
        const int secs    = bwOrder / 2;
        lpN_ = secs * 2;
        hpN_ = secs * 2;
        for (int rep = 0; rep < 2; ++rep)
            for (int k = 1; k <= secs; ++k)
            {
                const float q = (float) butterworthQ (bwOrder, k);
                const int   i = rep * secs + (k - 1);
                lp_[i].setCoefficients (BiquadFilter::designLowPass  ((float) fc, q, (float) sampleRate));
                hp_[i].setCoefficients (BiquadFilter::designHighPass ((float) fc, q, (float) sampleRate));
            }
    }
    else
    {
        complement_ = true;
        lpN_ = order / 2;
        hpN_ = 0;
        for (int k = 1; k <= lpN_; ++k)
            lp_[k - 1].setCoefficients (BiquadFilter::designLowPass ((float) fc,
                                                                     (float) butterworthQ (order, k),
                                                                     (float) sampleRate));
    }
}

void CrossoverBank::Split::reset()
{
    for (auto& f : lp_) f.reset();
    for (auto& f : hp_) f.reset();
}

void CrossoverBank::Split::process (float x, float& lo, float& hi) noexcept
{
    float y = x;
    for (int i = 0; i < lpN_; ++i)
        y = lp_[i].processSample (y);
    lo = y;

    if (complement_)
    {
        hi = x - y;                       // 恒等重建：lo + hi ≡ x（仅 12/36 阶）
    }
    else
    {
        float h = x;
        for (int i = 0; i < hpN_; ++i)
            h = hp_[i].processSample (h);
        hi = h;
    }
}

void CrossoverBank::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    maxBlock_   = juce::jmax (16, maxBlockSize);
    numCh_      = juce::jlimit (1, 2, numChannels);
    linear_.prepare (sampleRate_, maxBlock_, numCh_, quality_);
    linearReady_ = true;
    setParameters (xover_[0], xover_[1], xover_[2], slopeDb_, mode_, quality_);
    reset();
}

void CrossoverBank::reset()
{
    for (auto& row : splits_)
        for (auto& s : row)
            s.reset();
    linear_.reset();
}

void CrossoverBank::setParameters (float xover1, float xover2, float xover3, int slopeDb,
                                   Param::PhaseMode mode, Param::LinearQuality quality)
{
    // 结构性变化才清状态（结论④）：斜率 / 相位模式 / 质量变化
    const bool structural = (mode != mode_) || (slopeDb != slopeDb_)
                            || (mode == Param::PhaseMode::Linear && quality != quality_);

    xover_[0] = xover1;
    xover_[1] = xover2;
    xover_[2] = xover3;
    slopeDb_  = slopeDb;
    mode_     = mode;

    if (mode == Param::PhaseMode::Linear)
    {
        if (! linearReady_ || quality != quality_)
        {
            quality_    = quality;
            linear_.prepare (sampleRate_, maxBlock_, numCh_, quality_);
            linear_.reset();
            linearReady_ = true;
        }
        linear_.setParameters (xover1, xover2, xover3, slopeDb);   // FIR 设计（消息线程）
    }
    else
    {
        quality_ = quality;
        const bool lrMode = (mode == Param::PhaseMode::Natural);
        for (int ch = 0; ch < numCh_; ++ch)
            for (int e = 0; e < 3; ++e)
                splits_[e][ch].setCoefficients (xover_[e], slopeDb_, lrMode, sampleRate_);
    }

    if (structural)
        reset();
}

void CrossoverBank::process (const float* const* input,
                             float* const* bandL, float* const* bandLM,
                             float* const* bandHM, float* const* bandH,
                             int numChannels, int numSamples) noexcept
{
    if (mode_ == Param::PhaseMode::Linear)
    {
        linear_.process (input, bandL, bandLM, bandHM, bandH, numChannels, numSamples);
        return;
    }

    const int chans = juce::jmin (numChannels, numCh_);
    for (int ch = 0; ch < chans; ++ch)
    {
        const float* in = input[ch];
        float* oL  = bandL[ch];
        float* oLM = bandLM[ch];
        float* oHM = bandHM[ch];
        float* oH  = bandH[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            float L, t1, LM, t2, HM, H;
            splits_[0][ch].process (in[i], L,  t1);
            splits_[1][ch].process (t1,   LM, t2);
            splits_[2][ch].process (t2,   HM, H);
            oL[i]  = L;
            oLM[i] = LM;
            oHM[i] = HM;
            oH[i]  = H;
        }
    }
    for (int ch = chans; ch < numChannels; ++ch)
    {
        juce::FloatVectorOperations::clear (bandL[ch],  numSamples);
        juce::FloatVectorOperations::clear (bandLM[ch], numSamples);
        juce::FloatVectorOperations::clear (bandHM[ch], numSamples);
        juce::FloatVectorOperations::clear (bandH[ch],  numSamples);
    }
}

// ---------------- Phase：幅度响应评估（API facade 用）----------------
// 与 process() 的分解树严格同构：
//   L = LP1(x);  t1 = HP1(x);  LM = LP2(t1)
//   t2 = HP2(t1); HM = LP3(t2); H = HP3(t2)
// 故各段响应 = 沿路径的各级之积。
float CrossoverBank::getMagnitudeDb (int bandIndex, float freqHz) const
{
    const int band = juce::jlimit (0, 3, bandIndex);
    const double sr = sampleRate_;

    // ---- Linear Phase：评估该段 FIR 核的 DFT 幅度 ----
    if (mode_ == Param::PhaseMode::Linear)
    {
        if (! linearReady_)
            return 0.0f;
        const int fftSize = LinearPhaseCrossover::fftSizeForQuality (quality_);
        std::vector<float> kL, kLM, kHM, kH;
        int len = 0, delay = 0;
        LinearPhaseCrossover::designBandKernels (sr, xover_[0], xover_[1], xover_[2],
                                                 slopeDb_, fftSize, kL, kLM, kHM, kH,
                                                 len, delay);
        const std::vector<float>& k = (band == 0) ? kL : (band == 1) ? kLM
                                   : (band == 2) ? kHM : kH;
        if (k.empty())
            return 0.0f;

        // H(e^{jw}) = Σ k[n] · e^{-jwn}；核是零相位设计 + 整数延迟，
        // 所以只需幅度（不含延迟相位）。
        const double w = juce::MathConstants<double>::twoPi * freqHz / sr;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < (int) k.size(); ++n)
        {
            const double ph = -w * (double) n;
            re += (double) k[(size_t) n] * std::cos (ph);
            im += (double) k[(size_t) n] * std::sin (ph);
        }
        const double mag = std::sqrt (re * re + im * im);
        return (float) juce::Decibels::gainToDecibels (mag, -120.0);
    }

    // ---- IIR 模式：取通道 0 的滤波器链（立体声系数相同）----
    const Split& s0 = splits_[0][0];
    const Split& s1 = splits_[1][0];
    const Split& s2 = splits_[2][0];

    // 各级 LP 支路响应（沿路径）
    const std::complex<double> lp1 = s0.lowResponseAt (freqHz, sr);
    const std::complex<double> lp2 = s1.lowResponseAt (freqHz, sr);
    const std::complex<double> lp3 = s2.lowResponseAt (freqHz, sr);

    // 互补对模式下 HP 支路 = 1 − LP（恒等重建：hi := x − lo）
    auto hpOf = [] (const std::complex<double>& lp, const Split& s,
                    float f, double rate) -> std::complex<double>
    {
        if (s.complement_)
            return std::complex<double> (1.0, 0.0) - lp;
        return s.highResponseAt (f, rate);
    };

    std::complex<double> h (1.0, 0.0);
    switch (band)
    {
        case 0:  h = lp1;                                              break;
        case 1:  h = hpOf (lp1, s0, freqHz, sr) * lp2;                 break;
        case 2:  h = hpOf (lp1, s0, freqHz, sr)
                    * hpOf (lp2, s1, freqHz, sr) * lp3;                break;
        default: h = hpOf (lp1, s0, freqHz, sr)
                    * hpOf (lp2, s1, freqHz, sr)
                    * hpOf (lp3, s2, freqHz, sr);                      break;
    }

    const double mag = std::abs (h);
    return (float) juce::Decibels::gainToDecibels (mag, -120.0);
}
