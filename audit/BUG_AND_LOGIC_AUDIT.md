# Crunch MB (v1.0.4) 源码静态筛查报告：BUG 与逻辑谬误汇总

> **审查范围**：`260922 Crunch MB/src/` 下全部 C++20 源码（Processor、DSP、API Facade、UI 组件）与构建配置 `CMakeLists.txt`  
> **审查性质**：只读静态筛查（未修改业务源码）

---

## 目录

1. [致命 / 高危级 BUG（并发竞争、延迟错算、音频状态污染）](#一-致命--高危级-bug并发竞争延迟错算音频状态污染)
   - [BUG-01：消息线程与音频线程无锁并发重分配（Data Race / Use-After-Free 崩溃隐患）](#bug-01消息线程与音频线程无锁并发重分配data-race--use-after-free-崩溃隐患)
   - [BUG-02：过采样开启时，宿主延迟上报（PDC）与频谱 Pre 抽头延迟环发生“采样率单位混加”](#bug-02过采样开启时宿主延迟上报pdc与频谱-pre-抽头延迟环发生采样率单位混加)
   - [BUG-03：Auto-Gain 响度计（LoudnessMeter）双声道共用单声道滤波器状态，且 400ms 时间窗参数失效](#bug-03auto-gain-响度计loudnessmeter双声道共用单声道滤波器状态且-400ms-时间窗参数失效)
   - [BUG-04：LinearPhaseCrossover 固定延迟环在可变块长下延迟漂移，且全局 Mix 干湿声未做延迟对齐](#bug-04linearphasecrossover-固定延迟环在可变块长下延迟漂移且全局-mix-干湿声未做延迟对齐)
2. [算法与 DSP 逻辑谬误（名实不符、数学/物理逻辑错误）](#二-算法与-dsp-逻辑谬误名实不符数学物理逻辑错误)
   - [LOGIC-01：BandCompressor 在向上压缩（Range > 0）或扩展模式（Expand）下 Attack / Release 系数选反](#logic-01bandcompressor-在向上压缩range--0或扩展模式expand下-attack--release-系数选反)
   - [LOGIC-02：PhaseMode::Natural 与 Minimal 在分频器与求和器中数学恒等，“校正结构”形同虚设](#logic-02phasemodenatural-与-minimal-在分频器与求和器中数学恒等校正结构形同虚设)
   - [LOGIC-03：外部侧链（EXT）在分块循环中漏加 `done` 偏移，且超大块回退路径破坏过采样率](#logic-03外部侧链ext在分块循环中漏加-done-偏移且超大块回退路径破坏过采样率)
   - [LOGIC-04：BiquadFilter.h 二阶 LowCut / HighCut 命名与传递函数极性完全反置](#logic-04biquadfilterh-二阶-lowcut--highcut-命名与传递函数极性完全反置)
   - [LOGIC-05：SideChainFilter 实际阶数（24 dB/oct LR4）与注释不符，且每块无条件重算 16 次双二阶系数](#logic-05sidechainfilter-实际阶数24-dboct-lr4与注释不符且每块无条件重算-16-次双二阶系数)
3. [UI 交互、状态同步与 API 层的 BUG 与逻辑漏洞](#三-ui-交互状态同步与-api-层的-bug-与逻辑漏洞)
   - [UI-01：“MIDI Learn” 是未接线 MIDI 的纯 UI 演示假桩（Mock Stub），且高亮覆盖层坐标严重错位](#ui-01midi-learn-是未接线-midi-的纯-ui-演示假桩mock-stub且高亮覆盖层坐标严重错位)
   - [UI-02：鼠标拖拽分频线被硬编码钳制在 [40 Hz, 10000 Hz]，与参数范围 [20 Hz, 20000 Hz] 冲突](#ui-02鼠标拖拽分频线被硬编码钳制在-40-hz-10000-hz与参数范围-20-hz-20000-hz-冲突)
   - [UI-03：横向拖动 Band 1 / Band 2 中心频点时，单向顺序钳制导致撞墙后带宽被压缩](#ui-03横向拖动-band-1--band-2-中心频点时单向顺序钳制导致撞墙后带宽被压缩)
   - [UI-04：`DisplayView::syncUiFromParams` 在多参数同帧变化时提前 `break`，导致 Popover 漏同步](#ui-04displayviewsyncuifromparams-在多参数同帧变化时提前-break导致-popover-漏同步)
   - [UI-05：Bypass 宏判定阈值三处不一致（50% vs 0.5% vs 0.1%），导致 `0.5% < Mix < 50%` 时按钮死锁](#ui-05bypass-宏判定阈值三处不一致50-vs-05-vs-01导致-05--mix--50-时按钮死锁)
   - [API-01：`MBBackend::setParam` 修改分频点未执行倍频程间距钳制，导致 UI/APVTS 与底层 DSP 失步](#api-01mbbackendsetparam-修改分频点未执行倍频程间距钳制导致-uiapvts-与底层-dsp-失步)
   - [MISC-01 ~ MISC-05：其它中等/低危问题与死代码残留](#misc-01--misc-05其它中等低危问题与死代码残留)
4. [修复优先级建议清单](#四-修复优先级建议清单)

---

## 一、 致命 / 高危级 BUG（并发竞争、延迟错算、音频状态污染）

### BUG-01：消息线程与音频线程无锁并发重分配（Data Race / Use-After-Free 崩溃隐患）

- **涉及文件与行号**：
  - `src/PluginProcessor.cpp:226-236`
  - `src/dsp/MultibandDSP.cpp:115-144`
  - `src/dsp/CrossoverBank.cpp:116-126`
- **代码定位**：
  ```cpp
  // src/PluginProcessor.cpp:226-230
  void CrunchMBAudioProcessor::handleAsyncUpdate()
  {
      applyStructure();      // 消息线程：含 Linear FIR 设计
      updateLatency();
  }

  // src/dsp/MultibandDSP.cpp:123-128
  if (osChanged)
  {
      const int keepFactor = struct_.oversampling;
      prepare (sampleRate_, maxBlock_, numCh_); // 消息线程直接重分配所有 vector 与 oversampler_
      ...
  }

  // src/dsp/CrossoverBank.cpp:118-124
  if (! linearReady_ || quality != quality_)
  {
      quality_    = quality;
      linear_.prepare (sampleRate_, maxBlock_, numCh_, quality_); // 消息线程重分配 Ring/FFT/dly_
      linear_.reset();
      linearReady_ = true;
  }
  ```
- **根因分析**：
  1. 当用户或宿主自动化修改结构参数（`oversampling`、`linearQuality`、`phaseMode`、`xoverSlope`、`xover1..3`）时，`parameterChanged()` 触发 `AsyncUpdater`，在**消息线程**执行 `handleAsyncUpdate() -> applyStructure() -> dsp_.setStructure()`。
  2. 当 `oversampling` 改变时，`MultibandDSP::setStructure()` 在消息线程直接调用 `prepare()`，对 `bandStore_`、`scStore_`、`gStore_`、`outStore_`、`dryStore_`、`laBuf_` 执行 `std::vector::assign()`，对 `osIn_`、`scHold_` 执行 `setSize()`，并销毁重建 `oversampler_ = std::make_unique<...>()`。
  3. 当 `PhaseMode::Linear` 下改变 `linearQuality` 时，`CrossoverBank::setParameters()` 在消息线程调用 `linear_.prepare()`，销毁重建 `fft_` 并重新分配 `inFifo_`、`outFifo_`、`overlap_`、`dly_`、`fftIn_`、`fftOut_`。
  4. 与此同时，**音频线程**正在执行 `processBlock() -> dsp_.process()` 并读写上述全部容器和指针，两者之间**没有任何同步原语、原子状态切换或双缓冲机制**。
- **影响**：
  - 播放过程中切换 Oversampling（1x / 2x / 4x）或 Linear Quality（Low / Medium / High）触发 C++ 数据竞争（Data Race）与悬垂指针读写（Use-After-Free），极易导致宿主直接崩溃（`0xC0000005` / `0xC0000374`）。

---

### BUG-02：过采样开启时，宿主延迟上报（PDC）与频谱 Pre 抽头延迟环发生“采样率单位混加”

- **涉及文件与行号**：
  - `src/PluginProcessor.cpp:78-82, 136-154, 375-380`
  - `src/dsp/MultibandDSP.cpp:10-15, 146-159, 241`
- **代码定位**：
  ```cpp
  // src/PluginProcessor.cpp:142-153
  const int osFactor = dsp_.getOversamplingFactor();
  const int laSamples = (int) std::lround (
      Param::lookaheadIndexToMs (choiceIndex (apvts.getRawParameterValue (Param::ID::lookahead)))
      * 0.001 * sampleRate_ * (double) osFactor);
  const int lat = dsp_.getCrossoverLatencySamples() + juce::jmax (0, laSamples)
                + dsp_.getOversamplingLatencySamples();
  if (lat != lastLatency_)
  {
      setLatencySamples (lat);
      lastLatency_ = lat;
      preTapLatency_ = lat;      // 频谱 pre 抽头对齐用（音频线程读）
  }
  ```
- **根因分析**：
  1. **采样率单位混加**：
     - `dsp_.getCrossoverLatencySamples()`：由于 `crossover_` 运行在过采样率 `procRate_ = sampleRate_ * osFactor` 下（`MultibandDSP.cpp:14`），但在 `LinearPhaseCrossover` 中延迟固定按 FFT 点数算为 $3N/4$（`1536 / 3072 / 6144`），因此它返回的是 **过采样域（`procRate_`）的样本数**，折算回 1× 宿主采样率应除以 `osFactor`。
     - `laSamples`：第 145 行显式乘了 `osFactor`，也是 **过采样域（`procRate_`）的样本数**。
     - `dsp_.getOversamplingLatencySamples()`：调用 `oversampler_->getLatencyInSamples()`，JUCE 官方实现返回的是 **1× 宿主采样率下的样本数**。
     - `setLatencySamples(lat)`（上报给 DAW 的 PDC 延迟）以及 `preTapRing_`（在 `PluginProcessor::processBlock` 中以 1× 采样率逐样本读写，第 375–379 行）所需要的都是 **1× 宿主采样率样本数**。
     - 将两个 `osFactor ×` 单位的延迟与一个 `1×` 单位的延迟直接相加并作为 `1×` 延迟上报，在 `2x` / `4x` 过采样下会导致 PDC 延迟与频谱 Pre 抽头对齐延迟严重虚高（接近 2 倍 / 4 倍）。
  2. **`preTapRing_` 环形缓冲区负索引风险**：
     - `prepareToPlay`（第 79 行）分配 `ringSize = 6144 + laMax + 256 + max(samplesPerBlock, 4096)`。
     - `processBlock`（第 376–378 行）仅用单次 `if (rp < 0) rp += (int) preTapRing_.size();` 回绕。若 `preTapLatency_` 未做上限钳制（`jlimit(0, (int)preTapRing_.size() - 1, ...)`），一旦延迟计算值超过 `ringSize`，将导致 `rp < 0` 越界读取。

---

### BUG-03：Auto-Gain 响度计（LoudnessMeter）双声道共用单声道滤波器状态，且 400ms 时间窗参数失效

- **涉及文件与行号**：
  - `src/dsp/LoudnessMeter.h:42, 64-69`
  - `src/dsp/LoudnessMeter.cpp:85-115`
  - `src/dsp/MultibandDSP.cpp:51-52, 361-370, 539-548`
- **代码定位**：
  ```cpp
  // src/dsp/MultibandDSP.cpp:361-370
  if (autoGainEnabled_)
  {
      for (int ch = 0; ch < chans; ++ch)
          for (int i = 0; i < n; ++i)
          {
              float s = chPtr[ch][i];
              loudPre_.processSample (s);
          }
      loudPre_.endBlock (n * chans);
  }
  ```
- **根因分析**：
  1. **L/R 串联进同一个单声道 IIR 滤波器**：
     - `LoudnessMeter` 内部仅持有一组单声道滤波器状态 `Biquad shelf_; Biquad hpf_;`（`LoudnessMeter.h:64-65`）。
     - 在立体声（`chans == 2`）下，`MultibandDSP` 先把左声道一整块 `chPtr[0][0..n-1]` 送入 `processSample`，紧接着把右声道一整块 `chPtr[1][0..n-1]` **沿用左声道末尾的 IIR 历史状态**送入同一个 `shelf_` 和 `hpf_`，下一块开头又从右声道末尾跳回左声道开头。
     - 只要左右声道信号不完全相同（任何立体声信号），每个块边界都会产生两次人为波形跳变，激发高通/高频搁架滤波器的瞬态振铃，污染响度测量。
  2. **`windowSec_` 死参数**：
     - `MultibandDSP::prepare`（第 51–52 行）调用 `loudPre_.setWindowSec(0.400)` 设置 400ms 积分窗，但 `windowSec_` 成员变量在 `LoudnessMeter.cpp` 中**从未被使用**。
     - 实际滑窗长度固定为 `kWindowBlocks = 64` 个块（`LoudnessMeter.h:69`），导致积分时间完全随宿主块大小与过采样率漂移（从几十毫秒到数秒不等）。

---

### BUG-04：LinearPhaseCrossover 固定延迟环在可变块长下延迟漂移，且全局 Mix 干湿声未做延迟对齐

- **涉及文件与行号**：
  - `src/dsp/LinearPhaseCrossover.h:109-114, 201-225`
  - `src/dsp/MultibandDSP.cpp:410-412, 527-534, 585-588`
- **根因分析**：
  1. **`FirFilter` 固定延迟环 `dly_` 假设每次 `numSamples == maxBlockSize`**：
     - `FirFilter::prepare`（第 112–113 行）将输出延迟环 `dly_` 的长度固定设为 `callSize_ = maxBlockSize`。
     - 当宿主传入可变大小音频块 `numSamples < maxBlockSize`（例如 VST3 参数自动化分割块、播放停止尾块、FL Studio 非固定大小缓冲），或 `processChain` 分块循环出现尾块时，FIFO 卷积蓄水延迟为 `B - numSamples`，而 `dly_` 依然延迟 `maxBlockSize` 个样本，导致实际总延迟变为 `B + D + (maxBlockSize - numSamples)`，与上报的 `B + D` 失配。
  2. **Global Mix（`< 100%` 或 `> 100%`）干声未补偿分频器与 Lookahead 延迟**：
     - `MultibandDSP.cpp:410-412` 将未延迟的当前块输入直接存入 `dryStore_`。
     - 第 527–534 行在 `globalMixLin_ != 1.0f` 时，直接将经过了 `LinearPhaseCrossover`（数千样本延迟）和 `Lookahead`（数百至数千样本延迟）的湿声 `out[i]` 与零延迟的 `dryStore_[ch][i]` 混合，产生严重的梳状滤波（相位抵消）或可闻回声。

---

## 二、 算法与 DSP 逻辑谬误（名实不符、数学/物理逻辑错误）

### LOGIC-01：BandCompressor 在向上压缩（Range > 0）或扩展模式（Expand）下 Attack / Release 系数选反

- **涉及文件与行号**：`src/dsp/BandCompressor.cpp:72-120, 180-194`
- **代码定位**：
  ```cpp
  // src/dsp/BandCompressor.cpp:182, 189-190
  const float coeff = (targetL < smoothedGainDb_) ? attackCoeff_ : releaseCoeff_;
  smoothedGainDb_  += (targetL - smoothedGainDb_) * (1.0f - coeff);
  ```
- **根因分析**：
  - 在向下压缩（`Range < 0, Compress`）时，静态不动作时增益为 `0 dB`，触发压缩时 `targetL < 0 dB`；因此从 `0 dB` 向负增益运动时 `targetL < smoothedGainDb_`，选用 `attackCoeff_` 是正确的。
  - 但在**向上压缩（`Range > 0, Compress`）**或**向上扩展（`Range < 0, Expand`）**时，`computeTargetGainDb()` 算出的触发增益是**正值**（`targetL > 0 dB`）。
  - 此时从 `0 dB` 向 `+targetL` 抬升的过程中，`targetL > smoothedGainDb_`，判断式 `targetL < smoothedGainDb_` 为 `false`，导致**触发增益抬升时错误使用了 `releaseCoeff_`，而信号消失回落到 `0 dB` 时反而使用了 `attackCoeff_`**，Attack 与 Release 完全颠倒。

---

### LOGIC-02：PhaseMode::Natural 与 Minimal 在分频器与求和器中数学恒等，“校正结构”形同虚设

- **涉及文件与行号**：
  - `src/dsp/CrossoverBank.h:53-58, 71-72`
  - `src/dsp/CrossoverBank.cpp:15-28, 130-134`
  - `src/dsp/MultibandDSP.cpp:489-520`
- **代码定位**：
  ```cpp
  // src/dsp/CrossoverBank.cpp:25-28
  // lrMode 参数保留（历史签名）：两种 IIR 模式现共用同一拓扑选择。
  (void) lrMode;
  const int order = juce::jmax (2, slopeDb / 6);
  const bool useLR = (order == 4 || order == 8);

  // src/dsp/MultibandDSP.cpp:489-504
  if (struct_.mode == Param::PhaseMode::Minimal && ! gatesActive_)
  {
      // 校正结构：out = dry·m̄ + Σ band·(g_i − m̄)，dry = Σband
      float dry = 0.0f, gTot[Param::kNumBands];
      for (int b = 0; b < Param::kNumBands; ++b)
      {
          const float d = bandStore_[b][ch][i];
          dry += d;
          gTot[b] = (1.0f - mArr[b]) + mArr[b] * gStore_[b][ch][i];
      }
      const float mBar = 0.25f * (gTot[0] + gTot[1] + gTot[2] + gTot[3]);
      float acc = dry * mBar;
      for (int b = 0; b < Param::kNumBands; ++b)
          acc += bandStore_[b][ch][i] * (gTot[b] - mBar);
      out[i] = acc;
  }
  ```
- **根因分析**：
  1. 在 `CrossoverBank.cpp` 中，`lrMode` 被显式忽略（`(void) lrMode;`），`Natural` 和 `Minimal` 生成的分频滤波器系数与拓扑 **100% 相同**。
  2. 在 `MultibandDSP.cpp` 的求和阶段，虽然为 `Minimal` 写了一个看似不同的公式 $\text{acc} = \text{dry}\cdot\bar{m} + \sum_{b}\text{band}_b\cdot(g_{\text{tot},b} - \bar{m})$，但因为代码里定义的 `dry` 就是 $\sum_{b}\text{band}_b$（而非分频前的原始输入 $x$），由乘法分配律可知：
     $$\left(\sum_{b=0}^{3} \text{band}_b\right)\bar{m} + \sum_{b=0}^{3} \text{band}_b (g_{\text{tot},b} - \bar{m}) \equiv \sum_{b=0}^{3} \text{band}_b \cdot g_{\text{tot},b}$$
     这与 `Natural` 分支（第 508–519 行）在代数上**恒等**。因此用户切换 `Natural` 与 `Minimal` 相位模式不会产生任何实质性的声音或相位差异。

---

### LOGIC-03：外部侧链（EXT）在分块循环中漏加 `done` 偏移，且超大块回退路径破坏过采样率

- **涉及文件与行号**：`src/dsp/MultibandDSP.cpp:278, 334, 343-355, 396-403`
- **代码定位**：
  ```cpp
  // src/dsp/MultibandDSP.cpp:343-350
  for (int done = 0; done < numSamples; done += procBlock_)
  {
      const int n = juce::jmin (procBlock_, numSamples - done);
      float* chPtr[2] = { nullptr, nullptr };
      for (int ch = 0; ch < chans; ++ch)
          chPtr[ch] = channels[ch] + done;
      ...
      // 第 396-397 行（漏掉 + done）：
      const float* scIn[2] = { sidechain[0],
                               sidechainChannels > 1 ? sidechain[1] : sidechain[0] };
  ```
- **根因分析**：
  1. 当 `numSamples > procBlock_` 进入第二轮分块迭代（`done > 0`）时，主通道 `chPtr[ch]` 加上了 `+ done` 偏移，但外部侧链指针 `scIn` 依然指向 `sidechain[0]` 起始位置（漏了 `+ done`）。
  2. 当 `osFactor_ > 1` 且宿主偶发传入 `numSamples > maxBlock_` 时，`MultibandDSP::process()`（第 278 行）因 `numSamples <= maxBlock_` 为假，直接落入第 334 行的未过采样 `processChain()`，将 1× 采样率的音频直接送入按 `osFactor_ × sampleRate_` 设计的分频器和压缩器，导致分频点频率错误偏移 `osFactor_` 倍且过采样器状态断流。

---

### LOGIC-04：BiquadFilter.h 二阶 LowCut / HighCut 命名与传递函数极性完全反置

- **涉及文件与行号**：`src/dsp/BiquadFilter.h:127-161, 199-222`
- **根因分析**：
  - `designLowCut`（第 127 行）分子为 $b_0 = \frac{1-\cos\omega_0}{2}, b_1 = 1-\cos\omega_0, b_2 = \frac{1-\cos\omega_0}{2}$，实际是 **二阶低通（Low-Pass / High-Cut）**。
  - `designHighCut`（第 145 行）分子为 $b_0 = \frac{1+\cos\omega_0}{2}, b_1 = -(1+\cos\omega_0), b_2 = \frac{1+\cos\omega_0}{2}$，实际是 **二阶高通（High-Pass / Low-Cut）**。
  - 同文件内的一阶函数 `designFirstOrderLowCut`（第 199 行）和 `designFirstOrderHighCut`（第 212 行）却是正确的高通（Low-Cut）与低通（High-Cut）。
  - 当前 `CrossoverBank` 和 `SideChainFilter` 通过“把 `designLowCut` 赋给 `lp_`、把 `designHighCut` 赋给 `hp_`”负负得正，但 API 命名本身与其数学实现完全相反。

---

### LOGIC-05：SideChainFilter 实际阶数（24 dB/oct LR4）与注释不符，且每块无条件重算 16 次双二阶系数

- **涉及文件与行号**：`src/dsp/SideChainFilter.h:15, 29-78`、`src/dsp/MultibandDSP.cpp:253`
- **根因分析**：
  1. 注释（第 15、63 行）写“每端 2 阶 Butterworth（12 dB/oct，Q = 0.7071）”，但代码中 `kSections = 2`（第 29、64 行），级联了 **2 节** 2 阶 Butterworth，实际响应为 **4 阶 Linkwitz-Riley（24 dB/oct，截止频率处衰减 -6 dB）**。
  2. `SideChainFilter::setParameters()` 未对 `lowCutHz`、`highCutHz`、`freeMode` 做脏值缓存，导致 `MultibandDSP::updateScalars()` 在每个音频块都无条件重算 4 段 × 2 节 × 2（HP/LP）= 16 个双二阶滤波器的 `sin`/`cos` 系数（即使 `freeMode == false` 滤波器旁通时也每块重算）。

---

## 三、 UI 交互、状态同步与 API 层的 BUG 与逻辑漏洞

### UI-01：“MIDI Learn” 是未接线 MIDI 的纯 UI 演示假桩（Mock Stub），且高亮覆盖层坐标严重错位

- **涉及文件与行号**：
  - `src/PluginProcessor.h:38`、`src/PluginProcessor.cpp:316`
  - `src/ui/DisplayView.cpp:1998-2142, 2472-2476`
- **根因分析**：
  1. `CrunchMBAudioProcessor::acceptsMidi()` 返回 `false`，且 `processBlock` 完全忽略 `juce::MidiBuffer&` 参数，插件没有任何真实 MIDI CC 解析或参数绑定逻辑。
  2. 用户从右上角设置菜单打开 `MIDI Learn: On` 后点击控件，`handleMidiLearnClick()`（第 2074 行）直接按公式 `midiMap_[id] = 1 + (id * 7) % 120;` 生成一个假 CC 编号显示在界面上，且关闭编辑器即丢失。
  3. `drawMidiLearnOverlay()`（第 2119–2141 行）和 `midiControlId()`（第 2016–2046 行）仍使用旧版硬编码坐标（如 `sy(118.0f + j * 30.0f)`、`W - sx(218.0f)`），与重构后的 `EditorLayout` 及 `bandButtonRect()` 完全不匹配；第 2131–2133 行频点高亮处更是留着空语句 `(void) dotBand; (void) dp;`。

---

### UI-02：鼠标拖拽分频线被硬编码钳制在 [40 Hz, 10000 Hz]，与参数范围 [20 Hz, 20000 Hz] 冲突

- **涉及文件与行号**：
  - `src/Parameters.cpp:41-43, 88-98`
  - `src/ui/DisplayView.cpp:3228-3233`
- **代码定位**：
  ```cpp
  // src/ui/DisplayView.cpp:3228-3233
  if (drag_ == DragMode::Divider)
  {
      const float f = juce::jlimit (40.0f, 10000.0f, xToFreq (p.x));
      applyDividerFreq (dragIndex_, f);
      return;
  }
  ```
- **根因分析**：
  - 参数层与 `Param::clampCrossovers` 允许 `xover1` 低至 `20 Hz`、`xover3` 高至 `20000 Hz`，双击输入框或拖动频点中心也能设到 `[20, 20000]`。
  - 但直接拖拽分频线时被第 3230 行硬编码限制在 `40 Hz .. 10000 Hz`（若 `xover3` 原本在 `12 kHz`，用户一点击拖动该分频线就会瞬间跳回 `10 kHz`）。

---

### UI-03：横向拖动 Band 1 / Band 2 中心频点时，单向顺序钳制导致撞墙后带宽被压缩

- **涉及文件与行号**：`src/ui/DisplayView.cpp:1856-1875`
- **根因分析**：
  - 在 `applyCenterFreq` 中平移中频段（如 `band == 1`）时，`x1 = newLo; x2 = newHi;`，随后调用 `Param::clampCrossovers(x1, x2, x3)`。
  - 因为 `clampCrossovers` 固定按 `x1 -> x2 -> x3` 从左向右顺序钳制：当用户向右拖动 Band 1 使 `x3` 撞到 `20000 Hz` 上限时，`x2` 被卡在 `10000 Hz` 不再右移，但 `x1` 仍可继续向右移动到 `5000 Hz`，导致原本保持倍频程宽度平移的操作变成把 Band 1 挤压变窄。

---

### UI-04：`DisplayView::syncUiFromParams` 在多参数同帧变化时提前 `break`，导致 Popover 漏同步

- **涉及文件与行号**：`src/ui/DisplayView.cpp:352-360, 1061-1062`
- **代码定位**：
  ```cpp
  if (fullSync)
      bandPopover_->syncFromParams();
  else
      for (const auto& id : pendingParamIds_)
          if (bandPopover_->syncParam (id))
              break; // BUG：syncParam(id) 只同步单个参数，break 会丢弃同帧其余参数！
  ```
- **根因分析**：
  - `BandPopover::syncParam(id)` 每次只更新传入的那一个 `id` 对应的控件并返回 `true`。
  - `DisplayView::syncUiFromParams` 在收到第一个返回 `true` 的参数后立即 `break` 跳出循环，并在调用处清空整个 `pendingParamIds_`。
  - 当同一帧内有多个属于当前频段的参数发生变化（如 LINKED 抽屉联动、宿主同时自动化多个参数、加载预设）时，Popover 面板只会刷新第一个参数，其余旋钮/滑块停留在旧值。

---

### UI-05：Bypass 宏判定阈值三处不一致（50% vs 0.5% vs 0.1%），导致 `0.5% < Mix < 50%` 时按钮死锁

- **涉及文件与行号**：
  - `src/ui/DisplayView.cpp:1750-1755, 1881-1902`
  - `src/api/MBBackend.cpp:326-335`
- **代码定位**：
  ```cpp
  // src/ui/DisplayView.cpp:1750-1755
  bool DisplayView::isBandBypassed (int band) const
  {
      return getParamPhysical ("band" + juce::String (band) + "Mix") < 50.0f;
  }

  // src/ui/DisplayView.cpp:1895-1901
  if (cur > 0.5f)                                // 单段点击：未 bypass → 置 0
  {
      bypassMemo_[(size_t) band] = cur;
      setParamNorm (id, 0.0f);
  }
  else
      setParamNorm (id, bypassMemo_[(size_t) band] / 100.0f);   // 已 bypass → 恢复
  ```
- **根因分析**：
  - `isBandBypassed` 认为物理值 `Mix < 50.0f`（50%）即为 Bypass，但 `toggleBypass` 内部却拿物理值 `cur` 与 `0.5f`（0.5%，疑似将归一化值 `0.5` 误当成物理值）比较！
  - 当某段 `Mix` 处于 `(0.5%, 50%)` 区间（例如用户把 Mix 旋钮拧到 `30%`）时：
    1. UI 显示该段处于 Bypass 状态（`B` 按钮高亮，曲线切为未压缩基准）；
    2. 用户点击 `B` 按钮试图解除 Bypass：由于 `cur (30.0f) > 0.5f`，`toggleBypass` 把 `bypassMemo_` 覆写为 `30.0f` 并将 `Mix` 设为 `0%`；
    3. 用户再次点击 `B` 按钮：`Mix` 恢复为 `bypassMemo_`（`30%`），依然 `< 50%`，导致该频段永远停留在 Bypass 状态无法通过 `B` 按钮解除。

---

### API-01：`MBBackend::setParam` 修改分频点未执行倍频程间距钳制，导致 UI/APVTS 与底层 DSP 失步

- **涉及文件与行号**：
  - `src/api/MBBackend.cpp:194-223, 384-416`
  - `src/dsp/MultibandDSP.cpp:120`
- **根因分析**：
  - 通过 `MBBackend::setParam("xover1", freq)` 或宿主自动化直接修改 `xover1..3` 时，APVTS 不会触发 `Param::clampCrossovers(x1, x2, x3)`。
  - 而 `MultibandDSP::setStructure()`（第 120 行）在内部私自对副本调用了 `Param::clampCrossovers()`。
  - 结果：当外部或自动化把 `xover1` 推高到超过 `xover2 / 2` 时，UI 分频线和参数读数显示的是未钳制的频率，但音频引擎实际运行在被钳制后的频率上，造成视听不一致。

---

### MISC-01 ~ MISC-05：其它中等/低危问题与死代码残留

| 编号 | 位置 | 问题描述 |
| :--- | :--- | :--- |
| **MISC-01** | `src/PluginEditor.cpp:18, 23` | 编辑器构造时修改了进程级全局 `LookAndFeel::getDefaultLookAndFeel()` 默认字体及静态全局 `MBTheme::setTextScale(1.22f)`，会影响同一宿主进程中的其它 JUCE 插件或同进程离屏测试基准。 |
| **MISC-02** | `CMakeLists.txt:2` vs `src/api/MBBackend.cpp:13` | `CMakeLists.txt` 版本号为 `1.0.4`，而 `MBBackend.cpp` 中 `kPluginVersion` 仍硬编码为 `"1.0.0"`；`Parameters.h` 注释称有 62 / 84 个参数，实际为 86 个参数。 |
| **MISC-03** | `src/PluginProcessor.cpp:66-67, 86-87` | `prepareToPlay()` 内部重复调用了两次完全相同的 `applyStructure(); updateLatency();`（加上 `dsp_.prepare()` 内部的一次，共触发 3 次结构配置与 FIR 核计算）。 |
| **MISC-04** | `src/ui/DisplayView.cpp:1505, 1762` | `drawRangeBell` 注释写“4 段都画；非选中段低透明度”，但循环写成了 `for (int b = selectedBand_; b <= selectedBand_; ++b)`，导致 `const float a = sel ? 1.0f : 0.45f;` 的 `0.45f` 分支成为死代码。 |
| **MISC-05** | `src/ui/DisplayView.cpp:912-1006`、`src/dsp/MultibandDSP.cpp:189` | `buildBlurCache()` 与 `drawBlurredBackdrop()` 定义后从未被调用；`getBandGainReductionDb()` 注释称“`bandGainDb_` 已含 Range 钳制与 Makeup”，实际 `getBandGainDb(b)` 返回的是不含 Makeup 的压缩增益。 |

---

## 四、 修复优先级建议清单

1. **P0（崩溃防护与延迟/相位准确性）**
   - 为 `MultibandDSP` 结构重建（Oversampling / Linear Quality 切换）引入跨线程安全机制（如音频线程命令队列、双缓冲交换或 `juce::SpinLock` Try-Lock 回退），严禁消息线程直接重分配音频线程正在读写的容器。
   - 修正 `PluginProcessor::updateLatency()` 中的采样率单位换算：`crossover` 延迟与 `lookahead` 延迟在 `procRate_` 下计算后应除以 `osFactor` 再与 `oversampler_->getLatencyInSamples()` 相加，并对 `preTapLatency_` 做环形缓冲区容量上限保护。
   - 为 `MultibandDSP` 的全局干声参考 `dryStore_` 补齐与湿声一致的分频器 + Lookahead 延迟补偿环，并修复 `FirFilter` 在 `numSamples != maxBlockSize` 时的延迟漂移。
2. **P1（DSP 算法正确性）**
   - 修正 `BandCompressor::processGain()` 的 Attack/Release 选择判据，使其在 `Range > 0`（向上压缩）和 `Expand`（扩展）模式下按“偏离 0 dB 用 Attack、回归 0 dB 用 Release”工作。
   - 将 `LoudnessMeter` 的滤波器状态扩充为双声道独立（`shelf_[2], hpf_[2]`），并根据 `windowSec_ * sampleRate_` 动态计算滑窗容量。
   - 修正 `MultibandDSP::processChain` 中外部侧链 `scIn` 漏加 `+ done` 的问题。
   - 扶正 `BiquadFilter::designLowCut` / `designHighCut` 的命名或提供明确的 `designLowPass` / `designHighPass` 接口；为 `SideChainFilter::setParameters` 增加参数缓存。
3. **P2（UI 交互与状态一致性）**
   - 统一 `DisplayView::toggleBypass`、`isBandBypassed` 与 `MBBackend::isBypassed` 的阈值判据（消除 `0.5f` 与 `50.0f` 混用）。
   - 移除 `DisplayView::syncUiFromParams()` 中遍历 `pendingParamIds_` 时的提前 `break`。
   - 将 `DisplayView::mouseDrag` 中 `DragMode::Divider` 的频率钳制范围从 `[40, 10000]` 修正为 `[20, 20000]`，并优化 `applyCenterFreq` 的双向边界约束。
   - 清理或真正实现 MIDI Learn（开启 `acceptsMidi`、解析 `MidiBuffer` CC 并持久化映射，修正 Overlay 几何坐标）。
