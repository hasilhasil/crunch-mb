# Crunch MB 原生 UI 实装记录

基准：`mockup/approved-native-ui-reference.html`。这是此前确认的顶栏 Linked Control、单行 Global 版本；本次移植到 JUCE 原生编辑器。

## 已实现

- 删除 Linked Control 的锁定状态、锁定按钮及相关接口。顶栏按钮打开/收起，关闭按钮或点击面板外关闭。保留四段参数联动。
- 默认 1040 × 780，最小 920 × 560，最大 2080 × 1560。宽高可以独立调整；控件保持等比缩放，剩余空间用于频谱。
- 频谱左侧贴边，右侧贴电平表，底部连接 Global。分频手柄移到绘图区中部。
- 顶栏频段切换、Linked Control、量程、右上主题切换。主题保持 Blue / Red / Cream。MIDI Learn 原有入口归入设置菜单。
- Global 保持单行和完整名称。标签与状态共享字号和文字框，按实际字体测量最长状态，避免 `24 dB/oct` 等被截断。
- UI 文本使用内嵌 Barlow Regular / Medium；数值读数和坐标刻度继续使用 Poppins。附 Barlow OFL 许可证，无运行时字体下载。
- Band / Linked Control 使用半透明面板。Band 保留六个真实旋钮，并按确认稿排列 Compress/Expand、Ratio、Knee、Sidechain。
- Pre 频谱为灰色，Post 为主题色。Range 包络复用连续的归一化分频权重；总响应保持单一连续曲线。
- 修复面板空白点击穿透、面板互斥、关闭图形与命中区域不一致、菜单叠放、缩放后静止/冻结曲线坐标未更新等问题。
- 频谱绘制限定在绘图区内，避免渗入右侧电平表或底栏。

## 保留的产品行为

音频 DSP、参数 ID、参数范围、预设数据结构未作改动。曲线来自本插件自己的分频模型及增益数据；频谱来自实际音频分析，不使用设计稿里的模拟波形。图表仍显示真实双坐标刻度。

保留原有 Expert、Analyzer、Input/Output 的实际功能；其可用选项以插件参数为准。源码中未引入第三方插件二进制或反编译代码。

## 验证入口

测试程序：`build/CrunchMBTests_artefacts/Release/CrunchMBTests.exe`

```text
CrunchMBTests.exe --redraw-check
CrunchMBTests.exe --ui-regression
CrunchMBTests.exe --snapshot _review/ui_native_20261022
CrunchMBTests.exe --vst3-smoke "<本地 Crunch MB.vst3 包绝对路径>"
CrunchMBTests.exe
```

原生截图在 `_review/ui_native_20261022/`；截图模式将确定性音频送入真实处理器和频谱引擎。

旧 UI 回归中的固定坐标、底部梯形 Linked 按钮和锁定预期已随新布局更新；音频回归仍保留。最终结果见同目录验证记录。

## 交付范围

所有构建留在项目目录。未向系统 VST3 安装路径复制文件，未修改 FabFilter Pro-MB 或逆向样本。VST3 本地加载测试不等同于已在用户具体 DAW 工程里完成试听。
