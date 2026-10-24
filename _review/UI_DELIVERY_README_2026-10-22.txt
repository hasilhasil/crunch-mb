Crunch MB — 原生 UI 更新（2026-10-22，Windows x64）

VST3/Crunch MB.vst3：完整 VST3 插件包，须保留整个目录结构。
Standalone/Crunch MB.exe：独立运行版，可直接打开查看和使用。
Preview/：实际 JUCE 原生界面截图。
Licenses/：此次新增的 Barlow 字体许可证。

本版删除 Linked Control 锁定功能，采用已确认的顶栏 Linked Control、单行完整 Global、大频谱、半透明二级菜单和 Barlow UI 字体。
默认 1040×780，支持宽高独立调整，最小 920×560，最大 2080×1560。
音频 DSP 和参数 ID 保持原有实现。

使用：在支持自定义 VST3 扫描目录的宿主中添加本包的 VST3 目录并重新扫描。
若宿主只扫描系统标准目录，需要将完整 Crunch MB.vst3 文件夹安装到该宿主使用的 VST3 目录。
本次没有自动安装或覆盖系统目录中的插件，也没有改动 FabFilter Pro-MB。

验证：现有完整回归 1008 通过 / 0 失败；实际 VST3 加载测试 10 通过 / 0 失败。
尚未在你的具体 DAW 工程里试听。
