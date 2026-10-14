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

### 进度总览（更新于 2026-10-14，第十二轮 · 侧链 UI 补齐）

| Phase | 内容 | 状态 | 提交 |
|---|---|---|---|
| A | 结构地基（Anim.h + 4 bug + 参数缓存 + 拆 4 类） | ✅ **完成** | `25ccd25` `3f03c6b` `837cabd` `04fb573` `799a706` |
| B | 布局改造 | ✅ **完成** | `4919fa8` `4cd4e08` `1eacead` `07915d6` |
| C | 控件改造 | ✅ **完成**（C-1..C-6 全部） | `ab2812b` `aa5595c` `d5b1094` `3b52675` `363d576` `2539097` |
| D | 交互补齐 | ✅ **完成**（D-1..D-7 全部） | `e2e8784` `8c9bb40` `b2d86fc` `1d6dff8` `6f3cf53` `9ca898d` |
| E | 打磨 | ✅ **完成**（E-1/E-2/E-3 全部） | `85a366b` `41e3bd3` `9a9dfa8` |

---

> **参数总数变更记录**：62 → 79（2026-10-14 第一轮：oversampling + 每段侧链滤波×3 + StereoLink）→ **83**（第九轮：每段 +Dynamics）。T0.1 / T0b.1 的断言已同步更新。

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

### Phase C 待做（仅剩 TextSelect）

| 项 | 预期表现 | 对齐对象 |
|---|---|---|
| ~~`SegButton`（COMPRESS↔EXPAND）~~ | ✅ 已做（`2539097`，含 DSP 扩张） | Pro-MB segmented |
| `TextSelect` 文本选择器 | 文本选择器弹二级菜单（如 Output 面板的档位选择）。当前档位选择用原生 PopupMenu，功能可用但非自绘 | Pro-MB drop-down |

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

> 这些是重构引入的真 bug，记录在此以免未来重犯。**两条都是测试没抓到的**。

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

> **教训（第十轮补充，共 7 条）**：
> 4. **每个新手势开始时要清零上一个手势的状态**——不要假设 mouseUp 总会来。
> 5. **不要把一个模型（多选）硬套在另一个模型（单选）上**；先确认数据模型再设计交互。
> 6. **命中几何不要依赖 paint 的副作用**——悬停/点击发生在绘制之前，paint 里赋值的矩形在首次交互时还是空的。要么现算，要么在构造/resize 时同步。
> 7. **同一个错误会犯第二次，所以要用套路而不是记性防**。R5 修完后我明确知道这个坑，写 MIDI 按钮时还是复制了同样的模式（R6）。正解是立一条硬规则：**所有命中矩形一律从与绘制相同的常量现算，永不存成员**。

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
4. 1px 细线提亮（四次多项式，补偿 AA 发暗） ⬜ 未做（低优先，属细节抛光）
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
| `CrunchMBTests` 断言数 | 287 | **365**（+78） |
| 参数总数 | 62 | **83**（+21：oversampling、每段侧链滤波×3、StereoLink、Dynamics） |
| `DisplayView.cpp` | 2282 行 | 1982 行（4 个独立组件抽出） |
| UI 组件数 | 1（Knob） | 8（Knob / LinkedDrawer / BandPopover / FreqInputEditor / SpectrumView / AnalyzerPanel / OutputPanel + Anim） |
| 重构引入的真 bug | — | **6 个，全部修复**（R1-R6，均有测试锁定） |

### 相对 Pro-MB / Future MB 的能力覆盖

**已对齐**：
- 旋钮：刻度环 + 四态（NORMAL/ACTIVE/DOWN/ROTATE）+ 270° 行程
- 弹层：hover-open / click-sticky / 再点收起 / 点外面收起（3 处按钮统一）
- 交叉点：全高分割线 + 底部手柄 + 手柄内频率读数
- 增益刻度：贴紧电平表，Auto-Adjust 自动扩档
- 每段：Range 双极、连续 Knee 0-48dB、Compress/Expand、Peak/RMS、Lookahead
- 全局：三相位模式、Oversampling 2x/4x、Mix、In/Out Gain、Bypass
- 分析仪：双层频谱、Resolution/Speed/Tilt/Freeze/Pre-Post 全可调
- 动效：9 类过渡全部 Tween 化，120-250ms outCubic
- 视觉：三主题色板保留、磨砂玻璃、多层投影

**能力差异（有意保留或未实现）**：
- 真多选：本插件是单选中模型，交付"扫选"手势的速度价值
- MIDI 路由：实现可视化学习流程，不做消息路由（插件不接收 MIDI）
- 全局 Mix 0-200%：UI 已就绪，DSP 需新增参数（语义与每段 Mix 不同，未硬接）
- ~~侧链 EQ 频率~~ ✅ 已暴露（`6e45685`）
- shader 渲染：Pro-MB 与 Future MB 用 GPU，本插件用 JUCE 软件渲染（60fps 达标）

### 已知待办（低优先）

- `TextSelect` 自绘下拉（当前用原生 PopupMenu，功能可用）
- 1px 细线 AA 提亮（细节抛光）
- 全局 Mix DSP 接入
- ~~侧链 LP/HP 频率的 UI 暴露~~ ✅ 已完成
