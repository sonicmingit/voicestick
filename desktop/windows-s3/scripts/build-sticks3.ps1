param(
    [string]$RustTools,
    [string]$MsvcEnvironment,
    [switch]$Verify,
    [switch]$Probe
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if ($MsvcEnvironment) { . $MsvcEnvironment }
if ($RustTools) {
    $env:CARGO_HOME = Join-Path $RustTools 'cargo'
    $env:RUSTUP_HOME = Join-Path $RustTools 'rustup'
    $env:PATH = "$(Join-Path $env:CARGO_HOME 'bin');$env:PATH"
}
$env:CMAKE_GENERATOR = 'Ninja'
$env:VSCMD_ARG_TGT_ARCH = 'x64'
function Run-Checked([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Build command failed with code $LASTEXITCODE" }
}
Run-Checked { pnpm install --frozen-lockfile }
Run-Checked { pnpm build }
if ($Verify) {
    Run-Checked { pnpm test }
    Run-Checked { cargo fmt --all -- --check }
    Run-Checked { cargo test --workspace }
    Run-Checked { cargo check -p sayall-windows-app --features runtime-simulation }
}
if ($Probe) { Run-Checked { cargo run -p sayall-windows --example sticks3_probe -- --connect } }
Run-Checked { pnpm tauri build --debug --no-bundle --features hide-console }
$output = Join-Path (Get-Location) 'local-build'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -LiteralPath 'target/debug/sayall-windows-app.exe' -Destination (Join-Path $output 'VoiceStickS3.exe')
Copy-Item -LiteralPath 'STICKS3-README.md' -Destination (Join-Path $output '使用说明.md')
Copy-Item -LiteralPath 'LICENSE' -Destination (Join-Path $output 'LICENSE')
Copy-Item -LiteralPath 'THIRD_PARTY_NOTICES.md' -Destination $output
Copy-Item -LiteralPath 'ATTRIBUTION.md' -Destination $output
Copy-Item -LiteralPath 'licenses' -Destination $output -Recurse -Force
Get-FileHash -LiteralPath (Join-Path $output 'VoiceStickS3.exe') -Algorithm SHA256
Write-Output "Built: $(Join-Path $output 'VoiceStickS3.exe')"
