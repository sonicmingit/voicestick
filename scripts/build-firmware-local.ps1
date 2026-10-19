[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
if (!$env:IDF_PATH) { throw 'Open an ESP-IDF 5.5 PowerShell environment first.' }
if (!$BuildDirectory) { $BuildDirectory = Join-Path $projectDirectory 'build/firmware-buttons-idf55' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectDirectory 'build/firmware-local' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$version = (Get-Content -LiteralPath (Join-Path $projectDirectory 'firmware/version.txt') -Raw).Trim()
Push-Location (Join-Path $projectDirectory 'firmware')
try {
    & $Python (Join-Path $env:IDF_PATH 'tools/idf.py') -B $BuildDirectory -DIDF_TARGET=esp32s3 build
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
} finally { Pop-Location }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$ota = Join-Path $OutputDirectory "voicestick-sticks3-ota-$version.bin"
$merged = Join-Path $OutputDirectory "voicestick-sticks3-merged-$version.bin"
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'voice_stick.bin') -Destination $ota
Push-Location $BuildDirectory
try {
    & $Python -m esptool --chip esp32s3 merge_bin -o $merged '@flash_args'
    if ($LASTEXITCODE -ne 0) { throw 'Merging firmware failed.' }
} finally { Pop-Location }
Get-FileHash -Algorithm SHA256 -LiteralPath $ota, $merged |
    ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($_.Path))" } |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $projectDirectory 'docs/自定义按键使用说明.md') -Destination $OutputDirectory
Write-Output "OTA: $ota"
Write-Output "USB: $merged"
