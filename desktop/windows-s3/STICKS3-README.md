# VoiceStick S3 实验版

在独立目录中将 [SayAll Windows](https://github.com/GetSayAll/remote-mic-app-windows) 的 Vue / Tauri Windows 客户端移植到 M5Stack StickS3。保留原有页面布局、双按键编辑、应用启动、组合动作、主题、配置导入导出和本地诊断等公开功能。

这是本地试验版本。**自动化测试、设备握手与真实语音识别是不同的验证阶段；编译或握手通过不代表豆包已能稳定出字。** 最新结果见 [验证记录](Testing/WindowsStickS3.md)。

## 运行

双击 `local-build/VoiceStickS3.exe`。首次进入“连接”页面：

1. 确认 Windows 已配对 `VS-XXXX`，使用现有 VoiceStick 0.4.x 支持自定义按键的固件。本实验不刷写固件。
2. 通过原客户端的退出菜单退出 VoiceStick，再在本客户端扫描并连接 StickS3，等待设备初始化完成。
3. “连接”页可选择 **火山豆包 API、豆包输入法、微信输入法或其他工具**。默认保留已验证的火山 API 模式。火山模式下，首次启动自动导入原 VoiceStick 配置中的火山 Key、资源 ID 和热词；原配置保持不变。也可以输入自己的 API Key 并选择已开通的资源。
4. 点击“测试 API 连接”可用静音音频检查鉴权与最终响应；它会调用你的火山语音服务。Key 使用 Windows 当前账户的 DPAPI 加密保存在本机，不通过 IPC 回显，不写入日志。
5. 在记事本等输入框中，长按正面按键说话，松开后等待最终识别文字。识别期间保持目标输入框处于前台；焦点已改变时，结果保留在“连接”页，可手动复制。

语音直接调用火山引擎的公开流式语音接口，音频会发送到配置的火山账户。Vokie 的选择、检测、启动与安装入口保持移除。火山 API 模式不依赖 VB-CABLE、输入法语音快捷键或设备 HID 语音键应答；豆包、微信和其他工具保留各自的声卡、增益及语音快捷键设置。

## 输入工具、实时浮窗和剪贴板

- 选择“火山豆包 API”时，可以开启“显示实时语音浮窗”。浮窗实时更新中间文字与字数，完成后显示最终状态并自动收起；窗口置顶、不激活、不拦截鼠标，不会抢走正在输入的窗口焦点。
- 文字输出可选“自动输入到当前输入框”或“复制到剪贴板并输入到当前窗口”。后者先在最终识别完成后复制完整文字，再自动输入到当前窗口；剪贴板内容会保留，便于再次粘贴。默认仍是自动输入，选择会在重启后保留。正在进行的录音使用开始时的输出方式。
- 选择“豆包输入法”“微信输入法”或“其他工具”时，恢复原有虚拟声卡和按住说话快捷键配置。播放端选择 CABLE Input / CABLE In 16 Ch，对应工具的麦克风选择 CABLE Output，快捷键保持一致。微信默认左 Ctrl + 左 Win；其他工具记住用户选择。
- 豆包输入法的设备硬件语音键此前在本机未通过验收；恢复入口和通道不代表该固件/Windows 通道已修复。火山 API 已由用户确认能持续录音并出字。

## 两个按键

| 物理按键 | 单击 | 双击 | 长按 |
| --- | --- | --- | --- |
| 正面按键 | Enter | 未设置 | 按住说话 |
| 侧面按键 | Ctrl + Z | Ctrl + Y | Ctrl + V |

“按键”页只有这两张卡片，共六个设置格。两个按键的长按格都可以选择“按住说话”，其他格沿用上游的快捷键、打开应用、鼠标动作和组合动作编辑器。固件提供约 500 ms 长按门槛并预录音；开启双击时，单击需等待约 300 ms。关闭自定义按键时，正面长按保留录音，其他动作关闭。

配置继续使用上游的稳定按键 ID：`ok` 对应正面，`menu` 对应侧面。界面显示物理名称。连续执行和“移动光标后点击”的小米专用机制不适用于此固件，不应依赖它们。

## 移植范围

- 自定义 GATT：`8f2f0b84-6e6f-4b23-88f7-3a3ceafc5100`，使用现有 VoiceStick 的状态、控制和 16 kHz 单声道 Opus 通道。
- 火山模式下，设备 Opus 在本机解码为 16 kHz 单声道 PCM，经 gzip 二进制帧直接发送到火山 `bigmodel_async` 接口。每次录音独立请求，约 180 ms 聚合发送，END 触发末包；不依赖原 C++ 客户端的云端连接复用协议。
- 按下后后台建连，BLE 接收线程不等待网络；发送队列有界，连接、最终结果和整段录音都有超时。取消、断连、睡眠、退出会取消网络任务；旧代次结果不写入新的会话。
- 只插入最终识别文字，不把中间修订结果反复写进输入框；目标窗口改变时保留结果并提示复制。异常只取消当前语音，保留可用的蓝牙连接。
- 断连、丢失 END、音频序号缺口与退出会收尾；迟到的旧连接通知、旧会话音频和重复 END 不会启动新会话。连接失败按 2–30 秒退避重试。
- 配置标识为 `me.voicestick.sayall.sticks3`；运行数据位于 `%LOCALAPPDATA%\VoiceStickS3`，API 凭据单独存储。仅首次迁移自有 VoiceStick 的火山 ASR 字段，不迁移其他配置或官方 SayAll 数据。
- 官方更新源已关闭，避免实验版被替换为小米版本。官方 SayAll 更新源已关闭，本移植版不通过该渠道自动更新。

公开仓库里的部分会员/私有组件只有空壳；本实验保留其不可用状态，不包含未公开实现。小米的 ATVV、增强捕获 Helper、特定硬件恢复方式不能直接用于 StickS3。蓝牙音频接收、断连恢复和长期稳定性仍需真实设备验证。

## 构建和检查

需要 Windows x64、Rust stable、MSVC C++ 工具与 Windows SDK、Node.js、pnpm、CMake、Ninja、WebView2。

```powershell
pnpm install --frozen-lockfile
pnpm test
pnpm build
cargo fmt --all -- --check
cargo test --workspace
cargo check -p sayall-windows-app --features runtime-simulation
cargo run -p sayall-windows --example sticks3_probe -- --connect
pnpm tauri build --debug --no-bundle --features hide-console
```

`scripts/build-sticks3.ps1 -Verify -Probe` 可完成检查并生成 `local-build/VoiceStickS3.exe`。便携工具链可通过 `-RustTools` 和 `-MsvcEnvironment` 参数传入；脚本不更改用户的系统 PATH。发布包为 Windows x64 便携版，构建配置以发布说明为准。

当前测试针对两按键界面、原有通用界面功能与新协议。日志只记录阶段、会话号和计数，不保存原始音频/识别文字；上游可选的“回眸”功能遵从其本地保存开关。

## 来源与许可

上游基线：`GetSayAll/remote-mic-app-windows`，提交 `f3f52127973ef0327957c3058d4b5f3413ad803e`。派生代码继续使用 **GPL-3.0-only**，保留 [LICENSE](LICENSE)、来源与第三方说明。本实验界面标识为 VoiceStick S3，与官方 SayAll 无隶属关系。

上游 App Logo/Icon 另有授权要求，已替换为本仓库脚本绘制的原创设备图标；StickS3 展示图以现有 VoiceStick 设备图为基础，经图像生成工具移除背景并作视觉调整。新增与改编模块见 [ATTRIBUTION.md](ATTRIBUTION.md)。

## 电量功能状态

设备屏幕可显示电量。向客户端报告电量的配套固件代码尚未刷入真机，USB 写入尝试未完成；连接页可能显示“电量未知”。不要把源码构建通过视为固件已验证。
