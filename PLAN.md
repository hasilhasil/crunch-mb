# Crunch MB — 插件开发计划表（PLAN）

> **用途**：换模型重开发的唯一入口路线图。新模型先读本文件，再按 §2 顺序读文档，
> 严禁跳过 Phase 0 直接写 UI/DSP 代码。
> **背景**：首轮实现（已清理）编译通过、功能齐全，但 UI 效果与预期严重不符。
> 根因与教训见 §1——本轮计划针对这些教训重排了工序。

---

## 1. 首轮复盘（为什么"跟想象的完全不一样"）

| # | 教训 | 根因 | 本轮对策 |
|---|------|------|---------|
| L1 | **UI 纯靠文字规格直出代码，没有可视化中间态** | DESIGN.md 的 UI 章节是"功能框架"而非"视觉定稿"；我按理解写了像素级布局，你没有机会在编码前看到并纠偏 | **Phase 0 强制 UI 定稿**：先出静态视觉稿（HTML/mockup/布局截图，任选）经你确认，才进编码；UI 编码阶段对照定稿逐屏验收 |
| L2 | **DSP 正确性靠 DAW 人耳发现，没有离线断言** | 平坦度/零修改/延迟等验收项只写在 TECH_SPEC §10，没有对应的可执行测试 | **Phase 2 测试先行**：先写离线断言测试（LR 平坦度、互补恒等、FIR 归一化求和、延迟计数），全绿再进集成 |
| L3 | **一个大提交里塞了 26 个文件** | 没有按阶段拆分交付，出错时定位成本高 | **每阶段独立提交 + 构建通过为 gate**，任何阶段失败不进下一阶段 |
| L4 | **对"效果"的理解没有对齐基准** | 我按 Pro-MB/Clarity Vx 的文字描述实现，但视觉细节（曲线粗细/透明度/间距/层级）与你脑中的画面有偏差 | Phase 0 要求你提供**参考截图或手绘/工具稿**（哪怕粗糙），新模型必须逐元素对齐参考稿 |
| L5 | **音频线程上做了重活**（FIR 设计） | 首轮为省事把 FIR 重设计放参数更新点 | Phase 3 明确：FIR 设计放**消息线程**（`parameterChanged` 或异步），音频线程只消费结果 |

---

## 2. 新模型入门阅读清单（按顺序，必读）

| 序 | 文档 | 位置 | 读什么 |
|----|------|------|--------|
| 1 | **本文件 PLAN.md** | 本目录 | 工序、决策、坑 |
| 2 | **DESIGN.md** | 本目录 | 产品定义：三相位模式、4 频段、62 参数、交互设计（§6）、色彩规范（§7.2；§7 布局已被 UI_SPEC 取代） |
| 2.5 | **UI_SPEC.md** | 本目录 | **UI 权威规范（用户参考图 + 三要求制定）**：三层架构（四分区显示 / 底部控制区 / 频段 Popover）、参数-控件映射、开放点 O1–O4 |
| 3 | **TECH_SPEC.md** | 本目录 | 工程规范：参数精确规格（§5）、DSP 架构（§3）、音频线程红线（§4）、验收标准（§10） |
| 4 | **crunch-api-interface.md** | 工作区根 `E:\Audio Plugin Code\` | 前后端分离接口规范 v1.0.3（IPluginBackend / IMBBackend 契约） |
| 5 | ~~crunch-api-handoff.zh-CN.md~~ | 工作区根 | **已废除**，仅历史参考，勿依据其实现 |
| 6 | 参考实现（只读，勿复制粘贴） | `260916 Crunch Compressor\src\` 和 `260813 Crunch EQ Backup\Crunch EQ Source\src\` | 家族代码风格：LookAndFeel、环形缓冲区、SpectrumEngine、参数缓存差分。BiquadFilter.h、SpectrumAnalyzer/Engine、LookAndFeel.h 可**逐字节复用**（TECH_SPEC §2 有说明） |
| 7 | 竞品调研结论 | 会话沉淀于 DESIGN/TECH_SPEC 内 | Pro-MB 最小带宽 1 八度（官方论坛原话）、C4 延迟 64 samples、Future MB 三相位模式 42.7ms 线性相位 |

---

## 3. 已锁定的设计决策（共 14 项，全部已写入文档，不再重议）

| # | 决策 | 结论 | 文档位置 |
|---|------|------|---------|
| D1 | 相位模式 | **三模式**：Natural Phase（默认）/ **Minimal Phase**（原 Dynamic Phase 更名，勿与 DSP 术语 Minimum Phase 混淆）/ Linear Phase | DESIGN §2 |
| D2 | 频段 | **4 段固定**：Low / Low Mid / High Mid / High；自由交叉点 | DESIGN §3.1 |
| D3 | 最小带宽 | **每段 ≥ 1 八度**（Pro-MB 官方标准），`clampCrossovers` 统一钳制 | DESIGN §3.2 |
| D4 | 参数独立性 | 4 段**完全独立** + 5 组统一控制（Threshold/Attack/Release/Makeup/Mix），按住拖动 = **线性恒定偏移**（非比例） | DESIGN §6.1 |
| D5 | 交叉点斜率 | 12 / 24 / 36 / 48 dB/oct 可调 | DESIGN §3.2 |
| D6 | Lookahead | 三档 **Off / 1 ms / 3 ms**（与单段压缩器一致） | TECH_SPEC §5.1 |
| D7 | 增益补偿 | 每段 Makeup（±36dB）+ 总 Out Gain；**无 Auto Gain** | DESIGN §5 |
| D8 | 监听门 | **Solo / Mute / Bypass / Delta**；优先级 **Delta > Solo > Mute** | DESIGN §6.2 |
| D9 | Bypass 语义 | **方案 A：Mix=0 记忆宏**（无独立 DSP 路径；记忆值不序列化=已知边界） | DESIGN §6.6 |
| D10 | 侧链 | 全局立体声侧链总线 + 每段 EXT 开关（侧链恒用 IIR 分频，fallback INT） | DESIGN §6.3 |
| D11 | Mix | 每段独立平行 Mix（0-100%）+ 总 Mix 统一握柄（同 D4 逻辑）；无全局 dry/wet 参数 | DESIGN §6.4/6.5 |
| D12 | 显示区 | **双层频谱**（Pre 灰 + Post accent），**无滚动历史、无 GR 曲线、无 GR 读数**；环形历史仅存续供 API | DESIGN §7.3、TECH_SPEC §6.1 |
| D13 | 仿 EQ 曲线 | **动态语义（8A）**：y(f)=Σw_i(f)·g_i，压缩时频段凹陷；独立增益刻度层 | TECH_SPEC §6.4 |
| D14 | 按钮/手势/面板/色彩 | 24×24px 按钮（S/M/BYP/Δ，EXT 留面板）；**状态点常驻 + hover 展开**；手势分区 [线−40,线−16] 按钮列 / [线−4,线+4] 抓取带 / 其余=选段，双击=输频率；面板 **280px**；**面板仅保留 SIDECHAIN EXT**（S/M/BYP/Δ 镜像四按钮与显示区功能重复已删，2026-09-22）；**显示区四段统一主题 accent 色**（原 5A 四色方案已取消，无新增色值）；统一控制用 **6A 五握柄**（非 GROUP 开关）；背景频谱 **7B 纯双层频谱**（无 Analyzer 三态切换） | DESIGN §7.3-7.5 |
| — | **布局修订（2026-09-23）** | 用户参考图 + 三要求 → **三层架构**：显示区四分区（Clarity Vx 骨架，一级菜单）/ 底部矩形控制区（全局 + 5 统一控制旋钮）/ 频段 Popover（Pro-MB 样式二级菜单，承载每段 6 旋钮 + 滑杆 + EXPERT）。统一控制由 ⊞ 握柄升级为底座独立旋钮（语义不变）。开放点 O1–O4 待 Gate P0 确认 | **UI_SPEC.md（布局权威）** |

---

## 4. 阶段计划表（每阶段有独立 gate，全绿才进下一阶段）

### Phase 0 — UI 视觉定稿（编码前，强制）
**产出**：静态视觉稿（HTML mockup / 设计工具导出 / 手绘拍照任选）+ **UI_SPEC.md（UI 权威规范）**，覆盖两张主屏：
1. 显示区完整态：双层频谱 + 四分区（统一 accent 色）+ 交互曲线 + 分割线/手柄/状态点/按钮列(hover 态) + 电平表 + 顶栏
2. 控制面板完整态：段 Tab + 主旋钮行(含 ⊞ 握柄) + SIDECHAIN EXT + 底条 + 全局选择器 + Bypass（无 S/M/BYP/Δ 镜像行）

> **2026-09-23 更新**：用户提供了 UI 参考图（Clarity Vx Pro 四分区 + Pro-MB 频段弹出面板）与三条 UI 要求，
> 布局改为**三层架构**（显示区四分区 = 一级菜单 / 底部矩形控制区 / 频段 Popover 二级菜单），
> 详见 **UI_SPEC.md**（布局权威规范）。视觉稿按 UI_SPEC.md 重绘；开放点 O1–O4 须 Gate P0 一并确认。

**必须逐项对齐的参考基准**：
- 分区骨架参考 **Clarity Vx Pro 截图**（用户提供的那张：全高分割线 + 分割线左侧小按钮列）
- 曲线/分频交互参考 **Pro-MB / Future MB**（用户已明确）
- 控件风格/主题/字体严格 = **Crunch 家族**（DESIGN §7.2）

**Gate P0**：用户逐屏确认视觉稿 ✅ → 才允许创建任何 `src/` 代码。
⚠️ 若用户提供了自己的 UI 图，以用户图为准，本文档与 DESIGN 的布局描述服从用户图。

### Phase 1 — 骨架 + 参数系统
- CMakeLists（VST3+Standalone，字体引用兄弟项目 `FONT_DIR`）、.gitignore
- `Parameters.h/.cpp`：62 参数全量 + 枚举 + `clampCrossoovers` + 读数格式
- PluginProcessor 空壳（APVTS、buses 含侧链、状态序列化）+ PluginEditor 空壳（920×620、280px 分割）

**Gate P1**：`cmake --build` 通过；DAW 能加载出**空壳界面**；62 参数在宿主自动化列表可见。

### Phase 2 — DSP 内核（测试先行）
顺序实现 + **每步附离线断言测试**（独立 console target，不依赖 DAW）：
1. `BiquadFilter.h`（逐字节复用）→ 无测试（已验证）
2. `BandCompressor` → 断言：硬/软拐点传输曲线形状、attack/release 时间常数、ratio/threshold 正确
3. `CrossoverBank` → **断言①（关键）**：各斜率下 4 段幅度求和平坦（目标 ±0.01 dB @ 48k 扫频）；
   **断言②**：互补模式逐样本恒等（输入=输出，误差 <1e-6）
   ⚠️ 首轮已知数学结论：**LR 平坦和仅存在于 4/8 阶（24/48 dB/oct）；12/36 阶必须走互补对**
   （LP+HP 分子 ωc²ⁿ+s²ⁿ 在 fc 处对奇数 n 恒为 0）——实现前先重读 TECH_SPEC §3.2 与首轮结论
4. `LinearPhaseCrossover` → 断言③：Σ|H_i| 归一化后 FIR 求和 = 纯延迟（群延迟恒定）；
   断言④：延迟计数 = pad + (L−1)/2 与 `getLatencySamples()` 一致
5. `MultibandDSP` 集成 → 断言⑤：全部增益相同时（ratio=1/mix=100/makeup=0）三模式输出 = 输入
   （Natural 因 LR 相位除外，其断言改为"幅度平坦+群延迟范围内"）

**Gate P2**：所有断言绿 + 全目标构建通过。

### Phase 3 — 模式/前瞻/侧链集成
- 三相位模式切换 + 延迟上报（TECH_SPEC §3.5 表核对）
- Lookahead（Off/1/3ms）频段延迟线
- 侧链总线 + 每段 EXT + fallback
- 监听门 Delta/Solo/Mute（**含 5ms 平滑——首轮缺失，本轮必须补**，TECH_SPEC §3.7 有要求）
- Bypass 宏（Mix 记忆）
- **FIR 设计放消息线程**（L5 对策）

**Gate P3**：
- ✅ 离线断言全绿（CrunchMBTests 75/75：断言①–⑥ + T0/T1/T7/T8/T9/T10/T11/T12/T13）
- ✅ 断言⑥：Solo/Mute/Delta 切换无爆音（差分 ≤ 稳态×3 + 门斜坡 63%≈5ms）
- ✅ 2026-09-23 Studio One 实测反馈修复 4 bug（均有回归断言）：
  1. `readScalars` 后未调 `updateScalars` → 全部标量参数（阈值/比率/Mix/门/EXT/前瞻）不生效
  2. APVTS `getRawParameterValue` 是反归一化值（Choice=索引），误乘 (n−1) 致
     默认 Medium→High、Linear 延迟 3072→6144 翻倍、Slope24→48
  3. 侧链总线查找误用 `getBus(false,1)`（输出总线）→ EXT 恒 fallback INT
  4. 延迟上报读音频线程 `laSamples_` + `getSampleRate()`（未设播放配置时为 0）
     → lookahead 切换延迟不刷新
  5. 补接 `enabled` 旁通参数（规格 §5.1 有但从未接线）
- ⏳ **DAW 手动验收 16 项（Studio One）**：
  1. Phase Mode 三模式切换正常出声
  2. Linear 延迟 ≈ 32/64/128ms（Low/Medium/High，默认 Medium）
  3. **重开插件后延迟不翻倍**（本轮修复核心）
  4. Lookahead 三档 → 延迟 +0/+1/+3ms
  5. 3ms 前瞻瞬态起振略钝（主观，次要）
  6. EXT 侧链触发泵感（军鼓 Send → 底鼓 Sidechain）
  7. EXT 无侧链时 fallback INT（不误压）
  8. 侧链内容不出现在输出里
  9. Solo 只出该段
  10. Mute 该段静音
  11. Delta 输出 wet−dry（很薄很怪 = 正常）
  12. Delta > Solo 优先级
  13. 门切换无咔哒爆音
  14. 中性（ratio=1/mix=100/makeup=0）输出 = 输入
  15. 压缩效果可闻、Makeup 可补偿
  16. Enabled=off 完全直通

### Phase 4 — 显示区（对照 Phase 0 定稿逐屏比对）
- 双层频谱（SpectrumEngine 懒启动移植）
- 仿 EQ 动态曲线（统一 accent 色 + 独立增益刻度）
- 分区/分割线/手柄/状态点/hover 按钮列/双击频率输入/手势分区
- 电平表 + 顶栏 + 传输曲线 inset
- **Gate P4**：与 Phase 0 视觉稿并排比对一致 ✅ + 交互三手势按 DESIGN §7.3 验证。

#### Phase 4 宿主验收反馈（2026-09-23 Studio One 实测，待修清单）

**P0 阻断（核心功能不可见/不可用）**
1. B1 双层频谱未出现 — 根因：Editor 构造时未调 `setActive(true)`，分析线程从未激活
2. C2/C3 拖动分割线时**分区与分割线不跟随**（只有曲线跟）— 根因：drawPartitions/drawCrossovers 硬编码 200/1k/6k，未读参数
3. C6–C9 S/M/BYP/Δ 按钮列未绘制（只有状态点）
4. C10 全局 BYPASS 胶囊不可点击

**P1 视觉修正**
5. A2.1 选中段描边在分割线处双边重叠 → 视力两倍厚（改为只画三边/去共享边）
6. A2.2 Low 段名与左音量刻度重叠 → 段名右移；A2.3 四段名整体下移 ~6px（y 34→40）
7. A3 双刻度置顶绘制 + 透明度 100%（去掉 0.8 alpha）
8. A5 频谱显示到 20k：`SpectrumAnalyzer` fMax 22000→20000；底部刻度加 20k
9. A6 IN/OUT 柱顶文字被顶栏遮住 → 下移至 y≈32
10. A7 LINKED 抽屉底边与 global 条 +1px 叠合消缝
11. A8 global 条 100% 不透明（surfaceA→surface）；左刻度 −36 上移（kPlotBottom 596→588、kPlotH 556→548，频谱/曲线底缘与之对齐）
12. A9 删除顶栏 "Natural Phase · 24 dB/oct · LA Off"
13. B1 附带：曲线颜色对调 — 灰(inFill)=Post、主题色(accent)=Pre
14. B3 Range 钟型改 Pro-MB 样式：以段几何中心为中心的钟形，宽度=段宽×1.5（允许溢出分割线），形状 sin²；仍只对选中段显示
15. B5 IN/OUT  dB 读数刷新率降低（柱体实时、数字 250ms 节流）

**P2 新功能（C4，参考 Crunch EQ 频点 + Pro-MB）**
16. 每段 EQ 曲线中点画频点圆点（Crunch EQ 样式）；hover 浮出小窗（段名/Makeup/中心 Hz）
17. 拖动任一分区频点（垂直）= 改该段 Makeup（可跨段操作，同时选中该段）
18. **删除每段顶部 Makeup 旋钮**及其 MAKEUP 文字（C5 流程因此跳过）
19. C2 附带：分割线 hover 光标改左右箭头（LeftRightResizeCursor）
20. D3 resized() 同时失效 curvePathsValid_ 与 spectrumPathsValid_（保证曲线/频谱随窗口尺寸真实跟随）；刻度保持字号固定

**验收结论**：A1/A4/B4/C1/C11 通过；其余见上。Phase 4 UI 完成度约 50%，本清单做完后自检全流程。

### Phase 5 — 控制面板（对照 Phase 0 定稿）
- 段 Tab、选中段 attachment 重建、5 个 ⊞ 握柄（线性恒定偏移 + 钳制）、SIDECHAIN EXT 按钮、全局选择器、Bypass 反相显示、三主题 + 统一 accent 色（**无 S/M/BYP/Δ 镜像按钮**）
- **Gate P5**：与视觉稿一致 + 统一控制断言（4 段 delta 相同、钳制独立）。

### Phase 6 — API facade（前后端分离）
- `IPluginBackend.h` / `IMBBackend` / `MBBackend` 实现（对照 crunch-api-interface.md v1.0.3）
- UI 改走 facade（不直触 APVTS/DSP 内部——EQ 审计 D 组问题不得复现）
- **Gate P6**：TECH_SPEC §10.4 API 验收 9 项全过。

### Phase 7 — 验收 + 交付
- TECH_SPEC §10 完整四维验收（功能/音质/性能/API）
- 宿主冒烟：扫描、侧链总线、自动化、延迟上报、三主题
- 构建产物安装到 `C:\Program Files\Common Files\VST3`
- **Gate P7**：§10 checklist 全勾 + 用户确认"效果符合预期"。

---

## 5. 首轮实现偏差与已知技术问题（务必继承的结论）

1. **LR2/LR6（12/36 dB/oct）数学上不存在平坦和**：同极点 LP+HP 的分子在 fc 处对奇数阶恒为 0
   （null）。对策：12/36 走**互补对**（HP := 1−LP，复数恒等重建）；24/48 走 LR=(BW)²。
   ——若新模型自认为能推导出更好的四斜率全平坦 IIR 方案，允许改进，但必须附扫频断言证明。
2. **Minimal Phase 校正结构**：`out = dry·m̄ + Σ band·(m_i − m̄)`（dry=Σband，利用恒等重建对齐）；
   监听门激活时回退逐段求和（透明性承诺只作用于中性状态）。
3. **线性相位 FIR 延迟 ≈ 0.75×FFT 尺寸**（pad B + 群延迟）：Low≈1536 / Med≈3073 / High≈6145 samples
   @参数网格——**与 TECH_SPEC §3.5 表格（~1024/2048/4096）不一致**，实测后以 `getLatencySamples()`
   为准并回写表格。
4. **交叉点热切换不清滤波器状态**（只有斜率/相位模式变化才 reset），否则拖拽每帧爆音。
5. **Bypass 记忆值不序列化**：旁路状态下保存会话 → 重开 Mix=0（已知边界，DESIGN §9）。
6. **JUCE API 坑**（首轮踩过）：`juce::dsp::FFT::perform` 收 `std::complex<float>*`（非交错 float）；
   `TextEditor` 无 `textWasSubmitted` 成员（用 `onReturnKey`/`onEscapeKey`/`onFocusLost`）；
   `juce::Time` 无 `getMillisecondCounter64`；`Graphics::setColour` 不收 `ColourGradient`
   （用 `setGradientFill`）；`AudioBuffer` 无 `getTotalSize`。
7. **构建命令**（本机 cmake 不在 PATH）：
   `"E:\Microsoft Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`
   - 配置：`... -B build -A x64 -DJUCE_DIR="E:\Audio Plugin Code\260916 Crunch Compressor\JUCE-8.0.4"`
   - 构建：`... --build build --config Release --target CrunchMB_VST3`
   - ⚠️ `--target ALL_BUILD` 在本机会静默失败（0 错误退出 1），**用具体 target 构建**。
8. **字体路径**：`CMakeLists.txt` 的 `FONT_DIR` 指向兄弟项目 `260916 Crunch Compressor/src/ui/Fonts/`（含空格路径，已验证可用）。

---

## 6. 本轮仓库清理记录

- 已删除：首轮全部源码（`src/` 29 文件、`CMakeLists.txt`、`.gitignore`、`README.md`）+
  构建目录 `build/` + 8 个构建日志 txt —— 仅供新模型从零重写，避免被旧实现锚定思路。
- **保留（技术文档，禁删）**：`DESIGN.md`、`TECH_SPEC.md`、本文件 `PLAN.md`；
  工作区根 `crunch-api-interface.md`（现行 API 规范）、`crunch-api-handoff.zh-CN.md`（已废除留档）。
- 首轮源码中值得**参考而非复制**的部分已在 §2.6 指路；关键结论已沉淀进 §5。

---

*计划版本 v1.0（2026-09-22）。阶段 gate 由用户逐个确认，不批量跳过。*
