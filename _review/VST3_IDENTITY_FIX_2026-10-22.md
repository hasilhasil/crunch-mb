# Crunch MB 1.0.4：VST3 身份隔离修复

## 已知现象与证据边界

测试者使用 FL Studio 25.1.6；Crunch MB 与 imagiro piano 均为默认目录中的 VST3。测试者选择 Crunch MB 后加载了 imagiro piano。

本机没有测试者的 imagiro piano 文件和 FL 扫描数据库，不能确认是完整 CID 重复、宿主内部 ID 碰撞，还是缓存误关联。以下为针对身份匹配问题的隔离修复，并不构成该测试者电脑上的共存复测结果。

## 修改

- 固定新的产品代码 `K7mB`，不再使用 `CRMB`。
- `JUCE_VST3_CAN_REPLACE_VST2=0`；Crunch MB 只发布 VST3，不需要 VST2 替代身份。
- 音频组件 CID：`ABCDEF019182FAEB4B696C6F4B376D42`。
- 控制器 CID：`ABCDEF011234ABCD4B696C6F4B376D42`。
- 不提供旧 CID 替代映射。后续升级应保留本次的新身份，不再随版本变动。
- 新增 `cmake/VerifyVst3Identity.cmake`，检查二进制工厂生成的实际 manifest，意外更换身份会导致构建失败。
- 本次未修改 UI、DSP 或参数 ID。

## 最小验证与交付

- Release VST3、Standalone 构建完成；构建内的固定 CID 检查通过。
- 对默认目录中安装的 VST3 进行一次不创建编辑器的身份加载检查，6 项通过、0 项失败：名称/厂商、效果器类别、实例化结果和频段参数均属于 Crunch MB。
- 没有运行界面检查或全套 DSP 测试；停止 Computer Use 后没有继续操作任何软件界面。
- 已安装到 `C:\Program Files\Common Files\VST3\Crunch MB.vst3`，已核对安装 DLL 与交付 DLL 一致。
- DLL SHA-256：`AA8277C53C84BBA4D60CC8D0B4CA96AC76C964EC33F5F7E90836FD3BADCE6419`。
- 旧版备份：`_review/installed_backup_pre_1_0_4/`。
- 交付包：`dist/Crunch-MB-UI-20261022-v1.0.4.zip`。

## 升级代价

旧工程、收藏 `.fst` 或包含旧身份的预设不保证自动迁移。升级前保留副本，重新验证扫描，并从新扫描的 Crunch MB 条目加载。不要删除或更改 imagiro piano。具体操作见根目录 `FL_STUDIO_RESCAN.md`。
