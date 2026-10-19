# VoiceStick Linux

面向 GNOME 及其他 Linux 桌面环境的 GTK4 / libadwaita 配套应用。

应用通过 StatusNotifierItem 在顶栏显示图标，常用操作放在简短的菜单中。ASR 密钥、交互选项、各设备的外观及调试设置位于 Adwaita 偏好设置窗口。

## GNOME 顶栏图标

原生 GNOME 不显示托盘图标。请安装并启用 AppIndicator 扩展，然后注销并重新登录一次：

```sh
sudo apt install gnome-shell-extension-appindicator
gnome-extensions enable ubuntu-appindicators@ubuntu.com
```

Debian 软件包使用的扩展 UUID 为 `ubuntu-appindicators@ubuntu.com`。

## 依赖

Debian 13 / Trixie 安装命令：

```sh
sudo apt install \
  cmake ninja-build g++ pkg-config \
  libgtk-4-dev libadwaita-1-dev libsoup-3.0-dev \
  libglib2.0-dev libatspi2.0-dev libssl-dev \
  gnome-shell-extension-appindicator
```

## 构建

```sh
cmake -S desktop/linux -B desktop/linux/build -G Ninja
cmake --build desktop/linux/build
ctest --test-dir desktop/linux/build --output-on-failure
```

运行：

```sh
desktop/linux/build/VoiceStick
```

## 配置

```text
~/.config/voicestick/config.toml
~/.local/share/voicestick/DebugAudio/
~/.local/share/voicestick/VoiceStickApp.log
```

可以复制 [配置示例](Config/config.example.toml) 作为初始文件。首次启动也会打开引导向导。若存在旧目录 `~/.config/VoiceStick` 或 `~/.local/share/VoiceStick`，应用会在启动时将其重命名为小写路径。

## GNOME / Wayland 下的粘贴

识别文字始终会写入剪贴板。随后 VoiceStick 尝试通过 AT-SPI 插入；如果失败，会向 GNOME 申请远程桌面键盘共享权限（通过 `xdg-desktop-portal` 的 `CreateSession` → `SelectDevices` → `Start`），然后模拟 Ctrl+V。如果启用了 `Press Return After Paste`（粘贴后按回车），会延迟 120 ms 再发送回车，让当前应用先完成粘贴。在 mutter 下，IBus 无法向其他应用当前获得焦点的输入上下文提交文字。如果拒绝授权对话框，通知仍会提示手动按 Ctrl+V。

不需要 `wl-copy`、`wtype` 或 `/dev/uinput`。

## 补充说明

- BLE 通过 D-Bus 使用 BlueZ，用户应加入 `bluetooth` 用户组。
- 悬浮窗使用无窗口装饰的 GTK4 顶层窗口。GNOME 不提供 layer-shell。
- 应用暂不包含自动更新功能，请使用包管理器更新或重新构建。
- 与 Windows 共用的 C++ 代码目前仍以复制方式维护，尚未提取到 `desktop/shared`。
