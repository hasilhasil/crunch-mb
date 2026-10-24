# Crunch MB 1.0.4：FL Studio 身份冲突更新说明

## 适用情况

用户报告：FL Studio **25.1.6** 中扫描并打开 Crunch MB，实际加载了已安装的 imagiro piano；两者均为 VST3，位于默认 VST3 目录。

1.0.4 更换了 Crunch MB 的产品代码、音频组件 CID 和控制器 CID，并关闭 VST2 替代兼容模式。新标识不会随每次编译或版本号变化。界面、DSP 与参数 ID 未在本次身份修复中修改。

这是一项针对身份匹配问题的隔离修复。本机没有该测试者的 imagiro piano 文件及 FL 数据库，尚未直接确认两个产品是否存在完整 CID 重复，或 FL 内部标识/缓存记录发生误关联。不能把本机单独加载成功当作该测试者的共存复测结果。

## 安装与重新验证扫描

1. 退出 FL Studio 及占用 Crunch MB 的其他宿主。先保留旧工程副本和旧版安装包。
2. 将包内完整的 `VST3/Crunch MB.vst3` 文件夹复制到：
   `C:\Program Files\Common Files\VST3\Crunch MB.vst3`
   覆盖旧版本，并保留完整的 `Contents` 结构。不要只复制 DLL，也不要把旧版复制到另一个宿主扫描目录中共存。
3. 若以前在多个 VST3 目录安装过 Crunch MB，将多余的 **Crunch MB** 副本移到扫描目录外备份，只保留一个新版。不要删除或修改 imagiro piano。
4. 在 FL Studio 打开 **Options → Manage plugins**（也可从 File settings 进入 Manage plugins）。
5. 开启 **Verify plugins** 和 **Rescan previously verified plugins**。若 Crunch MB 曾被记为失败，同时开启 **Rescan plugins with errors**，然后点击 **Find installed plugins**。仅做 Fast scan 不足以更新身份记录。
6. 找到新扫描的 **Crunch MB**，核对厂商 **Kilo**、格式 **VST3**、类型 **Effect**、版本 **1.0.4**（若界面提供版本栏），路径为上述新版目录。
7. 在空白测试工程的 Mixer 插槽中，从 **More plugins** 或 **Plugin database → Installed → Effects → VST3** 中选择刚扫描的条目，确认显示 Crunch MB 的四段动态界面。
8. 再单独加载 imagiro piano，确认仍是它自己的钢琴界面。此共存步骤必须由装有这两个插件的测试者完成。
9. 旧收藏条目可能仍保存旧身份。先从新扫描的条目确认加载正确，再重新建立 Crunch MB 收藏。需要清理时只备份/移除旧的 **Crunch MB** 数据库条目，不要清空整套 Plugin database，也不要清理 imagiro 的条目。

FL 数据库收藏不是插件二进制；新版文件覆盖成功不代表旧 `.fst` 中的身份已改变。

## 旧工程与设置

- 本次有意不保留旧身份别名，以免继续让宿主走入被误关联的身份。
- 旧工程可能提示插件缺失、继续指向旧条目，或需要手动重新选择新版。请在工程副本上操作，不要直接覆盖唯一工程。
- 已知正常工作的旧版可用于取回参数。保留参数记录，或使用宿主能明确转移参数数据的方式迁移到新版。不同宿主的预设可能包含旧插件身份，不保证保存/加载预设就能完成迁移。
- 内部参数 ID 与状态数据格式未改；但宿主按旧 CID 找插件的步骤不会因此自动修复。

## 标识与证据

| 项目 | 1.0.4 固定值 |
|---|---|
| Product | Crunch MB |
| Vendor | Kilo |
| Plugin code | K7mB / 0x4B376D42 |
| VST2 replacement | Disabled |
| Windows audio component CID | ABCDEF019182FAEB4B696C6F4B376D42 |
| Windows controller CID | ABCDEF011234ABCD4B696C6F4B376D42 |
| Compatibility class CID | ABCDEF01C0DEF00D4B696C6F4B376D42 |

上表最后一行是 JUCE 的兼容性查询接口工厂类别，不代表声明兼容旧插件；本插件没有提供旧 CID 替代映射。

构建时由 JUCE manifest helper 加载当前二进制的工厂并生成 `moduleinfo.json`，随后 `cmake/VerifyVst3Identity.cmake` 校验固定身份。包内 `SHA256SUMS.txt` 可用于核对测试者实际安装的二进制与交付版本。

本机已对默认目录安装的 1.0.4 完成不创建界面的身份加载检查：6 项通过、0 项失败，名称/厂商为 Crunch MB / Kilo，类型为效果器，实例暴露预期的频段参数。未进行 FL Studio 25.1.6 与 imagiro piano 的共存复测。

如果按上述步骤仍串插件，请保留两条扫描记录的名称、厂商、路径、版本，以及各自 `Contents/Resources/moduleinfo.json`（若存在）。这些数据足以继续区分完整 CID 冲突、错误文件或 FL 缓存误关联，无需提供授权文件、工程或采样库。

官方扫描依据：[Image-Line：Installing Plugins](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)，其中建议使用 Verify plugins 与 Rescan previously verified plugins 更新扫描记录。
