# VoiceStick Watch 0.1.6

国行 Xiaomi Watch 5（M2505W1）前台语音输入原型。Windows 接收器复用 VoiceStick S3 的火山 API 配置、前台输入和可选剪贴板输出。手表以 16 kHz 单声道录音，可选 BLE 或无线 ADB 传输，电脑返回识别文字。请在可信的个人电脑上使用。

## 在 VoiceStick S3 客户端里连接（推荐）

打开「连接 → 小米 Watch 5」，直接选择「蓝牙 BLE」或「无线 ADB」。通过 ADB 安装或更新时，将手表 APK 放在新版 VoiceStickS3.exe 同一目录，然后点击「连接 ADB」。已授权且通过 ADB 在线的 Watch 5 可以自动识别；已配对但尚未在线时可填写手表无线调试页的连接 IP 和端口。客户端会安装或更新手表 APK、设置转发并打开手表应用。状态显示「已连接」表示收到手表心跳。左右键可以直接在这里设置，收到手表回报后结束等待。客户端可同时保持 StickS3 蓝牙连接，火山听写一次处理一台设备的一段录音。

客户端启动时恢复手表接收服务。如果无线调试地址变化，请在无线 ADB 页输入新地址并点击「连接 ADB」。手表音频使用连接页的火山 API 和文字输出设置，集成版共用桌面的识别状态；手表录音尚未写入「回眸」。

## 蓝牙直连（实验）

安装手表 APK 后，可直接蓝牙配对，无需打开无线调试。手表点顶部状态，选「使用蓝牙 BLE」并允许附近设备权限，再选「查看蓝牙配对码」。电脑的「连接 → 小米 Watch 5 → 蓝牙 BLE」输入表上显示的 8 组字符，点「连接 BLE」。已配对的电脑可直接重连。配对码是本机凭据，请勿公开。电脑显示「BLE 已连接」表示收到了手表通过 GATT 发来的已认证数据。使用时保持手表应用在前台，电脑联网访问火山。电脑选择「无线 ADB → 连接 ADB」可切换回无线调试。手表听写始终走火山 API，StickS3 的输入法设置与手表连接方式分别生效。

手表广播 GATT 服务，以适应协商包大小的 IMA ADPCM 帧传输音频，使用首次配对得到的随机密钥和每次连接的新挑战进行 AES-GCM 验证。Windows 蓝牙组件位于 `watch/ble_gateway.py`。在 Windows Python 虚拟环境安装 `watch/ble-requirements.txt` 后运行 `watch/build-ble.ps1`，指定环境的 Python 路径。将生成的 `VoiceStick-Watch-BLE.exe` 和 APK 放在客户端旁。

## 单独运行接收器（开发用）

1. 先在 VoiceStick S3 的连接页配置可用的火山 API Key 和文字输出方式。
2. 用 Android 平台工具连接手表的无线调试，运行 `adb devices -l` 确认 M2505W1 的当前序列号。
3. 解压初版包，在该目录打开 PowerShell，执行以下命令。脚本安装 APK、启动 Windows 接收器并设置 ADB 转发，每次运行都会更换配对令牌。无线调试地址改变或电脑重启后，用当前序列号重新运行。

```powershell
.\connect.ps1 -Serial '当前序列号' -Folder .
```

在电脑上将光标放到输入框。按住手表中间的麦克风说话，首次允许录音后再按一次。识别中的文字会显示在表上，松手后向电脑提交最终结果。超过一屏的内容可上下滑动。

点顶部连接状态进入设置。左右两个快捷键可各自设为回车、撤销或粘贴。选“绑定实体键”后按下并松开目标键，只有送到当前应用的按键可以绑定；绑定后在应用前台长按说话。切出应用会取消正在采集的录音。停止测试时可运行 `Stop-Process -Name watch5-bridge` 关闭独立接收器。

## 从源码构建

手表端使用 JDK 17、Android SDK Platform 35、Build Tools 35。指定 SDK 的实际路径：

```powershell
.\watch\build.ps1 -BuildTools 'Android SDK 构建工具目录' -PlatformJar 'Android SDK 平台的 android.jar'
```

在 `desktop/windows-s3` 的 Windows MSVC 构建环境运行 `cargo build --release -p sayall-windows --bin watch5-bridge`，将产出的 EXE 复制到仓库的 `build/watch5`。然后运行 `watch/connect.ps1 -Serial '当前序列号'`。构建脚本生成的本地开发签名留在构建目录。

独立接收器模式下，手表会话不经过桌面浮窗或“回眸”。M2505W1 真机验证了首页、连接、按屏幕录音、手表显示文字和 Windows 前台输入。本次测试的实体键被系统接管，请用屏幕麦克风录音。熄屏全局触发、免 ADB 配对和长时间稳定性仍待验证。

