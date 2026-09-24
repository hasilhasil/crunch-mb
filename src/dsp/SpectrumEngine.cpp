#include "SpectrumEngine.h"

SpectrumEngine::SpectrumEngine()
    : juce::Thread ("Crunch MB Spectrum Engine")
{
    ringPre_.resize ((size_t) fifoPre_.getTotalSize());
    ringPost_.resize ((size_t) fifoPost_.getTotalSize());
    startThread (juce::Thread::Priority::normal);
}

SpectrumEngine::~SpectrumEngine()
{
    stopThread (2000);
}

void SpectrumEngine::pushSamples (const float* pre, const float* post, int numSamples) noexcept
{
    if (pre == nullptr || post == nullptr || numSamples <= 0)
        return;

    // SPSC 单生产者侧：prepareToWrite 无锁，只报告能写入多少——
    // 消费者停滞时丢样本而非阻塞音频线程。
    int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
    fifoPre_.prepareToWrite (numSamples, s1, n1, s2, n2);

    if (n1 > 0)
        juce::FloatVectorOperations::copy (ringPre_.data() + s1, pre, n1);
    if (n2 > 0)
        juce::FloatVectorOperations::copy (ringPre_.data() + s2, pre + n1, n2);

    const int written = n1 + n2;

    int t1 = 0, m1 = 0, t2 = 0, m2 = 0;
    fifoPost_.prepareToWrite (written, t1, m1, t2, m2);

    if (m1 > 0)
        juce::FloatVectorOperations::copy (ringPost_.data() + t1, post, m1);
    if (m2 > 0)
        juce::FloatVectorOperations::copy (ringPost_.data() + t2, post + m1, m2);

    fifoPost_.finishedWrite (m1 + m2);
    fifoPre_.finishedWrite (written);
}

void SpectrumEngine::setUIActive (bool isActive) noexcept
{
    uiActive_.store (isActive);
    if (isActive) running_.store (true);      // 复活
}

void SpectrumEngine::suspend() noexcept
{
    running_.store (false);                   // 编辑器关闭：线程进入长睡眠
    uiActive_.store (false);
}

void SpectrumEngine::drain (juce::AbstractFifo& fifo, std::vector<float>& ring, SpectrumAnalyzer& analyzer)
{
    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return;

    int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
    fifo.prepareToRead (ready, s1, n1, s2, n2);

    if (n1 > 0)
        analyzer.pushSamples (ring.data() + s1, n1);
    if (n2 > 0)
        analyzer.pushSamples (ring.data() + s2, n2);

    fifo.finishedRead (n1 + n2);
}

void SpectrumEngine::analyseTick()
{
    const double sr = sampleRate_.load();
    if (sr <= 0.0)
        return;

    if (sr != currentRate_)
    {
        currentRate_ = sr;
        analyzerPre_.prepare (sr, 2048);
        analyzerPost_.prepare (sr, 2048);
        analyzerPre_.setTilt (tilt_.load());
        analyzerPost_.setTilt (tilt_.load());
        publishedSlot_.store (-1);      // 采样率变化后旧数据不得显示
        writeSlot_ = 0;
        uiSlot_    = -1;
        freqs_.clear();
        lastFrameMs_ = 0;
        return;
    }

    // 始终排空（MB 恒双层：Pre + Post）
    drain (fifoPre_,  ringPre_,  analyzerPre_);
    drain (fifoPost_, ringPost_, analyzerPost_);

    const auto nowMs = juce::Time::getMillisecondCounter();
    if (lastFrameMs_ != 0 && nowMs - lastFrameMs_ < kFrameIntervalMs)
        return;
    lastFrameMs_ = nowMs;

    // 两个分析器都必须无条件推进——单个短路表达式会饿死右侧那个
    const bool producedPre  = analyzerPre_.processPendingFrame();
    const bool producedPost = analyzerPost_.processPendingFrame();

    if (! (producedPre || producedPost))
        return;

    if (analyzerPre_.getLastFrameDeltaDb()  < 0.01f
     && analyzerPost_.getLastFrameDeltaDb() < 0.01f)
        return;   // 收敛（静音/稳态音）：不值得重绘

    scratchFreqs_.clearQuick();
    scratchPre_.clearQuick();
    scratchPost_.clearQuick();

    analyzerPre_.getLevels (scratchFreqs_, scratchPre_, kDisplayPoints);
    analyzerPost_.getLevels (scratchFreqs_, scratchPost_, kDisplayPoints);

    if (scratchPre_.size() != kDisplayPoints || scratchFreqs_.size() != kDisplayPoints)
        return;

    if (freqs_.size() != (size_t) kDisplayPoints)
    {
        freqs_.clear();
        freqs_.reserve ((size_t) kDisplayPoints);
        for (int i = 0; i < kDisplayPoints; ++i)
            freqs_.push_back (scratchFreqs_[(size_t) i]);
    }

    auto& slot = slots_[(size_t) writeSlot_];
    for (int i = 0; i < kDisplayPoints; ++i)
    {
        slot.pre[(size_t) i]  = scratchPre_[(size_t) i];
        slot.post[(size_t) i] = scratchPost_[(size_t) i];
    }

    publishedSlot_.store (writeSlot_, std::memory_order_release);
    publishVersion_.fetch_add (1, std::memory_order_release);
    writeSlot_ = (writeSlot_ + 1) % 3;
}

void SpectrumEngine::publishDecayFrame (double dtSec, double tauSec)
{
    // #8：把已发布的最后一帧按时间常数拉向 −100 dB（keep-hold 之后的下落）
    const int slot = publishedSlot_.load (std::memory_order_acquire);
    if (slot < 0)
        return;                                  // 从未发布过：无可衰减的画面

    auto& s = slots_[(size_t) slot];
    const float k = (float) (1.0 - std::exp (-dtSec / juce::jmax (1.0e-4, tauSec)));

    bool stillMoving = false;
    for (int i = 0; i < kDisplayPoints; ++i)
    {
        s.pre[(size_t) i]  += (-100.0f - s.pre[(size_t) i])  * k;
        s.post[(size_t) i] += (-100.0f - s.post[(size_t) i]) * k;
        if (s.pre[(size_t) i] > -99.5f || s.post[(size_t) i] > -99.5f)
            stillMoving = true;
    }
    publishedSlot_.store (slot, std::memory_order_release);   // 同一 slot 内容更新
    publishVersion_.fetch_add (1, std::memory_order_release);

    if (! stillMoving)
        starvedSinceMs_ = 0;      // 已到底：恢复"保持"语义（等下一次音频再保持新帧）
}

void SpectrumEngine::run()
{
    bool wasActive = false;
    // #8：Pro-MB 行为——编辑器开着但收不到音频（停止播放/无信号）时，
    // 先保持最后一帧 kHoldMs，再以 τ = kSilenceTauMs 的时间常数衰减到 −100 dB。
    constexpr int   kHoldMs      = 600;      // 保持时长
    constexpr double kSilenceTau = 0.600;    // 衰减时间常数（秒）

    while (! threadShouldExit())
    {
        // 挂起态（编辑器关闭）：长睡眠，不参与调度
        if (! running_.load())
        {
            wait (200);
            continue;
        }

        wait (5);   // ~200 唤醒/s，远高于分析所需 ~60Hz

        if (threadShouldExit())
            break;

        const bool active = uiActive_.load();
        const auto nowMs = juce::Time::getMillisecondCounter();

        if (active && ! wasActive)
        {
            // 编辑器刚打开。无人观看期间环形缓冲积了数秒前的音频，
            // 喂给分析器会留下永久积压（产销平衡，再也排不空）。
            // 从最新音频开始。
            fifoPre_.reset();
            fifoPost_.reset();
            starvedSinceMs_ = 0;
            lastDataMs_ = nowMs;
            lastHeartbeatMs_ = 0;
        }

        wasActive = active;

        if (! active)
            continue;   // 无编辑器：无需分析

        // #8：饥饿判定——ring 中的数据量在持续变化（音频线程在喂）
        //     ⚠️ 不能用"fifo 短暂为空"判定：host 每个块之间 fifo 都会空一下，
        //     那会误触发保持期 → 正常播放时频谱也停止刷新（2026-09-23 T18 回归）。
        const int ready = fifoPre_.getNumReady();
        if (ready != lastReadyCount_)
        {
            lastReadyCount_ = ready;
            lastDataMs_ = nowMs;               // 数据在流动 → 重置计时
        }
        starvedSinceMs_ = (nowMs - lastDataMs_ >= (juce::uint32) kHoldMs)
                            ? lastDataMs_ + kHoldMs : 0;
        if (starvedSinceMs_ != 0 && publishedSlot_.load (std::memory_order_acquire) >= 0)
        {
            // 已保持满 kHoldMs：按时间常数衰减最后一帧
            const double dt = (lastHeartbeatMs_ == 0)
                                ? 1.0 / 60.0
                                : juce::jlimit (0.001, 0.1,
                                                (double) (nowMs - lastHeartbeatMs_) / 1000.0);
            if (lastHeartbeatMs_ == 0)
                lastHeartbeatMs_ = nowMs;
            publishDecayFrame (dt, kSilenceTau);
            continue;
        }

        starvedSinceMs_ = 0;
        lastHeartbeatMs_ = 0;
        analyseTick();
    }
}

bool SpectrumEngine::getLatest (std::array<float, kDisplayPoints>& pre,
                                std::array<float, kDisplayPoints>& post)
{
    const int slot = publishedSlot_.load (std::memory_order_acquire);
    const auto ver = publishVersion_.load (std::memory_order_acquire);

    if (slot < 0)
        return false;
    // #8：衰减帧复用同一 slot，靠 publishVersion_ 单调递增让 UI 也能收到更新
    if (slot == uiSlot_ && ver == uiVersion_)
        return false;

    uiSlot_ = slot;
    uiVersion_ = ver;
    pre  = slots_[(size_t) slot].pre;
    post = slots_[(size_t) slot].post;
    return true;
}

bool SpectrumEngine::getFrequencies (std::vector<float>& freqs)
{
    if (freqs_.empty() || freqs.size() == freqs_.size())
        return false;

    freqs = freqs_;
    return true;
}
