#pragma once

#include <JuceHeader.h>

#include <cmath>

// ============================================================
// Anim — UI 动画基础设施（UI_PLAN_V2.md §6 / 01_UI_ANIMATION_PLAN.md）
//
// 三个零件：
//   ① Anim::  时长常量（全项目唯一真源，禁止散落魔法数）
//   ② Ease::  缓动函数（Pro-MB 的 CSS ease 等价物）
//   ③ Tween   动画标量（有明确起止、帧率无关、可判断 done）
//
// 为什么必须换掉指数平滑：
//   1 - exp(-dt/τ) 数学上永不完全结束，只是渐近；没有明确的 250ms。
//   Tween 有明确起止，才能 (a) 用 done() 判断能否停 Timer（UI_PLAN_V2 Phase E），
//   (b) 用 outCubic 得到 Pro-MB 那种"快出慢停"手感。
//
// 音频线程纪律：Tween 只在消息线程推进（timerCallback），音频线程不碰（TECH_SPEC §4）。
// ============================================================
namespace Anim
{
    // ---- 时长常量（毫秒）----
    static constexpr double kDrawerMs   = 250.0;   // LINKED 抽屉滑入（UI_SPEC §5.2）
    static constexpr double kPopoverMs  = 220.0;   // 频段 Popover（略快于抽屉，层次感）
    static constexpr double kFadeMs     = 160.0;   // hover / 选中 / 按钮列
    static constexpr double kKnobMs     = 120.0;   // 旋钮拖动高亮
    static constexpr double kBlinkMs    = 500.0;   // 光标半周期
    static constexpr double kMeterTauMs = 90.0;    // 显示量程平滑
    static constexpr double kRangeTauMs = 50.0;    // Range 拖动平滑

    // ---- 收敛阈值：小于此值直接吸附到目标，避免无限趋近 ----
    static constexpr float kSnapEps = 0.004f;
}

// ============================================================
// Ease — 缓动函数。t ∈ [0,1] → [0,1]
// ============================================================
namespace Ease
{
    /** 快出慢停（Pro-MB / iOS default）。入场/滑入首选。 */
    inline float outCubic (float t)
    {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }

    /** 两端慢中间快。位置横滑（Popover 切段）首选。 */
    inline float inOutCubic (float t)
    {
        return (t < 0.5f) ? (4.0f * t * t * t)
                          : (1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f);
    }

    /** 线性（无缓动）。电平表/实时驱动用。 */
    inline float linear (float t) { return t; }
}

// ============================================================
// Tween — 带动画的标量。
//
// 用法：
//   Tween t;
//   // 目标变化时：
//   t.start (nowMs, 1.0f, Anim::kFadeMs, t.value (nowMs));  // current 保证从视觉位置起跳，不回弹
//   // 每帧：
//   const float a = t.value (nowMs);
//   // 空闲判定：
//   if (t.done (nowMs)) ...
//
// ⚠️ start() 里必须传**当前显示值**作为 current，否则切目标时会从旧目标跳变。
// ============================================================
struct Tween
{
    double t0ms   = 0.0;      // 起点时刻（juce::Time::getMillisecondCounter()）
    double durMs  = Anim::kFadeMs;
    float  from   = 0.0f;
    float  to     = 0.0f;
    bool   active = false;

    /** 目标变化时调用。current = 当前显示值（保证从视觉位置起跳，不回弹） */
    void start (double nowMs, float newTarget, double ms, float current)
    {
        if (newTarget == to && active)
            return;                        // 目标未变且仍在动画中：不打断
        from   = current;
        to     = newTarget;
        t0ms   = nowMs;
        durMs  = (ms > 0.0) ? ms : Anim::kFadeMs;
        active = true;
    }

    /** 立即跳到目标（无动画）。用于初始化和 reset。 */
    void snapTo (float v)
    {
        from = to = v;
        active = false;
    }

    /** 当前值（已缓动）。 */
    float value (double nowMs) const
    {
        if (! active)
            return to;
        const double t = (nowMs - t0ms) / durMs;
        if (t >= 1.0) return to;
        if (t <= 0.0) return from;
        return from + (to - from) * Ease::outCubic ((float) t);
    }

    /** 是否已到目标（可用于停 Timer 的空闲判定）。 */
    bool done (double nowMs) const
    {
        return (! active) || ((nowMs - t0ms) >= durMs);
    }

    /** 当前目标（用于判断是否需要 repaint）。 */
    float target() const noexcept { return to; }
};

/** 便捷：把 Tween 推到目标（目标没变就不动）。 */
inline void tweenTo (Tween& t, double nowMs, float target, double ms)
{
    if (target != t.target())
        t.start (nowMs, target, ms, t.value (nowMs));
    else if (t.done (nowMs))
        t.snapTo (target);
}
