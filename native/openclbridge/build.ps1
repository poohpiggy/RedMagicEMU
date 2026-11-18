param(
    [Parameter(Mandatory)] [string]$OpenClHeaders,
    [string]$Ndk = "$env:LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358",
    [string]$Output = "$PSScriptRoot/../../build/diagnostics/opencl-native"
)

$ErrorActionPreference = 'Stop'
$headers = (Resolve-Path $OpenClHeaders).Path
$bin = Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$clang = Join-Path $bin 'clang.exe'
$linker = Join-Path $bin 'ld.lld.exe'
$dlltool = Join-Path $bin 'llvm-dlltool.exe'
$outDir = (New-Item -ItemType Directory -Force -Path $Output).FullName
$common = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', $headers)
$windows = @('--target=x86_64-pc-windows-msvc', '-ffreestanding', '-fno-builtin', '-fno-stack-protector')

function Invoke-Compiler($Tool, $Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool exited with $LASTEXITCODE" }
}

$core = Join-Path $PSScriptRoot 'openclbridge.c'
$worker = Join-Path $PSScriptRoot 'windows_worker.c'
$coreObject = Join-Path $outDir 'openclbridge.obj'
$abiObject = Join-Path $outDir 'abi_host.obj'
$workerObject = Join-Path $outDir 'windows_worker.obj'
$imports = Join-Path $outDir 'kernel32.def'
$importLib = Join-Path $outDir 'kernel32.lib'
@'
LIBRARY KERNEL32.dll
EXPORTS
GetStdHandle
ReadFile
WriteFile
GetLastError
GetProcessHeap
HeapAlloc
MapViewOfFile
LoadLibraryExW
GetProcAddress
ExitProcess
'@ | Set-Content -Encoding ascii -LiteralPath $imports

$crtImports = Join-Path $outDir 'msvcrt.def'
$crtLib = Join-Path $outDir 'msvcrt.lib'
@'
LIBRARY msvcrt.dll
EXPORTS
realloc
malloc
free
memcpy
memset
memcmp
'@ | Set-Content -Encoding ascii -LiteralPath $crtImports

Invoke-Compiler $dlltool @('-m', 'i386:x86-64', '-d', $imports, '-l', $importLib)
Invoke-Compiler $dlltool @('-m', 'i386:x86-64', '-d', $crtImports, '-l', $crtLib)
Invoke-Compiler $clang ($common + $windows + @('-c', $core, '-o', $coreObject))
Invoke-Compiler $linker @('-flavor', 'link', '/dll', '/noentry', '/nodefaultlib', "/out:$outDir/rme-opencl.dll", $coreObject)
Invoke-Compiler $clang ($common + $windows + @('-c', $worker, '-o', $workerObject))
Invoke-Compiler $clang ($common + $windows + @('-c', (Join-Path $PSScriptRoot 'abi_host.c'), '-o', $abiObject))
Invoke-Compiler $linker @('-flavor', 'link', '/subsystem:console', '/entry:mainCRTStartup', '/nodefaultlib',
    "/out:$outDir/rme-opencl-worker.exe", $coreObject, $abiObject, $workerObject, $importLib, $crtLib)

Invoke-Compiler $clang ($common + @('--target=aarch64-linux-android26', '-fPIC', '-fvisibility=hidden', '-shared',
    '-Wl,-z,max-page-size=16384', '-Wl,--no-undefined', '-Wl,-soname,librmeopenclbridge.so',
    $core, '-o', (Join-Path $outDir 'librmeopenclbridge.so')))
Write-Output "Built Windows OpenCL worker and Android ARM64 dispatcher in $outDir"
