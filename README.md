# Crunch MB

Crunch MB 是四频段动态处理插件，提供每段独立的压缩/扩展、内外侧链、并行混合和联动控制。界面以大面积频谱为中心，底部为单行 Global 栏，支持宽高独立调整和 Blue / Red / Cream 三种主题。

当前版本：**v0.1.4**（测试版），2026-10-22。交付格式为 Windows x64 **VST3** 和 **Standalone**。

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
2. 点击 LOW / LOW MID / HIGH MID / HIGH 频段或频谱中的相应区域，打开该段面板。
3. 拖动三个分频边界，确定各段处理范围。默认分频点为 **200 Hz / 1 kHz / 6 kHz**。
4. 在 Compress 模式下，把 Range 设为负值，降低 Threshold，并设置 Ratio、Attack、Release。
5. 按需要使用 Makeup 调整该段电平，使用单段 Mix 做并行混合。
6. 用 S 单独试听频段，用 B 对比处理前后；结束试听后检查 S、M、Δ 和 Monitor 的状态。

## 频段控制

点击频段打开半透明面板；点击面板外关闭。六个主旋钮为：

| 参数 | 范围 | 用途 |
|---|---|---|
| Threshold | −60～0 dB | 动态处理的触发阈值 |
| Range | −30～+30 dB | 限制增益变化幅度，并参与选择阈值上方/下方的处理方向 |
| Attack | 0.1～200 ms | 增益变化的起动时间 |
| Release | 10～1000 ms | 增益恢复时间 |
| Makeup | −36～+36 dB | 单段补偿增益 |
| Mix | 0～100% | 该频段干声与处理声的比例 |

下方提供 **Compress / Expand**、**Ratio**、**Knee** 和 **INT / EXT**。

- **Ratio：1:1～20:1**。左右拖动滑杆调整压缩/扩展曲线的比率。
- **Knee：0～48 dB**。0 为硬拐点，增大后阈值附近的过渡更平滑。
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
- **Δ（Delta）**：试听该段处理声相对干声的差值，包含该段 Mix 和 Makeup 的影响。它与 OUTPUT 中的 Gain Reduction 监听不同。

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
- **Speed**：Fast / Medium / Slow，设置频谱显示的衰减速度。
- **Tilt**：0 / 1.5 / 3 / 4.5 dB/oct，改变频谱显示倾斜度。
- **Freeze**：冻结频谱显示。

这些设置改变分析显示，不直接改变声音。右侧为输入、输出电平表。顶栏量程按钮设置动态曲线显示范围；Auto 自动选择范围，不是 Auto Gain。右上角圆形图标打开 Blue / Red / Cream 主题菜单。

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
| Gain Reduction | 四段增益包络的平均衰减量直接作为音频输出 |

**Gain Reduction 是已知的未完善功能，暂时保留。** 它没有捕捉“被压掉的音频”，而是把四段包含 Makeup 的线性增益取平均，计算 `0.1 × (1 − 平均增益)` 并限幅。结果通常近似直流或缓慢变化的控制信号，可能几乎听不到，不能替代真正的 Delta 试听。

## 状态保存与兼容性

处理参数与 Analyzer 设置通过宿主工程状态保存。主题和窗口尺寸目前不作为工程状态保存，重新创建编辑器会采用默认主题/尺寸。

本版沿用既有参数 ID 和状态数据格式，**更换 VST3 类标识**，不再声明替代旧 VST2/VST3 身份。旧工程、FL 收藏 `.fst` 和包含旧身份的预设不保证自动识别，需要重新扫描并从新条目加载。参数数据格式保持兼容不等于宿主会自动把旧实例迁移到新身份。

从 v0.1.4 起固定的产品代码为 `K7mB`；Windows VST3 音频组件 CID 为 `ABCDEF019182FAEB4B696C6F4B376D42`。后续版本保持这一身份不变。构建末尾检查实际二进制工厂生成的元数据，身份发生意外变化时构建失败。

## 版本记录

### v1.0.4 — 审计修复更新（DSP 稳定性、PDC 延迟对齐与 UI 自动化完善）

- **跨线程与变长缓冲安全**：预分配最大 4x 过采样缓冲与双过采样器实例，为线性相位 FIR 核更新引入双缓冲与独立 FFT，修复播放中切换过采样/线性相位质量或拖动分频点的并发竞争；修复 FL Studio 变长块下过采样上采样未截断子块及外部侧链越界读取问题。
- **PDC 延迟与相位对齐**：统一过采样下分频器与 Lookahead 的 1× 宿主采样率延迟换算；将线性相位 `FirFilter` 改为 `outFifo_` 预填零样本架构，彻底消除变长块下的延迟漂移与欠载；全局 `Mix` 与 `Monitor = Input` 干声参考对齐分频相位与 Lookahead 延迟。
- **动态与响度算法修正**：修复向上压缩（`Range > 0`）与扩展模式（`Expand`）下 Attack / Release 时间常数反置问题；支持 `StereoLink < 100%` 时右声道驱动表头与动态曲线；`LoudnessMeter` 改为双通道独立滤波并按 400ms 时间窗积分，关闭 Auto-Gain 时平滑释放增益。
- **UI 交互与宿主自动化**：为全部面板旋钮、滑杆及画布拖拽补齐 `beginChangeGesture` / `endChangeGesture`；统一单段 Bypass 判定阈值消除 `Mix < 50%` 时的按钮死锁；修复同帧多参数变更时 Popover 漏同步、中频段中心拖拽撞墙挤压带宽、`ValueField` 非法输入失焦残留等问题。详见 [`audit/AUDIT_FIX_CHANGELOG.md`](audit/AUDIT_FIX_CHANGELOG.md)。

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
