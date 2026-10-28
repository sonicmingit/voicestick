# VoiceStick S3 Windows 0.1.0 预览版

这是基于 SayAll Windows 公开源码的 StickS3 实验移植，与 SayAll 官方发行版无隶属关系。下载 `VoiceStick-S3-Windows-x64-0.1.0-preview.1.zip`，解压后运行 `VoiceStickS3.exe`。需要 Windows x64 和 WebView2；此包未做 Authenticode 签名。

## 功能

- 保留 StickS3 正面与侧面按键，各支持单击、双击和长按配置。
- 火山豆包 API 流式语音识别、实时浮窗及位置选择；支持自动输入，或复制到剪贴板后输入当前窗口。
- 恢复豆包输入法、微信输入法和其他工具的入口；相关输入法模式可能需要 VB-CABLE。
- 保留组合动作、回眸和个人中心；火山 API Key 由 Windows 当前账户加密保存在本机。

## 已知边界

- 火山 API 长按录音和输入已由真实设备用户确认；浮窗、输入与剪贴板也得到用户反馈。长期稳定性、睡眠唤醒及所有手势仍需进一步验证。
- 豆包输入法依赖设备的 HID 语音通道；该通道在测试设备上曾失败，不承诺可用。
- 无需为火山 API 刷写新固件。电量上报固件虽然已编译，但尚未成功刷入和真机验证，客户端可能显示“电量未知”。本次不发布固件镜像。
- 本地首次配置可能从原 VoiceStick 配置中导入火山 ASR 字段。音频会发送到用户配置的火山服务；请自行配置和承担对应服务费用。
- 官方 SayAll 自动更新源已关闭。本预览包为未签名的独立便携程序。

## 来源和许可

代码来源及改编范围见 [来源与归属](desktop/windows-s3/ATTRIBUTION.md)。`desktop/windows-s3/` 为 GPL-3.0-only；原 VoiceStick 项目按根目录 MIT 许可证发布。压缩包含 GPL 许可证和第三方许可说明，对应源码随本 Release 源码快照提供。
