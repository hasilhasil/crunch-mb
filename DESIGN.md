# Crunch MB — 四段多带压缩器 产品设计文档

一款基于 Crunch Compressor DSP 内核与 UI 体系构建的四段多带压缩器（Multiband Compressor），
VST3 + Standalone 格式。产品定位为 **低延迟、高透明度、无染色** 的多段动态处理器，
与 Crunch EQ / Crunch Compressor 构成统一的产品家族。

---

## 1. 产品定位与设计目标

### 1.1 核心目标
- **默认模式低延迟 + 声音干净**：默认相位模式下追求最低延迟与最大透明度，无明显可闻缺陷
- **三模式相位体系**：从三模式起步（Natural / Minimal / Linear），覆盖不同使用场景
- **无染色**：不引入任何谐波饱和 / 饱和度染色（无 Color/Style 模块），信号通路透明
- **产品家族一致性**：UI 视觉体系、交互模式、面板结构与 Crunch EQ / Crunch Compressor 高度统一

### 1.2 非目标（不在本期范围）
- Mid/Side 处理模式
- 动态 EQ 模式（每频段 bell 动态增益）
- Delta Matrix 跨频段增益路由（三体 Future MB 特性）
- 频域处理（Spectral Mode：Future MB 式时/频混合处理）——双层频谱**显示**分析仪已纳入范围（§7.3）
- 预设系统（与 EQ / Compressor 一致，宿主会话保存）
- 自动增益（Auto Gain）——本期以手动 Makeup Gain + 总输出增益替代
- 染色/饱和度模块（Color Processor）

---

## 2. 相位模式（三模式体系）

| 模式 | 显示名 | 核心特性 | 延迟 | 预振铃 | 定位 |
|------|--------|----------|------|--------|------|
| 0 | **Natural Phase**（自然相位） | 传统 LR4 IIR 交叉分频器，交叉点有适度静态相位偏移 | **0 样本** | 无 | 日常混音，追求最低延迟 |
| 1 | **Minimal Phase**（最小相位变化） | 不真正切分子带，按频段动态施加时变增益；无增益变化时信号完全零修改 | **0 样本** | 无 | 母带/平行处理，追求最大透明度 |
| 2 | **Linear Phase**（线性相位） | FIR 交叉分频器，完美相位平坦 | **可调质量**（10–50 ms） | 有（不可避免） | 母带/精度处理 |

> **命名说明**：Minimal Phase（最小相位变化）取义"相位变化最小化"——增益不变时零相位修改，
> 仅增益变化时引入微小相位效应。原型参考 FabFilter Pro-MB 的 "Dynamic Phase"
> 与三体 Future MB 的 "Liquid Phase"，核心原理相同。
> 注意：勿与 DSP 标准术语 "Minimum Phase"（最小相位滤波器）混淆——
> 后者是 Natural Phase 模式的技术基础（传统 IIR 滤波器的相位特性）。

### 2.1 Natural Phase（默认）
- 实现：Linkwitz-Riley 4 阶（LR4）IIR 互补滤波器组
- 优点：0 延迟、无预振铃、实现成熟可靠
- 缺点：交叉点附近有静态相位偏移（幅度求和恒平坦）
- 参考：Waves C4 使用同类技术（64 samples 延迟来自其特定实现，LR4 纯 IIR 可做到 0 样本）

### 2.2 Minimal Phase
- 实现：频段检测（band-pass 检测器）+ 时变频率相关增益施加（不在音频通路中真正分频）
- 优点：无增益变化时信号完全零修改（零延迟、零相位偏移、零预振铃）
- 缺点：实现复杂度最高；极端增益变化时有微小相位效应
- 参考：FabFilter Pro-MB "Dynamic Phase"、三体 Future MB "Liquid Phase"

### 2.3 Linear Phase
- 实现：FIR 交叉分频器（频率采样法设计 + overlap-save FFT 卷积），复用 Crunch EQ 的 LinearPhaseFilter 架构
- 优点：完美相位平坦，交叉点无任何 comb filter 效应
- 缺点：有延迟（10–50 ms 可调质量）、有预振铃
- 质量档位：Low / Medium / High（对应不同 FIR 长度/FFT 尺寸）

---

## 3. 频段划分

### 3.1 四个固定分区

| 分区 | 名称 | 代号 | 默认频率范围 |
|------|------|------|-------------|
| 0 | **Low**（低频） | L | 20 Hz – 200 Hz |
| 1 | **Low Mid**（中低频） | LM | 200 Hz – 1000 Hz |
| 2 | **High Mid**（中高频） | HM | 1000 Hz – 6000 Hz |
| 3 | **High**（高频） | H | 6000 Hz – 20000 Hz |

### 3.2 交叉点（自由可调 + 最小带宽约束）
- 三个交叉点：`xover1`（L/LM 边界）、`xover2`（LM/HM 边界）、`xover3`（HM/H 边界）
- **自由频段**：交叉点可自由拖拽调整（Pro-MB 风格），但**限制每分区最小频率带宽**
- 最小带宽约束：**每分区至少 1 个八度**（频率比 ≥ 2:1）（初版 UI 完成后按实际反馈复核）
  - `xover1 ≥ 40 Hz`（Low 分区至少 20–40 Hz）
  - `xover2 ≥ xover1 × 2`（LowMid 至少 1 个八度）
  - `xover3 ≥ xover2 × 2`（HighMid 至少 1 个八度）
  - `xover3 ≤ 10000 Hz`（High 分区至少 10000–20000 Hz）
  - **依据**：FabFilter Pro-MB 官方论坛确认最小带宽为 1 个八度，开发者 Frederik 说明
    *"smaller bands are not really worth the trouble — they start to affect a very small
    amount of the spectrum if you go below one octave"*。三体 Future MB 设计哲学类似
    （通过斜率而非极窄频段实现精确控制）。本插件沿用同一标准。
- 交叉点变化时动态钳制：保证相邻分区间距不小于最小带宽
- 交叉点斜率可调：**12 / 24 / 36 / 48 dB/oct**（LR 系列，48 dB/oct 为 LR8）

### 3.3 分区行为
- 四个分区始终存在（构成完整的频谱覆盖），不存在"删除频段"操作
- 每个分区有独立的独奏（Solo）/ 静音（Mute）/ 旁路（Bypass）/ Delta 监听控制

---

## 4. 信号流

```
                                          ┌──────────────────────────────────────────────────────────┐
                                          │                 Sidechain Input (外部)                    │
                                          └──────────┬───────────────────────────────────────────────┘
                                                     │ [同参数交叉分频器]
                                                     ↓
Input → Input Gain → [交叉分频器组] → Band L  → [SC: INT/EXT] → Compressor L → Makeup L → Mix L ─┐
                          │            Band LM → [SC: INT/EXT] → Compressor LM → Makeup LM → Mix LM ─┤
                          │            Band HM → [SC: INT/EXT] → Compressor HM → Makeup HM → Mix HM ─┼→ Sum → Output Gain → Output
                          │            Band H  → [SC: INT/EXT] → Compressor H  → Makeup H  → Mix H  ─┘
                          │
                          │   每段 Mix：out = mix_i × compressed_i + (1 − mix_i) × uncompressed_i
                          │   （平行压缩：干侧 = 该频段未压缩信号）
                          │
                          └─ Lookahead（可选，作用于全部频段）
```

### 4.1 信号流要点
- **交叉分频器**：输入信号分为 4 个频段（相位模式决定分频器类型）
- **侧链路由**：每段可独立选择 INT（本频段自身信号）或 EXT（外部侧链同频段信号）
- **每段独立 Mix**：平行压缩——湿侧 = 压缩后信号，干侧 = 该频段未压缩信号
- **Lookahead**：全局可选 Off / 1 ms / 3 ms（与单段压缩器一致），延迟线上报宿主
- **Solo/Mute/Delta**：Solo 独奏当前段，Mute 静音当前段，Delta 仅听该段处理差值；
  优先级 Delta > Solo > Mute（§6.2）
- **总输出**：四段求和 → Output Gain → 输出

---

## 5. 参数模型（总览）

详细规格见 `TECH_SPEC.md §5`。此处列功能分组。

### 5.1 全局参数（10 个）

| 参数 | 功能 | 类型 |
|------|------|------|
| `enabled` | 插件旁路（true = 工作） | Bool |
| `phaseMode` | 相位模式：Natural / Minimal / Linear | Choice |
| `linearQuality` | 线性相位质量：Low / Medium / High | Choice |
| `xoverSlope` | 交叉点斜率：12 / 24 / 36 / 48 dB/oct | Choice |
| `xover1` | 交叉点 1 频率（L/LM 边界） | Float |
| `xover2` | 交叉点 2 频率（LM/HM 边界） | Float |
| `xover3` | 交叉点 3 频率（HM/H 边界） | Float |
| `inputGain` | 总输入增益 | Float |
| `outputGain` | 总输出增益 | Float |
| `lookahead` | 预前瞻：Off / 1 ms / 3 ms | Choice |

### 5.2 每频段参数（13 个 × 4 段 = 52 个）

| 参数 | 功能 | 类型 | 说明 |
|------|------|------|------|
| `band{i}Threshold` | 阈值 | Float | −60…0 dB |
| `band{i}Ratio` | 压缩比 | Float | 1…20 : 1 |
| `band{i}Attack` | 启动时间 | Float | 0.1…200 ms |
| `band{i}Release` | 释放时间 | Float | 10…1000 ms |
| `band{i}Makeup` | 增益补偿 | Float | −36…+36 dB |
| `band{i}Mix` | 干湿混合 | Float | 0…100 %（平行压缩） |
| `band{i}Range` | 增益变化范围（限幅） | Float | −30…+30 dB（默认 −30；限定该段最大增益变化，功能同 Pro-MB Range，UI 钟型 layer 可视化） |
| `band{i}Knee` | 拐点类型 | Choice | Hard / Soft |
| `band{i}Detector` | 检测器 | Choice | Peak / RMS |
| `band{i}Solo` | 独奏 | Bool | 独奏本段 |
| `band{i}Mute` | 静音 | Bool | 静音本段 |
| `band{i}Ext` | 外部侧链 | Bool | true = 外部侧链 / false = 内部 |
| `band{i}Delta` | Delta 监听 | Bool | 仅输出该段 wet−dry 差值 |

**总参数数：62 个**（每段 13 × 4 + 全局 10；Bypass 为 UI 宏，不占参数位；`band{i}Range` 于 2026-09-23 新增）

---

## 6. 交互设计

### 6.1 统一控制按钮（Group Control）

**功能**：对 Attack / Release / Makeup / Threshold / Mix 五组参数各提供一个统一控制按钮。
按住按钮上下拖动时，同时修改四个频段对应参数值，**线性恒定偏移**（所有段同增/同减同一 delta 值）。

**操作逻辑**：
1. 鼠标按住统一控制按钮
2. 上下拖动 → 计算偏移量 `delta = dragDeltaY × sensitivity`
3. 对每个频段：`newValue[i] = clamp(oldValue[i] + delta, min, max)`
4. 释放鼠标结束

**关键约束**：
- **线性恒定修改**：所有段变化量相同（+5 dB 就是全部 +5 dB），**不是按比例缩放**
- 各段当前值的相对差异被完整保留
- 钳制范围：各段独立钳制到参数合法范围内
- 修改实时生效（通过 APVTS `setValueNotifyingHost`），支持宿主自动化记录

**UI 形态（已定，决策 6A — 五握柄）**：
- 五个目标参数（Threshold / Attack / Release / Makeup / Mix）的旋钮标签旁各一个 ⊞ 握柄
- 按住握柄上下拖动 = 该参数四段同 delta（线性恒定偏移）；每握柄独立、即按即用
- 无模式开关，零"忘记关闭"风险；具体握柄视觉细节待 UI 稿

### 6.2 Band Solo / Mute / Delta 监听

**优先级（已定）：Delta > Solo > Mute**。判定逻辑：

```
if (anyDelta):
    out_i = delta[i] ? (wet_i − dry_i) : 0     // 仅输出 Delta 段的处理差值
else if (anySolo):
    out_i = solo[i] ? signal_i : 0
else:
    out_i = mute[i] ? 0 : signal_i
```

| 操作 | 行为 |
|------|------|
| **Delta ON（任意段）** | 输出 = 所有 Delta 激活段的 (wet − dry) 差值之和，其余段静音 |
| **Solo ON** | 当前段正常输出，其余段静音 |
| **Mute ON** | 当前段静音，其余段正常输出 |
| **Solo + Mute 同时 ON** | Solo 优先——本段正常输出，其余段静音 |
| **多段 Solo** | 多个 Solo 段同时输出，其余段静音 |
| **全灭** | 全部段正常输出 |

- Delta = 仅听"该段压缩做了什么"（湿−干差值）；取自平行压缩现成的干/湿双路径，无新增 DSP 模块
- Solo/Mute/Delta 仅影响**监听/输出**，不影响检测与增益计算（Solo 时其他段仍在压缩，
  只是不发声），保证取消后无缝切换
- 所有状态切换经 ~5 ms 增益平滑

### 6.3 外部侧链（EXT）

- **全局侧链总线**：插件提供一个外部侧链输入（JUCE sidechain bus）
- **每段 EXT 按钮**：切换当前频段的检测信号源
  - **INT**（默认）：使用本频段自身的音频信号作为检测器输入
  - **EXT**：使用外部侧链信号经过同参数交叉分频后的对应频段信号作为检测器输入
- 外部侧链信号经过**同一套交叉分频器**（同频率、同斜率）分频，取对应频段作为该段的检测信号
- 适用场景：de-essing（高频段用外部触发）、ducking（低频段用 kick 触发）等

### 6.4 每段独立 Mix（平行压缩）

- 每段独立 Mix 旋钮（0–100%）
- 0% = 全干（该频段不压缩，直通），100% = 全湿（完全压缩后信号）
- 干侧 = 该频段的**未压缩信号**（经过交叉分频但未经压缩器），湿侧 = 压缩+Makeup 后信号
- 交叉渐变经 ~10 ms 平滑，避免拉链噪声

### 6.5 统一 Mix 控制

- 总 Mix 统一控制按钮，逻辑与 §6.1 完全一致
- 按住上下拖动同时修改四个频段的 Mix 值（线性恒定偏移）

### 6.6 Band Bypass（已定：方案 A — Mix 记忆宏）

- 按下：记忆该段当前 Mix 值 → Mix 写为 0%（经 `setValueNotifyingHost`，宿主可自动化）
- 再按：恢复记忆值
- **DSP 无独立路径**：Bypass 效果 ≡ Mix = 0%（干侧直通——无压缩、无 Makeup，完全透明）
- 记忆值为进程内状态（不序列化）：旁路状态下保存会话，重开后 Mix = 0%，需手动调回（已知边界）

---

## 7. UI 布局框架

> ⚠️ **本章布局描述已于 2026-09-23 被 `UI_SPEC.md` 取代**——用户提供了 UI 参考图
> （Clarity Vx Pro 四分区 + Pro-MB 弹出面板）与三条 UI 要求，新布局为三层架构
> （显示区四分区 / 底部控制区 / 频段 Popover 二级菜单）。**布局以 UI_SPEC.md 为准**；
> 本章仅保留作历史参考。色彩/家族一致性条款（§7.2）继续有效。

> **注意**：UI 详细设计由用户另行提供。本节仅定义功能区域框架与产品家族一致性要求。

### 7.1 整体框架（与 EQ / Compressor 一致）
```
┌──────────────────────────────────────────────────────────┐
│                    DisplayView（上部显示区）                 │
│  频段分区 / 双层频谱 / 仿EQ动态总曲线 / 传输曲线 / 交叉点拖拽 / 电平表 │
├──────────────────────────────────────────────────────────┤
│                 BandControlPanel（下部控制面板）              │
│  左列：全局设置 + 分区选择器 + SIDECHAIN EXT                  │
│  右区：选中段主旋钮行 + 底部参数条 + 统一控制                   │
└──────────────────────────────────────────────────────────┘
```

### 7.2 产品家族一致性要求
- **视觉体系**：沿用 CrunchPalette + CrunchLookAndFeel（三主题 Blue / Red / Cream）
- **字体**：Poppins 全家族（Regular / Medium / SemiBold / Bold）
- **旋钮**：暖灰圆环 + 强调色值弧 + 近白旋钮帽 + 深色指针
- **控件**：圆角药丸按钮、圆角选择器 + 强调色三角箭头
- **面板高度**：底部控制面板 280px（与 EQ / Compressor 一致；已定决策 9A，
  初版实测拥挤再放宽至 300px）
- **默认窗口**：920 × 620，可缩放范围 700 × 500 – 1840 × 1240
- **主题切换**：⚙ 设置按钮（U+2699），PopupMenu 三主题切换
- **渲染策略**：强制软件渲染器（Software Renderer），窗口拖拽暂停动画

### 7.3 DisplayView 显示元素（核心已定，细节待 UI 稿）
- **双层频谱（已定，决策 7）**：底层输入频谱（灰层）+ 上层输出频谱（accent 层），
  与 Pro-MB / Future MB 一致；**不保留滚动电平历史**（弃用 Compressor 的滚动样式）。
  频谱引擎移植自 Crunch EQ（SpectrumEngine：锁-free ring + 后台线程 + 三缓冲发布）
- **仿 EQ 动态总曲线（已定，决策 8A）**：各段当前施加增益（平滑 GR + Makeup）随频率的
  形状——压缩时对应频段向下凹陷、释放后回平，与 Pro-MB / Future MB 观感一致。
  压缩量仅经此曲线以频率域呈现；**无 GR 专用曲线与数字读数**；
  实时 GR 仍经 API/metering 暴露，仅 UI 不渲染
- **显示区统一主题 accent 色（变更 2026-09-22，原决策 5A 四色方案已取消）**：
  仿 EQ 曲线、分区底块、状态点、段名标签全部使用主题 accent 色；
  分段辨识依靠分割线 + 段名（L/LM/HM/H）+ 位置本身
- 四频段分区可视化：全高分割线 + 统一 accent 底块（低 alpha）
- 段按钮集合（已定）：S / M / BYP / Δ 四个，命中尺寸 **24×24 px**；EXT 不进显示区（留下面板）
- 按钮放置（已定，方案 D）：6px 状态点常驻（段激活即亮 accent），悬停频段时
  24px 按钮列淡入、移开淡出——同一时刻只出现一列，无碰撞
- 手势分区（已定）：按钮列 [线−40, 线−16] / 抓取带 [线−4, 线+4] / 频段区域 = 其余；
  三带像素零重叠，拖拽归属只认 mousedown 位置，3px 拖拽阈值，
  抓取带内双击 = 精确频率输入
- 传输曲线（选中段的静态 + 动态层，保留家族 CURVE 开关）
- 交叉点拖拽手柄（调整 xover1/2/3，遵守最小带宽约束）
- 输入/输出电平表（样式与家族一致；无 GR 曲线与 GR 读数）

### 7.5 显示区色彩规范（变更 2026-09-22）

> **原决策 5A（段位四色频谱序方案）已取消**：四分区不再用颜色区分，
> 曲线 / 底块 / 状态点 / 段名**全部统一为主题 accent 色**。

- accent 取值随主题切换：Blue `#2F9DFF` / Red `#E05B5B` / Cream `#E07856`
  （即 `CrunchLookAndFeel::accentForTheme()`，与家族完全一致，**无需新增任何色值**）
- 选中段描边同样使用 accent（与底块以 alpha 区分层次）
- 频段辨识方式：分割线 + 段名标签 + 频率位置（不依赖颜色）

### 7.4 BandControlPanel 控制元素（待 UI 设计确认）
- 分区选择器：Low / Low Mid / High Mid / High 四个 Tab / 按钮
- 选中段旋钮行：Threshold / Ratio / Attack / Release
- 选中段底部条：Makeup / Mix + Knee / Detector 选择器
- **EXT 切换按钮**（caption 建议 SIDECHAIN；显示区无 EXT，故此按钮不重复）
  - ⚠️ 原面板的 Solo / Mute / Bypass / Delta 四个镜像按钮**已删除**（变更 2026-09-22）：
    与显示区每段的 S/M/BYP/Δ 按钮**功能完全重复**（同一批参数的第二入口）；
    段状态改由显示区的常驻状态点 + hover 按钮列读取/操作
- 统一控制五握柄 ⊞（Threshold / Attack / Release / Makeup / Mix 各一个；已定 6A）
- 全局控制：Phase Mode / Linear Quality / Crossover Slope / Lookahead
- In / Out 旋钮 + Bypass 按钮

---

## 8. 显示数据需求

| 数据 | 来源 | 更新方式 | 备注 |
|------|------|----------|------|
| 双层频谱（Pre/Post） | SpectrumEngine（移植自 EQ） | 后台线程 16ms 限帧发布 | 512 点对数频率（已定决策 7） |
| 仿 EQ 动态总曲线 | 每段平滑增益 atomic（GR+Makeup）+ 分频权重 | 随增益实时更新 | 动态语义（已定决策 8A） |
| 输入/输出峰值 | processBlock | atomic store | IN = 输入增益前，OUT = 输出后 |
| 传输曲线 | BandCompressor 静态函数 | 参数变化时标记 dirty | 选中段（静态 + 动态层） |
| 频响/交叉点曲线 | 交叉分频器系数 | 参数变化时重算 | 4 条频段响应曲线 |
| 交叉点频率 | APVTS 参数 | 实时 | 可拖拽 |

---

## 9. 已知边界

- 无预设系统（与 EQ / Compressor 一致，宿主会话保存/恢复）
- 主题与窗口尺寸仅存于进程内 atomic，不写入状态数据（与 EQ / Compressor 一致）
- 无 undo/redo、无 A/B、无键盘快捷键（与 EQ / Compressor 一致）
- 无 Mid/Side、无动态 EQ、无 Delta Matrix、无频谱模式
- Minimal Phase 模式在极端参数下的精确行为需在实现阶段验证
- 交叉点最小带宽约束（1 八度）在 UI 拖拽和参数自动化中均需强制执行
- Bypass 记忆值不入序列化：旁路状态下保存会话，重开后 Mix = 0%（§6.6）
- 显示无滚动电平历史（已改双层频谱，决策 7）；电平历史环形缓冲区仅存续供 API 读取
- 双层频谱引入每实例 1 个后台分析线程（EQ 同架构），editor 关闭经 uiActive 门控休眠
