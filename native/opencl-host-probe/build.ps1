param(
    [string]$Ndk = "$env:LOCALAPPDATA\Android\Sdk\ndk\28.2.13676358"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$clang = Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
if (-not (Test-Path -LiteralPath $clang)) { throw "NDK clang not found: $clang" }
$outDir = Join-Path $root 'app/src/debug/jniLibs/arm64-v8a'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir 'libopenclhostprobe.so'
& $clang --target=aarch64-linux-android26 -std=c11 -O2 -Wall -Wextra -Werror `
    -fPIC -fvisibility=hidden -shared '-Wl,-z,max-page-size=16384' '-Wl,--no-undefined' `
    '-Wl,-soname,libopenclhostprobe.so' -o $out `
    (Join-Path $PSScriptRoot 'probe.c') (Join-Path $PSScriptRoot 'discovery.c') `
    (Join-Path $PSScriptRoot 'jni.c') -ldl
if ($LASTEXITCODE -ne 0) { throw "NDK build failed: $LASTEXITCODE" }
Write-Output "Built $out"
