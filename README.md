# Voice Stick

Voice Stick 将 M5Stack StickS3 变成适用于 macOS、Windows 和 Linux 的蓝牙按键语音输入设备。

**Windows 0.4.2 支持豆包输入法（测试）**：设备通过蓝牙 HID 唤起豆包，麦克风声音经 VB-CABLE 交给豆包识别并填入当前输入框，无需 ASR API Key。首次使用请按 [豆包输入法使用说明](docs/豆包输入法使用说明.md) 安装驱动、完成 Windows 配对并配置音频通道与快捷键。新版同时修复客户端删除系统配对、配对后重启客户端导致长按录音失效的问题，原有 API 模式继续可用。[下载客户端、固件及 VB-CABLE 驱动包](https://github.com/sonicmingit/voicestick/releases/tag/windows-v0.4.2)。

**Windows 0.4.0 + 配套固件支持自定义按键**：主键／侧键各自可设置单击、双击、长按，默认主键单击发送、长按录音，侧键单击撤销、双击重做、长按粘贴。入口为「设置 → 按键与快捷键…」，详见 [自定义按键使用说明](docs/自定义按键使用说明.md)。以下侧键发送说明适用于旧固件或关闭自定义按键后的兼容模式。

按住 StickS3 正面按键开始录音。松开后，macOS 菜单栏应用将音频发送给语音识别服务（ASR），显示识别文字，并在短暂的确认倒计时后，将最终结果粘贴到当前获得焦点的输入框。macOS 默认在粘贴后按回车，可在设置中关闭 `auto_enter`。Windows 0.4.0 默认采用主键长按录音、单击确认发送，详见 [自定义按键使用说明](docs/自定义按键使用说明.md)。

侧面按键位于 StickS3 侧边（GPIO12，协议角色为 `secondary`）。macOS 和 Linux 用它取消识别或待确认文字；空闲时可恢复上一次仍可恢复的输入确认。Windows 0.4.0 可分别配置侧键的单击、双击、长按；旧固件或关闭自定义按键时，使用原有的 [侧键确认发送](desktop/windows/Windows使用说明.md#侧键确认) 模式。

## 项目结构

- `firmware/`：M5Stack StickS3 / ESP32-S3 的 ESP-IDF 固件。
- `desktop/macos/`：原生 macOS 菜单栏应用的 Swift 包。
- `desktop/windows/`：Windows 桌面应用工作目录。
- `desktop/linux/`：使用 GTK4 / libadwaita 的 Linux 桌面应用。
- [通信协议](docs/通信协议.md)：StickS3 与 macOS 之间的 BLE 协议。
- [自定义按键使用说明](docs/自定义按键使用说明.md)：Windows 0.4.0 六个手势、快捷键录制、预设和固件升级。
- [豆包输入法接入测试](docs/豆包输入法接入测试.md)：开发分支的虚拟麦克风、语音快捷键与配套 BLE HID 固件。
- [豆包输入法使用说明](docs/豆包输入法使用说明.md)：Windows 安装、VB-CABLE、系统配对、豆包配置、长按输入与故障排查。
- [开发日志](docs/开发日志.md)：功能迭代、构建验证、硬件刷写和用户实测记录。
- [StickS3 硬件与按键说明](docs/StickS3硬件与按键说明.md)：中文位置说明图、三个按键、麦克风和接口引脚表。
- [火山引擎 ASR 技术说明](docs/火山引擎语音识别技术说明.md)：桌面客户端使用的语音识别接口说明。
- [API 配置与版本选择指南](docs/API配置与版本选择指南.md)：Windows 配置、语音模型版本、小时版与并发版计费，以及小米 MiMo 等第三方 LLM 的接入方法。
- [发布流程](docs/发布流程.md)：桌面端和固件的打包、签名、上传与验证步骤。
- `scripts/`：精灵图切片、调色板调整及 LVGL ARGB 二进制转换辅助脚本。

## 当前功能

- StickS3 的蓝牙广播名称为 `VS-XXXX`，其中 `XXXX` 来自 eFuse MAC 地址的最后两个字节。
- macOS 应用只连接已配对的 `VS-XXXX` 设备，可同时保持多个设备连接。菜单栏列出所有已配对设备，并显示已连接或扫描中的状态。
- 正面按键对应协议中的 `primary` 角色；应用将设备置为 `ready` 状态后，按下开始录音会话，松开结束录音。
- 固件从 ES8311 麦克风读取 16 kHz 单声道 PCM，编码为 Opus 后通过 BLE 通知发送。
- macOS 应用将收到的 Opus 数据封装为 Ogg Opus，通过 WebSocket 转发给 ASR。
- ASR 支持直接接入火山引擎，或通过 VoiceStick Cloud 中转。
- Windows 0.4.2 增加豆包输入法（测试）模式：VoiceStick 选择 CABLE Input 播放端，豆包麦克风选择 CABLE Output 录制端；通过已配对设备的 HID 键盘触发一致的语音快捷键。
- 识别过程中，macOS 应用显示悬浮窗和菜单栏状态。松开按键后，固件屏幕保持思考状态，直到文字被粘贴或取消。
- macOS 和 Linux 的最终文字进入 1.2 秒确认倒计时；期间按正面按键可暂停自动粘贴，再按一次确认粘贴，按侧键则取消。空闲时按侧键可恢复上一次仍可恢复的输入确认。
- Windows 0.4.0 支持主键／侧键各自的单击、双击、长按，提供快捷键录制、可调时间参数和命名预设。识别结果直接填入输入框，不自动回车；默认单击主键确认发送。旧固件继续使用侧键发送兼容模式，详见 [自定义按键使用说明](docs/自定义按键使用说明.md)。
- 可选的调试音频缓存将每次有效识别会话保存为 Ogg Opus；能获取来源设备 ID 时，文件名会包含该 ID。
- 应用启动、设备连接或重连、手动刷新菜单时，会根据包含哈希校验信息的清单检查固件更新，并分别为各个已连接设备提供更新。
- 固件根据应用发来的 `ui_state` 更新显示配对、就绪、录音、思考、待确认、错误和电池状态。无操作 30 秒后屏幕变暗；电池供电时默认 5 分钟后进入深度睡眠，可在客户端设置中将“设备自动睡眠”调整为 1～1440 分钟（需要配套新固件）。设置保存在客户端和设备中，重连时自动同步；充电或 USB 供电时则保持暗屏。正面按键可唤醒深度睡眠中的设备。

## 目标硬件

- 开发板：M5Stack StickS3 / ESP32-S3-PICO-1-N8R8。
- 设备屏幕顶部第二行常驻显示当前运行的固件版本，例如 `FW 0.4.0`，无需连接客户端即可确认刷机结果；较长版本号自动滚动显示。
- 正面按键：GPIO11，协议角色为 `primary`，用于按键录音和深度睡眠唤醒。
- 侧面按键：GPIO12，协议角色为 `secondary`；macOS / Linux 用于取消或恢复输入确认，Windows 0.4.0 中三个手势可独立配置。
- 电源管理芯片中断（PMIC IRQ）：GPIO13。
- 音频编解码器：ES8311，通过 I2S 传输，16 kHz / 16 位 / 单声道。
- 屏幕：135 × 240 的 ST7789P3 竖屏。
- LCD 背光：GPIO38 PWM。

主要引脚定义位于 `firmware/components/stick_s3_board/include/stick_s3_board.h`。

## 交互方式

下表描述 macOS 和 Linux 的按住说话流程；Windows 0.4.0 的自定义按键流程见 [自定义按键使用说明](docs/自定义按键使用说明.md)，旧固件交互见 [侧键确认兼容模式](desktop/windows/Windows使用说明.md#侧键确认)。

| 状态 | 正面按键 | 侧面按键 |
| --- | --- | --- |
| 未配对 / 未连接 | 不录音，屏幕显示 `VS-XXXX` | 无有效操作 |
| 已连接且空闲 | 按住录音 | 恢复上一次输入确认 |
| 录音中 | 松开结束录音 | 不取消当前录音 |
| 思考 / 等待最终结果 | 忽略新的录音操作 | 取消正在进行的识别 |
| 待确认倒计时 | 暂停自动粘贴并保留待确认文字 | 取消待确认文字 |
| 等待手动确认 | 确认粘贴 | 取消待确认文字 |

固件上报原始按键事件，即带有 `primary` 或 `secondary` 角色的 `button_down` / `button_up`。macOS 应用负责交互状态机，并将 `ui_state` 更新发回固件以控制屏幕。

macOS 默认在粘贴后按回车。如需只粘贴，可在设置中关闭 `Press Return after paste`（粘贴后按回车），或在配置文件中设置 `auto_enter = false`。

## 音频传输链路

```text
StickS3 麦克风 -> ES8311/I2S PCM -> Opus -> BLE -> macOS -> Ogg Opus -> ASR -> 粘贴
```

API 模式直接转发 Ogg Opus。Windows 豆包模式将 Opus 解码为 PCM，通过 WASAPI 写入 VB-CABLE，由豆包输入法识别。

## BLE 协议概览

GATT 服务：

```text
8f2f0b84-6e6f-4b23-88f7-3a3ceafc5100
```

特征值：

| 名称 | UUID | 方向 | 属性 |
| --- | --- | --- | --- |
| `audio_tx` | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5101` | StickS3 -> Mac | 通知（notify） |
| `state_tx` | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5102` | StickS3 -> Mac | 通知（notify） |
| `control_rx` | `8f2f0b84-6e6f-4b23-88f7-3a3ceafc5103` | Mac -> StickS3 | 无响应写入（write without response） |

完整数据帧格式见 [通信协议](docs/通信协议.md)。

## 固件构建

先准备 ESP-IDF。以下命令使用本地路径 `~/esp/v5.5.1/esp-idf`；如果你的 ESP-IDF 位于其他目录，请替换相应路径。

```sh
cd firmware
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
```

如果 `export.sh` 提示缺少 ESP-IDF Python 虚拟环境，请先运行一次对应的安装脚本：

```sh
"$HOME/esp/v5.5.1/esp-idf/install.sh" esp32s3
```

烧录并查看串口日志：

```sh
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

固件的 OTA 分区表包含两个 3 MB 应用分区，以及一个预留的 1984 KB `storage` 分区。使用旧版单应用分区表的设备，需先通过 USB 重新烧录一次以安装新分区表，之后才能使用 BLE OTA 更新：

```sh
idf.py -p /dev/cu.usbmodemXXXX erase-flash flash monitor
```

固件通过 ESP-IDF 组件管理器声明以下依赖：

- `espressif/button`
- `espressif/esp_codec_dev`
- `78/esp-opus`
- `lvgl/lvgl`

## macOS 桌面端构建

桌面应用采用 Swift 包，支持 macOS 12 及更新版本。

```sh
cd desktop/macos
swift build
```

运行：

```sh
swift run VoiceStickApp
```

应用在菜单栏运行，并申请蓝牙权限。文字输入通过模拟 `Command-V` 完成，也可选择随后按回车。如果 macOS 阻止键盘事件，请在系统设置中为运行中的终端或应用授予辅助功能权限。

构建支持 Sparkle 更新的 macOS 分发版本：

```sh
SPARKLE_PUBLIC_ED_KEY="..." scripts/build-macos.sh --release
scripts/make-dmg.sh
```

构建脚本会生成 `build/VoiceStick-<version>.app`、`build/VoiceStick-<version>.zip` 和 Sparkle 签名文件。将 DMG 和 ZIP 上传到 GitHub Releases，再更新 `website/public/appcast.xml` 对应的 GitHub Pages 更新源。

支持 WinSparkle 更新的 Windows 分发版本使用 MSI 作为更新包。Windows 签名证书应保存在本地签名机器上，例如通过 USB 硬件密钥提供：

```bat
scripts\build-msi.bat
```

脚本在本地为 `VoiceStick.exe`、`WinSparkle.dll` 和 `VoiceStick_<version>.msi` 签名。将 MSI 上传到对应的 GitHub Release，然后手动运行 `Deploy Website to GitHub Pages` 工作流，使共享 appcast 将 Windows 客户端指向该发布附件的地址。

## Linux 桌面端构建

Linux 应用使用 GTK4 + libadwaita。在 GNOME 下，需要安装 `gnome-shell-extension-appindicator`（Debian 的 UUID 为 `ubuntu-appindicators@ubuntu.com`），然后重新登录，顶栏图标才会出现。

```sh
sudo apt install cmake ninja-build g++ pkg-config \
  libgtk-4-dev libadwaita-1-dev libsoup-3.0-dev \
  libglib2.0-dev libatspi2.0-dev libssl-dev \
  gnome-shell-extension-appindicator
cmake -S desktop/linux -B desktop/linux/build -G Ninja
cmake --build desktop/linux/build
desktop/linux/build/VoiceStick
```

配置文件位于 `~/.config/voicestick/config.toml`。应用通过 GTK 复制文字，然后尝试使用 AT-SPI 插入；如果失败，则通过 GNOME 远程桌面门户模拟 Ctrl+V。详见 [Linux 使用说明](desktop/linux/Linux使用说明.md)。

推送 `v<version>` 标签后，GitHub Actions 可自动完成 macOS 和固件的发布。标签必须与 `VERSION` 一致，例如 `VERSION=0.2.1` 对应 `v0.2.1`。发布工作流将 macOS DMG、ZIP、签名和固件文件上传到 GitHub Releases，再将网站及 appcast 部署到 GitHub Pages。Windows MSI 随后由本地签名机器上传。完整发布步骤见 [发布流程](docs/发布流程.md)，其中包含先准备 Windows 包和后补 Windows 包两种顺序。

同一发布工作流还使用 ESP-IDF v5.5.1 构建 StickS3 固件，并将构建产物上传到阿里云 OSS：

| 文件 | 用途 |
| --- | --- |
| `voicestick-firmware-sticks3-ota-<version>.bin` | macOS 应用使用的 BLE OTA 镜像 |
| `voicestick-firmware-sticks3-merged-<version>.bin` | 浏览器 / USB 烧录镜像，写入偏移地址 `0x0` |
| `manifest.json` | 供应用和网站读取的最新固件元数据 |

构建产物同时发布到对应版本目录和 `latest` 目录：

```text
voicestick/firmwares/<version>/manifest.json
voicestick/firmwares/latest/manifest.json
```

macOS 应用在启动、设备连接或重连时检查固定的最新固件清单地址；持续运行期间，每 24 小时最多检查一次。菜单中的 `Check for Firmware Updates`（检查固件更新）可手动刷新。如果已连接设备上报的 `firmware_version` 低于清单版本，设备子菜单会显示 `Update to <version>...`（更新到指定版本）。应用下载清单中的 `ota_url`，校验 `ota_size` 和 `ota_sha256` 后，才开始 BLE OTA。

运行发布工作流前，请配置以下 GitHub 机密：

| 名称 | 说明 |
| --- | --- |
| `ALIYUN_OSS_ACCESS_KEY_ID` | OSS 上传访问密钥 ID |
| `ALIYUN_OSS_ACCESS_KEY_SECRET` | OSS 上传访问密钥 Secret |
| `ALIYUN_OSS_ENDPOINT` | OSS 服务地址，例如 `https://oss-cn-hangzhou.aliyuncs.com` |
| `ALIYUN_OSS_BUCKET` | OSS 存储桶名称 |

将仓库变量 `ALIYUN_OSS_PUBLIC_BASE_URL` 设置为 OSS 公共基础地址，例如 `https://xiaozhi-voice-assistant.oss-cn-shenzhen.aliyuncs.com`。可选变量 `ALIYUN_OSS_PREFIX` 用于设置 OSS 对象前缀，默认值为 `voicestick/firmwares`。

## 本地配置

配置文件路径：

```text
macOS:  ~/Library/Application Support/VoiceStick/config.toml
Windows: %APPDATA%\VoiceStick\config.toml
Linux:  ~/.config/voicestick/config.toml
```

macOS 可从示例文件创建配置：

```sh
mkdir -p "$HOME/Library/Application Support/VoiceStick"
cp desktop/macos/Config/config.example.toml "$HOME/Library/Application Support/VoiceStick/config.toml"
```

配置示例：

```toml
asr_provider = "volcengine"
voicestick_api_key = ""
voicestick_cloud_url = "wss://api.xiaozhi.me/voicestick/asr/"
volcengine_api_key = "your_volcengine_asr_api_key"
llm_base_url = "https://api.openai.com/v1"
llm_api_key = "your_openai_compatible_llm_api_key"
llm_model = "gpt-5.5"
interaction_mode = "hold_to_talk"
resource_id = "volc.seedasr.sauc.duration"
asr_hotwords = "小智,VoiceStick"
paired_device_ids = ""
device_theme_colors = ""
device_overlay_positions = ""
auto_enter = true
debug_audio_cache = false
# debug_audio_dir = "~/Library/Application Support/VoiceStick/DebugAudio"

[output]
target = "focused_app"
transform = "original"
translation_target = "en"

# 可选：单独设置某台设备的字幕翻译：
# [device.C3D8.output]
# transform = "translate"
# translation_target = "en"
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `asr_provider` | `volcengine`（火山引擎）、`voicestick_cloud`（云中转）；Windows 另支持 `doubao_ime`（豆包输入法测试模式） |
| `ime_audio_endpoint` | Windows 豆包模式的 VB-CABLE 播放端 ID，在客户端设置中选择 CABLE Input 后自动保存 |
| `ime_voice_shortcut` | Windows 豆包模式语音快捷键：`left_ctrl_win` 或 `right_alt`，需与豆包设置一致 |
| `volcengine_api_key` | 直接接入火山引擎的 API Key，通过 `X-Api-Key` 发送 |
| `voicestick_api_key` | VoiceStick Cloud 中转服务的 API Key，通过 `X-Api-Key` 发送 |
| `voicestick_cloud_url` | 云中转服务的 WebSocket 地址 |
| `llm_base_url` | 兼容 OpenAI 接口格式的 LLM API 基础地址，可使用小米 MiMo 等第三方服务 |
| `llm_api_key` | LLM 服务商的 API Key，供翻译使用 |
| `llm_model` | LLM 模型 ID，需与服务商实际支持的模型一致 |
| `interaction_mode` | 正面按键交互模式：`hold_to_talk`（按住说话）或 `click_to_talk`（点击开始 / 结束） |
| `resource_id` | 火山引擎资源 ID |
| `asr_hotwords` | 用英文逗号分隔的 ASR 热词，也会传给 LLM 作为翻译术语提示 |
| `paired_device_ids` | 用英文逗号分隔的 4 位十六进制设备 ID，例如 `C3D8,09AF` |
| `device_theme_colors` | 可选的设备悬浮窗颜色，例如 `C3D8:pink,09AF:green` |
| `device_overlay_positions` | 可选的设备悬浮窗位置，例如 `C3D8:top_left,09AF:bottom_right` |
| `auto_enter` | 是否在粘贴后按回车；Windows 启用侧键发送时忽略此项 |
| `debug_audio_cache` | 是否保存调试用 Ogg Opus 音频文件 |
| `debug_audio_dir` | 调试音频输出目录 |
| `[output].target` | `focused_app`（当前输入窗口）或 `subtitle`（字幕） |
| `[output].transform` | `original`（原文）或 `translate`（翻译） |
| `[output].translation_target` | LLM 翻译目标语言代码，例如 `en` 或 `zh-Hans` |
| `[device.<id>.output]` | 可选的设备独立文字处理模式与翻译目标设置 |

支持的火山引擎 `resource_id` 值：

- `volc.seedasr.sauc.duration`
- `volc.seedasr.sauc.concurrent`
- `volc.bigasr.sauc.duration`
- `volc.bigasr.sauc.concurrent`

请勿将真实 API Key 提交到仓库。完整配置步骤、版本区别及第三方 LLM 示例见 [API 配置指南](docs/API配置与版本选择指南.md)。

## 配对流程

1. 烧录并启动 StickS3，屏幕显示 `VS-XXXX`。
2. 启动 macOS 桌面应用。
3. 在菜单栏应用中打开 `Pair Device...`（配对设备）。
4. 在扫描列表中选择对应的 `VS-XXXX`，点击 `Pair`（配对）。
5. 保存后，桌面应用开始扫描并连接该设备。可重复此流程配对更多设备。

也可以手动编辑 `paired_device_ids`。保存多个 ID 后，桌面应用会忽略附近未配对的 VoiceStick 设备。

Windows 豆包模式还需在 Windows 系统蓝牙设置中配对设备以加载 HID 键盘服务，步骤见 [豆包输入法使用说明](docs/豆包输入法使用说明.md#4-在-windows-中配对-vs-设备)。

## 调试音频

启用方法：

```toml
debug_audio_cache = true
```

macOS 默认输出目录：

```text
~/Library/Application Support/VoiceStick/DebugAudio
```

每次有效识别会话会保存为可播放的 Ogg Opus 文件。短于 0.5 秒的录音会被桌面应用丢弃，不发送给 ASR。

## 许可证

本仓库基于原作者的 [78/voicestick](https://github.com/78/voicestick) 项目修改和维护。原项目代码及贡献的版权仍归各自权利人所有；上游项目的许可证版权声明为 `Copyright (c) 2026 Terrence`。本仓库的后续修改不会取代、转移或取消原作者的版权声明，也不代表原作者为本仓库的改动背书。

本项目采用 [MIT 许可证](LICENSE)。MIT 许可证允许使用、复制、修改和再分发，但分发本软件的副本或实质性部分时，必须保留原有版权声明和许可文本。因此，请勿在再分发时删除或替换 `LICENSE` 中原作者的版权声明。仓库内第三方依赖各自适用其目录中的许可证。项目及随附第三方依赖的中文许可译文见 [许可证中文参考译文](docs/许可证中文参考译文.md)。

根目录的 `VBCABLE_Driver_Pack45.zip` 为 [VB-Audio](https://vb-audio.com/Cable/) 提供的 VB-CABLE 原始分发包，属于 Donationware，欢迎支持原作者；按包内 `readme.txt` 的版权和许可原样再分发，不适用本项目的 MIT 许可证。该 ZIP 同时作为 Release 独立附件提供，需用户单独安装。
