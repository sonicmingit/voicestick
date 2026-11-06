# VoiceRely

VoiceRely 是 Windows x64 多设备语音输入客户端。可以在 StickS3 与 Android 手表之间轮流听写，两者使用同一个电脑客户端。

| 下载 | 适用场景 |
| --- | --- |
| [Windows 客户端便携包](Windows/VoiceRely-Windows-x64.zip) | 解压到同一目录，启动 VoiceRely.exe。包内含蓝牙组件、手表 APK、使用说明和许可证。 |
| [Android 手表 APK 0.1.7](Watch/VoiceRely-Watch-0.1.7.apk) | 安装到手表后，可通过 BLE 或无线 ADB 连接电脑。已在国行小米 Watch 5 上实测。 |
| [StickS3 OTA 固件 0.4.3](StickS3/voicestick-firmware-sticks3-ota-0.4.3.bin) | 已有兼容 OTA 分区的 StickS3 升级用应用镜像。 |
| [StickS3 完整固件 0.4.3](StickS3/voicestick-firmware-sticks3-merged-0.4.3.bin) | 用于 USB 完整刷写，包括引导程序和分区表。 |

StickS3 以上两个镜像取自原项目 0.4.3 发布包，已按发布清单核验。使用现有兼容固件连接 VoiceRely 无需刷机。Watch 安装的是 APK，使用手表原有系统。其他品牌手表的兼容条件见[项目首页](../README.md)。下载后可用 PowerShell 的 Get-FileHash 与 [SHA256SUMS.txt](SHA256SUMS.txt) 对照。

## 源码与使用说明

- [Windows 客户端及构建说明](../desktop/windows-s3/STICKS3-README.md)
- [手表应用、蓝牙组件源码与配对说明](../watch/README.md)
- [StickS3 固件源码](../firmware/)与[硬件按键说明](../docs/StickS3硬件与按键说明.md)

## 来源与许可证

原 Voice Stick 项目保留 [MIT 许可证](../LICENSE)。本客户端基于 [GetSayAll/remote-mic-app-windows](https://github.com/GetSayAll/remote-mic-app-windows) 改编；Windows 派生代码按 [GPL-3.0-only](../desktop/windows-s3/LICENSE) 分发。原项目 README、作者归属和上游链接继续保留在[客户端目录](../desktop/windows-s3/README.md)、[来源与归属](../desktop/windows-s3/ATTRIBUTION.md)及[第三方许可](../desktop/windows-s3/THIRD_PARTY_NOTICES.md)中。
