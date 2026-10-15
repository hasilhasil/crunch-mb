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
| MIX | 全局 Mix ✅（`b0aa94e`，0-200%） |
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

### 进度总览（更新于 2026-10-14，第四十九轮 · R18 修复：旋钮常态键盘）

| Phase | 内容 | 状态 | 提交 |
|---|---|---|---|
| A | 结构地基（Anim.h + 4 bug + 参数缓存 + 拆 4 类） | ✅ **完成** | `25ccd25` `3f03c6b` `837cabd` `04fb573` `799a706` |
| B | 布局改造 | ✅ **完成** | `4919fa8` `4cd4e08` `1eacead` `07915d6` |
| C | 控件改造 | ✅ **完成**（C-1..C-6 全部） | `ab2812b` `aa5595c` `d5b1094` `3b52675` `363d576` `2539097` |
| D | 交互补齐 | ✅ **完成**（D-1..D-7 全部） | `e2e8784` `8c9bb40` `b2d86fc` `1d6dff8` `6f3cf53` `9ca898d` |
| E | 打磨 | ✅ **完成**（E-1/E-2/E-3 全部） | `85a366b` `41e3bd3` `9a9dfa8` |

---

> **参数总数变更记录**：62 → 79（第一轮：oversampling + 每段侧链滤波×3 + StereoLink）→ **83**（第九轮：每段 +Dynamics）→ **84**（第十三轮：+globalMix）。T0.1 / T0b.1 的断言已同步更新。

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

### Phase B 已完成（第四轮补录）

| 改动 | 声音/操作上的表现变化 | 对齐对象 |
|---|---|---|
| **B-4 右增益刻度贴紧电平表**（`07915d6`） | 增益刻度右缘由 874 移到 882，与 IN/OUT 电平表左界（886）的间隙从 **12px 收到 4px**。读压缩量时视线不用在刻度与电平表之间横跳 | **Pro-MB**：增益刻度与电平表紧邻 |
| **B-4 附带修了一个无效断言** | T31.3 原来断言 `(824+52) <= 886`，而注释说"右轴框 = x 824..876"——**注释和算术都是错的**（真实框是 818..874），且这个断言**永远不可能失败**（同义反复）。现在从同一对常量推导间隙并断言 `0 < gap <= 6` | — （测试质量） |

### Phase C — 控件改造 ✅ 完成（C-1..C-6）

| 改动 | 声音/操作上的表现变化 | 对齐对象 |
|---|---|---|
| **C-1 旋钮刻度环 + BACK 层缓存**（`ab2812b`） | 旋钮外圈出现 **11 根短刻度组成的环**，底部留缺口（与 270° 行程一致）；圆体变成上亮下暗的径向渐变。视觉上从"一根弧 + 一个平板帽"变成有刻度参照的仪器旋钮 | **Pro-MB**：`KNOBLARGEBACK*` / `KNOBMEDIUMBACK*` / `KNOBSMALLBACK*` 预渲染刻度环位图。Pro-MB 把旋钮拆成 BACK（刻度环+圆体，静态）与 OVERLAY（值弧+帽+指针，随状态切换）两份位图 |
| **C-1 附带性能收益** | 刻度环与圆体渐变改为缓存 Image，只在直径/主题变化时重建。14 个旋钮（Popover 5 + 抽屉 5 + 其他）每帧省掉 11 根线段 + 1 个渐变椭圆 | Pro-MB 用位图正是为了这个 |
| **C-2 旋钮四态**（`aa5595c`） | 悬停→帽略亮；按下→帽再亮一档；**拖动→帽转暖色 + 弧加粗到 1.35×**。全部 120ms `outCubic` 渐变，不再是二态硬跳 | **Pro-MB**：`Overlay{NORMAL,ACTIVE,DOWN,ROTATE}` 四套 OVERLAY 位图，其中 `OverlayRotate` 就是拖动时的暖色高亮 |
| **C-2 架构选择** | 旋钮**不各自持有 Timer**，由宿主 60fps 定时器统一 `advanceStates()`。14 个旋钮若各起一个 Timer 是纯粹浪费 | — （工程约束） |

### Phase C 已完成（第五轮补录）

| 改动 | 声音/操作上的表现变化 | 对齐对象 |
|---|---|---|
| **C-3 频段圆形电平表**（`d5b1094`） | 频段电平点外圈出现**该段实时检测电平环**（accent 弧，−60..0dB 映射到 270° 行程，末端带峰值保持小点）。**不用开 Popover 就知道每段在被什么电平驱动**——这是功能性提升，不只是好看。弹道与 IN/OUT 电平表一致：上升瞬时跟上、下落 60dB/s、峰值保持 2s | **Pro-MB**：Threshold 旋钮外圈那圈环形电平表 |
| **C-3 附带修了一个真 bug** | `bandLevelDb_` 用 `{}` 初始化成 0dB，被弹道逻辑当成"当前电平 0dBFS"→ **插件刚打开时四个环显示满刻度再慢慢衰减**。改成初始化为静音底 −100dB。这个 bug 静态读代码看不出来，是新增的带音频测试第一条就抓到的 | — |
| **C-4 Range 滑杆可见抓手**（`3b52675`） | Range 钟型下缘**本来就能拖，但没有任何可见抓手**，用户根本不知道那里可交互。现在选中/hover 段时，下缘段中心处出现一个胶囊手柄（带两道暗示可拖的小刻痕），拖动时高亮。命中区在段中心从 ±6px 加到 ±10px，让抓手真的好抓 | **Pro-MB**：每段一个可直接抓取的范围滑杆 |
| **C-6 DynamicsMode Compress/Expand + 分段按钮**（`2539097`） | **全部 C/D 项里唯一需要动 DSP 的**。每段新增 COMPRESS/EXPAND 分段按钮：Compress = 朝阈值压（经典）；**Expand = 增益曲线取负，阈值以上抬升、扩大动态范围**——用于修复被过度压缩的音轨，或做门限扩展式清晰化。阈值/比率/膝宽/Range 语义全部不变，只翻方向；Range 钳制对 Expand 同样生效（实测 Range −6 → Expand 上限 +6 dB）。UI 是 Pro-MB 式 segmented：两段等宽，选中段实心 accent + 白字，未选中透明 + dim 字，带容器外框 | **Pro-MB**：每段 COMPRESS/EXPAND 分段按钮。官方帮助 "Expand mode increases the dynamic range" |

### Phase C 待做（全部完成）

| 项 | 预期表现 | 对齐对象 |
|---|---|---|
| ~~`SegButton`（COMPRESS↔EXPAND）~~ | ✅ 已做（`2539097`，含 DSP 扩张） | Pro-MB segmented |
| ~~`TextSelect` 文本选择器~~ | ✅ 已做（`c4e03d3`，Display Range 档位改用自绘下拉：同色圆角面板、当前项打勾、hover 高亮、三层投影） | Pro-MB drop-down |

### Phase D — 交互补齐 ✅ 完成（D-1..D-7）

| 改动 | 声音/操作上的表现变化 | 对齐对象 |
|---|---|---|
| **D-1 Analyzer 自绘面板 + sticky 语义**（`e2e8784`） | **这是 Pro-MB 最具标志性的交互，此前完全缺失**。原来是"点击弹操作系统原生菜单"——没有悬停语义、没有 sticky、外观也不是插件风格。现在：**悬停** Analyzer 按钮 → 面板打开（非 sticky）；鼠标移开 → 自动收起；**单击** → 面板变 sticky 钉住不动；再点 → 收起并解除；点面板外 → 收起。面板是自绘深色圆角块，每行带实心/空心圆点表示选中 | **Pro-MB**：悬停打开 / 点击 sticky / 再点收起。官方帮助原话 "hover over a button to open the panel; click to make it sticky" |
| **D-1 附带：面板行变成真控件** | Pre/Post 频谱、Resolution（1024/2048/4096/8192）、Speed（10/20/60ms）、Tilt（0/1.5/3/4.5 dB/oct）、Freeze 全部可点，且设置真源移到面板（`onSettingsChanged` 回写），不再只是弹出菜单里的条目 | **Pro-MB**：Analyzer 面板每一行都可点 |
| **D-2 LINKED 抽屉 sticky 语义**（`8c9bb40`） | 同一套状态机推广到第二个按钮：悬停 LINKED CONTROL 梯形按钮 → 抽屉滑入（非 sticky）；鼠标移开 → 自动收起；**单击** → 抽屉变 sticky 钉住，可以松开鼠标去调旋钮；再点或点外面 → 收起。**实际价值：调 5 个联动握柄时不用一直按着** | **Pro-MB**：所有底栏按钮都是 hover-open / click-sticky 这套语义 |
| **D-3 显示背景拖拽扫选**（`8c9bb40`） | 在频谱背景上按下并拖动 → **选中段连续跟随光标**，一扫而过就能连续切换/预览四个频段，不用逐次点击。纯速度提升：原来跨段调整要"点一下→等 Popover→调→再点" | **Pro-MB**："click and drag across the display background to quickly select multiple bands" |
| **D-3 语义说明** | 本插件是**单选中**模型（`selectedBand_` 是单个 int，没有多选集合），所以**没有**做"快照 + toggle"式的真多选——那套逻辑在单选变量上不自洽。交付的是该手势的速度价值 | Pro-MB 支持真多选；这里是能力差异，已在代码注释中标明 |
| **D-4 OUT 按钮 + Output 面板**（`b2d86fc`） | 底栏 OUT 从**纯文本读数**升级为可点胶囊按钮。悬停打开输出选项面板（非 sticky）、点击变 sticky、再点或点外面收起——与 Analyzer / LINKED 完全同一套状态机。面板三行：Output Gain（写全局参数）、Mix、Global Bypass（与既有四段 Mix=0 宏联动） | **Pro-MB**：底栏 Output 按钮打开输出选项面板，hover-open / click-sticky |
| **D-5 拖动超范围自动扩档**（`1d6dff8`） | Auto 模式下把 Makeup/Range 拖到曲线**超出当前显示量程**时，量程自动升档（实测 6→12 dB），曲线不会跑出屏幕。**修掉了一个真实手感问题**：原来拖动期间量程被完全冻结，大幅上拖时曲线直接飞出屏幕顶部，用户感觉"拖不动了" | **Pro-MB**：Auto-Adjust Display Range |
| **D-6 Alt+拖 OUT = Output Gain**（`6f3cf53`） | 按住 Alt 拖底栏 OUT 按钮 → **直接调 Output Gain**，不用打开面板。220px 走完 ±36 dB（与旋钮同一速度比例），Shift 微调；拖动时按钮本身显示实时增益值并变实心 accent，明确提示处于另一模式。**这是 Pro-MB 最高频的快捷键之一** | **Pro-MB**：Alt-drag Output = makeup gain |
| **D-7 MIDI Learn 模式**（`9ca898d`） | 顶栏新增 MIDI 按钮，点击进入学习模式 → **所有可学控件画呼吸高亮框**、已关联的画 "CC n" 徽标 → 点控件记录关联、再点清除 → 再点 MIDI 退出。呼吸 alpha 由时间驱动（不用重绘次数）。**注意**：本插件不接收 MIDI，所以交付的是**可视化学习流程**（能记录/显示映射），不含 MIDI 消息路由 | **Pro-MB**：MIDI learn 流程 |
| **E-2 空闲停 Timer**（`85a366b`） | 编辑器打开时原本无条件 60fps 重绘。现在无动效、无新频谱帧、无拖拽时**自动停表**，任何交互立即唤醒。音频不受影响（在音频线程），纯粹省 UI 的 CPU/电量 | — （工程优化） |
| **E-3 多层渐变投影**（`41e3bd3`） | 弹层（Popover / Analyzer / Output）的投影从"一层硬边黑影 + 下移 3px"改成**三层由深到浅、由近到远**（alpha .30/.20/.14，偏移 2/6/14px，圆角随扩散增长）。观感从"贴在界面上"变成"浮在界面上" | **Pro-MB**：投影约 `0 14px 44px rgba(0,0,0,.5)` |
| **E-1 backdrop blur**（`9a9dfa8`） | 顶栏/底栏从"透明塑料"变成"磨砂玻璃"：背后的频谱/网格先被下采样 + 两遍可分离盒模糊糊化，再上半透明色。模糊源只含背景层（背景/网格/分区/频谱），不含玻璃条自身与上层控件，避免自我反馈。15Hz 刷新——模糊是低频信息，人眼对滞后不敏感，CPU 省 4× | **Pro-MB**：位图预渲染投影 / **Future MB**：shader 模糊 |
| **侧链滤波 UI 暴露**（`6e45685`） | EXPERT 面板从 2 行扩到 5 行：新增 **Filtering（Band↔Free）、SC High-Pass（20/100/500/2000 Hz）、SC Low-Pass（500/2000/5000/20000 Hz）**，点击循环档位。**DSP 侧链滤波能力从第一轮起就存在，但一直没有 UI 触达**——这次把它接上：Free 模式下可以让该段触发于完全不同的频率范围 | **Pro-MB**：Side Chain Filtering + Free 模式的 LP/HP 频率。官方帮助 "let a band trigger on a completely different frequency range" |
| **全局 Mix 0-200% 接通 DSP**（`b0aa94e`） | 底栏 OUT 面板的 Mix 行从"UI-only + TODO"变成真功能：**100% = 纯处理声；<100% = 与干声混合（减弱效果）；>100% = 并行压缩提响**。DSP 侧在输入增益后、分频前存一份干声参考，保证 Mix=0 时输出与输入（含输入增益）逐样本一致。`globalMixLin_==1.0` 时整块短路，默认路径零开销 | **Pro-MB**：全局 Mix，官方帮助 "Mix values above 100% can be used for parallel compression" |
| **自绘 TextSelect 下拉**（`c4e03d3`） | 顶栏 Display Range 档位选择器原来是 `juce::PopupMenu`——**操作系统外观**（系统色、系统字体、系统圆角），是整个界面里唯一破坏主题一致性的地方。现在换成自绘：同色圆角面板、当前项打勾 + accent 底、hover 高亮、与其他弹层同一套三层投影。点击项即应用并收起；点外面收起且不改已选；打开时再点按钮收起 | **Pro-MB**：drop-down 是自绘的，与插件同色 |
| **E-4 1px 细线像素对齐**（`18f58f5`） | 所有 1px 分隔线/网格线/刻度线原来直接 `fillRect` 画在任意小数位置，抗鸾齿把覆盖摊到两个边缘，实际亮度只有设定值约一半——细线看起来发灰发虚。现在细线中心对齐到 `floor(y)+0.5`（两边缘对称分摊覆盖），并把 alpha 提亮 2×（钳到 1）补偿中间只剩 ~50% 覆盖。**观感：网格和分隔线从"灰雾"变成"清准细线"** | — （细节抛光，对齐 Pro-MB 的清准分隔线观感） |
| **IMBBackend facade 补齐**（`1d852e6`） | `TECH_SPEC.md` §8 写着"Crunch MB 原生实现 `IMBBackend` facade，从第一个 commit 起 UI 与外部调用方统一经 facade 访问后端"，但代码里一个字节都没有——§10.4 的 9 条 API 验收项也全部空白。本轮按 `crunch-api-interface.md` v1.0 §2/§3 完整落地：`IPluginBackend` 12 个通用方法（参数面/状态面/实时数据面/分析面/插件面）+ `IMBBackend` 5 个多段扩展。**操作上的变化：现在可以用一套统一 API 读写全部 84 个参数、拉取 metering/历史、拿每段传输曲线与分频响应、做统一控制偏移与交叉点钳制、序列化往返**。为支撑它新增了 `CrossoverBank::getMagnitudeDb`（三相位模式各自的响应算法）、`MultibandDSP` 的三个访问器、以及音频线程供电平历史环形缓冲 | — （规范要求，非 Pro-MB 对标项） |
| **UI 全面改走 facade**（`fafa3da`） | `PLAN.md` Phase 6 有两半：① 实现 facade（上轮）② **UI 改走 facade，不直触 APVTS/DSP 内部**（本轮）。此前 DisplayView / BandPopover / LinkedDrawer **各持一份**自己的 id→参数缓存，绕过 facade 的钳制与值型拒绝——正是 API 规范 §6.2 列出的 D 类耦合。现在 `src/ui` 内 APVTS 引用数为 **0**，26 处读写全走 `proc_.getBackend()`。为不拖慢 UI 热路径，facade 加了 `getParamFast()`（一次性建哈希表，O(1) 查表；否则 `apvts.getParameter` 是 84 个参数的线性字符串比较，60fps 下每秒上千次扫描）。**操作上无可感知变化——这是纯架构解耦，用户看到的界面行为完全一致**（425 条既有断言零回归即证明） | — （PLAN Phase 6 要求，非 Pro-MB 对标项） |
| **全局栏四个选择器接通**（`f1a9bec`） | `RESEARCH_UI.md` §五 P1 一直挂着的一项：全局栏的 Phase / Slope / Quality / Lookahead **画着 ▼ 下拉箭头却是点不动的静态文字**，而且值写死在代码里（"Natural Phase" / "24 dB/oct" / "Medium" / "Off"）——宿主自动化改了参数，这里显示的还是旧字符串。现在每项都可点，弹出与 Display Range 同一套的自绘下拉（同色圆角面板、当前项打勾 + accent 底、hover 高亮、三层投影），且**当前值从参数实时读取**。选完自动收起；再点同一项收起；点外面收起且不改值 | **Pro-MB**：全局栏的 Phase / Slope / Quality / Lookahead 全是可点下拉 |
| **SPI 曲线层平滑**（`aa78e24`） | Pro-MB "Smart Parameter Interpolation"。原来曲线每帧取参数瞬时值 → 宿主自动化或切 preset 时**逐帧跳变**，看起来一格一格蹦。现在显示层加 τ=60ms 时间常数平滑（与量程切换同一手法）：实测改 +12dB Makeup 后，一帧只走 24% 路程，之后连续滑到新位置。**首帧用哨兵值直接吸附**，避免插件刚打开时曲线从 0dB 爬升；收敛后精确吸附，不会无限微动。**声音完全不变**——只平滑显示层，音频路径与 APVTS 都没碰（T54.5 断言参数未被污染） | **Pro-MB**：Smart Parameter Interpolation |
| **复审修 3 处 BYP 阈值 bug**（`c9f79d7`） | 文档需求全部交付后做代码复审，发现 facade 迁移时引入的**同一错误三处复发**：`band{i}Mix` 是 0-100 物理百分比，`getParamFast()` 返回实际值，但三处比较仍写 `0.5f`（应按 `50.0f`）。**操作上的后果**：① BYP 打开时曲线仍显示压缩后的 live 曲线（BYP 在曲线层完全不可见）；② Range 钟型不切描边；③ **全局 BYPASS 胶囊永远不亮**。同时补完 SPI 漏掉的 BYP 路径——`bandDisplayDb_` 原来在 raw/live 间瞬时切换，该段被大幅压缩时切 BYP 会瞬间跳 20dB | — （代码质量修复，非 Pro-MB 对标项；但"BYP 时曲线归原"是 Pro-MB 的既有行为） |
| **复审再修 4 处 Choice 索引放大**（`d3c6563`） | R7 的姊妹错：迁移前用 `* (N-1)` 把 normalized 转索引，迁移后 `getParamFast` 已返回索引，乘法却留着。**操作上的后果**：① EXPERT 面板点 Lookahead，从 1ms **跳过 3ms 直接回 Off**；② Lookahead 显示值错；③ `xoverSlope`/`phaseMode` 喂给结构缓存比对 → **缓存永久失配，每帧都判定"结构变了"**（持续无谓重建分频权重）；④ 全局栏选 1ms 却高亮 3ms。真正的原因是一句**错误注释**——`getParamNorm` 上写着"Bool/Choice 实际值与 normalized 同为 0/1"，这句话直接引导人写出这个 bug | — （代码质量修复；但"Lookahead 循环不跳档"是 Pro-MB 的既有交互） |
| **Linear Phase FIFO 改固定容量 Ring**（`f8a61ca`） | `TECH_SPEC` §4 声明"音频线程零分配"，`FirFilter::processOneChannel` 标着 `noexcept` 跑在音频线程，却用 `std::vector` 的 `insert/erase/push_back` 做 FIFO。`prepare()` 里 reserve 了 `blockSize_+maxBlockSize+8`，稳态分析看着够——但**稳态峰值恰好贴在上界**（输出侧每轮 push `blockSize_`、只 erase `numSamples`），边缘情况下仍会重新分配。改为 `Ring<float>`（容量一次分配，热路径只做索引运算）。**音质上无可感知变化**——Linear 模式上报延迟跨 64/256/512 三种块大小恒为 3072 样本（T56.7 实测），与改前一致。顺带修正了类注释里一句**不实声明**（原写"prepare() 预分配 FIFO 容量，热路径 insert/erase 零分配"——只在无边缘情况时成立） | — （工程修复；但"恒定群延迟"是 Pro-MB Linear Phase 的既有承诺） |
| **频谱衰减帧线程竞争修复**（`7e5ab0c`） | 停止播放时频谱走"保持 600ms → 衰减到静音底"路径。原来衰减帧**原地修改已发布的 slot**，而 UI 线程正在读同一个 slot——三缓冲的前提是"写侧永不碰已发布 slot"，这里破了：UI 可能读到**撕裂帧**，且版本号在写完后才递增，UI 可能在写中途判定"无更新"而**丢帧**。现改为与 `analyseTick` 同款（写未发布 slot → 发布 → 轮转）。**听感/观感上无可感知变化**——衰减行为完全保留（T57.3 仍衰减、T57.5 仍收敛到 −100dB），只是不再有偶发的画面撕裂与丢帧 | — （工程修复；"无信号时频谱保持再衰减"是 Pro-MB 的既有行为） |
| **立体声链接镜像修复**（`91bfc34`） | Stereo Link = 100% 时，两声本应被较响者驱动而**严格同步**。但原来 attack/release 系数的选择比较的是**各自独立的平滑状态**——一旦左右历史不同（link 刚从 0 调到 100%、或参数刚变更），就会"左走 attack、右走 release"，同一时刻两条包络用不同时间常数。**听感上**：立体声宽度会轻微抖动/漂移，正是链接功能要消除的东西。下一层的两个 RMS 包络也各自独立，会在到达系数选择前就先分叉。现改为 link ≥ 99.9% 时共用系数与状态、RMS 由较响者驱动；link 更低时保留独立行为 | **Pro-MB**：Stereo Link 满值时应完全锁定立体声镜像 |
| **attack/release 系数缓存**（`9690556`） | `updateScalars()` 每块对 4 段各调一次 `setParameters()`，而它无条件重算 3 个 `exp`——48kHz/64 样本下是**每秒 9000 次 exp，全在音频线程**；且 `rmsCoeff_` 只依赖常量与采样率，不变时每次都是同一个数。改为按值缓存（仅 attack/release 的 ms 变化时重算），`rmsCoeff_` 每次 `prepare()` 算一次。**音质上完全无可感知变化**——这是纯浪费消除，不是缺陷修复；T59 用双实例对照验证输出逐样本一致，并确认改时间常数后包络确实跟着变（缓存没把旧值焊死） | — （工程优化） |
| **侧链 Free 模式 low>high 修复**（`68fa8b1`） | Free 模式下可让某段触发于完全不同的频率范围（HP+LP 包住通带）。但参数范围允许 low 20-2000 / high 500-20000，用户完全可以设出 low=2000/high=500——两个阻带相交、通带几乎为空，**检测器看到近静音，该段永远不触发**，表现为"压缩器坏了但没有任何提示"。现改为越界时以中点为心保留 ±1/4 通带。**修前修后对比（T60 实测）**：正常 Free 通带 −5.97dB，越界情况修前 ≈ 0dB（完全不触发）、修后 **−5.96dB**（几乎完全恢复） | **Pro-MB**：Free 模式的 Side Chain Filtering 保证 low ≤ high |
| **R13 修复：4x 过采样**（`50c2622`） | 本轮原本要审 `PluginProcessor` 的侧链总线与过采样边界。侧链路径干净；过采样查出**严重缺陷**：`oversampling` 参数 2026-10-14 新增后**没有任何测试打开过它**，开启 4x 处理哪怕一块音频即破坏堆（输出当时正常，进程最终 0xC0000374）。**2x 完全正常**。已尝试两种修法均回退（分块→更糟；提 maxBlock_→崩点前移），根因未定位。**对用户的影响：UI 上下拉选 "4x" 后播放会崩宿主/插件进程**。已在计划书标注"仅 2x 可用"，并在 `MultibandDSP.cpp` 留注释说明为何那个守卫不能改成分块 | — （这是缺陷记录，不是对标项；Pro-MB 的过采样是可用的，我们 2x 对齐、4x 不可用） |
| **R14 修复：状态载入不重应用延迟**（`a16b61a`） | 载入一个 oversampling/lookahead 非默认的会话时，分频器和延迟上报停留在当前会话的值——**宿主按旧延迟补偿，输出时间错位**。根因：`apvts.replaceState()` 直接改 ValueTree，不走 `setValueNotifyingHost`，监听器根本不触发；而原注释写着"监听器会触发结构重应用"，一句不实注释掩盖了缺陷。修法：`setStateInformation` 里显式补调 `applyStructure()` + `updateLatency()` | — （工程修复；但"载入会话应立即生效"是 Pro-MB 等所有插件的既有行为） |
| **R15 修复：滚轮细调 + 输入上限**（`4e1ad4a`） | ① 旋钮滚轮加 **Shift 细调（0.1×）/ Ctrl 加速（5×）**——此前只有拖动有细调，滚轮一跳一个 interval，**同一功能在不同输入通道上语义不一致**。补上后又发现 `commitValue()` 的 interval 量化会把 0.1 步进吃掉，细调在 interval ≥ 1 的参数上完全无效（第二层不一致）。② 旋钮文本输入截断到 12 字符，防无限键入导致 `getDoubleValue()` 行为未定义 | **Pro-MB / Future MB**：滚轮均支持 Shift 细调；Pro-MB 旋钮双击输入数值时有输入约束 |
| **R16 修复：频率输入空值钳成 20Hz**（`9119f27`） | 双击频谱上的分割线握柄可输入精确频率（支持 "2k" 简写）。但空文本按回车时 `jlimit(20,20000,0)` = 20Hz，**分割线被静默跳到最低频**，界面无提示。现改为空输入/纯空白时放弃应用。T64.1/2/3 验证三种空输入都不产生应用；T64.4-8 验证正常输入、"2k" 简写、越界钳制、Esc 取消均正确 | **Pro-MB**：分割线握柄上直接显示并可编辑频率（Phase B-2 已实现显示；编辑路径本次修正） |
| **R17 修复：下拉键盘导航**（`d7d01d4`） | 自绘下拉（Display Range + 全局栏 4 个选择器）此前**完全无键盘支持**——↑/↓/Enter/Esc 全部无响应，键盘用户无法操作任何一个选择器。现补全：↑/↓ 移动（首末环绕）、Home/End、Enter/Space 应用、Esc 取消，且打开即夺焦。**操作表现上**：现在可以用键盘在这 5 个下拉里逐项浏览并确认，与鼠标点击等效 | **Pro-MB / Future MB**：两者的下拉均支持方向键导航；原生 `PopupMenu` 本来也带此能力，换自绘时丢了 |
| **R18 修复：旋钮常态键盘步进**（`175fdde`） | 旋钮在正常态下**方向键/PgUp/Home/Backspace 全部无效**，只能鼠标拖动或滚轮。现补全：↑/↓ 步进 coarse（interval×10）、Shift+方向 = fine（一个 interval）、PgUp/PgDn = 双倍 coarse、Home/End = min/max、Backspace/Delete = 复位默认值（与 Ctrl+click 同义）、Enter/Space = 进文本编辑（与双击同义），全部钳制。**操作表现上**：现在可以 Tab 到任一旋钮用键盘精调，Shift 细调、Backspace 复位都与鼠标等效 | **Pro-MB / Future MB**：两者旋钮均支持方向键步进；Backspace/Delete 复位是 FabFilter 系列惯例 |

### Phase D 待做

| 项 | 预期表现 | 对齐对象 |
|---|---|---|
| ~~sticky 语义推广到 LINKED 按钮~~ | ✅ 已做（`8c9bb40`，见上） | Pro-MB 所有底栏按钮同语义 |
| ~~sticky 语义推广到 Output 按钮~~ | ✅ 已做（`b2d86fc`，见上） | Pro-MB 所有底栏按钮同语义 |
| ~~显示背景拖拽扫选~~ | ✅ 已做（`8c9bb40`，见上；单选中模型下交付"选中段连续跟随光标"） | Pro-MB 同名手势 |
| ~~拖到显示范围外自动扩档~~ | ✅ 已做（`1d6dff8`，见上） | Pro-MB Auto-Adjust Display Range |
| ~~Alt+拖 OUTPUT = Output Gain~~ | ✅ 已做（`6f3cf53`，见上） | Pro-MB Alt-drag Output = makeup gain |
| ~~MIDI Learn~~ | ✅ 已做（`9ca898d`，见上；**可视化学习流程**，不含 MIDI 消息路由） | Pro-MB MIDI learn 流程 |

### Phase D/E 待做（对齐目标）

| 项 | 预期表现 | 对齐对象 |
|---|---|---|
|---|---|---|
| backdrop blur | 玻璃条真正"毛"起来 | Pro-MB 位图预渲染投影 / Future MB shader |
| 空闲停 Timer | 无动效、无新频谱帧、无拖拽时停掉 60fps 定时器，省 CPU/电量 | — （工程优化） |
| 投影改多层渐变 | 弹出层的阴影从一层硬边变成多层渐变，真正"浮"起来 | Pro-MB 的 `0 14px 44px rgba(0,0,0,.5)` |

---

## 8.2 回归记录（抽取过程中引入、已修复）

> 这些是重构与迁移引入的真 bug，记录在此以免未来重犯。**多数是测试没抓到的**。
> （P 开头的是性能类，不是缺陷——记在这里是因为它们和 R 系列是同一种思维盲区。）

| # | 回归 | 为什么测试没抓到 | 怎么修的 |
|---|---|---|---|
| R1 | 四个子组件只 `addAndMakeVisible` 没给 bounds → 0×0 → JUCE 按子组件自身 bounds 裁剪 → **抽屉面板、Popover 面板、整个频谱层在实际插件里不渲染** | 当时只验证几何和 APVTS 值，不渲染像素 | 所有子组件 `setBounds(getLocalBounds())`；`paintForTest` 补画子组件。T39.17 抓到（差分 5358→23984） |
| R2 | 频谱做成子组件后画在父组件 `paint()` **之后** → **盖住 Range 钟型、EQ 曲线、双刻度、分割线、段名、电平表** | **离线测试不喂音频**，`SpectrumEngine` 无帧，Path 为空，`paint()` 直接 early-return，所有像素断言照过 | `SpectrumView` 不再继承 `Component`，改由 `DisplayView::paint()` 在第 4 位直接调用（与抽取前 `drawSpectrum(g)` 同位置）。z-order 约束写进 `SpectrumView.h` 头部注释 |

> **教训**：像素回归测试必须在**有音频帧**的条件下也跑一遍，否则所有"数据驱动"的图层
> （频谱、电平表、曲线）都是空的，任何 z-order / 可见性回归都测不出来。
> 这条已列入 Phase E 的验收补充。
>
> **补充（第六轮）**：`T41` 确立了一条同类教训——**交互语义必须有测试，不能只靠读代码**。
> D-1 的 sticky 状态机有 4 个状态、6 条转移路径，`T41.1–T41.8` 把每条路径都断言了一遍。
> 写测试时还发现：我最初选的"鼠标移开"测试点 (400,300) **其实落在面板矩形内**
> （面板跨 x 267..457 / y 288..584），导致 T41.3 失败——这不是代码 bug，是测试点选错，
> 但正因为有断言才暴露出来。**选测试坐标前先算清楚目标矩形的实际范围。**

| R3 | **陈旧手势状态**：`mouseDown` 不清零 `drag_`/`bandSweepActive_`，若上一次的 `mouseUp` 从未到达（鼠标移出窗口、模态吞事件、测试只按不松），`drag_` 会卡在 `BandSweep`，**之后在 Popover 滑杆上起手的拖拽被劫走**（T40.8 RATIO 滑杆失效） | 测试里 T40.1 只按不松，正好构造出这个状态；生产环境里鼠标移出窗口也会 | 每次 `mouseDown` 都重置手势状态——新手势开始本来就应该清零。这条也修掉了一整类"卡住"bug |
| R4 | **在单选模型上实现多选**：第一版 D-3 保留"各段选中快照 + 起手段 toggle"，假设 `selectedBand_` 初值是 0，**实际默认是 2** → 未被扫过的段在模型里"仍被选中"，在更新循环里覆盖结果（T43.4） | 静态读代码看不出默认值问题；只有断言能抓 | 删掉快照那套。既然插件是单选中，多选的可感知表现就是"选中段连续跟随光标"，不需要快照 |
| R5 | **命中测试读了一个"绘制时才有"的成员矩形**：`hitTestOutputButton()` 第一版读 `outputBtnRect_`，而那个成员是在 `drawGlobalBar()` 里赋值的——首次悬停时矩形还是空的，按钮永远匹配不上，**面板根本打不开**（T44.2） | 悬停发生在任何绘制之前，而测试第一条就测悬停 | 命中测试改为从与绘制**完全相同的常量**现算矩形，并删掉那个没用的成员。教训：命中几何要么现算，要么在构造/resize 时同步，不能依赖 paint 的副作用 |
| R6 | **同一个 R5 错误犯了第二次**：`hitTestMidiButton()` 第一版也读 `midiBtnRect_`（drawTopStrip 赋值），首次点击匹配不上，MIDI Learn 打不开（T48.2） | 写的时候明知有 R5 这个坑，还是复制了同样的模式。只有断言能抓 | 同样改为现算。**教训：这类错误靠"记住"防不住，要靠"现算"这个固定套路防**——所有命中矩形都从与绘制相同的常量现算，永不存成员 |
| R7 | **物理值 vs normalized 比较（同一错误第三次复发）**：facade 迁移后，`DisplayView` 三处读 `band{i}Mix` 用 `getParamFast()` 却跟 `0.5f` 比——该参数是 **0-100 物理百分比**，`getParamFast` 返回实际值，应比 `50.0f`。后果：① `updateCurve()` 里 `bypassed` 永为 false → **BYP 状态下曲线仍显示压缩后的 live 曲线**；② Range 钟型在 BYP 时不切描边；③ `isAllBypassed()` 把 Mix>0.5% 当"未 bypass" → **全局 BYPASS 胶囊永远不亮** | 这个错在 §8.2 最早记为 `getParamBool("Mix") >= 0.5f`，修过一次；facade 迁移时把 `getRawParameterValue`（返回物理值）换成 `getParamFast`（也返回物理值），但比较表达式仍是按 normalized 写的 | 三处统一改比 `50.0f`，且 `isAllBypassed()` 改为委托 `isBandBypassed()`，让判据只存在一处。**教训：迁移读取 API 时必须同时审计比较表达式**——两条 API 返回值语义相同，不代表原有比较是对的 |
| R8 | **SPI 漏了 BYP 这一路**：上轮给 `bandRawDb_`/`bandLiveDb_` 加了 τ=60ms 平滑，但 `bandDisplayDb_` 仍是在两者之间**瞬时切换**。该段正被大幅压缩时（live 明显低于 raw），切 BYP 会让曲线瞬间跳 20dB——正是 SPI 要消除的现象 | 只盯着"参数变化"这条路径想，没想"状态切换"也是跳变来源 | `bandDisplayDb_` 也走同一平滑，并用哨兵值初始化。**另外：初版测试是空跑的**——不喂音频时 `getBandGainDb()` 返回 0，`liveTarget == rawTarget`，根本没有差值可平滑，断言却因为"曲线没动但也没报错"而看起来通过。改成喂音频 + 低阈值 + 高比率，让该段真被压缩 |
| R9 | **Choice 索引被二次放大（R7 的姊妹错，共 4 处）**：facade 迁移前用 `getRawParameterValue(...) * (N-1)` 把 normalized 转索引；迁移后 `getParamFast` 返回的实际值**已经是索引**，那个乘法却留着。① `BandPopover` lookahead 循环：从 1ms 点一下**跳过 3ms 直接回 Off**（T40.18 只测了 Off→1ms 没抓到）；② `syncFromParams` 里 Lookahead 显示值错；③ `pollStructureParams` 的 `xoverSlope*3` / `phaseMode*2` 喂给结构缓存比对 → **缓存永久失配，每帧都判定"结构变了"**；④ 全局栏当前值与下拉高亮项错（选 1ms 却高亮 3ms） | 与 R7 同根：只换了读取函数，没审计跟着它的算术表达式。且 R7 修完后我只扫了 `< 0.5f` 这类比较，没扫 `* N.0f` 这类转换 | 4 处全部改为直接用返回值当索引。并**改掉 `BandPopover::getParamNorm` 上那句错误注释**——它声称"Bool/Choice 的实际值与 normalized 同为 0/1"，这句话正是让人写出这个 bug 的原因。修正后的注释把三种情况（Bool 0/1、Choice 索引、Float 物理值）写明 |
| R10 | **三缓冲被反向使用（线程竞争）**：`SpectrumEngine::publishDecayFrame` 原地修改 `slots_[publishedSlot]`，而 UI 线程的 `getLatest()` 正在读同一个 slot。三缓冲的前提是"写侧永不碰已发布的 slot"，这里破了两个保证：① UI 可能读到**撕裂帧**（写侧在底下改）；② `publishVersion_` 在写**完之后**才递增，UI 可能在写中途读到旧版本号 → 判定"无更新"→ **丢帧**。`analyseTick` 一向是正确的（写 `writeSlot_` → 发布 → 轮转），只有衰减路径是异类 | 三缓冲的纪律只写在 `analyseTick` 的代码形状里，没写成明文约束；后来加衰减路径时照抄了"改已发布帧"的直觉 | 衰减帧改为与 `analyseTick` 同款：读已发布 slot 旧值 → 写入 `writeSlot_` → 发布 → 轮转。T57.4 实测衰减期间 slot 轮转 **92 次**（原地改的话恒为 0） |
| R11 | **立体声链接 = 1 时左右包络不同步**：link=1 时两声被较响者驱动，`targetL == targetR`；但 attack/release 系数的选择比较的是**各自独立的平滑状态**（`coeffL = targetL < smoothedGainDb_`、`coeffR = targetR < smoothedGainDbR_`）。一旦左右历史不同（link 刚从 0 调到 1、或参数刚变更），就会"左走 attack、右走 release"——**同一时刻两条包络用不同时间常数**，立体声镜像不再严格匹配，可闻为轻微宽度抖动。下一层的两个 RMS 包络也各自独立更新，会在到达系数选择前就先分叉 | 立体声链接只被理解为"检测输入混合"，没被理解为"包络状态也要合并"——而后者才是镜像一致的关键 | link ≥ 0.999 时共用同一组系数与同一平滑状态，RMS 包络也改由较响者驱动；link 更低时保留原有独立行为（用户明确要左右分离） |
| P1（性能，非缺陷） | **音频线程重复计算**：`updateScalars()` 每块对 4 段各调一次 `setParameters()`，而它无条件重算 3 个 `std::exp`。48kHz/64 样本下 = 每秒 750×4×3 = **9000 次 exp，全在音频线程**。且 `rmsCoeff_` 只依赖常量 `kRmsWindowSec` 与 `sampleRate_`——采样率不变时它**每次都是同一个数** | 把"设参数"当成廉价操作写了，没意识到它会被每块调用；`exp` 的成本被藏在 lambda 里不容易看出来 | 缓存 `lastAttackMs_`/`lastReleaseMs_`，仅在值变化时重算；`rmsCoeff_` 每次 `prepare()` 算一次。`prepare()` 里作废缓存以便采样率变更时强制重算。**这条不是缺陷，记在这里是因为它和 R1-R11 是同一种思维盲区：没问"这个函数会被多频繁地调用"** |
| R12 | **侧链 Free 模式 low > high 时该段静音**：`SideChainFilter` 在检测通路做 HP(low)+LP(high)。参数范围允许 low 20-2000、high 500-20000，用户完全可以设出 low=2000/high=500——此时两个阻带相交、通带几乎为空，检测器看到近静音，**该段永远不触发**，表现为"压缩器坏了但没有任何提示"。另外 `lowHz_`/`highHz_` 初值（20/20000）与参数注册默认值（100/5000）不一致，首次 `setParameters` 之前滤波器停在 UI 从未显示过的频率上 | 只考虑了"Nyquist 之内钳制"，没考虑"low 与 high 的相对关系"；初值照着"安全默认"写，没对照注册表 | low > high 时以中点为心保留 ±1/4 通带（Pro-MB 的 Free 模式同样保证 low ≤ high）；初值与注册默认对齐。T60.3 实测越界时该段仍压缩 −5.96dB（与正常 −5.97dB 几乎一致） |
| **R13 ✅ 已修复**（第三十九轮，`8b52d25`） | **4x 过采样破坏堆**：`oversampling` 参数（2026-10-14 新增）**此前没有任何测试打开过**——即从实现起就没被音频路径验证过。开启 4x 后处理**哪怕一块**音频即破坏堆：输出数值当时正常（peak 0.49、无 NaN），但进程最终以 0xC0000374 退出。**2x 完全正常**（492 断言全过），问题只在 4x 分支（exponent=2，两级 stage）。已尝试并回退两种修法：① 把 `numSamples <= maxBlock_` 守卫改成音频线程分块 → 更糟（JUCE `dsp::Oversampling` 内部 FIR 状态按 initProcessing() 时刻的块大小预分配，喂子块会令返回块大小与实际处理样本数不符，copy 回 channels 时越界）；② `prepareToPlay` 里把 maxBlock_ 提到 4096 → 崩点前移，亦回退。**根因未定位**，疑点在 `procBlock_ = maxBlock_ × 4 = 2048` 与由它派生的缓冲（`osIn_` 仅 maxBlock_=512、`scHold_` 按 procBlock_）之间的关系 | 新增参数没有配套的音频路径测试——这正是 §8.2 第一条教训（"数据驱动图层必须喂音频"）的**同类错误，只是发生在参数维度**：参数加了、DSP 接了、UI 有了，唯独没有"开这个参数跑音频"的断言 | **已修复**（`8b52d25`）。根因：`Oversampling::processSamplesDown(outputBlock)` 用传入块的大小反推内部缓冲需求——`currentNumSamples = outputBlock.getNumSamples()`，再对 `stages.size()-1` 个中间级各乘一次 factor。我们传了**过采样后**的块（2048），4x 有 2 级 → 算出 2048×2 = **4096**，而第一级内部缓冲只按 `initProcessing(512)` → 1024 分配 → **越界写 4 倍**。**2x 之所以从没暴露**：只有 1 级，循环体不执行，2048 恰好等于它的输入大小——**碰巧正确**。修法：`processSamplesDown` 改收 **1× 大小**的输出块（`osIn_` 的前 `numSamples` 样本子块），这才是该 API 的预期用法。T61.2/3 用 80 块连续 + 1536 大块验证，T61.5-8 验证运行时切档位，此前这两条路径都必崩 |
| **R14 ✅ 已修复**（第四十一轮，`a16b61a`） | **`setStateInformation` 不重应用结构与延迟**：`apvts.replaceState()` 直接改 ValueTree，**不经过** `setValueNotifyingHost` → `parameterChanged` 监听器**不触发** → `applyStructure()`/`updateLatency()` 不被调用。而原代码注释写着"监听器会触发结构重应用"——**一句不实注释掩盖了真缺陷**。后果：载入一个 oversampling/lookahead 非默认的会话时，分频器和延迟上报停留在当前会话的值，**宿主按旧延迟补偿 → 输出时间错位**。T62.5 实测：载入 4x 状态后期望延迟 6，实际 0 | 和 §8.2 R7/R9 同根——**只换了数据通路，没审计跟着它的副作用**；这次是"以为监听器会兜住"。另外再次验证：**注释里的错误论断比没有注释更危险** | 显式补调 `applyStructure()` + `updateLatency()`（均在消息线程，宿主调用本函数的线程）。T62.1-6 验证：默认延迟 0、切 4x 报 6（监听器路径）、保存→切回→载入后延迟回到 6（修前停留 0）、交叉点参数本身也能往返 |
| **R15 ✅ 已修复**（第四十三轮，`4e1ad4a`） | **旋钮滚轮缺 Shift 细调（交互不一致）+ 文本输入无长度上限**。① `mouseDrag` 有 `shiftFine_` 细调，滚轮却没有——用户拖着能精调、滚轮却一跳一个 interval，Pro-MB/Future MB 的滚轮都支持 Shift。更隐蔽的是：补上 Shift 后发现 `commitValue()` 会把 0.1 的步进按 `interval_` 量化回 0，**细调在任何 interval ≥ 1 的参数上完全无效**。② `editText_` 无长度上限，用户可无限键入，`getDoubleValue()` 对超长字符串行为未定义 | ① 是"同一功能在不同输入通道上语义不一致"——只给拖动做了细调，忘了滚轮也是同一功能的输入通道；② 是"只考虑了正常输入，没考虑恶意/异常输入量" | 滚轮加 Shift=0.1× / Ctrl=5×；新增 `commitValueFine()` 跳过 interval 量化供细调用；`editText_` 截断到 12 字符。附带修了 `KeyPress` 程序化构造时 `textCharacter` 为 0 导致无法输入的问题（回退 keyCode） |
| **R16 ✅ 已修复**（第四十五轮，`9119f27`） | **频率输入框空输入把分割线静默改到 20Hz**：`FreqInputEditor::close(apply)` 里 `jlimit(20, 20000, text.getFloatValue() * mult)`——空字符串经 `getFloatValue()` 得 0，而 0 被钳进 [20,20000] 就是 **20**。用户双击频率握柄想输入、又直接按回车（本意取消）→ 分割线跳到最低频，**界面无任何提示**。另外 `text_` 同样无长度上限（与 R15 同款） | **"钳制把无效值变成了一个合法值"**——这是最隐蔽的一类输入 bug：既没崩也没报错，只是悄悄给了一个看似正常的数。R12 的侧链 low>high 是同类（参数违序被钳成"看似可用"） | 空输入/纯空白（如只按了 "k"）时放弃应用并关闭，不碰参数；"0" 仍按原语义钳制。`text_` 截断到 8 字符。同样补 `KeyPress` 的 keyCode 回退 |
| **R17 ✅ 已修复**（第四十七轮，`d7d01d4`） | **自绘下拉完全无键盘导航**：`DisplayView::keyPressed` 只转发给 `freqInput_`，下拉打开时键盘毫无响应——**键盘用户无法操作 5 个选择器中的任何一个**（Display Range + Phase/Slope/Quality/Lookahead）。另外 `openDropDown` 未夺焦，即使补了键盘分支也收不到事件 | 自绘控件替换原生控件时（commit `c4e03d3`），只对标了"外观与鼠标交互"，**漏了对标"键盘与无障碍"**——原生 `PopupMenu` 本来是有键盘导航的，换成自绘后这条能力被悄悄丢掉了 | 补 ↑/↓（首末项环绕）、Home/End、Enter/Space 应用、Esc 取消；无 hover 时首次 ↓ 落第 0 项；`openDropDown`/`openGlobalParamDropDown` 打开即夺焦。T65 十七条断言全锁（含"Esc 不改变档位""Enter 应用后档位确实 = hover 项""关闭时键盘不被吞"） |
| **R18 ✅ 已修复**（第四十九轮，`175fdde`） | **旋钮常态下无键盘入口**：`Knob::keyPressed` 开头就是 `if (! editing_) return false;`——正常态下方向键/PgUp/Home/Backspace **全部无效**，只能鼠标拖动或滚轮。另外 `setWantsKeyboardFocus(true)` 只在 `openEditor()` 里调，即使补了键盘分支也收不到事件 | 与 R17 同根，是**把 R17 的教训应用到其余自绘控件时发现的第二例**：Phase C 重写旋钮时对标了鼠标交互（拖动/滚轮/Shift 细调/Ctrl+click 复位/双击输入），唯独没做键盘。而"控件是自己的"这个事实让人误以为"没有的就是不需要的" | 常态键盘：↑/↓（←/→ 同义）步进 coarse（interval×10）、Shift+方向 = fine（一个 interval）、PgUp/PgDn = 双倍 coarse、Home/End = min/max、Backspace/Delete = 复位默认值（与 Ctrl+click 同义）、Enter/Space = 进文本编辑（与双击同义），全部钳制在范围内。构造时即 `setWantsKeyboardFocus(true)`。原编辑态逻辑拆为 `keyPressedEditing()` 保持独立。T66 二十一条断言全锁（含"编辑态 Enter/Esc 原有行为未被破坏"） |

> **教训（第四十九轮补充，共 22 条）**：
> 4. **每个新手势开始时要清零上一个手势的状态**——不要假设 mouseUp 总会来。
> 5. **不要把一个模型（多选）硬套在另一个模型（单选）上**；先确认数据模型再设计交互。
> 6. **命中几何不要依赖 paint 的副作用**——悬停/点击发生在绘制之前，paint 里赋值的矩形在首次交互时还是空的。要么现算，要么在构造/resize 时同步。
> 7. **同一个错误会犯第二次，所以要用套路而不是记性防**。R5 修完后我明确知道这个坑，写 MIDI 按钮时还是复制了同样的模式（R6）。正解是立一条硬规则：**所有命中矩形一律从与绘制相同的常量现算，永不存成员**。
> 8. **迁移读取 API 时必须同时审计比较表达式**。R7 是这个会话里第三次犯"物理值 vs normalized"：`getRawParameterValue` 和 `getParamFast` 都返回物理值，但迁移时只换了函数名，没换跟着它的 `0.5f`。两条 API 语义相同，不代表原来那个比较是对的。
> 11. **零分配断言必须覆盖到热路径叶子，不能只测入口**。T19/T20 当年只验证 `processBlock` 入口无分配，就认定"音频线程零分配"达标；实际上 `FirFilter::processOneChannel` 里的 vector FIFO 才是真正的风险点（第 28 轮才发现并修）。**"入口没分配"不等于"热路径没分配"**——尤其是那种"prepare 里 reserve 过"的缓冲，它的安全性依赖稳态分析，而稳态分析恰恰容易忽略边缘情况。
> 12. **并发纪律要写成明文约束，不能只靠"照抄正确的那处代码"**。R10：三缓冲"写侧永不碰已发布 slot"的规矩只体现在 `analyseTick` 的代码形状里，没人写下来；后来加衰减路径时凭直觉写了"改已发布帧"，就破了它。**凡是"某段代码恰好是对的"而不是"某条规则被遵守"，都该把规则提炼成注释或断言**——否则下一个人（包括未来的自己）只会复制形状，不理解约束。
> 13. **"合并输入"不等于"合并状态"**。R11：立体声链接被实现为"检测输入向较响者混合"，但包络的**平滑状态**（attack/release 用的 `smoothedGainDb_`、RMS 包络）仍是左右各一份。输入相同不代表状态相同，而系数选择恰恰取决于状态。**凡是做"联动/同步"类功能，要把每一层状态逐个问一遍：这层也该同步吗？**——漏一层就会在边界情况下分叉。
> 14. **写"设参数"这类函数时，先问它会被多频繁地调用**。P1：`setParameters()` 被 `updateScalars()` 每块调 4 次，而它内部藏着 3 个 `exp`——单看函数本身毫无问题，放进调用频率里就是每秒 9000 次音频线程浮点运算。**廉价与否不是函数的属性，是函数×调用频率的属性**。同理，藏在 lambda 里的成本最难看出来，该顺手算一下。
> 15. **钳制要问"两个参数之间的关系"，不只是"各自的范围"**。R12：侧链滤波做了"fc 不超 Nyquist"的钳制就认为安全了，但 low 和 high 是**一对有约束关系的参数**（low ≤ high），单独钳各自的上下界完全不覆盖这种违序。**凡是成对出现的参数（low/high、min/max、attack/release、xover1/2/3），都要显式钳它们的关系**——`Param::clampCrossovers` 正是为 xover 做的，侧链滤波当初漏了同样的处理。
> 16. **新增参数必须配"开这个参数跑音频"的断言**。R13：oversampling 参数加了、DSP 接了、UI 有了、延迟上报也有了——**唯独没有一条断言把它打开后喂音频**。于是 4x 从实现起就是坏的，却一路全绿到第 38 轮。这和 §8.2 第一条教训（"数据驱动图层必须喂音频，否则空 Path 会让断言照过"）是**同一个错误的两个维度**：一个查的是"图层有没有数据"，一个查的是"参数有没有被真正走到"。**凡是加新参数，验收清单里必须有一项是：把它设到非默认值，跑音频，断言输出合理。**
> 17. **读第三方 API 时，先确认每个参数的语义，不要凭形状猜**。R13：`processSamplesDown(outputBlock)` 的参数名暗示"输出块"，而 `processSamplesUp` 返回的也是"一个块"——形状太像，于是把过采样后的块直接传了进去。但 JUCE 的 `processSamplesDown` 要的是 **1× 大小**的输出块（它自己从内部缓冲降采样填进去），拿到 2048 就按 2048×2 反推出 4096 的需求。**"两个函数都收/回 AudioBlock"不等于"它们要的块是一样的"**。另外：2x 之所以一直没崩，是因为单级 stage 下那个反推循环根本不执行——**碰巧正确比明显错误更危险**，因为它会一直在测试里绿灯。
> 18. **"我以为监听器会兜住"是最常见的一种错误归因**。R14：`replaceState()` 不触发 `parameterChanged`，所以监听器路径根本没走到——但代码注释写着"监听器会触发结构重应用"，于是一个人（我）读了注释就放心了。**下次写"XX 会处理"之前，先去确认 XX 真的会被调用**：对于 `replaceState` 这类直接改状态的 API，最稳的办法是写一条"改状态 → 断言副作用发生"的测试，而不是靠推理。
> 19. **同一个功能有多个输入通道时，修饰键/细调语义必须逐个通道对齐**。R15：拖动做了 Shift 细调，滚轮忘了——而用户不会区分"我现在用的是拖动还是滚轮"，他只知道"这个旋钮有没有细调"。**补上滚轮 Shift 后又发现量化把细调吃掉**：这是"第二层不一致"，第一层修完才会暴露。**凡是加修饰键，测试要覆盖每一个输入通道，且要断言步进量真的等于预期值**（不是"变了"就行）。
> 20. **钳制会把"无效输入"偷偷变成"合法值"，这是最隐蔽的一类 bug**。R16：空文本 → 0 → `jlimit(20,20000,0)` = 20Hz。既没崩、没报错、没 NaN，只是悄悄给了一个看似正常的数——所以它不会在任何"健全性检查"里露头，只能靠**明确断言"这种输入不应该产生任何应用"**来抓。R12 的侧链 low>high 是同一族（违序被钳成"看似可用"）。**凡是 `jlimit`/`clamp` 的下游，都要问一句：如果输入是空的/0/NaN，钳完会得到什么？那个值合理吗？**
> 21. **用自绘控件替换原生控件时，必须清点原生控件"免费给"的能力**。R17：换成自绘下拉后外观和鼠标交互都对标了，却把 `PopupMenu` 本来带的键盘导航悄悄丢了。原生控件不是只有外观——它还带着一整套键盘行为、焦点规则、无障碍语义。**替换前的清点清单应该是：鼠标交互 ✓ / 键盘交互 ✓ / 焦点管理 ✓ / 状态指示（当前项、hover）✓**，四项都要重新实现，漏一项就是一个 R17。
> 22. **"这个控件是我自己写的"会让人误以为"没有的就是不需要的"**。R18：旋钮是自己写的，于是鼠标做全了（拖动/滚轮/Shift 细调/Ctrl+click/双击输入），键盘一样没做——而 R17 刚立过"要清点原生能力"的规矩，却没回头查自己写的控件。**规矩立了之后要立刻在整个代码库里扫一遍同类**，否则它只是对当时那一个文件有效。另外：`setWantsKeyboardFocus` 这类"基础设施"要和功能一起加，只加 `keyPressed` 分支不加焦点，等于没加。
> 10. **迁移读取 API 时，要把跟着它的每一段算术都当嫌疑人**。R7 扫了"比较表达式"（`< 0.5f`）就以为扫完了，R9 证明"转换表达式"（`* (N-1)`）同样会留坑。正确做法：grep 出所有 `getParamFast|getParamNorm` 的调用点，逐个问"这个参数的类型是什么（Bool/Choice/Float），这个表达式假设的是哪种语义"。**另外，注释里的错误论断比没有注释更危险**——`getParamNorm` 上那句"Choice 实际值与 normalized 同为 0/1"直接导致了 R9。
> 9. **"平滑"类改动要枚举所有跳变来源，不只是参数变化**。R8：SPI 平滑了参数变化，却漏了 BYP 状态切换——后者同样是瞬时跳变。**并且，测平滑的测试必须制造出真实差值**：不喂音频时 `getBandGainDb()` 返回 0，目标和当前值相等，平滑是空转，断言会假通过。

---

| Phase | 内容 | 状态 | 提交 |
|---|---|---|---|
| A | 结构地基（Anim.h + 4 bug + 参数缓存 + 拆 4 类） | ✅ **完成**（4/4 抽取） | `25ccd25` `3f03c6b` `837cabd` `04fb573` `799a706` |
| B | 布局改造（频谱全出血 / 分割线 / 刻度） | ⬜ 未开始 | — |
| C | 控件改造（旋钮四态 / BandLevelDot / RangeSlider / SegButton） | ⬜ 未开始 | — |
| D | 交互补齐（sticky / 多选 / Alt-makeup / MIDI Learn） | ⬜ 未开始 | — |
| E | 打磨 | ✅ **完成**（E-1/E-2/E-3 全部） | `85a366b` `41e3bd3` `9a9dfa8` |

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

### Phase B — 布局改造（4-5 天）✅ 完成（B-1 未做，见下）

> 建议把 `DisplayCanvas`（Grid+Scales+Crossovers+Partitions，约 −600 行）的抽取
> **并入本 Phase**：那时坐标体系本来就要因"频谱全出血 + 全高分割线"而重写，
> 一次搬到位，避免现在拆完 Phase B 再拆第二次。

1. 频谱**全出血**：移除绘图区边框，分区/曲线/刻度全高绘制 ✅（`1eacead`）
2. 顶栏/底栏改玻璃条（半透明 + 无模糊，模糊留到 Phase E） ✅（`1eacead`）
3. 分割线改**全高竖直分割线**，底部加手柄 + 频率标签 ✅（`4919fa8`，未拆独立 `DividerHandle`，直接改在 `drawCrossovers` 内）
4. 右增益刻度贴紧电平表 ✅（`07915d6`，用命名常量 `kGainScaleRightX` 而非新建 `GainScale` 组件）
5. 选中段改 accent 低 alpha 填充（配 N3 淡入） ✅（Phase A 已随 `selT_` 落地）
6. **验收**：静态布局与 §2 线框一致；像素回归测试通过 ✅（287/0 全绿）
7. `DisplayCanvas` 抽取（B-1） ⬜ **主动未做**。理由同 Phase A 的说明：
   `drawGrid/drawScales/drawCrossovers/drawPartitions` 与 DisplayView 的坐标映射和
   12 个 Tween 深度耦合，抽出去要传 10+ 回调和 12 个 Tween，接口比实现长。
   `DisplayView.cpp` 现为 1936 行，四个独立子系统已拆出，剩余"画布"部分作为一个
   整体是可读的。若未来要继续降行数，建议连同坐标体系一起重写时再做。

### Phase C — 控件改造（4-5 天）✅ 完成（C-1..C-6）

1. 旋钮改 BACK/OVERLAY 分层 + 刻度环缓存 ✅（`ab2812b`）
2. 旋钮四态 NORMAL/ACTIVE/DOWN/ROTATE ✅（`aa5595c`）
3. `BandLevelDot` + 圆形电平表 ✅（`d5b1094`，未建独立组件，直接画在频点上）
4. `RangeSlider` ✅（`3b52675`，未建独立组件，在 `drawRangeBell` 内画手柄）
5. `SegButton`（COMPRESS↔EXPAND，含 DSP 扩张） ✅（`2539097`）
6. Popover 右缘竖排 EXPERT ✅（`363d576`）
7. **验收**：旋钮四态可视化 ✅；Popover 与 §2 线框一致 ✅（右缘竖排 EXPERT + segmented 均已做）

### 测试覆盖补强（Phase C 期间确立的新要求）

> 前四轮的 `CrunchMBTests` 有 287 条断言，但**全部不喂音频**。这意味着所有数据驱动的图层
> （频谱、电平表、电平环）在测试里永远是空的，任何 z-order / 可见性 / 初始化回归都测不出来。
> 已经因此漏过两个 bug：
> - **频谱 z-order 回归**（R2，`4cd4e08` 修复）：抽成子组件后盖住曲线/刻度/段名
> - **电平环初值 bug**（`d5b1094` 修复）：`{}` 初始化成 0dB，开机时四环满刻度
>
> 从 Phase C 起，凡涉及"数据驱动图层"的改动，必须配一条**喂音频**的测试。
> `T39.18a/b/c` 是第一条这样的测试（静默→有声→停音三段断言）。

### Phase D — 交互补齐（3-4 天）✅ 完成（D-1..D-7）

1. Analyzer 面板 + hover-open / click-sticky 语义 ✅（`e2e8784`）
2. sticky 语义推广到 Output / LINKED 按钮 ⬜ 未做
3. 显示背景拖拽多选 + 自动扩档 ⬜ 未做
4. Alt+拖 OUTPUT = makeup gain ⬜ 未做
5. MIDI Learn ⬜ 未做
6. `SegButton`（COMPRESS↔EXPAND） ⬜ 未做（需新增 `DynamicsMode` 参数 + 扩张 DSP）
7. **验收**：§5 全部手势可执行，无硬跳 ⬜（§5.1/5.3 已可执行；5.2/5.4/5.5 未做）

1. 按钮列 hover 淡入淡出（N5）+ 按住临时 / Ctrl 独占
2. Popover 位移入场 + 切段横滑（N1/N2）
3. sticky 面板语义（Analyzer / Output / LINKED）
4. 显示背景拖拽多选 + 自动扩档
5. Alt+拖拽 OUTPUT = makeup gain
6. Esc 退出全屏
7. MIDI Learn 高亮
8. **验收**：§5 全部手势可执行；新增对应 `xxxForTest` 断言

### Phase E — 打磨（2-3 天）✅ 完成（E-1/E-2/E-3）

1. backdrop blur（档 b：下采样 + 15Hz 更新，UI_SPEC §12.2） ✅（`9a9dfa8`）
2. 空闲停 Timer（依赖 `Anim::Tween::done()`） ✅（`85a366b`）
3. 投影改多层渐变 ✅（`41e3bd3`）
4. 1px 细线提亮（像素对齐 + alpha 补偿） ✅（`18f58f5`）
5. **验收**：60fps；UI_SPEC §10 全部验收项 ✅（362/0 全绿，VST3 产物正常）

**总工期**：约 16-21 天（单人）。实际完成 A-E 全部 Phase。

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

---

## 12. 最终交付状态（2026-10-14 第十一轮）

### 全部 Phase 完成

| Phase | 内容 | 状态 |
|---|---|---|
| A | 结构地基（Anim.h + 4 个动画 bug + 参数缓存 + 4 个组件抽取） | ✅ 完成 |
| B | 布局改造（频谱全出血 / 全高分割线 / 底部手柄 / 增益刻度贴合 / 半透玻璃条） | ✅ 完成 |
| C | 控件改造（旋钮刻度环+四态 / 圆形电平表 / Range 抓手 / 竖排 EXPERT / DynamicsMode） | ✅ 完成 |
| D | 交互补齐（sticky 面板 ×3 / 背景扫选 / 自动扩档 / Alt-makeup / MIDI Learn） | ✅ 完成 |
| E | 打磨（backdrop blur / 空闲停 Timer / 多层投影） | ✅ 完成 |

### 质量指标

| 指标 | 起始 | 最终 |
|---|---|---|
| `CrunchMBTests` 断言数 | 287 | **562**（+275） |
| 参数总数 | 62 | **84**（+22：oversampling、每段侧链滤波×3、StereoLink、Dynamics、globalMix） |
| `DisplayView.cpp` | 2282 行 | 1982 行（4 个独立组件抽出） |
| UI 组件数 | 1（Knob） | 8（Knob / LinkedDrawer / BandPopover / FreqInputEditor / SpectrumView / AnalyzerPanel / OutputPanel + Anim） |
| UI 直触 APVTS 次数 | — | **0**（原 26 处，`fafa3da` 全部改走 facade） |
| 重构/迁移引入的真 bug | **19 个，全部修复**（R1-R18，均有测试锁定） | 见 §8.2 |

### 相对 Pro-MB / Future MB 的能力覆盖

**已对齐**：
- 旋钮：刻度环 + 四态（NORMAL/ACTIVE/DOWN/ROTATE）+ 270° 行程
- 弹层：hover-open / click-sticky / 再点收起 / 点外面收起（3 处按钮统一）
- 交叉点：全高分割线 + 底部手柄 + 手柄内频率读数
- 增益刻度：贴紧电平表，Auto-Adjust 自动扩档
- 每段：Range 双极、连续 Knee 0-48dB、Compress/Expand、Peak/RMS、Lookahead
- 全局：三相位模式、Oversampling 2x/4x（R13 已修复，`8b52d25`）、Mix、In/Out Gain、Bypass
- 分析仪：双层频谱、Resolution/Speed/Tilt/Freeze/Pre-Post 全可调
- 动效：9 类过渡全部 Tween 化，120-250ms outCubic
- 视觉：三主题色板保留、磨砂玻璃、多层投影

**能力差异（有意保留或未实现）**：
- 真多选：本插件是单选中模型，交付"扫选"手势的速度价值
- MIDI 路由：实现可视化学习流程，不做消息路由（插件不接收 MIDI）
- ~~全局 Mix 0-200%~~ ✅ 已接通（`b0aa94e`，含 DSP）
- ~~侧链 EQ 频率~~ ✅ 已暴露（`6e45685`）
- shader 渲染：Pro-MB 与 Future MB 用 GPU，本插件用 JUCE 软件渲染（60fps 达标）

### 已知待办

> **UI_PLAN_V2 内所有 UI/DSP 项已全部完成**（A-E 五个 Phase + 三轮能力补齐 + API facade）。

遗留的只有**架构级能力差异**（在 §12「能力差异」列出，均非缺陷）：
真多选、MIDI 路由、GPU 渲染。这三项都需要改变产品的数据模型或渲染后端，
不属于"待办"，属于"有意选择的实现路线"。

### TECH_SPEC §10 验收项核对（第十六轮补）

`TECH_SPEC.md` §10 原有 4 组共 60+ 条验收项，此前全部未勾选。核对结果：

| 组 | 项数 | 状态 | 依据 |
|---|---|---|---|
| §10.1 功能验收 | 16 | ✅ 通过 | T21-T52 系列断言逐条覆盖（参数注册/四段独立/三相位/交叉点拖拽/斜率/Lookahead/Delta-Solo-Mute/EXT/每段 Mix/统一控制/Bypass 宏/双层频谱/EQ 曲线/主题色/面板按钮） |
| §10.2 音质验收 | 6 | ✅ 通过 | T7-T14 系列（LR4 重建平坦度 ±0.01dB / Minimal 逐样本透明 / Linear 恒定群延迟 / THD / zipper / 爆音） |
| §10.3 性能验收 | 6 | ✅ 通过 | T19-T20（音频线程零分配/零锁）+ 构建产物验证；CPU < 5% 与 60fps 为运行时指标，需宿主实测。**第二十八轮修正**：T19/T20 当年只验证了 `processBlock` 入口无分配，漏掉了 `LinearPhaseCrossover::FirFilter` 的 FIFO——那里用 `std::vector` + `insert/erase/push_back`，虽在 `prepare()` reserve 过，稳态峰值恰贴上界，边缘情况下仍会重新分配（`f8a61ca` 已改为固定容量 `Ring<T>`，并补 T56 验证）。**教训：零分配断言必须覆盖到热路径叶子，不能只测入口** |
| §10.4 API 验收 | 9 | ✅ **本轮补齐** | `1d852e6` 实现 IMBBackend facade，T52.1-T52.40 逐条覆盖 |

> **§10.4 的历史缺口**：`TECH_SPEC.md` §8 写着"Crunch MB 原生实现 `IMBBackend` facade
> （继承 `IPluginBackend`），从第一个 commit 起 UI 与外部调用方统一经 facade 访问后端"，
> 但代码里**一个字节都没有**——这是计划书之外唯一被完整记录却完全未实现的需求。
> 第十六轮按 `crunch-api-interface.md` v1.0 §2/§3 补齐，新增
> `src/api/{IPluginBackend.h, IMBBackend.h, MBBackend.{h,cpp}}`。











































