# VoiceStick Watch 0.1.0

国行 Xiaomi Watch 5（M2505W1）前台语音输入原型。Windows 接收器复用 VoiceStick S3 的火山 API 配置、前台输入和可选剪贴板输出。手表发送 16 kHz 单声道 PCM，电脑返回识别文字。当前版本通过已授权的无线 ADB 转发连接，接收器只监听电脑回环地址。请在可信的个人电脑上使用。

## 安装和连接

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

本版使用独立接收器，手表会话尚未接入桌面浮窗或“回眸”。M2505W1 真机验证了首页、连接、按屏幕录音、手表显示文字和 Windows 前台输入。本次测试的实体键被系统接管，请用屏幕麦克风录音。熄屏全局触发、免 ADB 配对和长时间稳定性仍待验证。

