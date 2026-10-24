# Crunch MB VST3 宿主加载修复（2026-10-22）

## 现象与原因

- FL Studio 2026 从已保存的 `Crunch MB` 效果器条目加载时显示“无法打开插件”。
- Studio Pro / Studio One 的旧截图仍显示先前的 UI。
- FL 的 `.fst` 条目记录的是旧版 VST3 音频组件类 ID `56535443524D426372756E6368206D62`；先前交付的 VST3 却声明 `ABCDEF019182FAEB4B696C6F43524D42`。这是构建中 `JUCE_VST3_CAN_REPLACE_VST2=0` 造成的身份变化。仅比较安装文件与交付包的哈希无法发现这个问题。

## 修复

- 在 `CMakeLists.txt` 中启用 `JUCE_VST3_CAN_REPLACE_VST2=1`，恢复旧版 VST3 类 ID，并将版本提升至 1.0.1。
- 新构建 `moduleinfo.json` 的音频组件 CID 为 `56535443524D426372756E6368206D62`，与旧 `.fst` 条目一致。
- 新 VST3 已安装在 `C:\Program Files\Common Files\VST3\Crunch MB.vst3`；替换前的 DLL 和 `moduleinfo.json` 备份在 `_review/installed_backup_pre_1_0_1/`。
- `dist/Crunch-MB-UI-20261022/` 及同名 ZIP 已更新为 v1.0.1。新 DLL SHA-256 为 `58F61A43A01B7B2A8AE4E941A435D7119B3C58CB6BB42F6C59973D88A90083BB`。

## 实际宿主验证

- FL Studio 2026：完全重启后，从原有 `Crunch MB` 菜单条目加载成功。显示新顶栏品牌、顶栏 Linked Control 和单行 Global，原先的“未知原因无法打开”未再出现。
- Studio Pro 8：在工作区空白测试歌曲的主输出上添加 `Crunch MB` 成功。显示同一套新布局，不再是底部 Linked Control 的旧界面。
- 两项验证均为实际宿主窗口加载；未用离屏快照代替宿主验证。尚未打开用户既有工程检查历史实例，也未进行音频试听。
