# Crunch MB 审计问题修复更新日志 (Audit Fix Changelog)

> **依据文档**：`audit/1 (2).txt`、`audit/BUG_AND_LOGIC_AUDIT.md`  
> **验证结果**：离线全量断言套件 **1023 PASS / 0 FAIL**；VST3 原生宿主加载与渲染冒烟测试 **14 PASS / 0 FAIL**

---

## 一、 P0 级：跨线程安全、变长块越界防护与后台线程死循环修复

1. **消除消息线程与音频线程无锁并发重分配（BUG-01 / P0-1）**
   - **修复文件**：`src/dsp/MultibandDSP.h`、`src/dsp/MultibandDSP.cpp`、`src/dsp/LinearPhaseCrossover.h`、`src/PluginProcessor.h`、`src/PluginProcessor.cpp`
   - **修复内容**：
     - 在 `MultibandDSP::prepare()` 中按最大 `4x` 过采样容量一次性预分配全部工作缓冲（`bandStore_`、`scStore_`、`gStore_`、`outStore_`、`dryStore_`、`laBuf_`、`scHold_`）并预初始化 `oversampler2x_` 与 `oversampler4x_`。播放过程中切换 `Oversampling (Off / 2x / 4x)` 时仅切换活跃采样率与过采样器指针，不再在消息线程重分配 `std::vector` 或销毁重建 `juce::dsp::Oversampling`。
     - 在 `FirFilter` 中引入独立的 `designFft_`、`designIn_` 与双缓冲频域核 `irSpectrumSlots_[2]` + `activeIrSlot_` 原子切换，消除拖动分频点或修改斜率时消息线程与音频线程复用同一 `fftIn_` / `irSpectrum_` 造成的频域核撕裂。
     - 在 `CrunchMBAudioProcessor` 中引入 `dspLock_` 保护结构重配与音频块处理的并发边界。

2. **修复过采样在 FL Studio 变长块下的陈旧尾部污染、侧链越界读与超大块回退（P0-2 / LOGIC-03）**
   - **修复文件**：`src/dsp/MultibandDSP.cpp`
   - **修复内容**：
     - 上采样输入统一使用 `inFull.getSubBlock (0, (size_t) chunkN)`，使 `processSamplesUp()` 仅处理当次真实传入的 `chunkN` 个样本（不再把 `maxBlock_ - numSamples` 个未初始化/陈旧样本喂入多相 IIR 滤波器与下游链路）。
     - 外部侧链（`EXT`）零阶保持上采样按 `base + i / osFactor_` 严格限制在 `[0, numSamples)` 内读取，消除 `numSamples < maxBlock_` 时的缓冲区越界读取（`0xC0000005`）。
     - 当宿主偶发传入 `numSamples > maxBlock_` 时，外层按 `maxBlock_` 分片循环通过过采样器，不再跳过上采样直接以 1× 采样率回退进入 `processChain`。
     - 补齐 `processChain()` 分块循环中外部侧链指针 `scIn` 缺失的 `+ done` 偏移。

3. **修复 `SpectrumEngine` 跨线程竞争、200Hz 无限发布风暴与 Freeze 衰减失效（P0-3）**
   - **修复文件**：`src/dsp/SpectrumEngine.h`、`src/dsp/SpectrumEngine.cpp`、`src/ui/SpectrumView.h`、`src/ui/SpectrumView.cpp`
   - **修复内容**：
     - 为频率轴 `freqs_` 增加 `freqLock_` 与单调版本号 `freqsVersion_`，消除 `std::vector` 并发读写 UB，并支持宿主切换采样率后 `SpectrumView` 自动更新频率轴。
     - 编辑器打开时将非线程安全的 `fifoPre_.reset()` / `fifoPost_.reset()` 替换为消费者侧线程安全的 `finishedRead (getNumReady())`。
     - 修复 `run()` 中 `lastHeartbeatMs_` 只在首帧赋值的问题，按 `kFrameIntervalMs (16ms)` 节流衰减帧，并在频谱衰减到达 `-100 dB` 底噪后置位 `decayResting_ = true` 进入休眠态，彻底消除停止播放后后台线程 200Hz 无限递增 `publishVersion_` 导致的 UI 空闲降频失效。
     - 在 `publishDecayFrame()` 与饥饿分支中检查 `frozen_.load()`，确保开启 `Freeze` 后停止播放不会抹掉冻结的频谱。

4. **修复单声道主总线 + 立体声侧链（及 `mainIn` 禁用）通道映射（P0-4）**
   - **修复文件**：`src/PluginProcessor.cpp`
   - **修复内容**：
     - 使用主总线通道数 `juce::jmax (mainInCh, mainOutCh)`（而非包含侧链的总通道数 `buffer.getNumChannels()`）收敛 `safeCh`，避免 Mono 主总线 + Stereo 侧链（总 3 通道）时把侧链左声道当成主输出右声道处理。
     - 当 `mainInCh <= 0`（如 FL Studio 未接线槽位禁用主输入）时立即清零输出缓冲区并返回。

---

## 二、 P1 级：PDC 延迟补偿、相位对齐与 DSP 算法逻辑修复

5. **修复过采样下宿主延迟上报（PDC）与 `preTapRing_` 采样率单位混加（BUG-02 / P1-4）**
   - **修复文件**：`src/PluginProcessor.h`、`src/PluginProcessor.cpp`、`src/dsp/MultibandDSP.cpp`
   - **修复内容**：
     - 将 `MultibandDSP::getCrossoverLatencySamples()` 与 `getLatencySamples()` 统一折算为 **1× 宿主采样率样本数**（除以 `osFactor_`），并在 `PluginProcessor::updateLatency()` 中按 1× `sampleRate_` 计算 Lookahead 样本数，消除 2x/4x 过采样下 PDC 延迟虚高 2~4 倍的问题。
     - 将 `preTapLatency_` 改为 `std::atomic<int>`，并在 `processBlock` 读取时用 `juce::jlimit (0, ringCap - 1, ...)` 钳制保护。

6. **彻底修复 `LinearPhaseCrossover`（`FirFilter`）在变长块下的延迟漂移与 FIFO 欠载（BUG-04a / P1-5）**
   - **修复文件**：`src/dsp/LinearPhaseCrossover.h`
   - **修复内容**：
     - 在 `FirFilter::reset()` 中向 `outFifo_` 预填 `blockSize_` 个零样本，并移除原先假设 `numSamples == maxBlockSize` 的固定延迟环 `dly_`。
     - 满足不变量 $\text{inFifo.size()} + \text{outFifo.size()} \equiv B$，在任意定长或随机变长块序列（如 `1, 13, 64, 512, 1024`）下永不发生 FIFO 欠载，且脉冲延迟恒精确等于 $B + D$。

7. **修复全局 `Mix`（`0–200%`）与 `Monitor = Input` 的干湿声相位与延迟对齐（BUG-04b / P1-6）**
   - **修复文件**：`src/dsp/MultibandDSP.cpp`
   - **修复内容**：
     - 将全局干声参考 `dryStore_` 改为取自分频后、Lookahead 延迟后的四段未压缩信号之和（$\sum_{b=0}^{3} \text{bandStore\_}[b]$），使其天然具备与湿声完全一致的 Linear Phase FIR 延迟、Lookahead 延迟及 LR4/LR8 全通相位，彻底消除 `globalMix != 100%` 时的梳状滤波抵消与延迟回声。

8. **修复 `BandCompressor` 向上压缩/扩展模式下 Attack & Release 反置及右声道表头缺失（LOGIC-01 / P1-7）**
   - **修复文件**：`src/dsp/BandCompressor.h`、`src/dsp/BandCompressor.cpp`
   - **修复内容**：
     - 按“向目标动作方向偏离 0 dB 时使用 `attackCoeff_`、向 0 dB 恢复时使用 `releaseCoeff_`”选择包络平滑系数，修复向上压缩（`Range > 0`）与扩展模式（`Expand`）下 Attack 与 Release 完全颠倒的问题。
     - `getLastDetectorDb()` 返回 `jmax (lastDetectorDb_, lastDetectorDbR_)`、`getSmoothedGainDb()` 返回幅度较大声道的平滑增益，使 `StereoLink < 100%` 下纯右声道信号也能正常驱动 UI 动态曲线与圆环电平表。

9. **修复 `LoudnessMeter` 双声道状态串用、400ms 时间窗失效与 Auto-Gain 关闭跳变（BUG-03 / P1-8）**
   - **修复文件**：`src/dsp/LoudnessMeter.h`、`src/dsp/LoudnessMeter.cpp`、`src/dsp/MultibandDSP.cpp`
   - **修复内容**：
     - 将 `LoudnessMeter` 的 K 加权滤波器扩充为左右声道独立状态 `shelf_[2]`、`hpf_[2]`。
     - 使用 ~5ms 最小粒度桶与按 `windowSec_ * sampleRate_` 回溯积分，使 400ms 响度时间窗不再随宿主块大小漂移。
     - 关闭 Auto-Gain 时按约 25ms 时间常数将残余 `autoGainDb_` 平滑释放回 `0 dB`，消除块边界跳变爆音。

10. **扶正 `BiquadFilter` 二阶高低切命名极性、为 `SideChainFilter` 增加脏值缓存、修复 `historyCounter_` 回绕（LOGIC-04 / LOGIC-05 / P1-10）**
    - **修复文件**：`src/dsp/BiquadFilter.h`、`src/dsp/SideChainFilter.h`、`src/dsp/CrossoverBank.cpp`、`src/PluginProcessor.cpp`
    - **修复内容**：
      - 在 `BiquadFilter` 中提供语义明确的 `designLowPass` / `designHighPass`，并将 `designLowCut`（高通）与 `designHighCut`（低通）极性扶正。
      - 为 `SideChainFilter::setParameters()` 增加频率与开关脏值缓存，避免每个音频块无条件重算 16 次 `sin`/`cos`。
      - 修复 `PluginProcessor::pushHistoryPoint()` 中 `historyCounter_` 每满 4096 块被重置为 `1` 的问题，保持单调递增计数。

---

## 三、 P2 / P3 级：UI 交互、自动化手势、状态同步与 API 修复

11. **补齐全部旋钮、滑杆与画布拖拽的 `beginChangeGesture()` / `endChangeGesture()`（P2-11）**
    - **修复文件**：`src/ui/BandPopover.cpp`、`src/ui/LinkedDrawer.cpp`、`src/ui/DisplayView.h`、`src/ui/DisplayView.cpp`
    - **修复内容**：为 `BandPopover` 6 个主旋钮与 Ratio/Knee 滑杆、`LinkedDrawer` 6 个联动旋钮，以及 `DisplayView` 画布上的分频线、频点、曲线 Makeup、Range 边沿、Alt+OUT 拖拽全部补齐手势起止通知，支持宿主 Touch / Latch 自动化录制。

12. **修复 `DisplayView::syncUiFromParams()` 提前 `break` 漏同步与 Bypass 阈值死锁（UI-04 / UI-05）**
    - **修复文件**：`src/ui/DisplayView.cpp`、`src/api/MBBackend.cpp`
    - **修复内容**：
      - 移除 `syncUiFromParams()` 遍历 `pendingParamIds_` 时的提前 `break`，确保同帧多参数变化时 `BandPopover` 所有受影响控件完整刷新。
      - 将 `isBandBypassed()`、`toggleBypass()` 与 `MBBackend::isBypassed()` 的判定阈值统一为 `Mix <= 0.5%`，消除 `0.5% < Mix < 50%`（如 30%）时点击 `B` 按钮无法解除 Bypass 的死锁。

13. **修复中频段中心点拖拽撞墙压缩带宽、`LinkedDrawer`、`ValueField`、网格刻度与 `MIDI Learn` 坐标（UI-01 / UI-03 / P2-13~16 / MISC）**
    - **修复文件**：`src/ui/DisplayView.cpp`、`src/ui/LinkedDrawer.cpp`、`src/ui/ValueField.h`、`src/api/MBBackend.h`、`src/api/MBBackend.cpp`、`src/PluginEditor.h`、`src/PluginEditor.cpp`、`CMakeLists.txt`
    - **修复内容**：
      - 优化 `DisplayView::applyCenterFreq()`：平移 Band 1 / Band 2 中心频点触碰相邻边界时双边同步限幅，保持倍频程带宽比不被挤压。
      - `MBBackend::setParam()` 修改 `xover1..3` 时统一执行 1 八度最小带宽钳制；`MBBackend::loadState()` 补齐 `reapplyStructureAndLatency()`；`kPluginVersion` 更新为 `"1.0.4"`。
      - `LinkedDrawer` 首次懒创建旋钮时同步当前主题色 `pal_`，且联动 `MIX` 检查全部四段是否一致后再决定是否跳过。
      - `ValueField` 输入非法文本后若失焦自动恢复原值并关闭 `TextEditor`，避免残留遮挡。
      - `drawGrid()` 增益网格线步长与右轴刻度严格对齐（`±6→1dB, ±12→2dB, ±30→10dB`），Auto 量程在非拖拽稳态下支持带滞后自动回落。
      - `drawMidiLearnOverlay()` 与 `midiControlId()` 对齐至当前 `EditorLayout` 几何坐标。
      - `CrunchMBAudioProcessorEditor` 改用私有 `LookAndFeel` 实例设置默认字体，不再污染宿主全局 `LookAndFeel`；移除 `prepareToPlay()` 重复结构调用；为 `CMakeLists.txt` 的 `FONT_DIR` 增加 `if(NOT DEFINED FONT_DIR)` 守卫。
