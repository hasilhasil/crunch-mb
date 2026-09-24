# Phase 4 UI 调研报告：Pro-MB / Future MB 动画与交互特性

> 整理时间：2026-09-23　用途：指导 Crunch MB Phase 4 剩余工作 + Phase 5
> 来源：FabFilter 官方帮助（Display and workflow / Basic band controls / Spectrum analyzer / Knobs / Overview / Full Screen）、Pro-MB 产品页、Three-Body Technology Future MB 产品页、开源参考（one_MBComp、JUCE free course）

---

## 一、Pro-MB 官方文档提取（FabFilter）

### 1.1 显示层结构（最核心）
| 元素 | 官方描述 | 我们的现状 |
|---|---|---|
| **双层刻度** | "two display scales are drawn: the **yellow scale** ... corresponds to the band curves, range and the yellow overall curve. The **gray scale** is used by the spectrum analyzer and output level meter" | ✅ 已做（P1） |
| **黄色粗曲线** | "the **thick yellow curve shows the overall dynamic frequency response at the present moment**" | ✅ 已做（单线=实时总响应） |
| **Band 区（潜在动态范围）** | "Each band visualizes its **potential dynamic range**" | ✅ 已做（Range 钟型区） |
| **Display Range 档位** | 右上角下拉 ±3/6/12/30 dB；"dragging a curve outside the current range → **will expand automatically**" | ✅ 自动扩档已做；⏳ 档位 UI 待 Phase 5 |
| **Freeze** | Analyzer 设置面板有 Freeze 按钮（冻结最大值） | ❌ 未做 |
| **Pre/Post/SC 三层** | Analyzer 面板可独立开关 pre/post/sidechain 频谱 | ⚠️ 我们只有 Pre+Post（MB 双层），无 SC |

### 1.2 交互（Display and workflow）
| 交互 | 官方描述 | 我们的现状 |
|---|---|---|
| **点击空白创建 band** | 点显示区即建 band，鼠标滚轮调宽度 | ❌（我们是固定 4 段模型，按 UI_SPEC v1.8 决策不做自由建段） |
| **peak dot 纵拖=增益** | "drag vertically to adjust the **gain**" | ✅ 已做 |
| **peak dot 横拖=中心频率** | "or horizontally to adjust the **center frequency**" | ✅ 已做（#6 修复） |
| **双击 peak dot = 文本输入** | "double-click the peak dot, you start editing the gain ... press Tab ... edit the center frequency" | ✅ 已做（自绘输入框 #7） |
| **crossover 线独立拖** | "The crossover lines let you drag each crossover independently" | ✅ 已做 |
| **range line 上下拖** | "The range line lets you easily adjust the Range ... by dragging the line up and down" | ✅ 已做（Range 缘拖动） |
| **双击 crossover/slope/range = 键盘输入** | "double-click the crossovers, slope buttons and range line to enter a value with the keyboard" | ✅ crossover/range 已做；⏳ slope 按钮待 Phase 5 |
| **snap 吸附** | 靠近自动吸附、共享 crossover；hover 出现 Split/Unsnap 按钮 | ❌ 未做（我们有 1 八度钳制） |
| **框选多段** | 背景拖矩形选相邻段；Ctrl/Shift 多选 | ❌ 未做 |
| **hover 出现 solo/mute + slope 按钮** | "When you hover the mouse over a band, additional controls appear" | ✅ 已做（S/M/BYP/Δ 按钮列） |
| **水平缩放** | 拖动底部频率刻度缩放；双击复位 | ❌ 未做（按 UI_SPEC 决策不做） |
| **Full Screen** | 全屏模式做精细调整 | ❌ 未做（Editor 可缩放已做） |
| **bypassed band dim** | "While a band is bypassed, it is **dimmed in the display** and the bypass button **glows red**" | ✅ 已做（dim 45% + 胶囊变色） |

### 1.3 旋钮（Knobs）
- hover 发光 + **值气泡**（name + value）
- **垂直拖**（速度比例=慢拖更精细）、旋转拖（离得远更精细）、**滚轮**、**双击文本**
- **Shift 微调**、**Ctrl+click 复位**
- 文本简写：频率 `1k`、`A4`、`C#3+13`；dB `2x`=+6dB；`50%`
- **Smart Parameter Interpolation**（参数平滑过渡）

### 1.4 频谱（Analyzer）
- Pre/Post/SC 独立开关
- **Resolution**：1024/2048/4096/8192 点
- **Speed**：release 速度（快=动态清晰，慢=便于观察）
- **Tilt**：默认 4.5 dB/oct（≈ 心理声学）
- **Freeze**：停止下落，显示时间最大值
- 用 gray 刻度（−100…0 dB）

### 1.5 其他
- **Undo/Redo、A/B 对比、Copy**（顶栏）
- MIDI Learn
- **Oversampling**（最高 4×）、**Dynamic Phase / Linear Phase / Minimum Phase** 三模式
- GPU 加速、Retina/High-DPI、SSE 优化、64-bit 内部处理
- Resize：Medium/Large/Extra Large + Scaling 50%~300%
- iOS 版

---

## 二、Future MB（Three-Body Technology）提取

### 2.1 与 Pro-MB 相同的部分
- **"abandons the rigid logic of traditional crossovers"** —— 最多 6 个独立动态段，可 snap 成传统 crossover（与 Pro-MB "Think bands, not crossovers" 完全一致）
- 斜率 **6–48 dB/oct** 每段独立
- 三相位模式：Minimum / Linear / **Liquid Phase**（= Pro-MB 的 Dynamic Phase：仅增益变化时加相移，零延迟无预振铃）
- 实时频谱分析、L/R/M/S 监听、侧链监听、外置侧链
- Undo/Redo、A/B、预设浏览器（标签过滤+收藏）、主题皮肤、可缩放、全屏、GPU 加速

### 2.2 Future MB 独有（值得关注的差异化）
| 特性 | 说明 | 对我们的意义 |
|---|---|---|
| **Delta Matrix** | 把一个段的增益 delta 路由去调制其他段（正/反） | 概念新颖；可作为 Crunch MB 未来差异化点（Phase 6+） |
| **每段三级动态** | Gate + Upward/Spike + Compressor/Ducking 可同时开 | 超出我们当前单级压缩 |
| **Dual-Threshold Upward** | 双阈值向上压缩（只抬音乐细节不抬噪声 floor） | 上行压缩实现参考 |
| **Spectral Mode** | 时域/频域连续过渡（SPEC 滑块） | 高级功能 |
| **Clean/Glue/OTT 风格连续 morph** | 三种压缩特性实时混合 | 概念可借鉴为"音色"旋钮 |
| **Hilbert Envelope Detector** | Gate 用 Hilbert 包络检测，防抖动 | Gate 平滑的技术参考 |
| **Priority EQ（侧链聚焦）** | 每段侧链独立 EQ，检测与音频路径分离 | 解决"为触发而缩窄 band"的困境 |
| **Auto-Gain（ITU 1770）** | 实时响度匹配，bypass/调参时音量恒定 | **强烈建议参考**：用户调参时音量跳变是常见痛点 |
| **Compact Mode** | 把复杂预设锁进精简界面+自定义皮肤 | 产品化思路 |
| **实时预设浏览器** | 点击标签过滤、星标收藏 | Phase 5 预设系统参考 |

---

## 三、开源/自制参考（JUCE 生态）

| 资源 | 内容 |
|---|---|
| [one_MBComp](https://github.com/0bi0n3/one_MBComp) | JUCE 多频段压缩开源实现（Low/Mid/High 三段，各带控制），课程作业项目 |
| [The Audio Programer 免费课程](https://www.youtube.com/watch?v=Mo0Oco3Vimo) | 8 小时 JUCE 教程：3 段压缩器 + 频谱分析器（含 UI） |
| [Cycling74 Crossover Filter Design](https://cycling74.com/tutorials/crossover-filter-design-video-tutorial) | 分频滤波器设计（LR 派生思路） |
| [dsp.stackexchange: transparent crossover](https://dsp.stackexchange.com/questions/49686/how-do-you-program-a-completely-transparent-crossover) | LR crossover 平坦求和的数学讨论 |

---

## 四、可复用的动画/交互原则（提炼）

1. **双刻度分离**：曲线/Range 用窄量程（±3/6/12/30），频谱/电平用 −100…0 —— 避免"压缩量看不见"
2. **"potential range" 静态层 + 实时曲线分离**：Range 区作为背景不随压缩动；只有总响应曲线在动（Pro-MB/Future MB 共同范式）
3. **拖出范围自动扩档**：不让曲线被裁切
4. **峰值点只绑稳定基准**：绑 Makeup/基准线而非实时压缩曲线，保证可点击（本轮 #5 已实现）
5. **缓入缓出**：所有显示值走时间常数（频谱 release、Range 拖动、参数 SPI）
6. **hover 浮现控件**：控件不常驻，hover 才出现（保持画面干净）
7. **bypass 视觉降级**：dim + 发光按钮
8. **旋钮四交互模式 + 值气泡 + Shift 微调 + Ctrl 复位 + 文本简写**
9. **Freeze 冻结峰值**：便于读谱
10. **Auto-Gain**：调参时听感音量恒定（Future MB 亮点，Pro-MB 用 Output Level 手动补）

---

## 五、我们与 Pro-MB 的差距清单（按优先级）

| 优先级 | 项 | 说明 |
|---|---|---|
| **P0 已完成** | 双刻度、单实时粗线、Range 静态层、频点绑基准、横拖中心频率、双击输入、BYP dim | 本轮全部落地 |
| **P1 Phase 5** | 全局选择器下拉菜单（Phase/Slope/Quality/Lookahead/Display Range） | 当前是静态文字 |
| **P1 Phase 5** | 段参数旋钮组（Threshold/Range/Attack/Release/Output/Ratio/Knee）+ LINKED 抽屉 | 用户已确认这是缺失的核心 |
| **P2 Phase 5** | Undo/Redo、A/B 对比 | 顶栏图标占位 |
| **P2** | Freeze（频谱峰值冻结）、Analyzer 设置面板（Speed/Tilt/Resolution） | 读谱体验 |
| **P2** | 参数平滑（SPI）：切段/切 preset 时曲线不跳变 | Pro-MB "Smart Parameter Interpolation" |
| **P3** | Auto-Gain（ITU 1770 响度匹配） | 借鉴 Future MB |
| **P3** | snap 吸附 + Split/Unsnap hover 按钮 | Pro-MB 招牌交互（我们已用 1 八度钳制替代） |
| **P3** | 预设浏览器（标签过滤/收藏） | 产品化 |
