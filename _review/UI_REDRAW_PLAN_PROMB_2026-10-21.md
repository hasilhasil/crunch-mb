# Crunch MB：接近 Pro-MB 的 UI 重绘计划

日期：2026-10-21。依据当前 Crunch MB 源码、仓库历史文档、现有 UI 审计、目标 Pro-MB 的 REA 资源证据/原生引用、FabFilter 官方交互说明制定。本文件是实施计划，没有开始修改插件源码。

## 1. 重绘目标

保留 Crunch MB 的 Blue / Red / Cream、Poppins 字体与品牌；把版式改为接近 Pro-MB 的 **大主图、选中频段浮层、右侧窄输出表、紧凑顶栏与底栏**。原先固定控制台方向不作为本次默认布局。

优先做到打开窗口就能看懂当前选中段、处理范围、实际动态响应和输出；选中频段后，在主图下部就近调节；高频操作不进入深层菜单。原始 FabFilter 位图仅用于分析，发布版本使用重新绘制的 Crunch MB 控件。

## 2. 已有配色保留方式

| 主题 | 背景 | 面板 | 表面 | 主强调色 | 保留与调整 |
|---|---|---|---|---|---|
| Blue | `#101214` | `#1a1d20` | `#22262b` | `#2f9dff` | 保留冷色暗底与蓝色；降低未选频段存在感 |
| Red | `#101214` | `#1a1d20` | `#22262b` | `#e05b5b` | 保留红色；选中高亮与 clipping 通过图标/文字/形状区分 |
| Cream | `#f6f0e7` | `#efe8db` | `#fffdf8` | `#e07856` | 保留浅暖底；独立校验网格、阴影、禁用态的对比度 |

深色主题正文 `#d8d8d8`、次要文字 `#9aa0a6`，浅色主题正文 `#463f38`。保留现有浅色旋钮中心，重做环形刻度与状态，不照搬 FabFilter 的黄钮和每段六色体系。

同主题四段用编号、轮廓、明暗、选择描边区分。总响应使用清晰连续线，频谱用中性低对比填充，Range 用更轻的区域/交互线，实际动态变化用强调线和明确读数；避免所有图层都使用同等亮度和厚度的强调色。

## 3. 布局草案

以下尺寸是 **Crunch MB 的设计建议**，不是从 Pro-MB 反编译出的精确坐标。

```text
┌ Crunch MB · preset ───────── Undo/Redo · A/B · Theme · Help ┐
│ gain dB             大型交互主图                 level dB │
│        频谱 / 4 段区域 / 分频线 / Range / 节点      │ L R │
│                       当前动态总响应              │     │
│              ▽ 指向所选频段                      │     │
│  ┌ Threshold · Range · Attack · Release · Makeup ┐ │     │
│  │ Ratio / Knee · Compress/Expand · Mix · Expert │ │     │
│  └──────────────────────────────────────────────┘ │     │
│ 20Hz …………………………… 对数频率刻度 ……………………… 20kHz │ peak │
├ Phase · Slope · Quality · LA · OS · Analyzer · I/O · Size ┤
```

- 标准稿按 1100×700 逻辑像素设计，同时保留 920×620 紧凑稿；现有 700×500 最小窗口也要有完整可用布局，不能靠压小全部文字撑住。
- 标准稿顶栏约 48–50、高度约 32–36 的底栏，右侧输出表约 42–52 宽。主图的面积和视觉重心最大。
- 基本浮层建议约 640×200；参数主控按功能排布，Threshold 最突出，Range 次之，Attack/Release 与 Makeup 清晰成组。Mix 保留为 Crunch 的真实每段功能。
- Expert 在宽窗口向右扩展；窄窗口改为同一面板中的第二行。展开不能挤压旋钮、覆盖底栏或让控制器跑出窗口。
- 浮层箭头关联选中段；面板水平位置按图内边界限制，最左/最右段也完整可见。框选/联动时显示所有被控段编号。
- 字号按真实编辑器测量；基本标签目标 11–12、数值 13–15 逻辑像素。关键可点击区域建议至少约 24×24，旋钮实际命中范围清晰。避免旧版 8.5px 密集高级行。
- 这些尺寸进入 P2 原型验证后冻结；内容放不下时使用分组/折叠/溢出菜单，不能随机缩放各个局部。

## 4. Pro-MB 行为映射与边界

| Pro-MB 参照 | Crunch MB 本轮方案 | 需要后端另立项的部分 |
|---|---|---|
| 主图自由频段 | 保持固定四段与三个分频点；节点竖拖改该段 Makeup，分频拖动边界明确 | 自由创建/删除、六段、频谱留空、独立中心/带宽 |
| 选择驱动的频段面板 | 选择频段后显示并关联；切换频段保持清晰的指向反馈 | 无需先改音频拓扑 |
| 多选同调 | 基于现有 LINKED 规则扩展统一的 selection 集合；多值显示 Mixed；保留差值调整规则 | 对超界参数的组调策略需明确，不可静默抹平差异 |
| Threshold 检测环 | 仅展示实际可取得的检测信号；没有该数据则先补安全的 DSP→UI telemetry | 不能拿 GR 冒充输入检测电平 |
| Attack/Release 百分比 | 保留当前实际 ms 参数与单位 | 改为百分比需重新定义时间映射及兼容，不在纯重绘中伪装 |
| 逐段 Lookahead + 全局开关 | 现有全局 Off/1ms/3ms 统一放底栏 | 逐段 Lookahead 与延迟报告 |
| Stereo Link + M/S | 把现有 0–100% Stereo Link 真正接入 Expert | Mid-only/Side-only、M/S Pan；不能画无效外环 |
| Expert 开关 | 本轮定义为显示高级控件，折叠不修改声音；标签/帮助说明此行为 | 若要完全复制“关闭即禁用专家处理”，需另加状态与迁移方案 |
| Analyzer/Output hover + pinned | 各自实现 hidden/hover/pinned 状态与安全鼠标路径 | 精确延时、互斥行为待目标宿主测量 |
| Global Mix 0–200% | 先明确现有并行混合语义；要贴近 Pro-MB 则单独实现效果量缩放并做迁移 | 当前 >100% 额外叠干声算法与 Pro-MB 描述不同 |
| 处理模式名称 | 保留现有 Natural/Minimal/Linear 真实名称 | 未实现 Dynamic Phase 时不可改名为 Dynamic Phase |
| MIDI Learn | 从正式入口移除当前演示映射，或另立项实现真实 CC 输入和映射持久化 | 不保留“可点但没有真实学习”的功能 |

## 5. 分阶段计划表

| 阶段 | 优先级 | 具体工作 | 主要改动位置/产物 | 完成标准 |
|---|---|---|---|---|
| P0：冻结基线与功能契约 | 必须先做 | 保存旧预设/自动化状态、三主题/尺寸截图；列出参数 ID、单位、范围、作用域；完成目标宿主的 UI 测量清单 | `Parameters.*`、状态保存路径；功能映射表、目标运行测量表 | 每个新控件均对应真实参数/动作；不以过时 md 的勾选项作为完成证据 |
| P1：修复操作结果 | 必须先做 | 修 Mix 旁路误判、Ratio 起手跳值、Shift 负方向、Lookahead 显示和缓存、批量参数同步；补开始/结束宿主手势；替换点击循环数值 | `BandPopover.cpp`、`DisplayView.cpp`、`OutputPanel.cpp`、`api/MBBackend.*`、旋钮组件 | Ratio 4:1 零位移仍为 4:1；Shift 向下确实减小；Mix=30% 不显示旁路；宿主写多个参数全部刷新；一拖一对 gesture |
| P2：确定布局和组件边界 | 必须先做 | 做 1100×700、920×620、700×500 三份可运行 UI 原型；确定主图、右表、浮层和底栏；统一逻辑尺寸与绘制/命中几何 | `PluginEditor.*`、新 LayoutMetrics/PlotTransform；替换旧固定控制台概念稿 | 单选/多选/Expert/边缘频段均无越界、重叠和底栏遮挡；用户可直接审阅整体效果 |
| P3：主图重绘 | 核心 | 对数频率网格、独立增益/频谱标尺、四段区域、Range 线与节点状态、动态响应分层；限制滚轮作用范围 | 拆分 `DisplayView` 中主图绘制与交互；复用 `SpectrumView` 可用部分 | 绘制与命中使用同一变换；改变范围/尺寸后拖动仍对准；Range、Threshold、频谱、响应不混义 |
| P4：频段浮层和控件重做 | 核心 | 重新排 Threshold/Range/Attack/Release/Makeup；Ratio/Knee 用可靠滑条；Compress/Expand、每段 Mix 和精确输入完整；重新绘制旋钮状态 | `BandPopover.*`、`Knob.*`、统一 NumericEditor、主题控件绘制 | 常态/悬停/按下/编辑/禁用均清楚；拖动不跳、失焦处理可靠、输入能校验、自动化回显一致 |
| P5：Expert 与联动 | 核心 | SC Band/Free、In/Ext、连续 SC 频率、Stereo Link；选中集合复用原 LINKED 的参数操作；Mixed 状态与限幅策略清楚 | `BandPopover` Expert、`LinkedDrawer.*`、selection/参数操作层 | 120Hz/8kHz 可直接输入；低高截止合法；Stereo Link 实际生效；折叠不偷偷改声音；多段差值不被抹平 |
| P6：全局控制和电平反馈 | 核心 | 重排完整可读底栏；Analyzer 与 Output 三态弹层；连续 Input/Output/Mix、clipping/peak reset；Auto-Gain/Monitor 保留合理入口 | `OutputPanel.*`、Analyzer 面板、底栏与电平表 | 底栏无 Natural Phas… 这类截断；鼠标从按钮进入面板不闪退；冻结/旁路/固定状态可见；读数来自真实处理 |
| P7：可选后端对齐 | 独立功能改造 | Global Mix 效果量算法与迁移；精确总响应语义；补检测/GR telemetry；进一步 M/S、逐段 Lookahead、自由频段按子项目拆分 | `Parameters.*`、`PluginProcessor.*`、`dsp/MultibandDSP.*`、状态版本迁移 | 每项都有兼容策略与音频验证；旧工程可恢复；参数名与处理一致；未做的功能没有伪按钮 |
| P8：三主题、缩放和细节 | 发布前 | 三主题对比度、统一线宽/圆角/阴影/状态；统一字号；DPI 与插件缩放分离；轻量动画和缓存 | `MBTheme.h`、LayoutMetrics、各控件 | Blue/Red/Cream 均清楚；700×500 到 1840×1240 均可用；125/150/200% DPI 不错位；不再依靠局部缩放补洞 |
| P9：宿主验收与交付 | 发布前 | 真正 VST3 宿主的鼠标分发、自动化、撤销/重做、预设切换、保存重开、关窗重开、音频/性能回归；更新交付文档 | 真实宿主测试记录、截图矩阵、CHANGELOG、UI_SPEC 新版 | 当前阻塞 BUG 全部关闭；同一参数鼠标/数值输入/宿主三条路径一致；静音与播放状态均正常；文档与可运行构建一致 |

依赖顺序：P0 → P1 → P2 → P3/P4 → P5/P6 → P8 → P9。P7 每个后端子项目独立评估、验证后合入；需要 P7 telemetry 的 UI 反馈在数据到位前不使用模拟信号冒充。

## 6. 实施结构建议

现有 `DisplayView.cpp` 承担大量绘制、状态、参数与鼠标分发，不适合继续累加补丁。重绘时逐块拆出以下职责，优先复用可用数据接口和 DSP，不做无关重构：

- `PlotTransform`：频率/增益/电平坐标转换，绘制与命中共同使用。
- `SelectionModel`：单选/多选、主选中段、Mixed 值和联动操作目标。
- `BandControlPanel`：基本面板与 Expert；自适应定位集中处理。
- `ParameterControl` / `NumericEditor`：归一化、格式、解析、默认值、gesture 和宿主回显。
- `TransientPanelController`：Analyzer/Output 的 hover/pinned、焦点、鼠标安全路径。
- `MeterSnapshot`：音频线程安全地提供真实显示数据，消息线程绘制；不在音频线程执行 GUI 操作。

这些名称是建议的职责边界，可按现有仓库规范调整。参数 ID、预设与工程状态是兼容边界，不能因类名或控件重做而重排参数。

## 7. 关键验收场景

| 场景 | 通过要求 |
|---|---|
| 频段位于最左/最右、Expert 展开、最小窗口 | 面板完整可操作、箭头关联正确、节点不会被错误点击穿透 |
| Ratio=4:1，按下不移动；拖动途中按/松 Shift | 零位移不改变值；精调切换连续；上下方向正确 |
| 同一帧宿主更新 Threshold、Attack、Lookahead | 所有显示正确，无“只刷新第一项” |
| Mix=0/30/49/50/100%，Solo/Mute 与总 Bypass | UI 明确区分湿度、静音、独听、总旁路，无错误变暗 |
| 全局 Mix=200%，各段无增益变化 | 若采用效果缩放语义，不因多叠一份干声而凭空 +6dB；旧算法模式则明确标示与兼容 |
| 多选段参数不同、某段到上下限 | Mixed 值与变化规则可预测，差异不会被隐藏的绝对赋值抹平 |
| 打开频段面板后进入 Output/Analyzer | 状态转换清楚；hover 与 pinned 可区分；不因为间隙丢失操作 |
| 数值编辑输入空串、非法单位、越界值、Esc、失焦、粘贴 | 有合法解析/取消/限幅规则，不静默误写成 0 |
| 主题×窗口×DPI | 无截断、错位、点不到和文字过小；测试使用真实编辑器字号和组件树 |
| 旧工程/预设加载与宿主自动化 | 原 ID 和值正确恢复；需要迁移的后端变更有明确版本规则 |

测试聚焦实际用户操作和参数结果，不以“鼠标函数被调用”或“某个像素变色”替代。已有 PASS 831 的旧测试结果只能作为历史基线，不能代表本次新 UI 已验收。

## 8. 开工前仍需补测的目标细节

本次静态证据足以确定重绘方向与任务拆分；以下项目应在 P0 通过宿主运行补齐，不能写成已测事实：目标版本精确窗口/控件边界、选中面板跟随与避让、hover 延迟/焦点规则、动画时长、DPI、是否真实背景模糊，以及各状态的完整截图。

分析报告：`E:\Audio Plugin Code\261021 fbmb Crack New\_rea_ui\UI_REVERSE_FINDINGS_2026-10-21.md`。

官方依据：[主图](https://www.fabfilter.com/help/pro-mb/using/display)、[基本控件](https://www.fabfilter.com/help/pro-mb/using/basicbandcontrols)、[Expert](https://www.fabfilter.com/help/pro-mb/using/expertbandcontrols)、[Output](https://www.fabfilter.com/help/pro-mb/using/outputoptions)、[Analyzer](https://prod.fabfilter.com/help/pro-mb/using/analyzer)。目标二进制与当前官方文档的差异以 P0 实测为准。
