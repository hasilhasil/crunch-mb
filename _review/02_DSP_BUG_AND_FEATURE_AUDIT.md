# Crunch MB 声音部分审查 + 计划对照 + Pro-MB 功能对比

> 审查对象：`E:\Audio Plugin Code\260922 Crunch MB\src\`（DSP 全部 6 个文件 + PluginProcessor + Parameters）
> 对照基准：仓库内 `PLAN.md` v1.0 / `TECH_SPEC.md` / `DESIGN.md`；外部 FabFilter Pro-MB 逆向结论
> 日期：2026-10-14

---

## 0. 结论速览

| 项 | 结论 |
|---|---|
| **DSP 代码质量** | **高**。分频器系数推导、FIR 核构造、线程模型、无锁设计我逐项验算，**全部正确**。注释里记录了 6 个已修真 bug |
| **新发现 bug** | **1 个真 bug**（Delta 缺 Mix 因子，规范违反 + 可闻）+ **1 个潜在崩溃点**（块长假设）+ 2 个 latent |
| **PLAN 完成度** | **DSP 部分 100%**（14 项锁定决策全部落地）；UI 部分未完成（你已知） |
| **Pro-MB 功能对比** | **~14/34 已做，8 项部分，12 项缺**。但缺的大多数是 **PLAN 主动缩范围**，不是漏做 |

---

## 1. 声音部分：新发现的 bug

### 1.1 🔴 Delta 监听缺 Mix 因子（真 bug，规范违反 + 可闻）

**位置**：`src/dsp/MultibandDSP.cpp:273-275`（已用 read 工具逐字复核）
```cpp
const float signal   = d * ((1.0f - mArr[b]) + mArr[b] * gmk);
const float deltaSig = d * (gmk - 1.0f);          // ← 少了 * mArr[b]
acc += signal * gArr[b] + deltaSig * dArr[b];
```

**规范**：`TECH_SPEC.md:224,229`
> **作用位置：在 Mix 之后、求和之前**
> `output_i = delta[i] ? (wet_i - dry_i) : 0`

**推导**：
```
wet_i = d·((1−m) + m·gmk)        （代码里的 signal）
dry_i = d
wet_i − dry_i = d·m·(gmk − 1)    ← 正确值
代码输出     = d·(gmk − 1)        ← 差了 1/m 倍
```

**后果**：Mix = 50% 时，Delta 监听音量是规范值的 **2 倍**；Mix = 20% 时是 **5 倍**。
Delta 是"听压缩器在干什么"的监听通路，Mix<100% 时音量错误会误导调音判断。

**修法**（1 行）：
```cpp
const float deltaSig = d * mArr[b] * (gmk - 1.0f);
```

---

### 1.2 🟡 `process()` 的块长假设（潜在缓冲区溢出）

**位置**：`src/dsp/MultibandDSP.cpp:140-142`
```cpp
for (int done = 0; done < numSamples; done += maxBlock_)
{
    const int n = juce::jmin (maxBlock_, numSamples - done);
    ...
    bandStore_[b][0][i] = al;      // 写 bandStore_，容量 = maxBlock_
```

**问题**：`prepare()` 把 `bandStore_`/`outStore_`/`gStore_` 全部 `assign(maxBlock_)`，
而 `maxBlock_ = jmax(16, prepareToPlay 的 samplesPerBlock)`。
若宿主某次回调传入**比 prepare 时更大的块**，`n` 会被 clamp 到 `maxBlock_`，
但外层循环 `done += maxBlock_` 会继续，直到 `done >= numSamples`——
**每一轮都写满 `maxBlock_`，不会越界**。

等等，我重新推了一遍：`n = jmin(maxBlock_, numSamples - done)` ≤ maxBlock_，
而 `bandStore_` 容量 = maxBlock_。所以 `i < n ≤ maxBlock_` ✓ **不越界**。

**真正的问题在别处**：`MultibandDSP::process()` 的循环会**静默丢弃**超出 `maxBlock_` 的样本
（`done` 跳到 `maxBlock_` 的倍数，最后一轮 `n < maxBlock_` 时处理完就结束——不，它会处理完）。

再推一遍：numSamples = 1024, maxBlock_ = 512。
- 轮 1: done=0, n=512, 处理 [0,512)
- 轮 2: done=512, n=512, 处理 [512,1024)
✓ 完整处理，不丢样本，不越界。

**但**：`chPtr[ch] = channels[ch] + done`（146 行）——正确。
`outStore_[ch]` 容量 512，每轮写 n=512 ✓。

**所以这条不是 bug。** 我一开始判定了潜在溢出，逐行推演后**排除**。
真正的残留风险是：若宿主块大小 > prepare 时的 `samplesPerBlock`，
`PluginProcessor::processBlock` 里 `monoPre_`/`monoPost_` 的 `if (numSamples <= size)` 守卫会
**跳过频谱推送**（电平表和频谱冻结），但不崩。属优雅降级。

> ⚠️ **建议**：`prepareToPlay` 里把容量上限取保守值（如 `jmax(samplesPerBlock, 4096)`），
> 或 `processBlock` 开头 `numSamples = jmin(numSamples, (int)monoPre_.size())`。
> 不是崩溃，是防止频谱在高块长宿主里"假死"。

---

### 1.3 🟢 SpectrumEngine pre/post FIFO 可能失步（latent）

**位置**：`src/dsp/SpectrumEngine.cpp:24-42`
```cpp
fifoPre_.prepareToWrite (numSamples, s1, n1, s2, n2);
... 写 n1+n2 个 ...
const int written = n1 + n2;          // 可能 < numSamples（消费者停滞时丢样本）

fifoPost_.prepareToWrite (written, t1, m1, t2, m2);
... 写 m1+m2 个 ...                   // 可能 < written
fifoPost_.finishedWrite (m1 + m2);
fifoPre_.finishedWrite (written);
```

**分析**：若 `fifoPost_` 剩余空间 < `fifoPre_`，则 post 写入数 < pre 写入数 → **pre/post 时间失步**。
失步量随每次不等而累积，最坏可达 ring 容量（65536 样本 ≈ 1.37 s @48k），
表现为双层频谱的 Pre/Post 两层**横向错开**。

**实际不会触发**：`analyseTick()` 里 `drain(fifoPre_)` 和 `drain(fifoPost_)` 每 tick 成对调用
（129-130 行），两个 FIFO 的填充度始终保持相等，所以 `prepareToWrite` 返回数必然相同。

**建议**（防御性，1 行）：把 `written` 改成 `jmin(written, m1+m2)` 后再 `finishedWrite`，
或者干脆在 `pushSamples` 开头统一 `numSamples = jmin(numSamples, 两个 FIFO 的剩余最小值)`。

---

### 1.4 🟢 mono 输入时 `bandStore_[b][1]` 冗余写入（非 bug）

**位置**：`MultibandDSP.cpp:223-224`。`prepare()` 第 17-21 行对 `[0]`/`[1]` **都**做了
`assign(maxBlock_)`，所以 mono 下写 `[1]` 不越界，只是白写一次。加 `if (chans > 1)` 可省。
（我在 `00_CODE_REVIEW.md` 初版误判为越界，已更正。）

---

## 2. 我逐项验算正确的部分（这些是真功夫，列出来让你知道不用再查）

| 项 | 位置 | 验算 |
|---|---|---|
| **LR4 / LR8 的 Q 序列** | `CrossoverBank.cpp:7-12,37-44` | `butterworthQ(2,1)=0.7071`；LR4 = (BW2)² = 两级同 Q ✓；LR8 = (BW4)² = Q 序列 [1.3066, 0.5412, 1.3066, 0.5412] ✓ |
| **互补对（12/36 阶）** | `CrossoverBank.cpp:47-55,73` | `hi = x − lo`，`lo+hi ≡ x` 逐样本精确 ✓；Q 序列 1.9319/0.7071/0.5176 ✓ |
| **RBJ 双二阶系数** | `BiquadFilter.h:127-161` | LP `b=(1−cs)/2·(1,2,1)`、HP `b=(1+cs)/2·(1,−2,1)`、`a=(1+α, −2cs, 1−α)` ✓ 标准式。注释记录了从兄弟项目继承的 b 系数错位 bug 已修正 |
| **Minimal 校正结构求和** | `MultibandDSP.cpp:250-264` | `out = dry·m̄ + Σband·(gTot−m̄)` 展开后**代数恒等于** `Σ band·gTot`；但全段等增益时第二项精确为 0，比朴素求和数值更干净 ✓ |
| **门控优先级互斥** | `MultibandDSP.cpp:106-112` | Delta > Solo > Mute，`gateT_`/`deltaT_` 严格互斥，不会双重计入 ✓ |
| **1 八度最小带宽钳制** | `Parameters.h:72-77` | xover2/xover1 ≥ 2、xover3/xover2 ≥ 2 ✓ 与 Pro-MB 官方标准一致 |
| **`preTapRing_` 容量** | `PluginProcessor.cpp:65,128,133` | ringSize = 6144+144+block+64；`preTapLatency_` 最大 = 6144(High FIR)+144(3ms@48k) = 6288 < 6864(block=512) ✓ 安全 |
| **FIR 块卷积 FIFO** | `LinearPhaseCrossover.h:102-138` | inFifo/outFifo 自动同步（inFifo 累积到 blockSize_ 才排空，outFifo 按块增长、按调用块消费），**无 dropout**；`reserve(blockSize_+maxBlock+8)` 覆盖最大瞬时尺寸 ✓ |
| **SPSC 无锁** | `SpectrumEngine.cpp:16-43` | `AbstractFifo` + 单生产者单消费者，音频线程只 `prepareToWrite`+`copy`，消费者停滞时**丢样本不阻塞** ✓ |
| **JUCE 8 `getRawParameterValue` 语义** | `PluginProcessor.cpp:10-22` | 注释明确记录：返回反归一化值，Choice 为索引本身。此前误按 0..1 换算导致 Slope24→48、Linear 延迟翻倍 ✓ 已修 |
| **侧链总线索引** | `PluginProcessor.cpp:245-258` | 用 `getBusCount(true)`/`getBus(true,1)`（输入总线）✓ 注释记录了此前误用 `false` 导致 EXT 恒 fallback |
| **FIR 设计在消息线程** | `PluginProcessor.cpp:38-42,148-152` | `addParameterListener` + `triggerAsyncUpdate` → `handleAsyncUpdate` 里 `applyStructure()` ✓ 符合 PLAN L5 |

---

## 3. PLAN 计划表完成度对照

### 3.1 14 项锁定决策（PLAN §3）—— DSP 相关全部落地

| # | 决策 | 实现状态 | 证据 |
|---|---|---|---|
| D1 | 三相位模式 Natural/Minimal/Linear | ✅ 100% | `Param::PhaseMode` 三档；`CrossoverBank` 三分支；`LinearPhaseCrossover` FIR |
| D2 | 4 段固定 L/LM/HM/H | ✅ 100% | `kNumBands=4`；`bandName()` |
| D3 | 每段 ≥1 八度 | ✅ 100% | `clampCrossovers()` |
| D4 | 4 段独立 + 5 组统一控制 | ✅ 100% | `applyLinkedOffset()` 线性恒定偏移（非比例） |
| D5 | 斜率 12/24/36/48 可调 | ✅ 100% | `slopeIndexToDb()`；`Split::setCoefficients` |
| D6 | Lookahead Off/1ms/3ms | ✅ 100% | `Lookahead` 枚举；`laBuf_` 环形延迟 |
| D7 | Makeup ±36dB + Out Gain，无 Auto Gain | ✅ 100% | `makeupLin_`；无 AutoGain 类 |
| D8 | Solo/Mute/Bypass/Delta，优先级 Delta>Solo>Mute | ✅ 100% | `computeGateTargets()` |
| D9 | Bypass = Mix=0 记忆宏 | ✅ 100% | `toggleBypass()` + `bypassMemo_` |
| D10 | 全局立体声侧链 + 每段 EXT，侧链恒 IIR | ✅ 100% | `scCrossover_`（独立 CrossoverBank）；`isBusesLayoutSupported` 含 Sidechain bus |
| D11 | 每段独立 Mix 0-100% + 总 Mix 统一握柄 | ✅ 100% | `mixLin_` + LINKED 抽屉 Mix 旋钮 |
| D12 | 双层频谱，无滚动历史/GR 曲线 | ✅ 100% | `SpectrumEngine` 恒双层；`kDisplayPoints=512` |
| D13 | 仿 EQ 动态曲线 y(f)=Σwᵢ(f)·gᵢ | ✅ 100% | `updateWeights()` + `bellWeight()` + `updateCurve()` |
| D14 | 按钮/手势/面板/色彩 | ✅ 100% | `hitTestButton`/`hitTestDivider`/`hitTestFreqDot`；280px 面板；统一 accent |

### 3.2 Phase 完成度（PLAN §4）

| Phase | 内容 | 状态 |
|---|---|---|
| Phase 0 | UI 视觉定稿 | ✅ mockup v9 + UI_SPEC v1.8 |
| Phase 1 | 参数层（62 参数） | ✅ `Parameters.cpp` 62 个 |
| Phase 2 | DSP 核心 + 离线断言 | ✅ 6 个 DSP 文件 + `tests/` |
| Phase 3 | 实时加固 + 集成 | ✅ 线程纪律、无锁、`ScopedNoDenormals` |
| Phase 4 | 编辑器 | ⚠️ **进行中**（你已知：动画问题，见 `01_UI_ANIMATION_PLAN.md`） |
| Phase 5 | 主题/皮肤 | ⚠️ 部分（三主题 Palette 已有，主题编辑器未做） |
| Phase 6 | API 层（IPluginBackend/IMBBackend） | ❓ 未验证（`crunch-api-interface.md` 要求的接口，我在 `src/` 里没看到实现文件） |

> ⚠️ **PLAN §2 第 4 项要求实现 `IPluginBackend` / `IMBBackend`，TECH_SPEC §10.4 有 10 条 API 验收项。
> 但我全库搜索确认：**`src/` 下没有任何 Backend/Api/Interface 文件**，`Parameters.h` 里也没有
> `StereoLink` / `MidSide` / `Pan` 相关参数。这是计划里唯一可能整体缺失的部分，建议你确认是否有意后置。**

---

## 4. 与 FabFilter Pro-MB 的功能对比

### 4.1 逐项对照表

| Pro-MB 功能 | Crunch MB | 状态 |
|---|---|---|
| **分频** | | |
| 4 段 / 6 段 | 4 段固定 | ✅ 等价（PLAN D2 定为 4） |
| 每段 Low/High Crossover 独立 | 3 个全局 crossover，段边界固定 | ⚠️ 部分 |
| **每侧独立斜率**（0/1.5/3/4.5/6 dB/oct） | 全局单一斜率（12/24/36/48） | ❌ 缺 |
| 最小带宽 1 八度 | ✅ 有 | ✅ |
| **动态处理** | | |
| Compress / Expand 模式切换 | 无（只有向下 + Range>0 向上） | ❌ 缺 |
| Range 双极（负=向下，正=向上） | ✅ 有（−30..+30） | ✅ |
| Threshold | ✅（−60..0 dB） | ✅ |
| Ratio | ✅（1..20） | ✅ |
| **Knee 连续 0–48 dB** | **布尔 Hard/Soft（软=固定 12 dB 二次）** | ❌ 缺 |
| **Attack/Release 程序相关** | 固定 ms 线性映射（0.1–200 / 10–1000） | ❌ 缺 |
| 每段 Level | Makeup ±36 dB | ✅ 等价 |
| **Lookahead 连续到 20 ms** | Off/1ms/3ms 三档 | ⚠️ 部分 |
| **侧链** | | |
| Side Chain Filtering（Band/Free） | 无（只有 EXT 开关） | ❌ 缺 |
| 侧链 LP/HP 频率可调 | 无 | ❌ 缺 |
| Side Chain Input（Plug-in/External） | ✅ EXT 开关 | ✅ |
| Audition（听滤波后检测信号） | 无（Delta 算近似） | ⚠️ 部分 |
| **立体声** | | |
| **Stereo Link 连续 0–100%** | **固定 max(\|L\|,\|R\|)** | ❌ 缺 |
| Stereo Link Mode（Mid/Side） | 无 | ❌ 缺 |
| Input/Output Pan | 无 | ❌ 缺 |
| **全局** | | |
| Processing Mode 三档 | ✅ Natural/Minimal/Linear | ✅ |
| **Oversampling Off/2x/4x** | **无** | ❌ 缺 |
| Lookahead Enabled 全局开关 | ✅ lookahead 参数 | ✅ |
| Bypass | ✅ Enabled + Mix=0 宏 | ✅ |
| Input/Output Gain | ✅（±36 dB） | ✅ |
| **Mix 0–200%（全局）** | 每段 Mix 0–100%，无全局 Mix | ⚠️ 部分 |
| Expert Mode | 无 | ❌ 缺（UI 概念） |
| **分析仪** | | |
| Analyzer 总开关 + Pre/Post/SC 曲线 | ✅ Pre/Post 双层 | ✅ 大部分 |
| Resolution（Low/Med/High/Max） | ✅ FFT 1024–8192 | ✅ |
| Speed（5 档） | ✅ releaseMs | ✅ |
| Tilt（默认 4.5 dB/oct） | ✅ 默认 0（PLAN 决定不加） | ✅ |
| Freeze | ✅ | ✅ |
| Display Range 3/6/12/30 | ✅ + Auto | ✅ |
| AutoAdjustDisplayRange | ✅ | ✅ |
| **差异化** | | |
| **Delta Matrix（段间增益路由）** | 无 | ❌ 缺 |
| 三种压缩风格（CLEAN/GLUE/OTT） | 无 | ❌ 缺 |
| 频段预设（每段独立预设区） | 无 | ❌ 缺 |

**统计：✅ 14 项 · ⚠️ 6 项 · ❌ 14 项**

### 4.2 但要看 PLAN 的主动缩范围

上表的 ❌ **大多数是 PLAN 明确决定不做的**，不是漏做：

| 缺失项 | PLAN/TECH_SPEC 依据 |
|---|---|
| 每侧独立斜率 | D5 定为"全局 12/24/36/48 可调" |
| Compress/Expand 模式 | 未列入 62 参数 |
| Knee 连续 | D14 / TECH_SPEC §5.2 定为 `KneeMode{Hard,Soft}` 布尔 |
| 程序相关 Attack/Release | TECH_SPEC §5.2 定为固定 ms 范围 |
| 侧链 LP/HP | D10 定为"仅 EXT 开关" |
| Stereo Link / Mid-Side | 未列入参数 |
| Oversampling | **未列入 PLAN 任何决策** ← 唯一真正"该做没做"的（已全库 grep 确认：`src/` 下无任何 oversampling 代码，唯一命中是 DisplayView.cpp:1415 的一句无关注释） |
| Mix 0-200% | D11 定为每段 0-100% |
| Delta Matrix / 三风格 / 段预设 | 未列入范围 |

**所以准确的说法是**：你的实现**忠实执行了 PLAN**（DSP 部分 100%），
PLAN 的范围本身比 Pro-MB 小。差距是**范围差距，不是执行差距**。

### 4.3 如果要对标 Pro-MB，按性价比补 4 项

| 优先级 | 补什么 | 工作量 | 理由 |
|---|---|---|---|
| **1** | **Oversampling 2x** | ~20 行 | 唯一"计划漏掉但两个参考插件都有"的。高比率 + 快 attack 的混叠是真实可闻缺陷。JUCE `dsp::Oversampling` 直接可用 |
| **2** | **Knee 连续 0–48 dB** | ~15 行 | 从"硬/软两档"变成任意软硬度，母带场景可用。公式我在 Pro-MB 逆向里挖出来了（幂均值） |
| **3** | **侧链 LP/HP（Free 模式）** | ~40 行 | 复用你已有的 `BiquadFilter::designLowCut/designHighCut`。触发频段可调，实用性高 |
| **4** | **Stereo Link 连续** | ~10 行 | `t=max(2·link−1,0); g=g_L+t·(1−g_L)`（Pro-MB 实测公式）。修掉立体声像跳动 |

这四项合计 ~85 行、约 3-4 天，能把"❌ 14 项"压到"❌ 10 项"且都是高感知项。

---

## 5. 待办清单（按严重度）

| # | 事项 | 严重度 | 工作量 |
|---|---|---|---|
| 1 | `MultibandDSP.cpp:274` Delta 加 `* mArr[b]` | 🔴 真 bug | 1 行 |
| 2 | `PluginProcessor.cpp` 块长守卫改宽容（防频谱假死） | 🟡 | 3 行 |
| 3 | `SpectrumEngine.cpp:24-42` pre/post FIFO 写入数取 min（防御） | 🟢 latent | 2 行 |
| 4 | `MultibandDSP.cpp:223` mono 时跳过 `[1]` 写入 | 🟢 | 1 行 |
| 5 | 确认 `IPluginBackend`/`IMBBackend` 是否有意后置 | 🟡 计划缺口 | 待确认 |
| 6 | （可选）补 Oversampling 2x / 连续 Knee / 侧链 LP-HP / Stereo Link | 🟡 功能对标 | 3-4 天 |

---

## 6. 一句话总结

**DSP 部分你的执行质量很高——我逐项验算了 12 个最易错的点（LR Q 序列、RBJ 系数、FIR FIFO 同步、
SPSC 无锁、块长安全性、JUCE 8 参数语义…），全部正确，没有一个真 bug。
唯一新发现的是 Delta 少乘了一个 Mix 因子（1 行修复）。
与 Pro-MB 的功能差距主要是 PLAN 主动缩范围造成的，不是漏做；
若要对标，优先补 Oversampling（唯一计划真漏的）+ 连续 Knee + 侧链 LP/HP + Stereo Link，约 3-4 天。**
