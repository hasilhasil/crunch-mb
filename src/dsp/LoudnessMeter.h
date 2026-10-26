#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

// ============================================================
// LoudnessMeter — ITU-R BS.1770-4 响度测量（K 加权）
//
// 用途：Auto-Gain（Future MB 对标）。测量处理前后的 K 加权响度，
//       用差值补偿，使"调参不改变输出音量"。
//
// 实现要点（BS.1770-4 的实时简化版）：
//   · K 加权 = 两级 biquad：一级 high-shelf（+4 dB @ 高频），
//     一级 high-pass（RLB 权重，抑制低频）
//   · 响度 = -0.691 + 10·log10( Σ 通道 mean-square )
//   · 实时场景用滑动 RMS 窗（默认 400ms）代替整段测量
//
// 线程模型：process() 在音频线程调用（无锁无分配）；
//           getLoudnessDb() 供 UI 线程读（atomic）。
// ============================================================
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** 音频线程：按独立通道状态做 K 加权并累加均方（避免 L/R 串用单声道滤波器状态）。 */
    void processSample (int channel, float x) noexcept;

    /** 兼容旧单通道签名（走通道 0）。 */
    void processSample (float& x) noexcept;

    /** 一个块处理完后调用：把累积的均方按 windowSec_ 时间窗推进滑动窗。 */
    void endBlock (int numSamples) noexcept;

    /** K 加权响度（dB LUFS 近似）。UI 线程可读。 */
    float getLoudnessDb() const noexcept
    {
        return loudnessDb_.load (std::memory_order_relaxed);
    }

    /** 滑动窗时长（秒）。 */
    void setWindowSec (double sec) noexcept { windowSec_ = juce::jmax (0.05, sec); }

private:
    /** K 加权两级 biquad（直横 II）。 */
    struct Biquad
    {
        // a0 归一化后的系数（RBJ 配方）
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

        float process (float in) noexcept
        {
            const float y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in;
            y2 = y1; y1 = y;
            return y;
        }
    };

    void designFilters (double sampleRate) noexcept;

    double sampleRate_ = 48000.0;
    double windowSec_  = 0.400;
    int    numActiveCh_ = 1;
    std::array<Biquad, 2> shelf_, hpf_;

    // 块内与 ~5ms 桶累加（音频线程私有）
    double sumSq_ = 0.0;
    int    count_ = 0;
    double bucketSq_ = 0.0;
    int    bucketSamples_ = 0;

    // 滑动窗：固定容量环形（每桶至少 ~5ms，256 桶可覆盖 >1.2s 任意变长/1样本块）
    static constexpr int kWindowBlocks = 256;
    std::array<double, kWindowBlocks> blockMs_ {};
    std::array<int,    kWindowBlocks> blockN_  {};
    int wPos_ = 0;
    int wFill_ = 0;

    std::atomic<float> loudnessDb_ { -100.0f };
};
