# VoiceStick Windows

Windows 桌面客户端的原生 Win32/C++20 实现工作目录。

## Windows 支持范围

VoiceStick 支持 64 位 Windows 10 1903 / 1909 及更新版本，包括教育版等常规 Windows 10 版本。较新的系统可直接提供 BLE 广播地址类型；2019 年的 Windows 10 版本则回退到仅使用地址的 BLE 连接方式。

当前功能范围：

- Win32 托盘应用及悬浮窗。
- 配置文件位于 `%APPDATA%\VoiceStick\config.toml`。
- 调试音频缓存位于 `%LOCALAPPDATA%\VoiceStick\DebugAudio`。
- 通过 C++/WinRT 扫描 BLE 广播。
- VoiceStick 核心协议解析、Ogg Opus 封装、ASR 二进制帧处理及协调器状态机。
- 通过剪贴板和 `SendInput` 插入文字。
- 通过 WinSparkle 检查应用更新，与 macOS 的 Sparkle 更新器共用同一 appcast 更新源。

BLE GATT 实现包含音频与按键通知、控制指令写入及固件 OTA。具体硬件行为仍需在目标设备上验证。

API 字段、火山语音版本和小米 MiMo 等第三方翻译模型的配置见 [API 配置与版本选择指南](../../docs/API配置与版本选择指南.md)。

## 侧键确认

默认情况下，Windows 将识别文字粘贴到当前获得焦点的输入框，但不按回车。你可以先检查或编辑文字，再按 StickS3 侧键向当前应用发送回车。每次粘贴结果只能由同一台设备确认发送一次。在此模式下，文字粘贴前的侧键操作会被忽略。开始新录音、断开设备、恢复上一次输入或修改设置，都会清除待发送操作。确认时请保持目标输入框获得焦点。

托盘菜单中的“按侧键确认发送”和设置窗口中的对应选项控制此行为，并以 `side_button_send = true` 保存到 `%APPDATA%\VoiceStick\config.toml`。启用时，“填入后自动回车”不可用，`auto_enter` 设置也会被忽略。关闭侧键发送选项后，可恢复原有的取消 / 恢复行为及 `auto_enter` 设置。字幕输出保留原有的侧键行为。

侧键模式会在识别完成后直接填入文字，省去原先的 1.2 秒倒计时，并立即收起悬浮窗，不显示输入完成提示。切换到其他窗口后，请返回原输入窗口再按侧键，不会重复粘贴。关闭侧键模式后保留原倒计时交互。无需修改固件。

在 MSVC x64 环境中运行 `scripts/build-windows-local.ps1` 可生成中文定制版 MSI、便携 ZIP 和校验文件。定制版关闭官方客户端自动更新，保留本地功能。详见[中文定制版说明](../../docs/Windows中文定制版说明.md)。

## 构建

在普通 PowerShell 窗口中运行：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S desktop\windows -B desktop\windows\build-x64 -G Ninja -DCMAKE_MAKE_PROGRAM="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"'
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C desktop\windows\build-x64'
```

运行测试：

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir desktop\windows\build-x64 --output-on-failure'
```

运行应用：

```powershell
desktop\windows\build-x64\VoiceStick.exe
```

在本地构建已签名的 MSI：

```powershell
$env:SIGNING_SHA1 = "YOUR_CERT_THUMBPRINT"
scripts\build-msi.bat
```

将 `desktop\windows\build-msi-x64\VoiceStick_<version>.msi` 上传到对应的 GitHub Release，然后手动运行 `Deploy Website to GitHub Pages` 工作流。MSI 是 WinSparkle 更新包；appcast 条目记录 GitHub Release 的 MSI 地址、字节大小、版本、Windows 系统标记和 `/passive` 安装参数。

如果旧的 `desktop\windows\build` 目录曾在错误的 Visual Studio 环境中配置，请删除或忽略该构建目录。混用 x86 CMake 缓存和 x64 SDK 库会导致链接错误。

## 补充说明

代码遵循 Google C++ 命名风格：文件名使用 `snake_case`，类型使用 `CapWords`，函数和方法使用 `MixedCase()`，变量使用 `snake_case`，缩进为 4 个空格。
