# VoiceStick 0.4.0 Windows 中文定制版 + StickS3 固件

Windows x64 客户端和 StickS3 固件均为 0.4.0。自定义按键需要将设备固件升级到 0.4.0；旧固件仍可使用原有兼容操作。

## 更新内容

- 主键和侧键各自支持单击、双击、长按，共六个可配置手势。
- 新增「设置 → 按键与快捷键…」，支持动作配置、快捷键录制、时间参数、命名预设和恢复默认。
- 默认主键单击确认发送、长按录音；侧键单击撤销、双击重做、长按粘贴。
- 识别结果填入原输入框后立即收起悬浮窗；每次填入只允许确认发送一次。
- 固件通过 BLE 同步手势配置，并在确认录音手势后才开始传输预缓存音频。

## 下载

- `VoiceStick_0.4.0_zh-CN_custom-buttons_x64.msi`：Windows 安装版。
- `VoiceStick_0.4.0_zh-CN_custom-buttons_x64.zip`：便携版，解压后运行 `VoiceStick.exe`。
- `voicestick-firmware-sticks3-ota-0.4.0.bin`：设备 OTA 镜像。
- `voicestick-firmware-sticks3-merged-0.4.0.bin`：USB 刷写用的完整合并镜像。
- `manifest.json`：本次发布的固件版本、下载地址、SHA-256 和文件大小。
- `SHA256SUMS.txt`：以上安装包和固件文件的 SHA-256 校验值。

安装 Windows 客户端前请退出旧版 VoiceStick。原有配置继续使用，安装包不包含 API 密钥。此定制构建未签名，并关闭官方 Windows 客户端自动更新。

## 验证范围

开发记录中包含 Windows 自动测试、ESP-IDF 5.5.2 固件构建、安装包内容检查，以及 StickS3 0.4.0 的 BLE 连接和按键配置同步实测。用户完成实际操作测试并反馈通过。

此前有一次 Windows USB 配置描述符请求失败，原因尚未确认；BLE 连接、固件版本读取和按键配置同步已验证正常。实测记录见[开发日志](开发日志.md)。
