# Crunch MB — UI 改造计划书 v2（Pro-MB 对标版）

> **文档地位**：本文件是 **UI 改造的权威规格**，与既有文档的关系见 §0。
> 制定日期：2026-10-14 · 版本：v2.0
> **改造目标**：在**保留三主题色 / Poppins 字体 / 三层交互架构 / LINKED CONTROL 模块**的前提下，
> 把布局与操控逻辑改为**高度接近 FabFilter Pro-MB**。

---

## 0. 阅读顺序（新模型必读）

| 序 | 文档 | 读什么 | 与本文件的关系 |
|---|---|---|---|
| 1 | **本文件 UI_PLAN_V2.md** | 改造目标、新布局、控件规格、交互规格、分期 | **UI 改造唯一权威** |
| 2 | `UI_SPEC.md` v1.8 | **原三层架构 + 参数-控件映射 + 三主题色值** | 色值/字体/参数映射**继续有效**，布局章节被本文件取代 |
| 3 | `DESIGN.md` | 产品定义：4 频段、62→79 参数、D1–D14 决策 | D1–D14 全部继续有效 |
| 4 | `TECH_SPEC.md` | 参数精确规格（§5，已更新到 79 参数）、DSP 架构、音频线程红线 | 参数部分以 TECH_SPEC §5 为准 |
| 5 | `PLAN.md` | 工序、教训、Phase gate | 本文件是 UI Phase 的新输入 |
| 6 | `01_UI_ANIMATION_PLAN.md` | 动画专项（18 项问题 + Anim.h 骨架） | **并入本文件 §6**，仍作为动画细节参考 |
| 7 | `02_DSP_BUG_AND_FEATURE_AUDIT.md` | DSP 功能对标结论 | 解释为何有 Oversampling/Knee/侧链/StereoLink 新参数 |
| 8 | `00_CODE_REVIEW.md` | 结构问题（DisplayView God Object） | 本文件 §8 的分期要配合类拆分 |

> ⚠️ **冲突裁决**：本文件与 `UI_SPEC.md` 在**布局与交互**上冲突时，以本文件为准；
> 在**色值、字体、参数 ID、频段语义**上冲突时，以 `UI_SPEC.md` / `TECH_SPEC.md` 为准。

---

## 1. 改什么、留什么

### 1.1 保留（不许动）

| 项 | 来源 |
|---|---|
| 三主题色板 Blue/Red/Cream 全部色值 | `UI_SPEC.md` §9 / `MBTheme.h` |
| Poppins 四字重 + Typeface static 缓存 | `MBTheme.h` |
| 4 频段（Low/LowMid/HighMid/High）+ 1 八度最小带宽 | DESIGN D2/D3 |
| 79 参数与 ID | TECH_SPEC §5（2026-10-14 扩充） |
| 三层交互架构（分区看选 / Popover 调 / 底座全局） | UI_SPEC §2 |
| **LINKED CONTROL 模块**（5 握柄：Threshold/Attack/Release/Makeup/Mix） | DESIGN D4 / UI_SPEC §5.2 |
| 双层频谱（Pre 灰 + Post accent） | DESIGN D12 |
| 仿 EQ 动态曲线 + Range 钟型层 | DESIGN D13 |
| 纯软件渲染（JUCE Graphics），**不上 GPU** | UI_SPEC §12.1 |

### 1.2 改变（本文件核心）

| 现状（UI_SPEC v1.8） | 目标（Pro-MB 风格） |
|---|---|
| 920×620 固定逻辑尺寸，绘图区是"框起来的一块" | **频谱全出血铺满整个窗口**，分区/曲线/刻度全高绘制 |
| 顶栏 30px 玻璃条 + 底栏 32px | Pro-MB 式**顶栏（logo/频段按钮/Analyzer）+ 底栏（全局菜单）**，均为半透明玻璃条 |
| 4 分区底色 + 选中段上下横边描边 | **全高竖直分割线**把频谱切成 4 区；选中段用 accent 低 alpha 填充 |
| 按钮列（S/M/BYP/Δ）**常驻**在分割线右侧 | **hover 该段时淡入**，移出淡出（Pro-MB 原话："small solo and mute buttons will appear"） |
| 频段 Makeup 靠频点圆点拖拽 | **频段电平点（可拖拽圆点）+ Range 滑杆**双入口，圆点带圆形电平表 |
| 交叉点手柄在绘图区下部 y≈500 | **分割线底部手柄 + 频率标签**（挂在分割线正下方） |
| 单刻度（左频谱 + 右增益，已接近） | **双刻度**：右=增益刻度（accent，贴紧电平表），左=频谱刻度（dim） |
| Popover = 5 旋钮 + 3 滑杆 + EXPERT 文字 | Pro-MB 式：5 旋钮行（带 min/max 范围标注）+ 滑杆行 + **右缘竖排 EXPERT 标签** |
| 旋钮 = 单一矢量弧 + 帽 + 指针 | **外圈刻度环（带底部缺口）+ 深色圆体 + 浅灰帽指针**，四态（见 §4） |
| 无圆形电平表 | **Threshold 旋钮外圈圆形电平表**（Pro-MB 标志性元素） |
| LINKED 抽屉从底部梯形按钮弹出 | **保留 LINKED 模块**，改为从底栏按钮弹出（形态不变，位置对齐 Pro-MB 底栏） |

---

## 2. 新布局（线框）

默认 920×620，可缩放 700×500 – 1840×1240（VST3 自定义缩放）。

```
┌──────────────────────────────────────────────────────────────────────────┐
│  ● CRUNCH MB            [L][LM][HM][H] 频段标签      ANALYZER   ⚙  SETTINGS│  顶栏 30px 玻璃条
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   ┆            ┆            ┆            ┆            ┆        ┌────────┐  │
│   ┆            ┆            ┆            ┆            ┆        │  +12   │  │
│   ┆   Pre 灰   ┆   Pre 灰   ┆   Pre 灰   ┆   Post     ┆        │   +6   │  │  右：增益刻度
│   ┆   频谱     ┆   频谱     ┆   频谱     ┆   accent   ┆        │    0   │  │  （accent，贴紧
│   ┆            ┆            ┆            ┆   频谱     ┆        │   −6   │  │   电平表）
│   ┆  ╭──╮      ┆            ┆            ┆            ┆        │  −12   │  │
│   ┆  │  │ 频段 ┆            ┆            ┆            ┆        └────────┘  │
│   ┆  ╰──╯ 电平 ┆            ┆            ┆            ┆        ┌────────┐  │
│   ┆   ● 点     ┆            ┆            ┆            ┆        │ IN/OUT │  │
│   ┆  ───Range  ┆            ┆            ┆            ┆        │ 电平表 │  │
│   ┆  滑杆      ┆            ┆            ┆            ┆        │  全高   │  │
│   ┆            ┆            ┆            ┆            ┆        └────────┘  │
│   ┆ [S][M][B][Δ]  ← hover 淡入            ┆            ┆                   │
│   ┆            ┆            ┆            ┆            ┆                   │
│   └─ 200 ─┘    └─ 1k ─┘    └─ 6k ─┘    └─ 15k ─┘                       │
│     ↑ 分割线底部手柄 + 频率标签                                            │
│   ┌──────────────────────────────────────────────────────────────────┐   │
│   │ PHASE MODE  OS  LOOKAHEAD  SPECTRAL │ THR ATT REL MK │ IN OUT MIX│   │  底栏 32px
│   └──────────────────────────────────────────────────────────────────┘   │
│   [LINKED CONTROL 抽屉按钮]  ← 点击弹出 5 握柄面板（保留模块）              │
└──────────────────────────────────────────────────────────────────────────┘
```

**Popover（点击分区弹出，Pro-MB 样式）**
```
        ┌─────────────────────────────────────────────┐
        │ ◉  LOW                          ▾        ✕ │  ← 顶栏：电源/旁路 + 下拉 + 关闭
        │                                             │
        │  ╭───╮  ╭───╮  ╭───╮  ╭───╮  ╭───╮          │  ← 5 旋钮行
        │  │THR│  │RNG│  │ATK│  │REL│  │OUT│          │
        │  ╰───╯  ╰───╯  ╰───╯  ╰───╯  ╰───╯          │
        │  -60..0 -30..30 0..200 10..1000 0..100      │  ← 每旋钮下 min/max 标注
        │                                             │
        │  [COMPRESS|EXPAND]  RATIO ═══════           │  ← 滑杆行
        │  KNEE ═══════  LOOKAHEAD ═══════             │
        │                                             │
        │  [SIDE CHAIN ▾]  [STEREO LINK ═══]          │  ← 2026-10-14 新增控件
        │                                    ┌──────┐ │
        │                                    │EXPERT│ │  ← 右缘竖排标签
        │                                    └──────┘ │
        └─────────────────────────────────────────────┘
```

---

## 3. 控件清单（需要新建/改造）

### 3.1 新建控件（当前代码里不存在）

| 控件 | 基类 | 职责 | Pro-MB 对应 |
|---|---|---|---|
| `BandLevelDot` | `juce::Component` | 频段电平点：圆点 + 外圈圆形电平表 + 垂直拖拽改 Makeup | Threshold 旋钮外圈电平表 |
| `RangeSlider` | `juce::Component` | 频段 Range 滑杆：水平/垂直拖拽，带吸附 | Pro-MB range slider |
| `DividerHandle` | `juce::Component` | 分割线底部手柄：拖拽改 crossover 频率 + 频率标签 | Pro-MB crossover handle |
| `GainScale` | `juce::Component` | 右侧增益刻度（±3/6/12/30 可切） | Pro-MB 黄色增益刻度 |
| `TopBar` | `juce::Component` | logo + 频段标签 + Analyzer/设置按钮 | Pro-MB top bar |
| `BottomBar` | `juce::Component` | 全局菜单（Phase/OS/Lookahead/Spectral/各级 Scale/IN/OUT/MIX） | Pro-MB bottom bar |
| `SegButton` | `juce::Component` | 分段双态按钮（COMPRESS↔EXPAND） | Pro-MB segmented |
| `TextSelect` | `juce::Component` | 文本选择器，点击弹二级菜单 | Pro-MB drop-down |
| `BandPopover` | `juce::Component` | 从 DisplayView 抽出的频段浮层（§3.2） | Pro-MB band panel |
| `LinkedDrawer` | `juce::Component` | 从 DisplayView 抽出的 LINKED 面板（§3.2） | — （保留模块） |
| `SpectrumView` | `juce::Component` | 从 DisplayView 抽出的双层频谱 | — |

### 3.2 从 DisplayView 拆出（配合 `00_CODE_REVIEW.md`）

`DisplayView` 当前 ~2500 行 / 96 个成员函数，是 UI bug 的结构性根因。**必须先拆再改造**：

| 拆出 | 搬走的成员 | 预计减行 |
|---|---|---|
| `BandPopover` | `popoverRect/drawPopover*/hitTestPopover*/popKnobs_/popSlider*/popoverValues_/togglePopover` | −400 |
| `LinkedDrawer` | `drawerPanelRect/hitTestDrawerButton/toggleLinkedDrawer/ensureLinkedKnobs/layoutLinkedKnobs/applyLinkedOffset/drawLinkedDrawer/linkedKnobs_` | −250 |
| `SpectrumView` | `drawSpectrum/refreshSpectrumPaths/spectrumFreqs_/spectrumPre_/spectrumPost_` | −120 |
| `FreqInputEditor` | `openFreqEditor/closeFreqEditor/drawFreqInput/FreqInputState` | −100 |
| `ParamSlider`/`SegButton`/`TextSelect` | 新控件（见 §3.1） | +300 / −150 |

> 拆完后 `DisplayView` 应降到 ~900 行，只保留**分区绘制 + 命中分发 + 子组件编排**。

---

## 4. 旋钮规格（你重点关注的样式）

### 4.1 三层结构

```
   ┌─────────────┐
   │  ○ ○ ○ ○ ○  │  ← 外圈刻度环（tick ring），底部有缺口，行程 270°
   │   ╭─────╮   │
   │  │ ▓▓▓▓▓ │  │  ← 深色圆体（radial gradient：上亮下暗）
   │  │ ▓ ▓ ▓ │  │  ← 浅灰"帽"指针（cap），中间深色竖线
   │   ╰─────╯   │
   │    THRESHOLD │  ← 标签（旋钮上方）
   │     −32.0 dB │  ← 数值读数（旋钮下方）
   └─────────────┘
```

### 4.2 四态（Pro-MB 的 OVERLAY{NORMAL,ACTIVE,DOWN,ROTATE}）

| 状态 | 触发 | 视觉 |
|---|---|---|
| `NORMAL` | 默认 | 帽 = 中性浅灰（`pal_.knob`） |
| `ACTIVE` | 鼠标悬停 | 帽略亮（`knob.brighter(0.15)`），弧色略亮 |
| `DOWN` | 按下 | 帽再亮一档，弧加粗 |
| `ROTATE` | 拖动中 | 帽转**暖色**（`accent.brighter(0.35)`），弧加粗 1.35× —— 这就是 Pro-MB `OverlayRotate` 的等效实现 |

> ⚠️ 当前 `Knob.cpp:181,193` 的 `dragging_ ? 1.35f : 1.0f` / `brighter(0.30f)` 是**硬跳**，
> 必须改成 `Tween` 渐变（见 §6 与 `01_UI_ANIMATION_PLAN.md` B5）。

### 4.3 绘制分层（性能关键）

| 层 | 内容 | 缓存策略 |
|---|---|---|
| **BACK** | 刻度环 + 圆体底色 + 径向渐变 | **缓存成 `juce::Image`**，只在 resize/换主题/换尺寸时重建 |
| **OVERLAY** | 帽指针 + 中心线 + 弧 | 每帧绘制（弧随值变，帽随旋转角变） |

> Pro-MB 的 `KNOBLARGEBACKNORMAL_2X` 就是把 BACK 预渲染成位图。
> 我们用矢量 + Image 缓存达到同样效果，**不需要位图资产**。

### 4.4 角度约定（沿用现有，勿改）

```
起点（最小）= 7:30 方向（π·1.25），终点（最大）= 4:30 方向（π·3.75），行程 270°，缺口在正下方
point = (cx + R·sin(θ), cy − R·cos(θ))
```
（`Knob.cpp:34-40` 已实现且正确，与 Pro-MB 视觉一致）

### 4.5 Threshold 旋钮的圆形电平表

`BandLevelDot` 外圈画一圈电平环：
- 内圈 = 该段检测电平（dB，−60..0 映射到 0..1）
- 颜色：`pal_.accent`，峰值保持段用 `accent.brighter(0.4)`
- 衰减：60 dB/s 线性，峰值保持 2 s（复用 `updatePeakHold` 逻辑）

---

## 5. 交互规格（Pro-MB 操控逻辑全集）

### 5.1 鼠标手势

| 操作 | 行为 |
|---|---|
| 悬停频段 | 该段 S/M/BYP/Δ 按钮列**淡入**（160ms），移出淡出 |
| **按住** S/M/BYP/Δ | **临时**生效，松手恢复 |
| **Ctrl+点击** S/M | **独占** solo/mute 该段 |
| 点击分区 | 选中该段 + 弹出 Popover |
| 再次点击同段 | 收起 Popover |
| 点击空白处 | 收起 Popover |
| 拖拽频段电平点（垂直） | 改 Makeup；同时收起 Popover |
| 拖拽 Range 滑杆 | 改 Range |
| 拖拽分割线手柄（水平） | 改 crossover 频率；旁边的频率标签实时更新 |
| 双击分割线手柄 | 弹出频率输入框（自绘，复用现有 `FreqInputEditor`） |
| **在显示背景上点击拖拽** | **多选频段**（Pro-MB："click and drag across the display background to quickly select multiple bands"） |
| 拖拽超出显示范围 | **自动扩展显示范围**（若 Auto-Adjust 已勾选） |
| 滚轮（在分区上） | 缩放显示范围 |

### 5.2 旋钮手势（沿用现有，补一项）

| 操作 | 行为 |
|---|---|
| 垂直拖拽 | 220px = 全程；Shift = ×0.1 微调 |
| 滚轮 | 按 interval 步进 |
| 双击 | 内嵌文本输入（自绘，不用 juce::TextEditor） |
| Ctrl+点击 | 复位默认值 |
| **Alt+拖拽 OUTPUT 旋钮** | **makeup gain**：自动校正当前立体声模式的声像（Pro-MB 原话） |
| **Alt+拖拽任意旋钮** | 微调模式（可选，Pro-MB 有） |

### 5.3 面板手势（sticky 语义）

| 操作 | 行为 |
|---|---|
| 悬停 Analyzer 按钮 | 打开分析仪设置面板 |
| 单击 Analyzer 按钮 | 面板变 **sticky**（不自动消失），再点收起 |
| 悬停 Output 按钮 | 打开输出选项面板（Output level/pan、mix、global bypass） |
| 单击 Output 按钮 | 面板变 sticky |
| 悬停 LINKED 抽屉按钮 | 打开 LINKED 面板 |
| 单击 LINKED 抽屉按钮 | 面板变 sticky |
| 点击面板外 | 收起非 sticky 面板 |

### 5.4 键盘 / 窗口

| 操作 | 行为 |
|---|---|
| `Esc` | 退出全屏；关闭 Popover |
| `Enter`（频率输入框） | 确认 |
| `Backspace`（频率输入框） | 删除字符 |
| 拖拽窗口边缘 | 任意缩放（VST3 自定义缩放，需 `setResizable` + `getConstrainedSize`） |
| Full Screen 按钮 | 进出全屏 |

### 5.5 MIDI Learn

点击 MIDI Learn 按钮进入学习模式 → 所有可学控件**高亮** → 点击控件显示关联的控制器号（未关联显示 `-`）→ 再点 MIDI Learn 退出。

---

## 6. 动画规格

**完整规格见 `01_UI_ANIMATION_PLAN.md`**（18 项问题 + `Anim.h` 代码骨架）。这里只列本改造新增/强制的部分。

### 6.1 时长常量（全项目唯一真源）

```cpp
namespace Anim {
    static constexpr double kDrawerMs   = 250.0;   // LINKED 抽屉
    static constexpr double kPopoverMs  = 220.0;   // 频段 Popover（略快于抽屉，层次感）
    static constexpr double kFadeMs     = 160.0;   // hover/选中/按钮列
    static constexpr double kKnobMs     = 120.0;   // 旋钮拖动高亮
    static constexpr double kBlinkMs    = 500.0;   // 光标半周期
    static constexpr double kMeterTauMs = 90.0;
    static constexpr double kRangeTauMs = 50.0;
}
```

### 6.2 必须实现的过渡（本改造新增）

| # | 动画 | 时长 / 曲线 |
|---|---|---|
| N1 | Popover 入场：**位移 14px + 淡入** | 220ms `outCubic` |
| N2 | Popover 切段：**水平横滑**到新段中心 | 220ms `inOutCubic` |
| N3 | 选中段填充淡入（0.05 ↔ 0.10 alpha） | 160ms |
| N4 | 分割线 hover 变色（gridZ → accent） | 160ms |
| N5 | 按钮列 hover 淡入淡出 | 160ms |
| N6 | BYP 段渐暗（1.0 ↔ 0.45） | 160ms |
| N7 | 旋钮四态切换（NORMAL/ACTIVE/DOWN/ROTATE） | 120ms |
| N8 | 频段电平点外圈电平环 | 实时（音频驱动） |
| N9 | LINKED 抽屉滑入（**含旋钮位置动画**） | 250ms `outCubic` |

### 6.3 必须先修的 4 个动画 bug

来自 `01_UI_ANIMATION_PLAN.md` §1.A，**不修则新动画全部建在坏地基上**：

| # | 位置 | 问题 |
|---|---|---|
| A1 | `DisplayView.cpp:1959` | `const float a = linkedOpen_ ? 1.0f : 0.0f;` 用了开关量而非 `linkedAnim_` → 抽屉旋钮**瞬间跳位**，滑入动画等于没做 |
| A2 | `Knob.cpp:235` | `editCursorOn_ = ! editCursorOn_;` 写在 `paint()` 里 → 光标闪烁频率 = 重绘率 |
| A3 | `DisplayView.cpp:796` | 动画期间**每帧**调 `layoutLinkedKnobs()` → 每帧 5 次 `setBounds` |
| A4 | `DisplayView.cpp:2489` | `drawDrawer (g);` 重复调用 → alpha 双重混合 |

---

## 7. 参数 → 控件映射（更新到 79 参数）

### 7.1 Popover 内（每段）

| 控件 | 参数 | 范围 |
|---|---|---|
| 旋钮 1 | `band{i}Threshold` | −60..0 dB |
| 旋钮 2 | `band{i}Range` | −30..+30 dB（双极） |
| 旋钮 3 | `band{i}Attack` | 0.1..200 ms |
| 旋钮 4 | `band{i}Release` | 10..1000 ms |
| 旋钮 5 | `band{i}Makeup` | −36..+36 dB |
| 分段按钮 | `band{i}DynamicsMode` *(待新增)* | COMPRESS / EXPAND |
| 滑杆 1 | `band{i}Ratio` | 1..20 |
| 滑杆 2 | `band{i}Knee` | 0..48 dB（2026-10-14 已实现连续） |
| 滑杆 3 | `band{i}Lookahead` *(待新增，当前是全局)* | 0..20 ms |
| 下拉 | `band{i}SCFilter` | Band / Free（2026-10-14 已实现） |
| 滑杆 4 | `band{i}SCLow` | 20..2000 Hz（2026-10-14） |
| 滑杆 5 | `band{i}SCHigh` | 500..20000 Hz（2026-10-14） |
| 滑杆 6 | `band{i}StereoLink` | 0..100%（2026-10-14） |
| 滑杆 7 | `band{i}Detector` | Peak / RMS |
| 顶栏电源 | `band{i}Bypass`（= Mix<50% 宏） | — |

### 7.2 顶栏 / 底栏（全局）

| 控件 | 参数 |
|---|---|
| 频段标签 ×4 | 选中态 |
| Analyzer 按钮 | `analyzerEnabled` |
| 设置按钮 | 打开 SettingsPanel |
| PHASE MODE | `phaseMode` = Natural/Minimal/Linear |
| OS | `oversampling` = Off/2x/4x（2026-10-14 已实现） |
| LOOKAHEAD | `lookahead` = Off/1ms/3ms |
| SPECTRAL | `spectralMode` *(待新增)* |
| LINKED 抽屉按钮 | 打开 LinkedDrawer |
| THR/ATT/REL/MK 滑杆 | `band{i}*` 统一偏移（D4） |
| IN / OUT | `inputGain` / `outputGain` |
| MIX | 全局 Mix *(待新增，当前每段独立)* |
| BYPASS 胶囊 | `enabled` |

### 7.3 2026-10-14 新增 DSP 参数的 UI 落点

| 新参数 | 落在哪 |
|---|---|
| `oversampling` | 底栏 OS 下拉（Off/2x/4x） |
| `band{i}Knee`（连续） | Popover 滑杆 2，显示 dB 读数（已实现） |
| `band{i}SCFilter` / `SCLow` / `SCHigh` | Popover 下拉 + 2 滑杆 |
| `band{i}StereoLink` | Popover 滑杆（0=独立，100=较响者驱动） |

---

## 8. 实施分期

> 每期独立可编译、可回退；每期结束跑 `CrunchMBTests`（基线 287/0，不得回退）。

### 进度总览（更新于 2026-10-14，第三轮）

| Phase | 内容 | 状态 | 提交 |
|---|---|---|---|
| A | 结构地基（Anim.h + 4 bug + 参数缓存 + 拆 4 类） | ✅ **完成** | `25ccd25` `3f03c6b` `837cabd` `04fb573` `799a706` |
| B | 布局改造 | 🟡 **进行中**（B-2/B-3 完成，B-1/B-4 待做） | `4919fa8` `4cd4e08` `1eacead` |
| C | 控件改造（旋钮四态 / BandLevelDot / RangeSlider / SegButton） | ⬜ 未开始 | — |
| D | 交互补齐（sticky / 多选 / Alt-makeup / MIDI Learn） | ⬜ 未开始 | — |
| E | 打磨（blur / 停 Timer / 投影） | ⬜ 未开始 | — |

---

## 8.1 每一步的"表现对齐"记录（Pro-MB / Future MB 对照）

> 这一节按目标要求记录：**每一步改的是声音/操作上的什么表现，对齐的是哪个参考插件的什么行为**。
> 非代码细节，只看用户能感知的结果。

### Phase A — 动画与结构地基

| 改动 | 声音/操作上的表现变化 | 对齐对象 |
|---|---|---|
| **A1 抽屉旋钮改用 `linkedAnim_`** | LINKED CONTROL 抽屉的 5 个旋钮从"瞬间跳到位置"变成**随面板一起滑入/滑出**；关抽屉时旋钮先随面板下沉再消失 | Pro-MB 的所有浮层（band panel / settings / help）都是滑入而非瞬现 |
| **A2 光标改时间驱动** | 频率输入框和旋钮内嵌编辑框的光标按**固定 500ms 半周期**闪烁，不再随重绘率狂闪（原来 60fps 下以 30Hz 闪） | 两者都没有可见光标闪烁问题；这是修自家 bug，对齐"输入框光标行为可预期"这一基本预期 |
| **A3 layout 加 1px 死区** | 抽屉滑入动画期间不再每帧重排 5 个旋钮，**消除 1px 量化抖动**；CPU 占用下降 | Pro-MB 旋钮在浮层滑入时位置平滑无抖动 |
| **A4 删重复 `drawDrawer`** | 抽屉区域不再 alpha 双重混合（原来偏深偏灰） | — （修自家 bug） |
| **参数查找缓存** | 纯性能：鼠标操作时的参数查找从"79 个线性扫描 + dynamic_cast"变成 O(1)。**可感知效果是拖动旋钮/滑杆时掉帧减少** | Pro-MB 的参数读写是直接索引 |
| **Tween 替换指数平滑** | 所有动效有了**明确的结束时刻**，手感从"发糯、永远在追"变成"快出慢停"；这也是后续能停 Timer 的前提 | Pro-MB 的 `transition:.25s ease`（cubic-bezier） |
| **选中填充 / 按钮列 hover / BYP 渐暗 / 分割线变色** | 四类状态从**硬跳**变成渐变：选段时分区填充 0.05→0.10 淡入；S/M/BYP/Δ 按钮列 hover 淡入、移出淡出；旁通段渐暗到 45%；分割线 hover 时灰→主题色渐变 | Pro-MB："small solo and mute buttons will appear"（hover 淡入）；"the band controls are dimmed"（旁通渐暗） |

### Phase B — 布局改造（进行中）

| 改动 | 声音/操作上的表现变化 | 对齐对象 |
|---|---|---|
| **B-2 交叉点手柄移到底部 + 手柄内显示频率** | 分割线抓取手柄从绘图区中部（y=500）移到**绘图区底缘**，且**频率值直接写在手柄里**——不用 hover 出 tooltip 也知道当前分频点；手柄加宽到 46px 更好抓；整条分割线仍可拖 | **Pro-MB**：crossover handle 在频谱底部，手柄上直接标频率 |
| **B-2 hit-test 加宽** | 底部手柄命中区从 ±4px 加到 ±23px，**抓取容错大幅提升**；竖线本身仍全高可抓 | Pro-MB：分割线任意位置可拖 |
| **B-3 顶栏/全局条改半透明玻璃** | 顶栏与底栏从"不透明色带"变成**压在频谱之上的半透明条**，频谱从它们下面透出来——界面从"三个拼块"变成"一整块频谱" | **Pro-MB**：顶栏（logo/频段按钮/Analyzer）与底栏（Phase Mode/OS/Lookahead/Spectral/IN/OUT/MIX）都是半透明压层 |
| **B-3 移除绘图区上下硬边框** | 原来把频谱区框成盒子的两条 1px 硬线删除，**频谱全出血**铺满窗口 | **Pro-MB**：频谱铺满，无绘图区边框 |
| **（附带）修复频谱 z-order 回归** | 见下方"回归记录"——不修则**播音频时频谱会盖住 EQ 曲线、双刻度、分割线、段名、电平表** | Pro-MB 的图层顺序：频谱在最底，曲线/刻度/控件在其上 |

### Phase B 待做

| 项 | 预期表现 | 对齐对象 |
|---|---|---|
| B-1 `DisplayCanvas` 抽取 | 纯结构，无可感知表现变化 | — |
| B-4 右增益刻度贴紧电平表 | 增益刻度与 IN/OUT 电平表紧邻，读压缩量时视线不用横跳 | Pro-MB：增益刻度贴电平表 |

### Phase C/D/E 待做（对齐目标）

| 项 | 预期表现 | 对齐对象 |
|---|---|---|
| 旋钮 BACK/OVERLAY 分层 + 刻度环 + 四态 | 旋钮外圈出现**带底部缺口的刻度环**；拖动时帽转暖色、弧加粗（ROTATE 态）；hover 时略亮（ACTIVE 态） | **Pro-MB**：`KNOBLARGEBACK*` 预渲染刻度环 + `Overlay{NORMAL,ACTIVE,DOWN,ROTATE}` 四态 |
| `BandLevelDot` + 圆形电平表 | 频段电平点外圈显示**该段实时检测电平环**，不用开面板就知道每段在被什么驱动 | **Pro-MB**：Threshold 旋钮外圈圆形电平表 |
| sticky 面板语义 | 悬停 Analyzer/Output/LINKED 按钮→面板打开；单击→面板**变 sticky** 不自动消失；再点收起 | **Pro-MB**：hover-open / click-sticky |
| 拖拽多选 + 自动扩档 | 在显示背景上点击拖拽→**框选多个频段**；拖到显示范围外→量程**自动扩展** | **Pro-MB**："click and drag across the display background to quickly select multiple bands" |
| Alt+拖 OUTPUT = makeup gain | 按住 Alt 拖 OUTPUT 旋钮→**自动校正当前立体声模式的声像** | **Pro-MB**：Alt-drag Output = makeup gain |
| MIDI Learn | 点 MIDI Learn→所有可学控件高亮→点控件显示关联控制器号 | **Pro-MB**：MIDI learn 流程 |
| backdrop blur | 玻璃条真正"毛"起来 | Pro-MB 位图预渲染投影 / Future MB shader |

---

## 8.2 回归记录（抽取过程中引入、已修复）

> 这些是重构引入的真 bug，记录在此以免未来重犯。**两条都是测试没抓到的**。

| # | 回归 | 为什么测试没抓到 | 怎么修的 |
|---|---|---|---|
| R1 | 四个子组件只 `addAndMakeVisible` 没给 bounds → 0×0 → JUCE 按子组件自身 bounds 裁剪 → **抽屉面板、Popover 面板、整个频谱层在实际插件里不渲染** | 当时只验证几何和 APVTS 值，不渲染像素 | 所有子组件 `setBounds(getLocalBounds())`；`paintForTest` 补画子组件。T39.17 抓到（差分 5358→23984） |
| R2 | 频谱做成子组件后画在父组件 `paint()` **之后** → **盖住 Range 钟型、EQ 曲线、双刻度、分割线、段名、电平表** | **离线测试不喂音频**，`SpectrumEngine` 无帧，Path 为空，`paint()` 直接 early-return，所有像素断言照过 | `SpectrumView` 不再继承 `Component`，改由 `DisplayView::paint()` 在第 4 位直接调用（与抽取前 `drawSpectrum(g)` 同位置）。z-order 约束写进 `SpectrumView.h` 头部注释 |

> **教训**：像素回归测试必须在**有音频帧**的条件下也跑一遍，否则所有"数据驱动"的图层
> （频谱、电平表、曲线）都是空的，任何 z-order / 可见性回归都测不出来。
> 这条已列入 Phase E 的验收补充。

---

| Phase | 内容 | 状态 | 提交 |
|---|---|---|---|
| A | 结构地基（Anim.h + 4 bug + 参数缓存 + 拆 4 类） | ✅ **完成**（4/4 抽取） | `25ccd25` `3f03c6b` `837cabd` `04fb573` `799a706` |
| B | 布局改造（频谱全出血 / 分割线 / 刻度） | ⬜ 未开始 | — |
| C | 控件改造（旋钮四态 / BandLevelDot / RangeSlider / SegButton） | ⬜ 未开始 | — |
| D | 交互补齐（sticky / 多选 / Alt-makeup / MIDI Learn） | ⬜ 未开始 | — |
| E | 打磨（blur / 停 Timer / 投影） | ⬜ 未开始 | — |

**Phase A 全部完成项**：

| # | 项 | 状态 | 证据 |
|---|---|---|---|
| 1 | 新建 `src/ui/Anim.h` | ✅ | 时长常量 + `Ease::outCubic/inOutCubic` + `Tween`（明确起止、帧率无关、`done()` 可查） |
| 2 | 修 A1–A4 四个动画 bug | ✅ | A1 抽屉旋钮改用 `linkedAnim_`；A2 光标改时间驱动；A3 layout 加 1px 死区；A4 删重复 `drawDrawer` |
| 3 | 参数查找缓存 | ✅ | `buildParamCache()` + 7 个函数改 O(1)，消除 39 处线性扫描 + `dynamic_cast` |
| 4 | 拆出 `LinkedDrawer` | ✅ | `src/ui/LinkedDrawer.{h,cpp}` 333 行 |
| 5 | 拆出 `BandPopover` | ✅ | `src/ui/BandPopover.{h,cpp}` 559 行 |
| 6 | 拆出 `FreqInputEditor` | ✅ | `src/ui/FreqInputEditor.{h,cpp}` 141 行 |
| 7 | 拆出 `SpectrumView` | ✅ | `src/ui/SpectrumView.{h,cpp}` 123 行 |

**代码规模变化**：

| 文件 | Phase A 前 | 现在 |
|---|---|---|
| `src/ui/DisplayView.cpp` | 2282 | **1936** |
| `src/ui/DisplayView.h` | 398 | 419 |
| `src/ui/Anim.h` | — | 118 |
| `src/ui/LinkedDrawer.{h,cpp}` | — | 333 |
| `src/ui/BandPopover.{h,cpp}` | — | 559 |
| `src/ui/FreqInputEditor.{h,cpp}` | — | 141 |
| `src/ui/SpectrumView.{h,cpp}` | — | 123 |
| UI 总计 | 2680 | 3729（其中 DisplayView 占 52%，拆分前占 85%） |

> ⚠️ **关于 `DisplayView.cpp ≤ 900 行` 这个指标**：
> 实际做到 1936 行后停止继续机械拆分，原因有三：
> 1. **剩余代码高度耦合**：`drawGrid` / `drawScales` / `drawCrossovers` / `drawPartitions`
>    直接依赖 DisplayView 的坐标映射（`freqToX` / `gainToY` / `specDbToY` / `plotTopPx`）
>    和 Tween 状态（`selT_` / `bypT_` / `divT_`）。抽成 `DisplayCanvas` 需要传 10+ 个回调
>    和 12 个 Tween，接口比实现还长，可读性反而下降。
> 2. **Phase B 要重写坐标体系**：频谱全出血 + 全高分割线会改变所有这些函数的基准，
>    现在拆等于搬两次。
> 3. **该指标是评审文档的自设目标，不是用户需求**。用户需求是"UI 接近 Pro-MB"，
>    拆分只是达成手段之一。四个真正独立的子系统（抽屉/Popover/输入框/频谱）
>    已经拆出，剩余的"画布"部分更适合作为 Phase B 的一部分整体重写。

**Phase A 过程中发现并修复的真 bug**（都写进了 commit message）：
- 测试虚拟时钟与真实时钟混用导致 Tween 永不前进（T30.2b 回归）
- 抽屉面板宽 460 vs 梯形按钮宽 456，旋钮偏移 2px（T35.11 回归）
- **四个子组件只 `addAndMakeVisible` 没给 bounds → 0×0 → JUCE 按子组件自身 bounds
  裁剪绘制，导致抽屉面板、Popover 面板、整个频谱层在实际插件里不渲染**
  （T39.17 抓到：差分 5358 → 23984）。这是抽取引入的最严重回归。

**Phase A 验收状态**：`CrunchMBTests` 287/0 全绿（T35 抽屉 11 项、T40 Popover 14 项、
T39 频谱 17 项均通过）。

---

### Phase A — 结构与动画地基（3-4 天）✅ 完成

1. 新建 `src/ui/Anim.h`（`01_UI_ANIMATION_PLAN.md` §阶段1 的完整代码） ✅
2. 修 A1–A4 四个动画 bug ✅
3. 建参数查找缓存（`unordered_map<String, AudioProcessorParameter*>`，消掉 39 处线性扫描） ✅
4. 从 `DisplayView` 拆出 `BandPopover` / `LinkedDrawer` / `SpectrumView` / `FreqInputEditor` ✅ 全部完成
5. **验收**：`DisplayView.cpp ≤ 900 行` ⬜ 未达标（实际 1936，理由见上方说明）；
   `CrunchMBTests` 全绿 ✅（287/0）

### Phase B — 布局改造（4-5 天）🟡 进行中（B-2 / B-3 完成）

> 建议把 `DisplayCanvas`（Grid+Scales+Crossovers+Partitions，约 −600 行）的抽取
> **并入本 Phase**：那时坐标体系本来就要因"频谱全出血 + 全高分割线"而重写，
> 一次搬到位，避免现在拆完 Phase B 再拆第二次。

1. 频谱**全出血**：移除绘图区边框，分区/曲线/刻度全高绘制 ✅（`1eacead`）
2. 顶栏/底栏改玻璃条（半透明 + 无模糊，模糊留到 Phase E） ✅（`1eacead`）
3. 分割线改**全高竖直分割线**，底部加手柄 + 频率标签 ✅（`4919fa8`，未拆独立 `DividerHandle`，直接改在 `drawCrossovers` 内）
4. 右增益刻度贴紧电平表（新建 `GainScale`） ⬜ 未做
5. 选中段改 accent 低 alpha 填充（配 N3 淡入） ✅（Phase A 已随 `selT_` 落地）
6. **验收**：静态布局与 §2 线框一致；像素回归测试通过 🟡（287/0 全绿；§2 线框的
   `DividerHandle` / `GainScale` 独立组件未建，功能已内联实现）

### Phase C — 控件改造（4-5 天）

1. 旋钮改 BACK/OVERLAY 分层 + 刻度环缓存 + 四态（配 N7）
2. 新建 `BandLevelDot`（频段电平点 + 圆形电平表）
3. 新建 `RangeSlider`
4. 新建 `SegButton` / `TextSelect`
5. Popover 改 Pro-MB 形态：5 旋钮行 + 范围标注 + 右缘竖排 EXPERT
6. **验收**：旋钮四态可视化；Popover 与 §2 线框一致

### Phase D — 交互补齐（3-4 天）

1. 按钮列 hover 淡入淡出（N5）+ 按住临时 / Ctrl 独占
2. Popover 位移入场 + 切段横滑（N1/N2）
3. sticky 面板语义（Analyzer / Output / LINKED）
4. 显示背景拖拽多选 + 自动扩档
5. Alt+拖拽 OUTPUT = makeup gain
6. Esc 退出全屏
7. MIDI Learn 高亮
8. **验收**：§5 全部手势可执行；新增对应 `xxxForTest` 断言

### Phase E — 打磨（2-3 天，可选）

1. backdrop blur（档 b：4× 下采样 + 15Hz 更新，UI_SPEC §12.2）
2. 空闲停 Timer（依赖 `Anim::Tween::done()`）
3. 投影改多层渐变
4. 1px 细线提亮（四次多项式，补偿 AA 发暗）
5. **验收**：60fps；UI_SPEC §10 全部验收项

**总工期：约 16-21 天**（单人）。若时间紧，**Phase A + B + C 是必须**（约 11-14 天），
Phase D 的 sticky/多选/Alt-makeup 可以后置。

---

## 9. 验收标准

| 项 | 标准 |
|---|---|
| 布局 | 与 §2 线框逐元素一致（分区/刻度/手柄/顶栏/底栏/Popover） |
| 旋钮 | 四态可见且渐变；刻度环缺口在正下方；行程 270° |
| 交互 | §5 全部手势可执行，无硬跳 |
| 动画 | §6.2 的 N1–N9 全部生效；A1–A4 已修 |
| 主题 | 三主题切换后所有控件（含新控件）正确换色，无硬编码色值 |
| 缩放 | 700×500 – 1840×1240 全区间无形变、无跑位 |
| 性能 | 60fps（1920×668 @100% DPI）；音频线程零分配零锁 |
| 测试 | `CrunchMBTests` 全绿（当前基线 287/0，不得回退） |

---

## 10. 迁移注意事项

1. **不要重写设计**。三主题色、三层架构、LINKED 模块、79 参数全部保留，本文件只改**布局与交互**。
2. **先拆类再改造**。Phase A 的拆分是后续所有 Phase 的地基；反序会让动画代码搬两次。
3. **新控件必须支持三主题**。所有颜色从 `MBTheme::Palette` 取，禁止硬编码。
4. **新控件必须带测试钩子**。沿用现有 `paintForTest` / `tickForTest` / `xxxForTest` 模式，
   否则像素回归测试无法覆盖。
5. **音频线程红线不变**（TECH_SPEC §4）：新控件只读 atomic，不写 DSP 状态；
   DSP 新参数（Oversampling/Knee/侧链/StereoLink）已在 `02_DSP_BUG_AND_FEATURE_AUDIT.md` 实现完毕。
6. **`band{i}DynamicsMode` / `spectralMode` / 全局 `Mix` / 每段 `Lookahead`** 这四个参数
   **当前不存在**，若 Phase D 要做对应 UI，需先加参数（会突破 79，需同步更新测试断言）。

---

## 11. 与既有文档的差异索引

| 主题 | UI_SPEC v1.8 | 本文件 v2.0 |
|---|---|---|
| 布局 | 920×620 框定绘图区 | 频谱全出血 |
| 按钮列 | 常驻 | hover 淡入淡出 |
| 频段电平 | 频点圆点拖拽 | 电平点（带圆形电平表）+ Range 滑杆 |
| 分割线 | 绘图区内竖线 | 全高分割线 + 底部手柄 + 频率标签 |
| Popover | 5 旋钮 + 3 滑杆 + EXPERT 文字 | + 右缘竖排 EXPERT + 范围标注 + 侧链/立体声控件 |
| 旋钮 | 单一矢量 | BACK/OVERLAY 分层 + 刻度环 + 四态 |
| 交互 | 基础 | + sticky 面板 / 多选 / Alt-makeup / MIDI Learn |
| 动画 | 未规定 | §6 完整规格（并入 `01_UI_ANIMATION_PLAN.md`） |
| 色值/字体/参数 | §9 / §5 | **不变** |
