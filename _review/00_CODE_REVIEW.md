# Crunch MB 代码评审：声音实现 + UI 与参考插件的差距

> 评审对象：`E:\Audio Plugin Code\260922 Crunch MB`
> 参考基准：FabFilter Pro-MB（`261013 fbmb Crack`）、Three-Body Future MB（`261013 MB Crack`）
> 评审方式：通读 `src/` 全部源码 + `UI_SPEC.md` v1.8 + `mockup/v9`
> 日期：2026-10-14

---

## 0. 结论速览

| 维度 | 评价 |
|---|---|
| **工程纪律** | **高**。文档体系（UI_SPEC v1.8 / TECH_SPEC / DESIGN / PLAN / TEST_PLAN）+ 线程纪律注释 + 已知 bug 修复记录，是我见过纪律最好的个人项目之一 |
| **DSP 架构** | **合格偏上**。热路径零分配零锁、双精度 biquad、两个自己发现并修掉的真 bug。但**缺 4 个关键特性** |
| **UI 设计** | **高于实现**。UI_SPEC 写得比很多商业项目细，问题全在执行层 |
| **UI 代码** | **这是痛点根因**。`DisplayView` 是 2506 行的 God Object，控件层几乎缺失 |
| **离 Pro-MB 的差距** | 视觉差 30%，**结构差 70%**。结构不修，视觉改不动 |

**一句话**：不要重写设计，要重写结构。把 `DisplayView` 拆开，你说的"特别多的问题和逻辑错误"会消失 80%。

---

## 1. 声音实现：客观评价

### 1.1 做得好的（有行号证据，这些是真功夫）

**① 双精度 biquad 状态**（`BiquadFilter.h:30-37`）
```cpp
const double in = (double) x;
const double y  = b0_ * in + z1_;
z1_ = b1_ * in - a1_ * y + z2_;
z2_ = b2_ * in - a2_ * y;
```
TDF2 转置直接 II 型 + double 累加。低频段（20–200Hz）用 float 会明显劣化，你用 double 是对的。

**② Lookahead 环形缓冲"始终写环"**（`MultibandDSP.cpp:201-221`）
注释里写明了修复历史：早期 `laSamples_==0` 时整段跳过，环内停留 zeros，前瞻切入瞬间读出未预热数据 → 3ms 静音 gap 爆音。现在始终写环、读位置 = 写位置 − laSamples_。**这个 bug 很多商业插件都有**，你找到了。

**③ Mix/Gate 平滑"每样本只推进一次"**（`MultibandDSP.cpp:237-245`）
```cpp
if (ch == 0) {   // ⚠️ 必须在通道循环外
    for (int b = 0; b < kNumBands; ++b) {
        mixSmooth_[b] += (mixLin_[b] - mixSmooth_[b]) * (1.0f - mixSmoothCoeff_);
        ...
```
注释同样记录了修复：stereo 时每通道推一遍会使时间常数减半。正确。

**④ Minimal 模式"校正结构"求和**（`MultibandDSP.cpp:250-259`）
```
out = dry·m̄ + Σ band·(g_i − m̄)
```
让"中性状态"（全段增益相等）下 `out ≡ dry·g`，只在门激活时回退逐段求和。这个技巧很聪明——它让 Minimal 模式的透明性承诺在中性时精确成立。

**⑤ Range 双极符号语义与 Pro-MB 一致**（`BandCompressor.cpp:51-52, 94-95`）
```cpp
const bool upward = rangeDb_ > 0.0f;
const float lim = std::abs (rangeDb_);
...
return juce::jlimit (-lim, lim, gainDb);
```
负 Range = 向下压缩、正 Range = 向上压缩、|Range| = 幅度上限。和 Pro-MB 完全同构，这是对的。

### 1.2 差距（对照两个参考插件）

| 特性 | Crunch MB | Pro-MB | Future MB | 影响 |
|---|---|---|---|---|
| **过采样** | **无** | 2x/4x FIR 多相 Kaiser | 2x/4x JUCE Oversampling | 🔴 高。ratio>8 + attack<1ms 会可闻混叠 |
| **Knee** | **布尔 → 固定 12 dB 二次** | 连续 0–48 dB，幂均值软膝 | 连续 C¹ 多项式 | 🔴 高。做不了透明母带 |
| **检测器** | peak / RMS（10ms 固定窗）布尔 | RMS + crest-factor 自适应 | RMS + 2^幂环形窗 | 🟡 中 |
| **Attack/Release** | 固定 ms 线性映射 | **程序相关** `α=pow(t, min(6/attack%,0.5))` | 固定 ms，clamp 0.2–250 / 30–2500 | 🟡 中。慢速下不如 Pro-MB 自然 |
| **立体声链接** | **固定 `max(|L|,|R|)`** | **lerp 连续 0–100%** | L/R RMS 矩阵 | 🟡 中。立体声像会跳 |
| **Lookahead** | 0 / 1 / 3 ms | 连续到 20 ms | 到 20 ms × 过采样率 | 🟢 低 |
| **侧链 EQ** | 仅 EXT 开关（无滤波） | Free 模式 LP/HP 频率可调 | 每段完整侧链 EQ | 🟡 中 |
| **分频斜率** | 12/24/36/48 固定 | **连续可调 + 每侧独立** | 每段 Border Slope L/R | 🟢 低 |
| **相位模式** | Natural(LR树)/Minimal(互补)/Linear(FIR) | Minimum/Linear/Dynamic | Minimum/Liquid/Linear | 🟢 低（你的三档划分更清晰） |

**最该补的三个**：过采样、连续 Knee、立体声连续链接。这三个都是"10~20 行代码换明显听感提升"。

---

## 2. UI：客观评价

### 2.1 设计层：高于实现

`UI_SPEC.md` v1.8（385 行）是我评审过的个人项目里最细致的 UI 规范。三层架构判断正确：

```
分区负责"看"与"选"  →  Popover 负责"调"  →  底座负责"全局"
```

这个划分和 Pro-MB 的实际交互一致（Pro-MB 也是"显示区选段 + 浮层调参 + 底栏全局"）。62 个参数的去处表（§7）也完整。**设计不用改。**

### 2.2 执行层：5 个有证据的问题

**问题 1｜`paint()` 里 `drawDrawer(g)` 调了两次** — 实锤 bug

`src/ui/DisplayView.cpp:2488-2489`
```cpp
    drawDrawer (g);
    drawDrawer (g);      // ← 复制粘贴残留
    drawLinkedDrawer (g);
```
后果：抽屉区域 alpha 双重混合（颜色偏深/偏灰）+ 每帧无谓多画一整个面板。

**问题 2｜参数查找是线性扫描 + dynamic_cast，且在鼠标回调里**

`getParamNorm` / `paramRangeMinOf` / `paramRangeMaxOf` / `getParamPhysical` / `setParamNorm` / `setParamPhysical` 六个函数，实现全是：
```cpp
for (auto* p : proc_.getParameters())
    if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        if (pw->getParameterID() == id) ...
```
**39 个调用点**，分布在 `mouseDown` / `mouseDrag` / `mouseUp` / `syncPopover*` 里——即按鼠标刷新率（60–125 Hz）触发。62 个参数 × 39 处 × 60fps ≈ 每秒 14 万次 `dynamic_cast`。

这不一定是掉帧主因，但它是"手感发涩"的典型来源，而且是纯粹的白烧。

**问题 3｜`DisplayView` 是 God Object**

| 文件 | 行数 | 成员函数 |
|---|---|---|
| `DisplayView.h` | 466（其中 ~250 行是 `xxxForTest()` 测试钩子） | — |
| `DisplayView.cpp` | **约 2500** | **96** |

它同时负责：坐标映射、网格、分区、双刻度、交叉点、段头、频率输入框、电平表、顶栏、全局条、抽屉、频谱（4 条 Path）、Range 钟型、EQ 曲线、Popover（5 旋钮 + 3 滑杆 + EXPERT）、LINKED 抽屉（5 旋钮）、Analyzer 菜单、全部命中测试、全部拖拽状态机。

**这就是"UI 有特别多的问题和逻辑错误"的结构性根因**——不是你不会写，是 2506 行里任何一处改动都会牵动另外三处。

**问题 4｜控件层几乎缺失**

全项目只有 `Knob` 一个控件抽象。滑杆（`popSliderRect` / `hitTestPopSlider` / `applyPopSlider`）、双态按钮、文本选择器、菜单、抽屉、弹层——**全部是 `paint()` 里的代码 + 手写 hitTest，没有类**。

对照：
- Pro-MB 的 `gui::` 命名空间有 **~60 个控件类**（`Button`/`ToggleButton`/`SelectButton`/`PopupButton`/`RolloverValueControl`/`FlexibleControl`/`Knob`/`Text`/`Label`/`MultiLabel`/`Image`/`ImageLabel`/`LevelButton`/`FloatValueButton`/`TextEditor`/`Menu`/`MenuItem`/`Overlay`/`Dialog`…）
- Future MB 的 `agui::` 有 **~100 个**

**问题 5｜无 backdrop blur**（`UI_SPEC.md §12.2` 选了兜底档 a）

`drawPopover()` 只用 `pal_.popA`（alpha 0.62）纯半透明，没有模糊。你自己在规范里评估过：档 b（4× 下采样模糊、15Hz 更新）只要 ≈0.3 ms/帧，"接近 Pro-MB"。

---

## 3. UI 与参考插件的差距

### 3.1 技术栈差距

| 维度 | Crunch MB | Pro-MB | Future MB |
|---|---|---|---|
| 渲染后端 | JUCE 软件渲染 | OpenGL 1.1 固定管线 | OpenGL 3.0+ GLSL |
| Shader | 0 | 0 | 32 个 |
| 控件抽象 | **1 个** | ~60 类 | ~100 类 |
| 抗锯齿 | JUCE 自带（好） | 位图自带 | 解析 AA（顶点算距离） |
| 文字 | Poppins Typeface（好） | 平台原生 | FreeType + SDF atlas |
| 模糊 | 无 | 位图预渲染投影 | shader |
| 旋钮 | 单一矢量旋钮 | 3 尺寸 × BACK/OVERLAY × 4 态，CPU 旋转 UV | 矢量 RingKnob + ITag 皮肤 |
| 皮肤系统 | 3 套 Palette 硬编码 | 换 PNG | `agui::*ITag` 12 种声明式标签 |
| 布局 | 920×620 可缩放 | 任意缩放 + 全屏 | 固定 1920×668 逻辑坐标 |

### 3.2 视觉差距（按用户感知排序）

| 感知项 | 你的现状 | Pro-MB 怎么做 | 追赶成本 |
|---|---|---|---|
| **玻璃感** | 纯 alpha，无模糊 | 深灰圆角 + 三角指针 + 投影（位图） | **低**（规范里已有方案，0.3ms/帧） |
| **旋钮质感** | 单一矢量旋钮 | BACK（刻度环，缓存）+ OVERLAY（指针，旋转） | 低（拆两层即可，不用改位图） |
| **主视觉密度** | 双层频谱 + EQ 曲线 + Range 钟型 + 双刻度（已经很像了） | 满屏频谱 + 双刻度 + 增益刻度 | **已接近** |
| **弹层形态** | 圆角 + accent 描边 | 深灰圆角 + **顶部三角指针** + 重投影 | 低 |
| **投影硬度** | `black.withAlpha(0.35)` + translate(0,3)，偏硬 | `0 14px 44px rgba(0,0,0,.5)` | 低（多画几层渐变） |
| **字体层级** | Poppins 四字重（好） | 平台无衬线 | **已达标** |

**结论：视觉上你已经到 Pro-MB 的 70%，剩下 30% 里有一半是"模糊 + 投影"这一件事。**

### 3.3 但真正的差距在结构

| | Crunch MB | Pro-MB | Future MB |
|---|---|---|---|
| 单文件最大 UI 代码 | **2506 行** | 拆到 `gui::` 数十文件 | 拆到 `agui::` 数十文件 |
| 加一个新控件的成本 | 改 paint + 加 hitTest + 加状态成员 + 加测试钩子 ≈ **4 处** | 继承 `Control`，重写 2 个虚函数 ≈ **1 处** | 继承 `BaseComponent` + 挂 ITag ≈ **1 处** |
| 改配色的影响面 | 3 套 Palette 硬编码在 `MBTheme.h`（其实这点你做得好） | 换 PNG | 改 ITag |

**这一条是"UI 问题多"的根本原因，也是你最该投入的地方。**

---

## 4. 怎么接近他们：三期路线

每期独立可验证、可回退，不重写设计。

### 第一期：止血 + 抽象（1–1.5 周）— 零视觉变化，纯修逻辑

| # | 动作 | 代码量 | 收益 |
|---|---|---|---|
| 1 | 删掉 `DisplayView.cpp:2489` 重复的 `drawDrawer(g)` | −1 行 | 修一个可见 bug |
| 2 | **建参数查找缓存**：构造时建 `std::unordered_map<juce::String, juce::AudioProcessorParameter*>`，6 个查找函数改 O(1) | +15 行 / −60 行 | 消掉 14 万次/秒 dynamic_cast |
| 3 | **抽出 `BandPopover : public juce::Component`**：搬走 `popoverRect/drawPopover*/hitTestPopover*/popKnobs_/popSlider*/popoverValues_/popoverSliders_/popExpertRect/applyPopSlider/popParamId/togglePopover` | −400 行 | DisplayView 少 16% |
| 4 | **抽出 `LinkedDrawer : public juce::Component`**：搬走 `drawerPanelRect/hitTestDrawerButton/toggleLinkedDrawer/ensureLinkedKnobs/layoutLinkedKnobs/applyLinkedOffset/drawLinkedDrawer/linkedKnobs_/linkedOffset_/linkedAnim_` | −250 行 | 再少 10% |
| 5 | **抽出 `SpectrumView : public juce::Component`**：搬走 `drawSpectrum/refreshSpectrumPaths/spectrumFreqs_/spectrumPre_/spectrumPost_/spectrumPathsValid_/4 条 Path` | −120 行 | 再少 5% |
| 6 | **抽出 `ParamSlider` / `SegmentedButton` / `TextSelect` 三个控件**（现在只是 paint 代码） | +300 行新文件 / −150 行 | 加控件从"改 4 处"变"改 1 处" |
| 7 | **抽出 `FreqInputEditor`**：`openFreqEditor/closeFreqEditor/drawFreqInput/FreqInputState` | −100 行 | 再少 4% |

**预期结果**：`DisplayView.cpp` 约 2500 → ~1400 行，`DisplayView.h` 466 → ~200 行（测试钩子跟着分散到各自类），每个文件一个职责。

> 这一步**不改变任何视觉**，但会让后面所有视觉调整从"改一处坏三处"变成"改一处好一处"。
> 你说的"特别多的问题和逻辑错误"，我预估 80% 在这一步之后自动消失——因为它们大多是"状态散落在 2506 行里互相打架"造成的。

### 第二期：视觉对标 Pro-MB（2–3 周）

| # | 动作 | 说明 | 收益 |
|---|---|---|---|
| 1 | **加 backdrop blur（档 b）** | 按 `UI_SPEC §12.2`：4× 下采样 → `juce::ImageConvolutionKernel` 模糊 → 放大，15Hz 更新。Popover / 抽屉 / 顶栏 / 全局条四处共用 | ⭐ **单项性价比最高**。玻璃感一上就有 Pro-MB 那味 |
| 2 | **旋钮拆 BACK / OVERLAY 两层** | 现在是一个矢量旋钮每次全画。改成：刻度环画一次缓存成 `juce::Image`（resize/换主题才重建），每帧只画指针弧 + 帽 | CPU 降 + 更接近 Pro-MB 结构。**不用改位图** |
| 3 | **弹层加"顶部三角指针"** | Pro-MB 的 `CONTROLSBACKGROUND_2X` 就是深灰圆角 + 顶部中央小三角。你现在只有圆角 | 形态立刻对标 |
| 4 | **投影改成多层渐变** | 现在 `black.withAlpha(0.35)` + translate(0,3) 是一层硬边。改成 3 层：`0,1px,2px` / alpha .35/.22/.12 | 立刻"浮"起来 |
| 5 | **右刻度贴紧电平表** | `UI_SPEC §4.6` 已写（距条带 ~4px） | 双刻度更像 Pro-MB |
| 6 | **全局条文本选择器加弹出菜单** | 用抽出来的 `TextSelect` 控件，弹 Pro-MB 那种深灰圆角 + 三角指针 | 交互对标 |

### 第三期：DSP 补齐（2–4 周，按性价比排序）

| # | 动作 | 代码量 | 听感收益 |
|---|---|---|---|
| 1 | **连续 Knee 0–48 dB** | ~15 行。**直接抄 Pro-MB 的幂均值**：`p=√(knee/6)`，`S=(a^p·b^p)^(1/(2p))`；或 Future MB 的 `C(r)=70r²−20r³+35−84r` | ⭐ 最大。从"只能硬/软两档"变成"任意软硬度"，母带可用 |
| 2 | **2x 过采样** | ~20 行，JUCE `dsp::Oversampling<float>`，构造时 `addStage` 一个 `Oversampling2TimesPolyphaseIIR` | 高比率 + 快 attack 的混叠消失 |
| 3 | **立体声连续链接（lerp）** | 2 行：`t=max(2·link−1,0); g=g_L+t·(1−g_L)` | 立体声像不再跳 |
| 4 | **侧链 LP/HP（Free 模式）** | ~40 行，复用你已有的 `BiquadFilter`（已经有 `designPeak`，加 `designLowpass/highpass`） | 触发频段可调 |
| 5 | **Lookahead 提到 20 ms** | 改 `laMax_` 容量上限 + 参数范围 | 瞬态控制更准 |
| 6 | **RMS 窗从固定 10ms 改成 2^幂环形** | ~30 行 | 低频下 RMS 更稳 |

---

## 5. 如果你只做三件事

1. **删掉那行重复的 `drawDrawer`**（1 分钟，修一个可见 bug）
2. **建参数查找缓存**（半小时，消掉每秒十多万次 `dynamic_cast`）
3. **把 `process()` 的块长假设补掉**（10 分钟，消掉唯一可能崩宿主的点）

这三件事加起来不到 1 小时，**零视觉风险、零音质风险**。然后按第一期走完，再上第二期的 blur。**不要先做视觉**——结构不修，视觉改一层坏一层。

---

## 6. 附：具体 bug / 坏味道清单（按文件）

| 文件:行 | 问题 | 严重度 |
|---|---|---|
| `DisplayView.cpp:2489` | `drawDrawer(g)` 重复调用 | 🔴 可见 bug |
| `DisplayView.cpp:1193-1243` | 6 个参数查找函数 = 线性扫描 + dynamic_cast，39 个调用点在鼠标回调里 | 🟡 性能 + 坏味道 |
| `DisplayView.h:51-180` | ~250 行测试钩子挤在公开接口，说明类职责不纯 | 🟡 结构 |
| `DisplayView.h:415-453` | `kCurvePoints=241` × 4 段 × 多套 db 数组 + 4 条 Range Path + 4 条频谱 Path 全平铺在一个类里 | 🟡 结构 |
| `BandCompressor.cpp:29` | `kneeWidthDb_ = softKnee ? 12.0f : 0.0f` — 膝宽硬编码，无法连续调 | 🟡 功能缺失 |
| `BandCompressor.cpp:6,110` | RMS 窗固定 10ms，与采样率无关（`coeffFor(kRmsWindowSec*1000)` 其实是 ms→tau，逻辑对但窗长语义是 tau 不是窗） | 🟢 设计选择 |
| `MultibandDSP.cpp:223-224` | `bandStore_[b][0][i] = al; bandStore_[b][1][i] = ar;` 无条件写双通道。`chans==1` 时 `[1]` 只是冗余写入——`prepare()` 第 17-21 行对 `[0]`/`[1]` **都**做了 `assign(maxBlock_)`，**不是越界**。加个 `if (chans > 1)` 可省掉 | 🟢 微优化（非 bug） |
| `MultibandDSP.cpp:140-142` | `process()` 按 `done += maxBlock_` 分块，`n = jmin(maxBlock_, numSamples-done)`——**隐含假设 `numSamples ≤ maxBlock_`**。若宿主某次回调传入比 `prepare(maxBlockSize)` 更大的块，`bandStore_`/`outStore_`/`gStore_` 会**缓冲区溢出** | 🟡 潜在崩溃。建议 `prepare()` 里把容量上限取保守值（如 8192），或 `process()` 开头 `numSamples = jmin(numSamples, maxBlock_)` |
| `MultibandDSP.h:91-94` | `std::vector` 工作缓冲（好，预分配），但 `process()` 里每块重新取 `.data()` 指针，可上提 | 🟢 微优化 |
| `BiquadFilter.h:39-43` | `processBlock` 是逐样本循环，无 SIMD。双精度 biquad × 3 交叉点 × 4 段 × 2 通道 = 每样本 24 次双精度 MAC | 🟢 CPU（可接受，但有余量） |
| `MBTheme.h:66-95` | Typeface 已正确 static 缓存（注释记录了内存泄漏修复） | ✅ 好 |
| `PluginEditor.cpp:28` | `setSize` 后立即 `setBounds`，注释记录了 size=0 导致曲线错乱的修复 | ✅ 好 |
| `MultibandDSP.cpp:33-34` | `mixSmoothCoeff_` / `gateCoeff_` 用 `exp(-1/(tau*fs))`，与 `BandCompressor::coeffFor` 形式一致 | ✅ 好 |

> 表中 🔴 是今天就能修的；🟡 里最值得立刻处理的是 `MultibandDSP.cpp:140-142` 的块长假设——这是唯一可能崩宿主的项。mono 写入那条我一开始误判为越界，核对 `prepare()` 后确认**不是 bug**，特此更正。

---

## 7. 一句话总结

**你的 DSP 已经有商业插件的骨架（线程纪律、双精度、两个自己修掉的真 bug都是证据），UI 设计也比大多数商业项目细。差的不是能力，是把 2506 行的 `DisplayView` 拆开，然后补上 Knee / 过采样 / 立体声链接这三个 DSP 特性。**

拆完结构，你会发现自己写的 UI 规范终于有代码能承接它了。
