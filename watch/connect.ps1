param([Parameter(Mandatory)][string]$Serial,
      [string]$Folder=(Join-Path $PSScriptRoot '../build/watch5'))
$ErrorActionPreference='Stop'
function Device([string[]]$arguments) {
    & adb -s $Serial @arguments
    if ($LASTEXITCODE) { throw "ADB 操作失败" }
}
$receiver=(Resolve-Path "$Folder/watch5-bridge.exe").Path
$apk=(Resolve-Path "$Folder/VoiceStick-Watch-0.1.5.apk").Path
$model=(Device @('shell','getprop','ro.product.model')).Trim()
if ($model -ne 'M2505W1') { throw "目标设备型号为 $model，请选择 Watch 5。" }
Device @('install','-r',$apk)
$running=@(Get-Process watch5-bridge -ErrorAction SilentlyContinue | Where-Object Path -eq $receiver)
foreach($proc in $running) { Stop-Process -Id $proc.Id; $proc.WaitForExit() }
$token=[guid]::NewGuid().ToString('N')
$env:VOICESTICK_WATCH_TOKEN=$token
try { $bridge=Start-Process $receiver -WindowStyle Hidden -PassThru } finally { Remove-Item Env:VOICESTICK_WATCH_TOKEN }
try {
    Start-Sleep -Milliseconds 500
    if ($bridge.HasExited) { throw '接收器启动失败，请检查 38465 端口是否被占用。' }
    Device @('reverse','tcp:38465','tcp:38465')
    Device @('shell','am','start','-n','cn.sonic.voicestick.watch/.WatchActivity','--es','pair',$token)
    Write-Output "VoiceStick 手表接收器已启动 (PID $($bridge.Id))"
} catch {
    if (!$bridge.HasExited) { $bridge.Kill() }
    throw
}

