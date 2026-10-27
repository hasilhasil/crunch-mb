# Crunch MB

Crunch MB 是四频段动态处理插件，提供每段独立的压缩/扩展、内外侧链、并行混合和联动控制。界面以大面积频谱为中心，底部为单行 Global 栏，支持宽高独立调整，内置 **Classic（经典平面）** 与 **Neumorphism（新拟物）** 两套主题包、`Blue / Red / Cream / NeuLight / NeuTeal / NeuDark` 六套配色包以及中英双语切换。

当前版本：**v0.1.5**（测试版），2026-10-27。交付格式为 Windows x64 **VST3** 和 **Standalone**。

**从 v0.1.3 或更早版本升级：本版更换了插件身份，必须进行验证扫描。** 针对 FL Studio 扫描后打开到其他插件的反馈，详见随包的 [FL Studio 更新与重新扫描说明](FL_STUDIO_RESCAN.md)。旧工程及旧收藏条目不保证自动迁移；升级前保留工程副本与旧安装包。

本文按当前源码说明实际行为；历史设计和研究文档用于记录设计过程，不代表每项功能均已实现或与其他产品完全一致。

## 安装与启动

1. 安装包中 `VST3/Crunch MB.vst3` 是完整目录包。复制整个目录，保留 `Contents` 内的文件结构。
2. Windows 标准 VST3 目录为 `C:\Program Files\Common Files\VST3\`。也可放入宿主支持的自定义 VST3 扫描目录。
3. 更新前完全退出占用插件的宿主，再覆盖文件。重新启动宿主；若列表仍是旧版本，再重新扫描插件。
4. 在音轨、总线或主输出的效果器插槽中加载 **Crunch MB**。

独立版位于安装包的 `Standalone/Crunch MB.exe`。其音频输入/输出取决于设备设置与接线，不会自动接收 DAW 音轨。

默认窗口 **1200×780**；最小 **920×560**；最大 **2080×1560**。拖动窗口右下角调整大小。宽高可独立变化，额外空间主要分配给频谱；控件保持统一缩放和可读的下限。

## 快速开始

1. 保持 Monitor 为 **Output**、全局 Mix 为 **100%**，先关闭 Auto Gain，便于判断原始处理效果。
2. 点击主频谱中的对应频段区域或段名（LOW / LOW MID / HIGH MID / HIGH），打开该段参数面板；也可在顶栏预设选择窗口从二级分类菜单中加载预设，并用右侧 **A | B** 按钮一键对比两套参数状态。
3. 拖动三个分频边界，确定各段处理范围。默认分频点为 **200 Hz / 1 kHz / 6 kHz**。
4. 在 Compress 模式下，把 Range 设为负值，降低 Threshold，并设置 Ratio、Attack、Release。
5. 按需要使用 Makeup 调整该段电平，使用单段 Mix 做并行混合。
6. 用 S 单独试听频段，用 B 对比处理前后；结束试听后检查 S、M、Δ 和 Monitor 的状态。

## 频段控制

点击频段打开面板；点击面板外关闭。六个主旋钮（均支持拖动、滚轮、Shift 细调、Ctrl+单击复位及双击直接输入数值）为：

| 参数 | 范围 | 用途 |
|---|---|---|
| Threshold | −60～0 dB | 动态处理的触发阈值 |
| Range | −30～+30 dB | 限制增益变化幅度，并参与选择阈值上方/下方的处理方向 |
| Attack | 0.1～200 ms | 增益变化的起动时间 |
| Release | 10～1000 ms | 增益恢复时间 |
| Makeup | −36～+36 dB | 单段补偿增益 |
| Mix | 0～100% | 该频段干声与处理声的比例 |

下方提供 **Compress / Expand**、**Ratio**、**Knee** 和 **INT / EXT**。

- **Ratio：1:1～20:1**。左右拖动滑杆调整压缩/扩展曲线的比率，或**双击数值区域直接输入精确比率**（如 `4.5`、`8:1`），Enter 确认、Esc 取消。
- **Knee：0～48 dB**。0 为硬拐点，增大后阈值附近的过渡更平滑；支持左右拖动滑杆或**双击直接输入精确 dB 值**（如 `12`、`18.5`）。
- **INT / EXT**：使用内部输入或宿主送入的外部侧链。外部侧链必须在宿主中路由；未获得有效外部输入时会回退到内部检测。
- **Compress / Expand**：Expand 反转 Compress 的目标增益方向。

Range 的实际方向如下，增益变化均受 `|Range|` 限制：

| 模式 | Range 为负 | Range 为正 |
|---|---|---|
| Compress | 阈值以上衰减，向下压缩 | 阈值以下抬升，向上压缩 |
| Expand | 阈值以上抬升，向上扩展 | 阈值以下衰减，向下扩展 |

Range 为 0 时不产生动态增益变化；Makeup 等其他控制仍可改变输出。

### S / M / B / Δ

- **S（Solo）**：单独试听选中的频段。
- **M（Mute）**：静音该频段。
- **B（Bypass）**：把该段 Mix 设为 0；取消时恢复先前 Mix。它是 Mix 宏，不是卸载分频器。
- **Δ（Delta）**：试听该段处理声相对干声的差值，包含该段 Mix 和 Makeup 的影响。

### EXPERT

| 控制 | 选项/范围 | 说明 |
|---|---|---|
| Detector | Peak / RMS | 峰值检测或均方根检测 |
| Lookahead | Off / 1 ms / 3 ms | **全局参数**，在任何频段修改都会作用于整个插件 |
| SC Filtering | Band / Free | Band 使用该频段检测范围；Free 使用以下高通/低通设置 |
| High-pass | 20～2000 Hz | 自由侧链检测的高通截止频率 |
| Low-pass | 500～20000 Hz | 自由侧链检测的低通截止频率 |

高通、低通字段支持拖动、滚轮和双击输入，例如 `173 Hz`、`7.3k`。Enter 确认，Esc 取消；非法文本不改变当前值，输入会限制在有效范围内。Detector、Lookahead 等离散选项使用分段按钮选择。

## Linked Control

顶栏 **LINKED CONTROL** 打开全四段联动面板，排列为：

**Threshold / Range / Attack / Release / Makeup / Mix**。

前五项是相对偏移，**Mix 是四段统一的绝对值**。例如四段 Attack 为 10 / 20 / 30 / 40 ms，将 Linked Attack 调到 +50 ms，结果为 60 / 70 / 80 / 90 ms。

联动时，每段 S/M/B/Δ 下方暂显实际结果，停止操作后约 2.2 秒淡出。每段仍遵守参数上下限，到达边界时会限幅。面板没有“锁定”功能。

## Global 栏

| 控制 | 作用 |
|---|---|
| Phase | Natural Phase / Minimal Phase / Linear Phase，选择分频方式 |
| Slope | 12 / 24 / 36 / 48 dB/oct，选择分频斜率 |
| Quality | Low / Medium / High，设置 Linear Phase 的处理质量 |
| Lookahead | Off / 1 ms / 3 ms，前瞻时间 |
| Oversampling | Off / 2x / 4x，过采样倍数 |
| Analyzer | 打开频谱分析设置 |
| OUT | 打开全局输入/输出与监听设置 |
| Bypass | 四段 Mix 统一设为 0，取消时恢复各段先前 Mix |

Linear Phase、Lookahead 和 Oversampling 会影响延迟；插件向宿主报告延迟，由宿主进行补偿。Quality 用于 Linear Phase；提高质量或过采样倍数通常会增加资源开销。

顶栏电源开关对应处理器 Enabled，与底部通过四段 Mix 实现的 Bypass 宏不同。

## Analyzer 与电平表

- **Pre / Post**：分别显示处理前和处理后的频谱。
- **Resolution**：Low / Medium / High / Maximum，对应 1024 / 2048 / 4096 / 8192 点 FFT。
- **Speed**：Fast / Medium / Slow（单级指数衰减时间常数 $\tau = 100 / 200 / 400\text{ ms}$，配合 3 点空间核平滑），设置频谱显示的回落速度。
- **Tilt**：0 / 1.5 / 3 / 4.5 dB/oct，改变频谱显示倾斜度。
- **Freeze**：冻结频谱显示。

这些设置改变分析显示，不直接改变声音。右侧为输入、输出电平表。顶栏提供二级分类预设选择窗口、**A | B** 状态对比按钮（默认 **A** 状态，点击切换至 **B** 状态并独立保存两套参数快照）以及动态曲线量程选择按钮（Auto 自动选择显示范围，不是 Auto Gain）。右上角三滑杆设置图标打开 **Theme（主题）** 菜单卡片，支持实时切换主题包、6 套配色包、布局包、中英语言包、一键回滚与 **MIDI Learn**，且用户手动更改的 UI 主题配置会自动持久化保存，下次打开插件时自动保持上次选择的主题与配色。

## OUTPUT 与全局 MIX

OUTPUT 包含 Input Gain、Output Gain、Mix、Global Bypass、Auto Gain 和 Monitor。

- **Input Gain / Output Gain**：均为 −36～+36 dB。
- **Mix**：0～200%，精度 0.1%。
- **Auto Gain**：根据输入与处理后的响度差进行自动电平补偿，默认关闭。
- 连续参数单击选中并高亮，拖动或滚轮调整；Shift 用于微调，双击输入，Enter 确认、Esc 取消。Ctrl+单击旋钮恢复其默认值。

### MIX 为什么能到 200%？

当前实现先从干声过渡到处理声，再保持处理声并逐渐叠加干声。设干声为 D、处理声为 W、旋钮百分比除以 100 为 m：

```text
0%～100%：输出 = (1 − m) × D + m × W
100%～200%：输出 = W + (m − 1) × D
```

| MIX | 混合结果 |
|---:|---|
| 0% | 只有干声 |
| 50% | 0.5 × 干声 + 0.5 × 处理声 |
| 100% | 只有处理声 |
| 150% | 处理声 + 0.5 × 干声 |
| 200% | 处理声 + 完整干声 |

**200% 不表示压缩强度翻倍**，而是并行叠加。若干声与处理声完全一致且同相，200% 的幅度可比 100% 高约 6 dB；相位、延迟和动态处理会影响实际结果。

干声参考取自分频后、Lookahead 延迟后的四段未压缩信号之和，处理声取自四段求和之后；混合发生在 Auto Gain 和 Output Gain 之前。干声支路与处理声具备相同的分频相位和 Lookahead/Linear Phase 延迟，并行混合时不会产生延迟回声或梳状滤波抵消。

### Monitor 监听哪里？

Monitor 会用所选信号替换正常输出。常规使用及导出前应检查其为 **Output**。

| 选项 | 当前实际来源 |
|---|---|
| Output | 正常处理输出 |
| Input | 与处理声相位/延迟对齐的未压缩干声 |
| Band 1～4 | 对应频段分频后的信号，再应用 Output Gain；不是该段最终动态处理输出 |

## 状态保存与兼容性

处理参数、预设索引、A/B 槽位、Analyzer 设置与四包正交 UI 组合状态（Theme / Palette / Layout / Language）均通过宿主工程状态保存；同时用户手动修改的 UI 主题、配色、布局与语言偏好会自动写入本地用户配置文件（`%APPDATA%\Kilo\CrunchMB\ui_preferences.xml`），下次在宿主中新建或重新打开插件时将自动保持上次选择的主题与配色。窗口尺寸采用默认尺寸或宿主保存尺寸。

本版沿用既有参数 ID 和状态数据格式，**更换 VST3 类标识**，不再声明替代旧 VST2/VST3 身份。旧工程、FL 收藏 `.fst` 和包含旧身份的预设不保证自动识别，需要重新扫描并从新条目加载。参数数据格式保持兼容不等于宿主会自动把旧实例迁移到新身份。

从 v0.1.4 起固定的产品代码为 `K7mB`；Windows VST3 音频组件 CID 为 `ABCDEF019182FAEB4B696C6F4B376D42`。后续版本保持这一身份不变。构建末尾检查实际二进制工厂生成的元数据，身份发生意外变化时构建失败。

## 版本记录

### v0.1.5 — 2026-10-27（测试版：声音/交互重构、视觉 UI 升级与全量代码审计修复汇总）

本版本汇总了全量代码静态审计（`audit/BUG_AND_LOGIC_AUDIT.md` & `audit/AUDIT_FIX_CHANGELOG.md`）以及多轮实测交互与视觉打磨的全部修复与功能更新：

#### 1. 频谱、电平表、动态曲线与拖拽跟手性修复
- **频谱分析回落公式与平滑重构**：采用单级一阶指数衰减模型（Fast / Medium / Slow 对应 $\tau = 100 / 200 / 400\text{ ms}$）配合 3 点空间核平滑（`0.25 / 0.50 / 0.25`），消除原先 `10 / 20 / 60 ms` 造成的频谱剧烈闪烁跳动，同时避免二阶串联滤波造成的峰值顶部悬停粘滞。
- **右侧 IN / OUT 电平表平滑弹道**：将右侧输入/输出音量条从原先的瞬时跳顶 + `60 dB/s` 急坠改为一阶指数弹道平滑（起音 $\tau_{\text{attack}} = 35\text{ ms}$、释放 $\tau_{\text{release}} = 220\text{ ms}$），消除音频块峰值波动造成的电平条高频抖动。
- **Band 二级菜单及浮层面板关闭淡出动画同步**：修复 `BandPopover`（及 `LinkedDrawer` / `OutputPanel` / `AnalyzerPanel`）在关闭瞬间因 `if (!open_)` 提前返回导致面板背景第 1 帧瞬间消失、而内部 6 个旋钮子组件仍在继续淡出造成的“旋钮悬空淡出”脱节 Bug，使背景面板、滑杆与全部子控件在关闭过程中共享同一 `anim_` 曲线同步丝滑淡出。
- **EQ 动态曲线与 Range 上边界完全同步**：统一 `bandRawDb_` 与 `bandDisplayDb_` 为单阶同源平滑，并将 `refreshCurvePaths()` 中 Range 填充区的上边界 `yTop` 直接绑定至当前 EQ 曲线 `curveDb_`，解决移动曲线或动态压缩时 Range 上缘与 EQ 主线脱节的问题。
- **分频边界与 Makeup 拖拽零滞后跟手**：修复 `uiWakeFromParam` 在高频鼠标移动中无条件调用 `startTimerHz(60)` 重置定时器倒计时导致 `timerCallback` 饥饿停更的 Bug；`MBBackend::setCrossovers` 改为走 O(1) 参数缓存并仅在数值变化时触发通知；新增 `syncCurveImmediate()`，在拖动分频线、中心频点、Makeup 与 Range 时当帧立即重算权重并跳过平滑延迟。
- **移除未完善的 `Gain Reduction` 监听档位**：从 `Parameters`、`OutputPanel` 与 `MultibandDSP` 中移除 `OUTPUT → 监听信号 → Gain Reduction`，保留 `Output / Input / Band 1..4` 共 6 个有效监听信号源。

#### 2. 顶栏预设系统、A/B 对比、二级菜单与主题体系升级
- **顶栏二级分类预设选择器与 `Save As...` 按钮**：移除顶栏原四个频段按钮，新增带二级子菜单的预设选择窗口（4 大分类 × 3 项预设 = 12 项多段动态预设，支持中英双语与悬停展开），并在二级菜单底部绘制 `Save As...`（`另存为...`）按钮。
- **顶栏 `A | B` 状态对比按钮**：紧邻预设选择器右侧新增 `A | B` 双字母对比按钮，默认处于 `A` 状态并以当前主题强调色高光显示，点击切换至 `B` 状态并独立保存/恢复两套完整插件参数快照。
- **右上角新设置图标与统一 `Theme（主题）` 菜单**：重绘右上角按钮为三道错落调节滑杆矢量图标；移除旧版 4 行主题下拉框，仅保留新版四包正交主题卡片（标题统一为 `Theme` / `主题`），并将 **MIDI Learn** 开关整合至卡片底部；用户手动修改的主题、配色、布局与语言自动持久化至本地用户配置，下次打开插件时自动保持上次设置。
- **Band 二级菜单 `Ratio` 与 `Knee` 双击精确输入**：支持在频段二级菜单中双击 `Ratio`（`1.0～20.0`）或 `Knee`（`0.0～48.0 dB`）直接键入精确数值（Enter 确认、Esc 取消、点击外部自动提交），交互体验与双击旋钮完全一致。

#### 3. 视觉细节、旋钮几何、新拟物阴影与字体统一
- **经典平面主题旋钮指针几何修正**：修复 Classic 主题下旋钮指针越出内部圆帽（`capR`）边界刺入外环的问题，将指针严格收于内圆边界之内，并为指针靠近内圆边界的外端添加平滑圆角。
- **新拟物（Neumorphism）左上高光柔化与 `S / M / B / Δ` 一体化内凹底座**：收敛 `ThemeTokens` 中浮层卡片与按钮的 `lightAlpha` 高光权重，将 `NeuPainter::drawRaisedRect` 升级为 6 层平方衰减柔化核；为选中频段的 `S / M / B / Δ` 按钮增加一体化内凹胶囊底座槽与微凸键帽，拉开按钮净间距，彻底消除相邻白影穿透粘连。
- **Classic 主题全 6 套配色浮层半透明统一 & 频点信息浮窗放大**：将 `NeuLight / NeuTeal / NeuDark` 的 `popA` 透明度统一为 `0.68 / 0.62`，使经典主题下全部 6 套配色的频段浮层均具半透明质感；放大频点上方浮出信息窗口（`156×58`）并跟随当前激活主题与配色包。
- **全局字体统一为 `Google Sans Flex` 与中文排版校准**：内嵌 `Google Sans Flex`（Regular / Medium / SemiBold / Bold）替换原混用的 Poppins 与 Barlow；保持左上角 `CRUNCH MB` 品牌标题不变，在中文模式下对标签字面做 `0.92x` 视觉比例与字重均衡校准。

#### 4. 源码审计（Audit P0–P3）核心底层修复汇总
- **跨线程安全与变长缓冲防护（P0）**：在 `MultibandDSP::prepare()` 中按最大 4x 过采样预分配全部缓冲与双过采样器实例；为 `LinearPhaseCrossover` 的 `FirFilter` 引入独立 `designFft_` 与双缓冲频域核原子切换；修复 FL Studio 变长块下过采样上采样未截断子块、外部侧链越界读取（`0xC0000005`）及超大块回退问题；修复 `SpectrumEngine` 频率轴并发读写、停止播放后 200Hz 无限发布风暴及 `Freeze` 状态丢失；修复单声道主总线 + 立体声侧链通道映射。
- **PDC 延迟补偿、相位对齐与动态算法（P1）**：统一 2x/4x 过采样下分频器与 Lookahead 的 1× 宿主采样率延迟换算，且在插件总开关 `enabled = false` 时将上报延迟归零；`FirFilter` 改为 `outFifo_` 预填零样本架构，彻底消除变长块下的延迟漂移与欠载；全局 `Mix` 与 `Monitor = Input` 干声参考取自分频与 Lookahead 延迟后的四段未压缩信号之和，消除并行混合梳状滤波；修复向上压缩（`Range > 0`）与扩展模式（`Expand`）下 Attack / Release 系数选反问题；`LoudnessMeter` 改为双声道独立 K 加权滤波并按真实 400ms 时间窗回溯积分，关闭 Auto-Gain 时按 25ms 平滑释放增益。
- **宿主自动化手势、状态同步与 API 健壮性（P2/P3）**：为全部旋钮、滑杆及画布拖拽补齐 `beginChangeGesture` / `endChangeGesture`；统一单段 Bypass 判定阈值（`Mix <= 0.5%`）消除 `Mix < 50%` 时的按钮死锁；修复同帧多参数变更时 `BandPopover` 漏同步、横向拖动中频段中心频点撞墙挤压带宽、`Param::clampCrossovers` 极端逆序入参断言失败及 `ValueField` 非法输入失焦残留等问题。详见 [`audit/AUDIT_FIX_CHANGELOG.md`](audit/AUDIT_FIX_CHANGELOG.md)。

### v0.1.4 — 2026-10-22（测试版）

- 针对 FL Studio 25.1.6 中 Crunch MB 被关联到 imagiro piano 的报告，重置并固定独立 VST3 产品身份。
- 关闭未发布的 VST2 替代兼容模式，不再暴露旧组件的兼容替代关系。
- 新增构建身份检查和 FL 重新验证扫描说明；旧工程需要留存备份并检查实例迁移。
- 本机没有报告者的 imagiro piano 文件及扫描数据库，完整 CID 重复、FL 内部标识碰撞或缓存误关联的具体原因尚未取得直接证据。需要报告者按更新说明复测。

### v0.1.3 — 2026-10-22（测试版）

- 修复 RATIO 拖动立即跳到 20:1 且无法调回的问题。
- RATIO 与 KNEE 拖动跨度统一采用实际可见滑轨宽度。
- 主题图标以按钮中心定位，三个圆点均匀分布。
- 补充使用说明、全局 MIX 算法和 Monitor 实际信号来源。

### v0.1.2 — 2026-10-22（测试版）

- 默认窗口加宽为 1200×780。
- 重排并增大 Analyzer / OUTPUT 菜单文字，完善 OUTPUT / EXPERT 精确输入。
- 修复 Auto 乱码；Linked Control 加入 Range 和各段实际值暂显。

### v0.1.1 — 2026-10-22（测试版）

- 修复 VST3 插件身份与既有安装不一致导致的宿主加载问题。
- 接入确认后的原生界面：大频谱、顶栏 Linked Control、单行 Global、半透明菜单和 Barlow UI 字体。

## 从源码构建

依赖 CMake 3.24+、支持 C++20 的编译器和 JUCE 8.0.4。默认共享兄弟目录 `../260916 Crunch Compressor/JUCE-8.0.4` 和其中的 Poppins 字体；Barlow 位于本仓库 `src/ui/Fonts`。单独复制仓库时需补齐这些依赖。

```powershell
cmake -S . -B build
cmake --build build --config Release --target CrunchMB_VST3 CrunchMB_Standalone --parallel 4
```

JUCE 位于其他位置时，配置时传入 `-DJUCE_DIR="实际 JUCE 路径"`。Poppins 共享路径由 `CMakeLists.txt` 中的 `FONT_DIR` 指定。

```text
build/CrunchMB_artefacts/Release/VST3/Crunch MB.vst3/
build/CrunchMB_artefacts/Release/Standalone/Crunch MB.exe
```

构建不会自动覆盖系统 VST3 目录。更新时退出宿主，再复制完整插件包。

RATIO 修复的定向检查：

```powershell
cmake --build build --config Release --target CrunchMBTests --parallel 4
$ratioSnapshots = Join-Path (Get-Location) '_review\ratio_drag_20261022'
& '.\build\CrunchMBTests_artefacts\Release\CrunchMBTests.exe' --ratio-drag-check $ratioSnapshots
```

v0.1.3 的 RATIO 修复检查结果为 **84 项通过、0 项失败**，覆盖四段、默认/最小窗口、原位拖动、正反方向、20:1 回退、上下限和拖动释放，并输出三种主题截图。v0.1.4 身份修复不重复运行这套界面检查。历史测试和设计记录位于 `tests/` 与 `_review/`。

字体许可证见 `src/ui/Fonts/OFL.txt` 及字体随附的许可文件；JUCE 的使用和分发须遵守其适用许可证。
