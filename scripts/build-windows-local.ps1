[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$ToolchainEnvironment,
    [string]$WinSparkleSource,
    [string]$WixPath = 'wix',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')]
    [string]$PackageSuffix = 'zh-CN_custom-buttons_x64'
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $projectDirectory 'desktop/windows/build-local-x64' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectDirectory 'build/windows-local' }
if ($ToolchainEnvironment) { . $ToolchainEnvironment }

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

$version = (Get-Content -LiteralPath (Join-Path $projectDirectory 'VERSION') -Raw).Trim()
$configure = @('-S', (Join-Path $projectDirectory 'desktop/windows'), '-B', $BuildDirectory,
               '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
               '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
               '-DVOICESTICK_ENABLE_APP_UPDATES=OFF')
if ($WinSparkleSource) { $configure += "-DFETCHCONTENT_SOURCE_DIR_WINSPARKLE=$WinSparkleSource" }
Invoke-Checked 'cmake' $configure
Invoke-Checked 'cmake' @('--build', $BuildDirectory, '--parallel', '4')
Invoke-Checked 'ctest' @('--test-dir', $BuildDirectory, '--output-on-failure')

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$notices = Join-Path $OutputDirectory 'THIRD-PARTY-NOTICES.txt'
if (!$WinSparkleSource) { $WinSparkleSource = Join-Path $BuildDirectory '_deps/winsparkle-src' }
$licenseFiles = @((Join-Path $projectDirectory 'LICENSE'),
    (Join-Path $projectDirectory 'desktop/windows/third_party/cjson/LICENSE'),
    (Join-Path $projectDirectory 'desktop/windows/third_party/tomlplusplus/LICENSE'),
    (Join-Path $WinSparkleSource 'COPYING'), (Join-Path $WinSparkleSource 'COPYING.expat'),
    (Join-Path $BuildDirectory '_deps/opus-src/COPYING'))
$licenseText = foreach ($file in $licenseFiles) {
    if (!(Test-Path -LiteralPath $file)) { throw "License file missing: $file" }
    "=== $([IO.Path]::GetFileName((Split-Path -Parent $file)))/$([IO.Path]::GetFileName($file)) ==="
    Get-Content -LiteralPath $file -Raw
}
$licenseText | Set-Content -LiteralPath $notices -Encoding utf8
$installer = Join-Path $OutputDirectory "VoiceStick_${version}_${PackageSuffix}.msi"
Invoke-Checked $WixPath @('build', (Join-Path $projectDirectory 'desktop/windows/installer/VoiceStick.wxs'),
    (Join-Path $projectDirectory 'desktop/windows/installer/zh-CN.wxl'),
    '-arch', 'x64', '-culture', 'zh-CN', '-ext', 'WixToolset.UI.wixext', '-ext', 'WixToolset.Util.wixext',
    '-d', "ProductVersion=$version", '-d', "BuildDir=$BuildDirectory", '-d', "ProjectDir=$projectDirectory",
    '-d', 'ProductName=VoiceStick 中文定制版', '-d', "NoticesFile=$notices", '-o', $installer)

$stagingRoot = Join-Path $BuildDirectory ('package-stage-' + [guid]::NewGuid().ToString('N'))
$portable = Join-Path $stagingRoot 'VoiceStick'
New-Item -ItemType Directory -Force -Path $portable | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'VoiceStick.exe'),
    (Join-Path $BuildDirectory 'WinSparkle.dll'), (Join-Path $projectDirectory 'LICENSE'), $notices -Destination $portable
Copy-Item -LiteralPath (Join-Path $projectDirectory 'docs/Windows中文定制版说明.md') -Destination (Join-Path $portable '使用说明.md')
Copy-Item -LiteralPath (Join-Path $projectDirectory 'docs/自定义按键使用说明.md') -Destination $portable
$releaseNotes = Join-Path $projectDirectory "docs/发布说明-Windows-$version.md"
if (Test-Path -LiteralPath $releaseNotes) {
    Copy-Item -LiteralPath $releaseNotes -Destination (Join-Path $portable '更新说明.md')
}
$archive = Join-Path $OutputDirectory "VoiceStick_${version}_${PackageSuffix}.zip"
Compress-Archive -LiteralPath $portable -DestinationPath $archive -Force
$hashes = Get-FileHash -Algorithm SHA256 -LiteralPath $installer, $archive
$hashes | ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($_.Path))" } |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding utf8
Write-Output "Installer: $installer"
Write-Output "Portable:  $archive"
