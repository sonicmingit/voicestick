param([string]$Python="python")
$ErrorActionPreference="Stop"
$root=(Resolve-Path "$PSScriptRoot/..").Path
& $Python -m PyInstaller --noconfirm --onefile --noconsole --name VoiceStick-Watch-BLE --collect-all winrt --hidden-import bleak.backends.winrt.scanner --hidden-import bleak.backends.winrt.client --workpath "$root/build/watch5-freeze" --specpath "$root/build/watch5-spec" --distpath "$root/build/watch5" "$PSScriptRoot/ble_gateway.py"
if ($LASTEXITCODE) {throw "BLE 接收组件构建失败"}
