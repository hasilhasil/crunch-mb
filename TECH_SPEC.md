# Crunch MB — 四段多带压缩器 技术规范文档（TECH_SPEC）

> 本文件是 Crunch MB 的工程实施规范。所有代码实现必须遵守本文件的参数规格、
> 架构约定、实时约束与 API 映射。与 DESIGN.md 配套阅读。

---

## 1. 技术栈与版本

| 项目 | 取值 | 说明 |
|------|------|------|
| 语言标准 | C++20（`CMAKE_CXX_STANDARD 20`，扩展关闭） | 与 EQ / Compressor 一致 |
| 构建系统 | CMake ≥ 3.24 | 与 EQ / Compressor 一致 |
| 音频框架 | JUCE **8.0.4** | 与 EQ / Compressor 一致，内嵌 JUCE 源码树 |
| 插件外壳 | `juce_add_plugin` + `juce_generate_juce_header` | — |
| 参数管理 | `juce::AudioProcessorValueTreeState`（APVTS） | 全部 62 参数 |
| DSP 库 | `juce_dsp`（FFT/IIR）+ 自研 DSP 模块 | — |
| 编译选项 | MSVC: `/utf-8 /permissive-`；其他: `-Wall -Wextra` | — |
| 总线布局 | 立体声主输入 + 立体声主输出 + **立体声侧链输入** | 侧链为新增；实现侧布局协商宽容（兼容 mono/禁用主输入的宿主提议——FL Studio 实测要求，默认规格不变，2026-09-23） |
| MIDI | 不需要 | — |

**插件标识**：

| 项 | 值 |
|----|-----|
| PRODUCT_NAME | `Crunch MB` |
| COMPANY_NAME / BUNDLE_ID | `Kilo` / `com.kilo.crunchmb` |
| VERSION | 1.0.0 |
| Manufacturer / Plugin Code | `Kilo` / `CRMB` |
| FORMATS | VST3、Standalone |
| 类别 | Fx |
| 类名 | `CrunchMB`（target）/ `CrunchMBAudioProcessorEditor`（EDITOR_NAME） |

---

## 2. 目录结构

```
260922 Crunch MB/
├── CMakeLists.txt              # JUCE CMake 构建
├── README.md                   # 构建说明
├── DESIGN.md                   # 产品设计文档
├── TECH_SPEC.md                # 本文件（技术规范）
├── .gitignore
└── src/
    ├── PluginProcessor.h/.cpp  # AudioProcessor + APVTS + 音频线程 + 环形缓冲区
    ├── PluginEditor.h/.cpp     # 顶层编辑器
    ├── Parameters.h/.cpp       # 参数 ID / 枚举 / 范围 / 布局工厂 / 钳制逻辑
    ├── dsp/
    │   ├── BiquadFilter.h      # RBJ 双二阶（从 EQ/Compressor 复用，字节级一致）
    │   ├── CrossoverBank.h/.cpp   # 四段交叉分频器组（三相位模式）
    │   ├── BandCompressor.h/.cpp  # 单段压缩器（从 Compressor.h 演化，去掉 lookahead/color）
    │   ├── MinimalPhaseDSP.h/.cpp  # Minimal Phase 模式 DSP（时变增益）
    │   ├── LinearPhaseCrossover.h/.cpp  # 线性相位 FIR 交叉分频器（复用 LinearPhaseFilter 架构）
    │   ├── SpectrumAnalyzer.h/.cpp   # FFT 分析仪（从 EQ 逐字节复用）
    │   ├── SpectrumEngine.h/.cpp     # 频谱引擎（从 EQ 复用：锁-free ring + 后台线程 + 三缓冲）
    │   └── MultibandDSP.h/.cpp    # 多带压缩主引擎（编排 4 段 + 交叉分频 + Mix + Lookahead）
    └── ui/
        ├── LookAndFeel.h       # 主题 + 控件绘制（从 EQ/Compressor 复用；显示区四段统一 accent 色，无新增色值）
        ├── DisplayView.h/.cpp  # 频段分区 / 双层频谱 / 仿EQ动态总曲线 / 传输曲线 / 交叉点拖拽 / 电平表（无 GR 曲线）
        ├── BandControlPanel.h/.cpp  # 频段选择 + 参数旋钮 + 统一控制 + 全局控制
        └── GroupControlButton.h     # 统一控制按钮组件（按住拖动统一修改）
```

---

## 3. DSP 架构

### 3.1 多带压缩主引擎（MultibandDSP）

```
processBlock:
  1. updateParameters()          — 读 APVTS 缓存差分，更新各模块参数
  2. updateLatency()             — 根据 lookahead + phaseMode 计算并上报延迟
  3. inputGain                   — 总输入增益
  4. lookaheadDelay              — 全局前瞻延迟线（如果启用）
  5. crossoverSplit              — 交叉分频器将输入分为 4 段
  6. sidechainSplit              — 侧链信号同参数分频（如果 EXT 启用）
  7. perBandProcess              — 对每段：
     a. 选择检测源（INT/EXT）
     b. BandCompressor.processSample()
     c. Makeup Gain
     d. Mix（平行压缩：干/湿混合）
     e. Delta/Solo/Mute 监听处理（优先级 Delta > Solo > Mute）
  8. sumBands                    — 四段求和
  9. outputGain                  — 总输出增益
  10. spectrumPush               — 单声道 pre/post 采样送 SpectrumEngine（锁-free ring）
  11. recordHistory              — 记录电平历史（in/out，仅供 API）到环形缓冲区
  12. publishMeters              — atomic store 峰值/实时GR（GR 仅供 API，UI 不渲染）
```

### 3.2 交叉分频器组（CrossoverBank）

**三模式架构**：

#### 模式 0：Natural Phase（LR4 IIR）
- **拓扑**：级联式（串行分解树）
  ```
  Input → LR4_LP(fc1) ──────────────────────────────→ Band L
       → LR4_HP(fc1) → LR4_LP(fc2) ──────────────────→ Band LM
                     → LR4_HP(fc2) → LR4_LP(fc3) ───→ Band HM
                                   → LR4_HP(fc3) ───→ Band H
  ```
- **滤波器**：Linkwitz-Riley 4 阶 = 2 × Butterworth 2 阶级联（Q = 0.707）
- **斜率映射**：12 dB/oct = LR2（1×Butterworth 2 阶）、24 dB/oct = LR4（2×BW2）、
  36 dB/oct = LR6（3×BW2）、48 dB/oct = LR8（4×BW2）
- **重建特性**：LR 对在交叉点相加恒为全通（|H_LP| + |H_HP| = 1），幅度平坦
- **延迟**：0 样本
- **实现**：复用 `BiquadFilter`（Transposed Direct Form II，double 精度系数）

#### 模式 1：Minimal Phase（时变增益）
- **核心原理**：不在音频通路中真正分频。用带通检测器测量各频段电平，
  计算每段增益，再以时变频率相关增益形式施加到完整信号上
- **信号路径**：
  ```
  Input ──┬── [带通检测器×4] → BandCompressor×4 → gain L/LM/HM/H
          │
          └── [时变增益滤波器] → Output
              ↑ 4 个增益值驱动一个频率相关增益函数
  ```
- **增益施加方式**：构建一个 4 频段的时变增益滤波器（partition-of-unity）：
  - 用 LR2（12 dB/oct）滤波器做频带权重 `w_i(f)`，满足 `Σ w_i(f) = 1`
  - 瞬时增益 `G(f, t) = Σ w_i(f) · g_i(t)`，其中 `g_i(t)` 为第 i 段压缩增益
  - 当所有 `g_i(t) = 1`（0 dB）时，`G(f,t) = 1`（完全平坦）→ 信号零修改
  - 当部分 `g_i(t) ≠ 1` 时，仅该频段增益变化，相位偏移正比于增益变化量
- **零修改保证**：当 4 段增益均为 0 dB（即无压缩发生）时，
  时变增益滤波器退化为标量 ×1 乘法，信号逐样本完全不变
- **实现复杂度**：高。核心是将 4 个时变增益通过平滑频率权重施加到信号上
- **延迟**：0 样本

#### 模式 2：Linear Phase（FIR）
- **设计方法**：频率采样法
  1. 在 FFT 网格上采样各频段目标幅度响应
  2. 构造对称线性相位谱
  3. IFFT 得到 FIR 冲激响应
- **卷积**：overlap-save FFT 卷积（复用 LinearPhaseFilter 架构）
- **质量档位**（实测回写——PLAN §5 结论③，Phase 2 断言④ 脉冲验证 ✓）：
  | 档位 | FFT 尺寸 | 核长 L | 总延迟 = pad + (L−1)/2 | @48 kHz | 适用场景 |
  |------|---------|-------|------------------------|---------|---------|
  | Low | 2048 | 1025 | **1536 样本** | 32 ms | 快速工作/低延迟需求 |
  | Medium | 4096 | 2049 | **3072 样本** | 64 ms | 一般使用（默认） |
  | High | 8192 | 4097 | **6144 样本** | 128 ms | 精度处理/母带 |
- **延迟上报**：`setLatencySamples(blockSize + (irLength-1)/2)`（FirFilter 含 callSize
  固定输出延迟环：总延迟与宿主调用块大小无关，脉冲峰位实测 = 上报值）
- **预振铃**：不可避免（线性相位固有特性），质量越高越明显

> **实现注记（2026-09-23 用户决策修订：分段干净优先）**：
> 四段求和拓扑按斜率分流：
> - **24/48 dB/oct（4/8 阶）全部 IIR 模式** = LR 树（LP/HP 独立链）：分段干净
>   （50Hz→band0 90%+、2400Hz→band2 90%+ @T10b），内侧交叉点求和涟漪
>   LR4 0.489dB / LR8 1.729dB 实测（断言① 容差 ±0.6/2.0dB）
> - **12/36 dB/oct（2/6 阶）** = 恒等重建互补对（hi := x − lo）：LR 不存在于
>   奇数阶，逐样本求和精确（断言① ±0.01dB / ② <1e-6）；HP 通道有相位感知
>   泄漏（BW 在 fc 下方 4 八度相位已 −37.7° → x−lo 残留 30% 功率）
> - **Linear Phase** = FIR 恒等核（零相位 ⇒ 分段干净且求和精确，代价是延迟）
>
> **修订缘由（用户 2026-09-23 决策）**：此前 Minimal 全斜率用互补对以求"求和
> 精确"，但其相位泄漏使低频 30% 落入 LM 段被独立压缩——切换模式后声音差异大、
> 用户需重调设置。用户明确"分频干净更重要"：现 Minimal 在 24/48 档与 Natural
> 同拓扑（LR 树），实测总输出 **0dB 差、bit 级一致**（T14.2）；Linear 与 IIR
> 差 0.12dB（BW4 FIR vs LR4 形状，T14.3）。三模式切换不再需要重调设置。

### 3.3 单段压缩器（BandCompressor）

从 Crunch Compressor 的 `Compressor` 类演化，**去掉 lookahead 和 color**。

| 模块 | 实现 |
|------|------|
| 架构 | 前馈式（Feed-Forward），逐样本处理 |
| 检测器 | Peak（L/R 联动最大绝对值）/ RMS（10ms 积分窗） |
| 增益计算 | 硬拐点 = 线性插值；软拐点 = 12dB 二次插值 |
| Attack/Release | 一阶指数平滑，独立系数 |
| Makeup | 手动增益补偿（−36…+36 dB） |
| Mix | 平行压缩：`out = mix×wet + (1−mix)×dry`，10ms 平滑 |
| 延迟 | 0（lookahead 在 MultibandDSP 层统一处理） |

**每段独立**：4 个 BandCompressor 实例完全独立，不共享参数。

### 3.4 Lookahead

- **全局统一**：一个 Lookahead 设置作用于全部 4 段
- **实现**：输入信号经环形延迟线，检测器看到当前样本、增益施加于延迟后样本
- **延迟上报**：启用时 `setLatencySamples(lookaheadSamples)`
- **与相位模式的关系**：lookahead 延迟与相位模式延迟可叠加
  - Natural/Minimal Phase + lookahead = lookahead 延迟
  - Linear Phase + lookahead = FIR 延迟 + lookahead 延迟

### 3.5 延迟规格汇总

| 相位模式 | Lookahead | 总延迟（48 kHz） | 备注 |
|----------|-----------|-----------------|------|
| Natural Phase | Off | **0 样本** | — |
| Natural Phase | 1 ms | 48 样本 | — |
| Natural Phase | 3 ms | 144 样本 | — |
| Minimal Phase | Off | **0 样本** | — |
| Minimal Phase | 1 ms | 48 样本 | — |
| Minimal Phase | 3 ms | 144 样本 | — |
| Linear Phase (Low) | Off | **1536 样本（32 ms）** | 实测回写 |
| Linear Phase (Medium) | Off | **3072 样本（64 ms）** | 默认；实测回写 |
| Linear Phase (High) | Off | **6144 样本（128 ms）** | 实测回写 |
| Linear Phase (Medium) | 3 ms | **3216 样本（67 ms）** | 叠加（Phase 3 实测确认） |

> 实际样本数以 `getLatencySamples()` 为准。宿主自动 PDC。

### 3.6 侧链处理

- **总线声明**：JUCE `BusesProperties` 增加 sidechain input bus
  ```cpp
  .withInput("Input", stereo(), true)
  .withOutput("Output", stereo(), true)
  .withInput("Sidechain", stereo(), true)  // 可选侧链
  ```
- **侧链分频**：外部侧链信号经**独立 IIR 分频器**（同 fc / 同斜率，恒 Natural 形状）
  分频为 4 段，供各段 EXT 模式使用（FIR 设计不上音频线程，侧链恒 IIR）
- **INT/EXT 切换**：每段检测器输入源独立选择
  - INT：本段自身音频信号
  - EXT：外部侧链同频段信号（侧链总线禁用/无通道时 fallback 到 INT）
- ⚠️ **总线索引**：侧链在**输入**总线 1，查找必须用 `getBusCount(true)` /
  `getBus(true, 1)` / `getBusBuffer(buffer, true, 1)`——误用 `false`（输出总线）
  会永远取不到侧链（2026-09-23 修复，断言 T13.2 锁定）

### 3.7 Delta / Solo / Mute 监听处理

- **作用位置**：在 Mix 之后、求和之前（只影响输出，不影响检测/压缩）
- **优先级（已定）**：Delta > Solo > Mute
- **逻辑**：
  ```
  if (anyDelta):
      output_i = delta[i] ? (wet_i - dry_i) : 0   // 仅输出 Delta 段的湿-干差值
  else if (anySolo):
      output_i = solo[i] ? signal_i : 0
  else:
      output_i = mute[i] ? 0 : signal_i
  ```
- wet_i / dry_i 即平行压缩双路径的现成信号，Delta 无新增 DSP 模块
- **平滑**：状态切换经 ~5 ms 增益平滑，避免爆音
- **Bypass（方案 A UI 宏）**：按下调 Mix=0% 并记忆、再按恢复；DSP 上等效 Mix=0
  （干侧直通），无独立旁路路径（详见 DESIGN.md §6.6）

### 3.8 最小带宽钳制

- **约束**：每分区至少 1 个八度（频率比 ≥ 2:1）
- **钳制规则**（参数变化时执行）：
  ```
  xover1 = clamp(xover1, 40, xover2 / 2)
  xover2 = clamp(xover2, max(xover1 * 2, 80), xover3 / 2)
  xover3 = clamp(xover3, max(xover2 * 2, 160), 10000)
  ```
- **执行时机**：`updateParameters()` 中检测到 xover 变化时，先钳制再更新分频器
- **UI 侧**：交叉点拖拽时实时钳制，到边界时停止移动

---

## 4. 音频线程红线（任何阶段不得违反）

1. **零堆分配**：processBlock 热路径无 new / resize / push_back / vector 构造
   ——所有缓冲区在 `prepareToPlay` 预分配
2. **零锁**：参数读取只经 `std::atomic` + 缓存差分（`updateParameters()`）；
   显示数据经环形缓冲区（release/acquire）无锁传递
3. **零阻塞**：不等待任何线程、不调用任何可能阻塞的系统 API
4. **ScopedNoDenormals**：processBlock 开头必须声明
5. **参数安全**：⚠️ **`apvts.getRawParameterValue()` 返回 `getRawDenormalisedValue()`
   （反归一化值），不是 normalized 0..1**——Choice 参数是**索引本身**（0/1/2…）、
   Bool 是 0/1、Float 是物理值。读取 Choice 必须 `(int) lround(raw)`，
   **绝不能再乘 `(n−1)`**（2026-09-23 Studio One 实测事故：默认 Medium 被误算成
   High → Linear 延迟 3072→6144 翻倍、Slope24→48；断言 T11.2/T11.3 锁定该语义）
6. **非有限值防护**：输出检测 NaN/Inf/超大值（>1e12），异常时清零并复位滤波器状态
7. **延迟上报**：lookahead/相位模式变化时经 `setLatencySamples()` 动态上报；
   lookahead 部分必须从 APVTS 参数直接换算（勿读音频线程的 `laSamples_`——
   切换瞬间消息线程可能读到旧值）；采样率用 prepareToPlay 缓存值（勿依赖
   `getSampleRate()`，部分宿主/测试路径未调 `setPlayConfigDetails` 时返回 0）
8. **测试 harness 禁忌**：console 测试**不可调用 `MessageManager::stopDispatchLoop()`**
   ——它把 `quitMessagePosted` 永久置位，导致后续所有 `Message::post()` 失败
   （AsyncUpdater 投递静默死亡）。驱动异步更新用 `handleUpdateNowIfNeeded()`

---

## 5. 参数规格（精确）

### 5.1 全局参数

| ID | 类型 | 范围 | 步长 | 默认 | 单位 | 偏斜 | 显示格式 |
|----|------|------|------|------|------|------|---------|
| `enabled` | Bool | — | — | true | — | — | — |
| `phaseMode` | Choice | Natural / Minimal / Linear | — | Natural | — | — | — |
| `linearQuality` | Choice | Low / Medium / High | — | Medium | — | — | — |
| `xoverSlope` | Choice | 12 / 24 / 36 / 48 dB/oct | — | 24 dB/oct | — | — | — |
| `xover1` | Float | 40…2500 Hz | 1.0 | 200 | Hz | 对数(中心 200) | 整数或 1 位小数 |
| `xover2` | Float | 80…5000 Hz | 1.0 | 1000 | Hz | 对数(中心 1000) | 同上 |
| `xover3` | Float | 160…10000 Hz | 1.0 | 6000 | Hz | 对数(中心 6000) | 同上 |
| `inputGain` | Float | −36…+36 dB | 0.1 | 0 | dB | 线性 | 2 位小数 |
| `outputGain` | Float | −36…+36 dB | 0.1 | 0 | dB | 线性 | 2 位小数 |
| `lookahead` | Choice | Off / 1 ms / 3 ms | — | Off | — | — | — |

### 5.2 每频段参数（13 个 × 4 段；i = 0(L) / 1(LM) / 2(HM) / 3(H)）

| ID 模板 | 类型 | 范围 | 步长 | 默认 | 单位 | 偏斜 | 显示格式 |
|---------|------|------|------|------|------|------|---------|
| `band{i}Threshold` | Float | −60…0 | 0.1 | −20 | dB | 线性 | 2 位小数 |
| `band{i}Ratio` | Float | 1…20 | 0.1 | 4 | : 1 | 线性 | 2 位小数 |
| `band{i}Attack` | Float | 0.1…200 | 0.1 | 10 | ms | 对数(中心 10) | ≥1 取整，<1 保留 1 位 |
| `band{i}Release` | Float | 10…1000 | 0.5 | 100 | ms | 对数(中心 100) | 同上 |
| `band{i}Makeup` | Float | −36…+36 | 0.1 | 0 | dB | 线性 | 2 位小数 |
| `band{i}Mix` | Float | 0…100 | 0.1 | 100 | % | 线性 | 整数 |
| `band{i}Range` | Float | −30…+30 | 0.1 | −30 | dB | 线性 | 2 位小数 |
| `band{i}Knee` | Choice | Hard / Soft | — | Soft | — | — | — |
| `band{i}Detector` | Choice | Peak / RMS | — | Peak | — | — | — |
| `band{i}Solo` | Bool | — | — | false | — | — | — |
| `band{i}Mute` | Bool | — | — | false | — | — | — |
| `band{i}Ext` | Bool | — | — | false | — | — | — |
| `band{i}Delta` | Bool | — | — | false | — | — | — |

### 5.3 参数 ID 命名

全局：camelCase 单词（`inputGain`、`phaseMode`、`xover1`、`xoverSlope`、`linearQuality`、`lookahead`）

频段：`"band" + i + PascalCase 字段`

```cpp
// Parameters.h
namespace Param {
    inline juce::String bandThreshold(int i) { return "band" + juce::String(i) + "Threshold"; }
    inline juce::String bandRatio    (int i) { return "band" + juce::String(i) + "Ratio"; }
    inline juce::String bandAttack   (int i) { return "band" + juce::String(i) + "Attack"; }
    inline juce::String bandRelease  (int i) { return "band" + juce::String(i) + "Release"; }
    inline juce::String bandMakeup   (int i) { return "band" + juce::String(i) + "Makeup"; }
    inline juce::String bandMix      (int i) { return "band" + juce::String(i) + "Mix"; }
    inline juce::String bandKnee     (int i) { return "band" + juce::String(i) + "Knee"; }
    inline juce::String bandDetector (int i) { return "band" + juce::String(i) + "Detector"; }
    inline juce::String bandSolo     (int i) { return "band" + juce::String(i) + "Solo"; }
    inline juce::String bandMute     (int i) { return "band" + juce::String(i) + "Mute"; }
    inline juce::String bandExt      (int i) { return "band" + juce::String(i) + "Ext"; }
    inline juce::String bandDelta    (int i) { return "band" + juce::String(i) + "Delta"; }
}
```

宿主显示名：`"Low Threshold"` / `"Low Mid Ratio"` / `"High Mid Attack"` / `"High Release"` 等。
频段名映射：`{"Low", "Low Mid", "High Mid", "High"}`。

### 5.4 枚举定义

```cpp
namespace Param {
    constexpr int kNumBands = 4;
    constexpr int kLowBand = 0, kLowMidBand = 1, kHighMidBand = 2, kHighBand = 3;

    enum class PhaseMode { Natural = 0, Minimal = 1, Linear = 2 };
    enum class KneeMode  { Hard = 0, Soft = 1 };
    enum class Detector  { Peak = 0, RMS = 1 };
    enum class Slope     { Slope12 = 0, Slope24 = 1, Slope36 = 2, Slope48 = 3 };
    enum class Lookahead { Off = 0, Ms1 = 1, Ms3 = 2 };
    enum class LinearQuality { Low = 0, Medium = 1, High = 2 };

    inline int slopeIndexToDb(int idx)   { return 12 * (jlimit(0, 3, idx) + 1); }
    inline float lookaheadIndexToMs(int idx) {
        switch (jlimit(0, 2, idx)) { case 1: return 1; case 2: return 3; }
        return 0;
    }
}
```

---

## 6. 显示数据通路

### 6.1 环形缓冲区（复用 Compressor 架构，扩展为多段）

- **容量**：`kHistoryCapacity = 32768`（~16 s @ 0.5 ms/点）
- **每点记录**：`histIn_`（总输入电平）、`histOut_`（总输出电平）——共 2 条曲线。
  不记录 GR 历史通道（UI 不显示 GR 曲线；实时 GR 经 `getMeteringSnapshot` 提供，
  后续如需 GR 历史可低成本加回）
- **写入**：audio thread 每 `historyStride_`（sr × 0.0005）样本写一次
- **读取**：仅 API `readMultibandHistory` 消费；UI 不再读取（显示已改双层频谱，决策 7）
- **同步**：`std::atomic<unsigned> historyCounter_`（release/acquire，unsigned 回绕安全）
- **预分配**：`prepareToPlay` 中一次性分配，之后永不 resize

### 6.2 传输曲线

- `getTransferCurve(bandIndex, minDb, maxDb, points, inDb, outDb)`
- 静态计算，参数变化时标记 dirty，UI 下次 paint 时重算
- 每段独立传输曲线（threshold/ratio/knee/makeup 不同）

### 6.3 峰值/GR 原子量

```cpp
std::atomic<float> inputPeakDb_ { -100.0f };
std::atomic<float> outputPeakDb_ { -100.0f };
std::atomic<float> gainReductionDb_[4];  // 每段实时 GR：仅供 API/metering，UI 不渲染
```

### 6.4 交叉点/频段响应曲线 与 仿 EQ 动态总曲线

- 频段响应：交叉分频器各段幅度响应 `getBandMagnitudeDb(bandIndex, freq)`，
  参数变化时标记 dirty，UI 重算并绘制
- **仿 EQ 动态总曲线（已定 8A）**：`y(f) = Σ w_i(f) · g_i`
  - `w_i(f)` = 交叉分频权重（Natural/Linear 用实际分频响应；Minimal 用检测器权重）
  - `g_i` = 该段当前施加增益 = `−GR_i（平滑后） + Makeup_i`，音频线程 atomic 发布
  - 无压缩时 `g_i = Makeup_i`（纯 Makeup 台阶）；压缩时对应段向下凹陷
  - 频率 x 轴与频谱层共用；增益 y 轴为**独立刻度层**（Pro-MB 双刻度方案，
    典型 0…−24 dB，随最大 GR 自动扩展）
  - UI 每 tick 重建曲线（128–256 点，Path 容量复用，无分配）

### 6.5 双层频谱引擎（已定 7，移植自 Crunch EQ）

- **组成**：`SpectrumEngine`（juce::Thread 后台线程）+ `SpectrumAnalyzer`（FFT），
  从 EQ 源码逐字节复用（512 点对数频率、Hann 窗、16 ms 限帧、三缓冲发布）
- **信号抽头**：Pre = 输入增益前（对齐 IN 电平表），Post = 最终输出（对齐 OUT 电平表）；
  mono 求和后经锁-free `AbstractFifo` 推入
- **绘制**：底层 Pre 灰层（`inputFill` 样式，EQ 同款渐变填充）+
  上层 Post accent 层——与 EQ / Pro-MB 双层观感一致
- **生命周期**：每实例 1 后台线程；`uiActive_` 门控——editor 打开才分析，
  关闭即休眠（修正 EQ 的构造即常驻问题）
- **红线**：音频线程仅 `pushSamples`（锁-free ring，零分配零阻塞）

---

## 7. 状态序列化

- 与 EQ / Compressor 一致：`apvts.copyState()` → ValueTree → XML → `copyXmlToBinary`
- **不入持久化状态**：theme / editorWidth / editorHeight / selectedBand（进程内 atomic）
- 62 个参数全部由 APVTS 自动序列化，无额外序列化逻辑

---

## 8. API 接口设计

> **接口规范详见 `crunch-api-interface.md`**（Crunch 系列统一后端接口规范，v1.0）。
> Crunch MB 原生实现 `IMBBackend` facade（继承 `IPluginBackend`），从第一个 commit 起
> UI 与外部调用方统一经 facade 访问后端。传输层适配由接入方自行实现。

### 8.1 Facade 实现要求

- 实现 `IPluginBackend` 全部通用接口（参数面 / 状态面 / 实时数据面 / 分析面 / 插件面）
- 实现 `IMBBackend` 多段扩展接口（`readMultibandHistory` / `setBandGroupDelta` /
  `setBandGroupDelta` / `setCrossover` / `getBandGainReduction`）
- 参数元数据（`ParamInfo`）覆盖全部 62 参数
- 所有物理量使用实际值（dB / Hz / ms / %），内部处理 APVTS normalized 转换
- 值型拒绝：业务拒绝通过返回值字段表达，参数越界时钳制

### 8.2 接口与 DSP 层的映射

| Facade 方法 | DSP/Processor 数据源 | 线程安全 |
|-------------|---------------------|---------|
| `getParam` / `setParam` | APVTS typed parameter views | ✅ |
| `getMeteringSnapshot` | atomic<float> peaks/GR | ✅ RT_SAFE |
| `readMultibandHistory` | 环形缓冲区（lock-free） | ✅ RT_SAFE |
| `getTransferCurve` | BandCompressor 静态增益函数 | 消息线程 |
| `getFrequencyResponse` | CrossoverBank 幅度响应 | 消息线程 |
| `setBandGroupDelta` | APVTS × 4 段批量写入 | 消息线程 |
| `setCrossover` | APVTS + 最小带宽钳制 | 消息线程 |
| `getStateSnapshot` / `loadState` | APVTS XML 序列化 | 消息线程 |

---

## 9. 构建方式

```powershell
# 本地 JUCE（离线）
cmake -B build -A x64 -DJUCE_DIR="<JUCE-8.0.4 目录>" "260922 Crunch MB"
cmake --build build --config Release

# 或联网：省略 -DJUCE_DIR，由 FetchContent 拉取 JUCE 8.0.4
```

- 产物路径：`build\CrunchMB_artefacts\Release\VST3\Crunch MB.vst3`
- 链接库：`juce_audio_plugin_client`、`juce_audio_devices`、`juce_audio_utils`、
  `juce_dsp`、`juce_recommended_config_flags`

---

## 10. 验收标准

### 10.1 功能验收
- [x] 84 参数全部经 APVTS 注册、可自动化、正确序列化（62 → 84：oversampling + 每段侧链滤波×3 + StereoLink + Dynamics + globalMix）
- [ ] 四频段压缩器独立工作，各参数独立生效
- [ ] 三种相位模式正确切换，延迟正确上报
- [ ] 交叉点拖拽 + 最小带宽约束（1 八度）正确执行
- [ ] 交叉点斜率 12/24/36/48 dB/oct 正确切换
- [ ] Lookahead Off/1/3 ms 正确实现 + 延迟上报
- [ ] Delta/Solo/Mute 正确切换（优先级 Delta > Solo > Mute），切换无爆音
- [ ] Delta 监听输出 = 该段 wet−dry 差值，无新增失真
- [ ] EXT 外部侧链切换正确（INT/EXT），无外部侧链时 fallback
- [ ] 每段独立 Mix（平行压缩）正确
- [ ] 统一控制五握柄（已定 6A）：按住拖动线性恒定偏移，4 段钳制正确
- [ ] Bypass 宏：按下 Mix=0% 且记忆、再按恢复；DSP 等效 Mix=0%（完全透明）
- [ ] 双层频谱（Pre 灰 / Post accent）正常刷新，editor 关闭时引擎休眠
- [ ] 仿 EQ 动态总曲线随压缩实时凹陷（8A），增益刻度独立于频谱刻度
- [ ] 显示区四段曲线/底块/状态点/段名统一使用主题 accent 色（三主题下正确切换；原 5A 四色已取消）
- [ ] 面板无 Solo/Mute/Bypass/Delta 镜像按钮（与显示区功能重复已删），仅保留 SIDECHAIN EXT

### 10.2 音质验收
- [ ] Natural Phase 模式：LR4 交叉分频重建后幅度恒平坦（±0.01 dB）
- [ ] Minimal Phase 模式：无压缩时输出 = 输入（逐样本误差 < 1e-6）
- [ ] Linear Phase 模式：群延迟恒定，幅度平坦
- [ ] 无谐波失真（THD < 0.001%，排除 Minimal Phase 增益变化时的微小相位效应）
- [ ] 无 zipper noise（参数自动化平滑）
- [ ] 无爆音（Solo/Mute/EXT 切换平滑）

### 10.3 性能验收
- [ ] 音频线程零堆分配（warm-up 后 processBlock 无 malloc）
- [ ] 音频线程零锁
- [ ] CPU 使用率 < 5%（48 kHz / 512 buffer / 4 段 / Natural Phase）
- [ ] UI 60 fps 流畅（窗口拖拽不掉帧）
- [ ] 编辑器打开/关闭无内存泄漏
- [ ] 频谱后台线程不阻塞音频线程（pushSamples 全程无锁）

### 10.4 API 验收
- [x] `IPluginBackend` 全部通用接口正确实现（`1d852e6` + T52）
- [x] `IMBBackend` 多段扩展接口正确实现（`1d852e6` + T52）
- [x] 84 参数的 `ParamInfo` 元数据完整（范围/默认值/单位/类别）——由 APVTS 派生，不会与注册表漂移
- [x] `setParam` 实际值 ↔ normalized 转换正确，越界钳制正确
- [x] `setBandGroupDelta` 线性恒定偏移正确（非比例缩放），4 段钳制独立
- [x] `setCrossover` 最小带宽钳制正确（1 八度）
- [x] `readMultibandHistory` 无锁读取正确（counter 对齐）
- [x] `getTransferCurve` / `getFrequencyResponse` 返回值正确（三相位模式各自的响应算法）
- [x] `getStateSnapshot` / `loadState` 序列化往返无损（含幂等性）

