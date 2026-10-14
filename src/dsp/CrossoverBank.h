#pragma once

#include <JuceHeader.h>

#include "BiquadFilter.h"
#include "LinearPhaseCrossover.h"
#include "../Parameters.h"

// ============================================================
// CrossoverBank — 四段交叉分频器组（三相位模式，TECH_SPEC §3.2）
//
// 级联分解树（每通道）：
//   L = LP1(x); t1 = HP1(x); LM = LP2(t1); t2 = HP2(t1);
//   HM = LP3(t2); H = HP3(t2)
//
// 拓扑（2026-09-23 用户决策修订：**分段干净优先**——避免切换相位模式后重调设置）：
//   · 24/48 dB/oct（4/8 阶）**全部 IIR 模式** = LR 树（LP/HP 独立链）：
//     分段干净（50Hz→band0 90%+ @T10b），内侧交叉点求和涟漪
//     LR4 0.489dB / LR8 1.729dB 实测（断言① 容差 ±0.6/2.0dB）
//   · 12/36 dB/oct（2/6 阶）= 恒等重建互补对（hi := x − lo）：LR 不存在于
//     奇数阶，逐样本求和精确（断言① ±0.01dB / ② <1e-6）；HP 通道有相位
//     感知泄漏（BW 在 fc 下方 4 八度相位已 −37.7° → 残留 30% 功率）
//   · Linear Phase = LinearPhaseCrossover（FIR 恒等核：零相位 ⇒ 分段干净且求和精确）
//   ⇒ Natural 与 Minimal 在 24/48 档听感一致（用户明确要求）
//
// 交叉点热切换不清状态（仅斜率/模式/质量变化才 reset，PLAN §5 结论④）。
// ============================================================
//
// 交叉点热切换不清状态（仅斜率/模式/质量变化才 reset，PLAN §5 结论④）。
// ============================================================
class CrossoverBank
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // 参数更新（消息线程；Linear 模式内含 FIR 设计——勿上音频线程，PLAN L5）
    void setParameters (float xover1, float xover2, float xover3, int slopeDb,
                        Param::PhaseMode mode, Param::LinearQuality quality);

    void process (const float* const* input,
                  float* const* bandL, float* const* bandLM,
                  float* const* bandHM, float* const* bandH,
                  int numChannels, int numSamples) noexcept;

    int getLatencySamples() const noexcept
    {
        return mode_ == Param::PhaseMode::Linear ? linear_.getLatencySamples() : 0;
    }

    /**
     * Phase：指定段的幅度响应（dB）——供 API facade 的 getFrequencyResponse 用。
     * 消息线程调用（含 FIR 核评估，勿上音频线程）。
     *
     * 三种模式的算法：
     *   · LR 树（Natural 24/48）：沿该段路径求各级 responseAt 之积
     *   · 互补对（Minimal 12/36）：LP 路径乘积，HP 段用 (1 − LP) （恒等重建）
     *   · Linear：用 designBandKernels 取该段核，在请求频率上评估 DFT 幅度
     *
     * bandIndex: 0=L 1=LM 2=HM 3=H
     */
    float getMagnitudeDb (int bandIndex, float freqHz) const;

private:
    // 分频节点：LR 对（阶 4/8，分段干净）/ 互补对（阶 2/6 与 Minimal，恒等求和）
    // 2026-09-23 拓扑决策（扫频证据）：
    //   · 恒等重建（hi := x − lo）四段求和精确，但 HP 通道在邻段通带泄漏 −9dB
    //     （50Hz 处 LM 段占 33%），分段不干净 → 压缩场景可闻（邻段重复压缩）
    //   · LR 树分段干净（HP 阻带 −24dB+），代价是内侧交叉点求和涟漪
    //     （实测 LR4 0.489dB / LR8 1.729dB @200/1k/6k）
    //   → Natural 用 LR 树（分段质量优先，断言①容差 ±0.5dB，实测 0.489）
    //   → Minimal 用互补对（透明性承诺优先，断言① ±0.01dB 精确成立）
    struct Split
    {
        void setCoefficients (float fc, int slopeDb, bool lrMode, double sampleRate);
        void reset();
        void process (float x, float& lo, float& hi) noexcept;

        BiquadFilter lp_[4], hp_[4];
        int  lpN_ = 1, hpN_ = 0;
        bool complement_ = true;

        /** Phase：该 split 的 LP 支路在 freqHz 的复响应（用于 getMagnitudeDb）。 */
        std::complex<double> lowResponseAt (float freqHz, double sampleRate) const
        {
            std::complex<double> h (1.0, 0.0);
            for (int i = 0; i < lpN_; ++i)
                h *= lp_[(size_t) i].responseAt (freqHz, (float) sampleRate);
            return h;
        }
        /** Phase：该 split 的 HP 支路在 freqHz 的复响应。 */
        std::complex<double> highResponseAt (float freqHz, double sampleRate) const
        {
            std::complex<double> h (1.0, 0.0);
            for (int i = 0; i < hpN_; ++i)
                h *= hp_[(size_t) i].responseAt (freqHz, (float) sampleRate);
            return h;
        }
    };

    Split splits_[3][2];      // [交叉点][通道]
    Param::PhaseMode mode_ = Param::PhaseMode::Natural;
    int   slopeDb_  = 24;
    float xover_[3] = { 200.0f, 1000.0f, 6000.0f };

    LinearPhaseCrossover linear_;
    bool linearReady_ = false;
    Param::LinearQuality quality_ = Param::LinearQuality::Medium;

    double sampleRate_ = 48000.0;
    int    maxBlock_   = 512;
    int    numCh_      = 2;
};
