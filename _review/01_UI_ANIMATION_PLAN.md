# Crunch MB UI 修改计划书 v2（动画专项）

> 目标：**保持现有三主题色 / 三层架构 / 可交互样式与使用逻辑**，把动画质感与交互细节对标
> FabFilter Pro-MB 与 Three-Body Future MB。
> 依据：通读 `src/ui/` 全部源码 + 两个参考插件的逆向结论（`261013 fbmb Crack` / `261013 MB Crack`）
> 日期：2026-10-14

---

## 0. 一句话诊断

你的动画**不是"调得不好"，而是"根本没有动画"**——除了抽屉/Popover 有一个半成品 alpha 渐隐，
其余所有状态切换（选中、hover、bypass、分割线、旋钮拖动）**全是二值硬跳**。
另外有 **4 个真 bug**，其中 2 个会直接可见。

---

## 1. 现状清单：18 项，分 4 类

### A 类 · 真 bug（有可见错误）

| # | 位置 | 代码 | 后果 |
|---|---|---|---|
| **A1** | `DisplayView.cpp:1959` | `const float a = linkedOpen_ ? 1.0f : 0.0f;` | **实锤。** 抽屉旋钮位置用开关量而非动画量 `linkedAnim_`。打开时旋钮**瞬间跳到终位**、关闭时**瞬间跳到面板下方**。`linkedAnim_` 被正确计算了（786-797 行）却只用于 alpha（2012 行）——**滑入动画等于没做** |
| **A2** | `Knob.cpp:235` | `editCursorOn_ = ! editCursorOn_;` 写在 `paint()` 内 | **实锤。** 在绘制函数里改状态。光标闪烁频率 = 重绘率：DisplayView 60Hz 时光标以 30Hz 狂闪；若只在事件触发重绘则每次重绘翻一次。**必须改为时间驱动** |
| **A3** | `DisplayView.cpp:796` | 动画期间**每帧**调 `layoutLinkedKnobs()` | 每帧 5 次 `setBounds()` → 5 次 `Knob::resized()`。CPU 白烧，且 `setBounds` 取 int 会有 1px 量化抖动 |
| **A4** | `DisplayView.cpp:2489` | `drawDrawer (g);` 连续调两次 | 抽屉区域 alpha 双重混合（偏深偏灰） |

### B 类 · 动画缺失（有状态、无过渡）

| # | 位置 | 现状 | 参考插件怎么做 |
|---|---|---|---|
| B1 | `popoverRect():2043` + `drawPopover():2078` | rect 固定，只把 alpha 乘 `a` | Pro-MB/Future MB 面板入场带**位移**。你的 mockup 本来写了 `transform:translateY(14px)` + `transition:.25s ease`——**没实现** |
| B2 | `mouseDown():1734` | 切段时 `popoverBand_ = i` 直接跳 | 面板应**平滑横滑**到新段中心（Future MB 的 `ComponentBoundsProgress`） |
| B3 | `drawPartitions():200` | `sel ? 0.10f : 0.05f` 硬跳 | 选中态淡入淡出 |
| B4 | `drawCrossovers():287` | `hot ? pal_.accent : pal_.gridZ` 硬跳；手柄无 hover 反馈 | 分割线 hover 渐变色 + 手柄高亮 |
| B5 | `Knob.cpp:181,193` | `dragging_ ? 1.35f : 1.0f` / `brighter(0.30f)` 硬跳 | 拖动时弧加亮加粗是**渐变**的 |
| B6 | `mouseMove():1476` | `hoverBand_` 二值；按钮列瞬时显隐 | UI_SPEC §4.2 原文"按钮列 hover 该段时**淡入、移出淡出**"——未实现 |
| B7 | `drawPartitions():197` | BYP dim `0.45f` 常量 | bypass 段**渐暗**（Pro-MB："the band controls are dimmed"） |

### C 类 · 动画曲线不对

| # | 位置 | 现状 | 问题 |
|---|---|---|---|
| C1 | `DisplayView.cpp:786,805` | `k = 1 - exp(-dt/τ)`, `τ=0.085` | 指数平滑**没有明确的 250ms**。它数学上永不完全结束，只是渐近；且不同初值路径差异大。这是"动效发糯"的数学根源 |
| C2 | 全文件 | 无一处 easing 函数 | Pro-MB 的 CSS 是 `transition:.25s ease`（cubic-bezier）。你需要 `outCubic` 这类"快出慢停"曲线 |
| C3 | `curveRangeDisplay_`(τ=90ms) / `rangeDisplay_`(τ=50ms) | 两个独立指数平滑，各自渐进 | 数值变化与视觉变化不同步；档位切换时曲线和 Range 层"追"的速度不一致 |

### D 类 · 交互细节

| # | 位置 | 问题 |
|---|---|---|
| **D1** | `Knob.cpp:68-81` | 头文件注释写"垂直拖动：**速度比例**（慢拖=精细，FabFilter 同款逻辑）"，成员 `slowAccum_` **从未被使用**（只在 `mouseDown`/`mouseUp` 里置 0）。实际实现是 `delta = (downY - curY)/220 * span`——**绝对线性**，注释与代码不一致，且没有亚像素累积，慢拖并不更精细 |
| D2 | `Knob.cpp` 全文 | **无 hover 态**。Pro-MB 有 `OverlayActive`（悬停变亮），你只有 `dragging_` |
| D3 | `Knob.cpp:183-195` | 弧外侧**无刻度环**。Pro-MB 的 `KNOBLARGEBACKNORMAL_2X` 就是"深色圆体 + 外圈刻度环"（我直接看图确认了） |
| D4 | `timerCallback():814` | 无条件 `repaint()`。即使完全空闲（无动效、无新频谱帧、无拖拽）也 60Hz 全幅重绘 |

---

## 2. 修改计划：5 阶段

### 阶段 1 · 建动画基础设施（2-3 天）— 后面全靠它

新增 `src/ui/Anim.h`，只放三个零件：

```cpp
#pragma once
#include <JuceHeader.h>
#include <cmath>

// ① 时长常量 —— 全项目唯一真源，禁止散落魔法数
namespace Anim
{
    static constexpr double kDrawerMs   = 250.0;   // UI_SPEC §5.2 原文 ~250ms
    static constexpr double kPopoverMs  = 220.0;   // 比抽屉略快，层次感
    static constexpr double kFadeMs     = 160.0;   // hover / 选中 / 按钮列
    static constexpr double kBlinkMs    = 500.0;   // 光标半周期
}

// ② 缓动 —— Pro-MB 的 CSS ease 等价物
namespace Ease
{
    inline float outCubic (float t)
    {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;                 // 快出慢停
    }
    inline float inOutCubic (float t)
    {
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f;
    }
}

// ③ 动画标量 —— 有明确起止，帧率无关
struct Tween
{
    double t0ms   = 0.0;
    double durMs  = Anim::kFadeMs;
    float  from   = 0.0f;
    float  to     = 1.0f;
    bool   active = false;

    /** 目标变化时调用；current = 当前显示值（保证从视觉位置起跳，不回弹） */
    void start (double nowMs, float newTarget, double ms, float current)
    {
        if (newTarget == to && active) return;
        from   = current;
        to     = newTarget;
        t0ms   = nowMs;
        durMs  = ms;
        active = true;
    }

    float value (double nowMs) const
    {
        if (! active) return to;
        const double t = (nowMs - t0ms) / durMs;
        if (t >= 1.0) return to;
        if (t <= 0.0) return from;
        return from + (to - from) * Ease::outCubic ((float) t);
    }

    bool done (double nowMs) const { return ! active || (nowMs - t0ms) >= durMs; }
};
```

**为什么必须换掉指数平滑**：`1-exp(-dt/τ)` 是"永远在追"，而 `Tween` 是"250ms 内一定到"。
后者才能 (a) 用 `done()` 判断能否停 Timer（修 D4），(b) 用 `outCubic` 得到 Pro-MB 那种"冲出-回稳"手感。

### 阶段 2 · 修 4 个真 bug（半天，可立即验证）

| # | 改法 |
|---|---|
| **A1** | `layoutLinkedKnobs()` 第 1959 行 → `const float a = juce::jlimit(0.0f, 1.0f, linkedAnim_);` |
| **A2** | 删掉 `Knob::paint()` 里的 `editCursorOn_ = ! editCursorOn_;`。改为：`DisplayView::timerCallback()` 里 `freqIn_.cursorVisible = (nowMs % 1000) < 500;`，`paint()` 只读。**Knob 的编辑框同理**——把 `editCursorOn_` 改成由外部时间戳驱动 |
| **A3** | 把 `layoutLinkedKnobs()` 从动画循环里挪出来：只在 `toggleLinkedDrawer()` 和**动画值变化超过 0.5px** 时调一次 |
| **A4** | 删掉第 2489 行重复的 `drawDrawer (g);` |

### 阶段 3 · 补齐缺失的过渡（3-4 天）

**统一模式**：每个需要过渡的视觉状态配一个 `Tween`，`timerCallback` 推进，`draw*` 读 `value()`。

| # | 加什么 | 实现要点 |
|---|---|---|
| **B1** | Popover 入场（位移+淡入） | `py = baseY + (1.0f - e) * sy(14.0f)`，`alpha = e`，220ms。**mockup 的 `translateY(14px)` 终于落地** |
| **B2** | Popover 切段横滑 | 成员 `popoverFromX_`；切段时记旧 X，Tween 从旧 X → 新 X，220ms `inOutCubic`。`popoverRect()` 读 Tween 值 |
| **B3** | 选中态淡入 | `std::array<Tween,4> selT_`；alpha 在 0.05 ↔ 0.10 间过渡；描边 alpha 0 ↔ 0.9 |
| **B4** | 分割线 hover 渐变 | `std::array<Tween,3> divT_`；`gridZ → accent` 颜色插值 160ms；手柄 hover 也加一个 Tween |
| **B5** | 旋钮拖动加亮 | `Knob` 加 `Tween dragT_`；`mouseDown/Up` 里 `start()`；`thickness = base * (1 + 0.35f*dragT_.value())`，弧色 `accent → accent.brighter(0.3f*dragT_)` |
| **B6** | 按钮列淡入淡出 | `std::array<Tween,4> btnT_`；`mouseMove` 里 `start()`；`drawBandHeaders` 用 `btnT_[b].value()` 乘 alpha |
| **B7** | BYP 渐暗 | `std::array<Tween,4> bypT_`；`pollStructureParams()` 里检测 Mix 变化并 `start()`；`drawPartitions` 用 `0.45f + 0.55f*(1-bypT_)` |

### 阶段 4 · 旋钮对标 Pro-MB（2-3 天）

| # | 动作 | 说明 |
|---|---|---|
| **D3a** | **加刻度环** | 在弧外侧画一圈短刻度线（Pro-MB 的 BACK 就是这个）。**缓存成 `juce::Image`**，只在 resize / 换主题时重建。这是**视觉提升最大**的一项 |
| **D3b** | **BACK / OVERLAY 分离** | 刻度环+底 = BACK（静态缓存）；指针+帽+数值 = OVERLAY（每帧画）。CPU 下降，结构对标 Pro-MB |
| **D2** | **加 hover 态** | `mouseEnter/mouseExit` 设 `hovered_`，配一个 `Tween`；弧色 `accent → accent.brighter(0.15f)` 渐变 |
| **D1** | **修拖拽为相对式** | 两种改法，选一个：<br>① **真正用上 `slowAccum_`**（亚像素累积）：`slowAccum_ += dy; const double full = slowAccum_/220.0*span; commitValue(dragStartValue_ + full); slowAccum_ -= std::floor(full)*220.0;` → 慢拖自然精细<br>② 或**删掉 `slowAccum_` 和那句注释**，保持绝对线性（也可接受，但别留死代码） |
| — | 数值读数过渡 | 被自动化改参数时读数 80ms 淡入，避免数字乱跳 |

### 阶段 5 · 性能与收尾（1-2 天）

| # | 动作 |
|---|---|
| **D4** | **空闲停 Timer**：`timerCallback` 末尾判断"所有 Tween `done()` && 无新频谱帧 && 无拖拽 && 无光标闪烁" → `stopTimer()`；`mouseDown/mouseMove/toggleXxx` 里 `startTimerHz(60)`。**这一条依赖阶段 1 的 `done()`，所以必须最后做** |
| — | `resized()` 统一失效所有缓存 Image（刻度环等） |
| — | `setTheme()` 统一失效所有缓存 Image |
| — | 用你已有的 `paintForTest` / `tickForTest` 加动画断言（见 §4） |

---

## 3. 实施顺序与依赖

```
阶段1 Anim.h（Tween + outCubic）
   ├─→ 阶段2 修 4 个 bug        （0.5 天，立即见效）
   └─→ 阶段3 补 7 处过渡        （3-4 天）
          └─→ 阶段4 旋钮对标     （2-3 天）  ← 时间紧就只做 D3a 刻度环
                 └─→ 阶段5 性能  （1-2 天）  ← 依赖阶段1 的 done()
```

**总工期：约 7-11 天**（单人，含自测）。

**如果只有 3 天**：阶段 2（修 bug）+ 阶段 1 的 `Anim.h` + B1（Popover 位移）+ D3a（刻度环）。
这四样是"可见错误全消 + 动效立刻有质变"的最小集。

---

## 4. 验收标准（用你现成的测试钩子）

你已经有 `paintForTest` / `tickForTest` / `dbgXxx` 一整套钩子，直接加断言：

| 项 | 断言 |
|---|---|
| A1 | `toggleLinkedDrawer(true)` 后第 1 帧 `linkedAnim_ < 0.2`；连续 `tickForTest()` 15 次（250ms@60fps）后 `linkedAnim_ == 1.0` |
| A2 | 连续 `tickForTest()` 10 次，光标 `cursorVisible_` 只翻转 1 次（时间驱动，与重绘次数解耦） |
| A3 | 动画期间 `layoutLinkedKnobs()` 调用次数 ≤ 15（每 ~16ms 一次），不是每帧 60 次 |
| B1 | `togglePopover(true)` 后，`popoverAnim_` 从 0 到 1 的用时 ∈ [200, 240] ms |
| B2 | 切段后 `popoverFromX_` 与当前 X 不同，且 220ms 内收敛 |
| B3 | `selectedBand_` 切换后，`selT_[old]` 160ms 内归 0、`selT_[new]` 160ms 内到 1 |
| D4 | 空闲 5 秒（无交互、无动效）后 Timer 处于停止态 |

---

## 5. 明确不做的事

- ❌ **不改三主题色值**（你要求保持 `MBTheme.h` 的 Blue/Red/Cream）
- ❌ **不改三层架构**（分区看选 + Popover 调 + 底座全局）
- ❌ **不上 GPU / OpenGL**。Pro-MB 自己都是 OpenGL 1.1 固定管线贴图，JUCE 软件渲染 60fps 完全够（你的 `UI_SPEC §12.1` 已经论证过）
- ❌ **不重写 `DisplayView`**。类拆分已在 `00_CODE_REVIEW.md` 规划，与本计划书**并行不冲突**；本计划只加动画，不改结构
- ❌ **不加 backdrop blur**。这项在 `00_CODE_REVIEW.md` 第二期，属视觉而非动画；且会影响帧预算，应在动画全部落地、帧时间有余量后再上

---

## 6. 附：与 `00_CODE_REVIEW.md` 的关系

| | `00_CODE_REVIEW.md` | 本计划书 |
|---|---|---|
| 关注点 | **结构**（God Object、控件层缺失、参数查找） | **动画**（18 项，4 类） |
| 改完的效果 | "改一处不坏三处" | "动起来有质感" |
| 冲突 | 无。拆分 `BandPopover` 时，把 `popoverAnim_` 和新的 `popoverFromX_` 一起搬过去即可 | |

**建议**：先做本计划书的阶段 1+2（2.5-3.5 天），动效基础打好；再按 `00_CODE_REVIEW.md` 拆分类。反序会让动画代码搬两次。
