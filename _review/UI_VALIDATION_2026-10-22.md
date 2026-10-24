# 原生 UI 交付验证

- Release 构建：VST3、Standalone、测试程序全部成功。
- 完整现有回归：**PASS 1008 / FAIL 0**，见 `build/ui-full-regression.log`。
- 实际 VST3 包加载：**PASS 10 / FAIL 0**，见 `build/ui-vst3-smoke.log`。覆盖扫描、实例化、原生编辑器创建、默认尺寸、独立宽高调整、音频处理及状态保存。
- 已检查原生截图：默认 Band、Linked Control、最小尺寸、横向拉宽、纵向拉高、最小尺寸 Expert 展开。
- 截图使用真实处理器生成。后续静止截图里的频谱自然衰减，响应曲线保留。
- 尚未在用户具体 DAW 工程中试听；没有修改系统插件安装目录。

交付包：`dist/Crunch-MB-UI-20261022/`。

完整实现说明：`UI_IMPLEMENTATION_2026-10-22.md`。
