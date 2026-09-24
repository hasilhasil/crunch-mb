#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

#include "BandCompressor.h"
#include "CrossoverBank.h"
#include "../Parameters.h"

// ============================================================
// MultibandDSP — 多带压缩主引擎（TECH_SPEC §3.1）
//
// 链路：inputGain → [分频] → perBand(检测[INT/EXT] → 压缩/Range →
//       Makeup → Mix) → 监听门(Delta>Solo>Mute, 5ms 平滑) → 求和 →
//       outputGain。Lookahead = 分频后每段延迟线（检测看实时、增益作用于延迟后）。
//
// 线程纪律（PLAN L5 / TECH_SPEC §4）：
//   · setStructure()   —— 消息线程（Linear 模式含 FIR 设计，禁上音频线程）
//   · updateScalars()  —— 音频线程（只读标量，零分配零锁）
//   · process()        —— 音频线程（热路径零分配）
//
// Minimal Phase 校正结构（PLAN §5 结论②）：全段增益相等时 out ≡ dry·g；
//   监听门激活时回退逐段求和（透明性承诺只作用于中性状态）。
// ============================================================
class MultibandDSP
{
public:
    struct BandParams
    {
        float thresholdDb = -20.0f;
        float rangeDb     = -30.0f;
        float ratio       = 4.0f;
        float attackMs    = 10.0f;
        float releaseMs   = 100.0f;
        float makeupDb    = 0.0f;
        float mixPct      = 100.0f;
        bool  softKnee    = true;
        bool  useRms      = false;
        bool  solo        = false;
        bool  mute        = false;
        bool  delta       = false;
        bool  ext         = false;
    };

    struct StructureParams
    {
        float xover1 = 200.0f, xover2 = 1000.0f, xover3 = 6000.0f;
        int   slopeDb = 24;
        Param::PhaseMode     mode    = Param::PhaseMode::Natural;
        Param::LinearQuality quality = Param::LinearQuality::Medium;
    };

    struct ScalarParams
    {
        float inputGainDb  = 0.0f;
        float outputGainDb = 0.0f;
        int   lookaheadMs  = 0;                                   // 0 / 1 / 3
        std::array<BandParams, Param::kNumBands> bands;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels = 2);
    void reset();

    void setStructure (const StructureParams& s);                 // 消息线程
    void updateScalars (const ScalarParams& p) noexcept;          // 音频线程

    void process (float* const* channels, int numSamples,
                  const float* const* sidechain = nullptr,
                  int sidechainChannels = 0) noexcept;

    int   getLatencySamples() const noexcept;                     // 分频 + lookahead（音频线程视图）
    int   getCrossoverLatencySamples() const noexcept;            // 仅分频（消息线程延迟上报用）
    float getBandGainDb (int band) const noexcept;
    float getBandGateSmoothed (int band) const noexcept;          // 门平滑值（UI/测试）
    bool  gatesActive() const noexcept { return gatesActive_; }

private:
    StructureParams struct_;
    double sampleRate_ = 48000.0;
    int    maxBlock_   = 512;
    int    numCh_      = 2;

    CrossoverBank crossover_;                 // 主信号分频
    CrossoverBank scCrossover_;               // 侧链分频（恒 IIR，DESIGN §6.3）

    std::array<BandCompressor, Param::kNumBands> comps_;

    // 预分配工作缓冲：bandStore_[段][通道]、scStore_、outStore_、gStore_（段增益）
    std::array<std::array<std::vector<float>, 2>, Param::kNumBands> bandStore_;
    std::array<std::array<std::vector<float>, 2>, Param::kNumBands> scStore_;
    std::array<std::vector<float>, 2> outStore_;
    std::array<std::vector<float>, Param::kNumBands> gStore_;

    // Lookahead：每段每通道环形延迟（容量按 3ms@maxSR 预分配，标量切换零分配）
    std::array<std::array<std::vector<float>, 2>, Param::kNumBands> laBuf_;
    std::array<std::array<int, 2>, Param::kNumBands> laPos_ {};
    int laMax_ = 0, laSamples_ = 0;

    // 监听门（目标 + 5ms 平滑）
    std::array<float, Param::kNumBands> gateT_   { { 1, 1, 1, 1 } };
    std::array<float, Param::kNumBands> deltaT_  { { 0, 0, 0, 0 } };
    std::array<float, Param::kNumBands> gateSm_  { { 1, 1, 1, 1 } };
    std::array<float, Param::kNumBands> deltaSm_ { { 0, 0, 0, 0 } };
    bool gatesActive_ = false;
    float gateCoeff_ = 0.0f;

    // Mix 10ms 平滑（TECH_SPEC §3.3）
    std::array<float, Param::kNumBands> makeupLin_ { { 1, 1, 1, 1 } };
    std::array<float, Param::kNumBands> mixLin_    { { 1, 1, 1, 1 } };
    std::array<float, Param::kNumBands> mixSmooth_ { { 1, 1, 1, 1 } };
    float mixSmoothCoeff_ = 0.0f;

    std::array<bool, Param::kNumBands> extEnabled_ { { false, false, false, false } };

    float inGain_  = 1.0f;
    float outGain_ = 1.0f;

    void computeGateTargets (const ScalarParams& p) noexcept;
};
