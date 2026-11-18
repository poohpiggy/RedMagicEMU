param(
    [Parameter(Mandatory)] [string]$OpenClHeaders,
    [string]$Ndk = "$env:LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358",
    [string]$Output = "$PSScriptRoot/../../build/diagnostics/opencl-native/android",
    [switch]$Profile
)

$ErrorActionPreference = 'Stop'
$headers = (Resolve-Path $OpenClHeaders).Path
$ndkBin = Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$clang = Join-Path $ndkBin 'clang.exe'
$cxx = Join-Path $ndkBin 'clang++.exe'
$outDir = (New-Item -ItemType Directory -Force -Path $Output).FullName
$common = @('--target=aarch64-linux-android26', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-I', $headers)
$shared = @('-shared', '-Wl,--no-undefined', '-Wl,-z,max-page-size=16384')

function Invoke-Compiler($Tool, $Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool exited with $LASTEXITCODE" }
}

$io = Join-Path $PSScriptRoot 'abi_io.c'
$guest = Join-Path $outDir 'libOpenCL.so'
$exports = Join-Path $outDir 'guest.exports'
'{ global: cl[A-Z]*; local: *; };' | Set-Content -Encoding ascii -LiteralPath $exports
Invoke-Compiler $clang ($common + $shared + @('-std=c11', '-Wl,-soname,libOpenCL.so', "-Wl,--version-script=$exports",
    (Join-Path $PSScriptRoot 'abi_guest.c'), $io, '-o', $guest))

$objects = foreach ($name in 'abi_host', 'abi_host_posix', 'abi_io') {
    $object = Join-Path $outDir "$name.o"
    $flags = if ($Profile -and $name -eq 'abi_host') { @('-DRME_CL_PROFILE') } else { @() }
    Invoke-Compiler $clang ($common + $flags + @('-std=c11', '-fvisibility=hidden', '-c', (Join-Path $PSScriptRoot "$name.c"), '-o', $object))
    $object
}
$hostLib = Join-Path $outDir 'libaemuclhost.so'
Invoke-Compiler $cxx ($common + $shared + @('-std=c++17', '-fvisibility=hidden', '-static-libstdc++',
    '-Wl,-soname,libaemuclhost.so', (Join-Path $PSScriptRoot 'android_service.cpp')) + $objects + @('-ldl', '-llog', '-o', $hostLib))
Write-Output "Built Android ARM64 OpenCL client and host in $outDir"
