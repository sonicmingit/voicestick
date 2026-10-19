[CmdletBinding()]
param(
    [string]$WindowsBuild,
    [string]$FirmwareBuild,
    [string]$OutputDirectory,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
if (!$WindowsBuild) { $WindowsBuild = Join-Path $projectDirectory 'desktop/windows/build-local-x64' }
if (!$FirmwareBuild) { $FirmwareBuild = Join-Path $projectDirectory 'build/firmware-doubao-idf55' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectDirectory 'build/doubao-input-test' }
$WindowsBuild = [IO.Path]::GetFullPath($WindowsBuild)
$FirmwareBuild = [IO.Path]::GetFullPath($FirmwareBuild)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$clientDirectory = Join-Path $OutputDirectory 'client'
$firmwareDirectory = Join-Path $OutputDirectory 'firmware'
New-Item -ItemType Directory -Force -Path $clientDirectory, $firmwareDirectory | Out-Null
foreach ($file in @('VoiceStick.exe', 'WinSparkle.dll', 'voicestick_ime_probe.exe')) {
    Copy-Item -LiteralPath (Join-Path $WindowsBuild $file) -Destination $clientDirectory
}
Copy-Item -LiteralPath (Join-Path $projectDirectory 'LICENSE') -Destination $clientDirectory
$sparkleCache = Get-Content -LiteralPath (Join-Path $WindowsBuild 'CMakeCache.txt') |
    Where-Object { $_ -match '^FETCHCONTENT_SOURCE_DIR_WINSPARKLE:' } | Select-Object -First 1
$sparkleSource = if ($sparkleCache) { ($sparkleCache -split '=', 2)[1] } else { Join-Path $WindowsBuild '_deps/winsparkle-src' }
$licenseFiles = @((Join-Path $WindowsBuild '_deps/opus-src/COPYING'),
    (Join-Path $projectDirectory 'desktop/windows/third_party/cjson/LICENSE'),
    (Join-Path $projectDirectory 'desktop/windows/third_party/tomlplusplus/LICENSE'),
    (Join-Path $sparkleSource 'COPYING'), (Join-Path $sparkleSource 'COPYING.expat'))
$notices = foreach ($file in $licenseFiles) {
    "=== $([IO.Path]::GetFileName((Split-Path -Parent $file)))/$([IO.Path]::GetFileName($file)) ==="
    Get-Content -LiteralPath $file -Raw
}
$notices | Set-Content -LiteralPath (Join-Path $clientDirectory 'THIRD-PARTY-NOTICES.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $projectDirectory 'docs/豆包输入法接入测试.md') -Destination (Join-Path $OutputDirectory '使用说明.md')
Copy-Item -LiteralPath (Join-Path $projectDirectory 'build/doubao-tests.log') -Destination (Join-Path $OutputDirectory '自动化测试结果.txt')
Copy-Item -LiteralPath (Join-Path $FirmwareBuild 'voice_stick.bin') -Destination (Join-Path $firmwareDirectory 'sticks3-doubao-test-ota.bin')
Push-Location $FirmwareBuild
try {
    & $Python -m esptool --chip esp32s3 merge_bin -o (Join-Path $firmwareDirectory 'sticks3-doubao-test-usb-merged.bin') '@flash_args'
    if ($LASTEXITCODE -ne 0) { throw 'Merging firmware failed.' }
} finally { Pop-Location }
$rollback = Join-Path $projectDirectory 'build/firmware-local/voicestick-sticks3-merged-0.4.0.bin'
if (Test-Path -LiteralPath $rollback) {
    Copy-Item -LiteralPath $rollback -Destination (Join-Path $firmwareDirectory 'rollback-0.4.0-usb-merged.bin')
}
$revision = & git -C $projectDirectory rev-parse HEAD
$branch = & git -C $projectDirectory branch --show-current
@"
Source branch: $branch
Source revision: $revision
Experimental build. End-to-end device HID + VB-CABLE + Doubao recognition is not yet verified.
VB-CABLE is a separate dependency: https://vb-audio.com/Cable/
USB merged image: offset 0x0. OTA image: application OTA partition only.
No driver or firmware is installed by this packaging script.
"@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BUILD.txt') -Encoding utf8
$checksums = Get-ChildItem -LiteralPath $OutputDirectory -File -Recurse |
    Where-Object { $_.Extension -in '.exe','.dll','.bin' } |
    ForEach-Object {
        $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
        $relative = [IO.Path]::GetRelativePath($OutputDirectory, $_.FullName).Replace('\', '/')
        "$($hash.Hash.ToLowerInvariant())  $relative"
    }
$checksums | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding utf8
$archive = Join-Path (Split-Path -Parent $OutputDirectory) 'VoiceStick_Doubao_Test.zip'
Compress-Archive -Path (Join-Path $OutputDirectory '*') -DestinationPath $archive -Force
Write-Output "Test package: $archive"
Write-Output "Extracted files: $OutputDirectory"
