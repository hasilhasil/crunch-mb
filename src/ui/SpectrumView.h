#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

#include "MBTheme.h"
#include "../dsp/SpectrumEngine.h"

class CrunchMBAudioProcessor;

// ============================================================
// SpectrumView — 双层频谱视图（从 DisplayView 拆出）
//
// 职责：
//   · 每帧从 SpectrumEngine 取最新帧（无锁三缓冲）
//   · 按坐标映射把 dB 值变成 Path（fill + stroke）
//   · 绘制：Pre = accent，Post = inFill（B1 用户验收指定）
//
// 设计约束：
//   · Path 只在内容变化时重建（新帧 / 坐标变化），避免每帧 512 点重算
//   · guard 用"内容为空"而非脏标志——脏标志为 true 的那一帧会整层不画，
//     但 Path 里还有上一帧几何可用（2026-09-23 修复：用户看到频谱偶发消失）
//   · 音频线程只写 atomic，本组件全在消息线程（TECH_SPEC §4）
// ============================================================
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (CrunchMBAudioProcessor& processor);
    ~SpectrumView() override;

    void paint (juce::Graphics&) override;

    // ---- 坐标映射注入（由 DisplayView 提供，避免这里再算一套）----
    void setCoordMapper (std::function<float (float)> freqToX,
                         std::function<float (float)> dbToY,
                         std::function<float ()> bottomY);

    /** 每帧：取新帧并（必要时）重建 Path。返回 true 表示有新帧。 */
    bool updateFromEngine();

    /** 坐标/尺寸变化 → 强制下次重建。 */
    void invalidate() { pathsValid_ = false; }

    /** Analyzer Pre/Post 开关。 */
    void setLayerVisible (bool pre, bool post) { preOn_ = pre; postOn_ = post; repaint(); }

    void setTheme (MBTheme::Theme t) { pal_ = MBTheme::palette (t); repaint(); }

    /** 测试钩子 */
    bool hasFrameForTest() const { return hasFrame_; }
    bool pathsReadyForTest() const { return pathsValid_ && ! (preFill_.isEmpty() && preStroke_.isEmpty()); }

private:
    void refreshPaths();

    CrunchMBAudioProcessor& proc_;
    MBTheme::Palette pal_ = MBTheme::palette (MBTheme::Theme::Blue);

    static constexpr int kPoints = SpectrumEngine::kDisplayPoints;

    std::vector<float> freqs_;
    std::array<float, kPoints> pre_ {}, post_ {};
    bool hasFrame_ = false;

    juce::Path preFill_, preStroke_, postFill_, postStroke_;
    bool pathsValid_ = false;

    bool preOn_ = true, postOn_ = true;

    std::function<float (float)> freqToX_;
    std::function<float (float)> dbToY_;
    std::function<float ()>       bottomY_;
};
