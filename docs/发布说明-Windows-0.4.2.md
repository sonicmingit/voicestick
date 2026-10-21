# VoiceStick 0.4.2：豆包输入法支持与 Windows 蓝牙修复

本版发布 Windows x64 中文客户端和 StickS3 配套固件。支持通过设备长按唤起豆包输入法、经 VB-CABLE 传输麦克风音频；原有火山引擎和 VoiceStick Cloud API 模式继续可用。界面的豆包服务仍标记为“测试”。

## 豆包输入法怎么用

1. 安装本版客户端及带 HID 键盘服务的配套固件。
2. 单独下载 `VBCABLE_Driver_Pack45.zip`，完整解压，以管理员身份运行 `VBCABLE_Setup_x64.exe`，安装完成后重启 Windows。
3. 从托盘退出旧版客户端，在 Windows 蓝牙设置中配对 `VS-XXXX`，再启动本版 VoiceStick。
4. VoiceStick 选择“豆包输入法（测试）”和 `CABLE Input`；豆包输入法的麦克风选择 `CABLE Output`。两边语音快捷键保持一致，例如长按左 Ctrl + 左 Win。
5. VoiceStick 输出设为原文输入，聚焦目标输入框并切到豆包，长按设备说话，松开后等待豆包出字。此模式无需 ASR API Key。

详细教程：[豆包输入法使用说明](https://github.com/sonicmingit/voicestick/blob/windows-v0.4.2/docs/豆包输入法使用说明.md)。客户端安装目录、便携包和本 Release 的独立附件也附带该教程，包含驱动安装、音频方向、系统配对、快捷键、切回 API 及故障排查。

VoiceStick 的“正在聆听”和豆包语音框会同时出现。豆包负责填入文字；默认主键单击会发送回车，请先等文字出现再确认。字幕、翻译、ASR 热词和识别文字恢复请切回 API 模式。

## 修复内容

- 修复客户端启动时自动删除 Windows 蓝牙配对的问题。该行为会移除已配对设备的 HID 键盘绑定，使豆包模式报告“蓝牙键盘尚未就绪”。
- 服务发现超时和连接失败恢复时同样保留系统配对，只回收客户端自身的连接句柄并有限次重试。
- 取消自动关闭／重开整台电脑蓝牙的恢复行为。
- 修复 Windows 已配对、HID 保持连接时，重启客户端收不到设备信息和按键配置确认的问题。连接时重新订阅客户端自身的状态特征，确保重新完成能力与按键同步，避免长按录音事件被忽略。
- 保留客户端版本显示、设备版本显示、睡眠配置及原有按键功能。

## 安装与验证

退出旧客户端后安装 MSI，或解压便携 ZIP 后运行 `VoiceStick.exe`。原有配置继续使用。已经装有配套 HID 固件的设备无需为这次连接修复重新刷机；首次使用豆包或原固件不包含 HID 时，请安装本版固件。

若旧客户端已经删除了配对，先退出旧客户端，在 Windows 蓝牙设置中重新配对 VS 设备，再启动本修复版。豆包模式仍需要配套 HID 固件、VB-CABLE 和一致的语音快捷键。

本机已在 VS-5614 上验证：修复版启动及保持配对后的客户端重启，均保留系统配对和连接、读取设备信息并收到按键配置确认；Windows HID 键盘驱动正常加载，4 项本地测试通过。2026-10-21 11:17:59–11:18:05，保持 Windows 配对后的真实长按录音完成 API 识别和文字粘贴，用户确认 API 恢复。11:19:31–11:19:42，豆包模式收到硬件按键成功确认、检测到豆包语音窗口、音频 END 后释放按键并完成会话；用户确认语音输入对话框出现。尚未单独确认豆包出字准确度、音频首尾完整性及其他长时间稳定性场景。

此本地定制版关闭官方客户端自动更新，设备固件更新保留；安装包未签名。

## 发布附件

- Windows 客户端：`VoiceStick_0.4.2_zh-CN_custom-buttons_x64.msi`、`VoiceStick_0.4.2_zh-CN_custom-buttons_x64.zip`，二选一。
- StickS3 固件：`voicestick-firmware-sticks3-ota-0.4.2.bin`、`voicestick-firmware-sticks3-merged-0.4.2.bin`。合并镜像经 USB 写入 `0x0`，OTA 镜像仅用于应用 OTA。
- `manifest.json`、`SHA256SUMS.txt`：固件下载元数据及所有附件的校验值。
- `VBCABLE_Driver_Pack45.zip`、`豆包输入法使用说明.md`：手动安装的虚拟声卡驱动与详细教程。

VB-CABLE 由 [VB-Audio](https://vb-audio.com/Cable/) 提供，属于 Donationware，欢迎支持原作者。本 Release 按包内许可原样提供独立 ZIP，保留版权和许可；它不适用项目的 MIT 许可证，不由 VoiceStick 安装程序自动安装。

本 Release 的固件清单使用 GitHub 下载地址；未更新上游阿里云的固件更新源。若客户端自动检查未提示本版本，可从本页下载配套固件升级。
