param([string]$Ndk = "$env:LOCALAPPDATA\Android\Sdk\ndk\28.2.13676358")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$clang = Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
$glslc = Join-Path $Ndk 'shader-tools/windows-x86_64/glslc.exe'
$validator = Join-Path $Ndk 'shader-tools/windows-x86_64/spirv-val.exe'
foreach ($tool in @($clang, $glslc, $validator)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing NDK tool: $tool" }
}
$tempDir = Join-Path $env:TEMP ('rme-vulkan-host-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null
try {
    $spv = Join-Path $tempDir 'compute.spv'
    & $glslc '-fshader-stage=compute' '--target-env=vulkan1.2' (Join-Path $root 'tools/diagnostics/vulkan-compute/compute.comp') -o $spv
    if ($LASTEXITCODE -ne 0) { throw 'Shader compilation failed' }
    & $validator '--target-env' 'vulkan1.2' $spv
    if ($LASTEXITCODE -ne 0) { throw 'SPIR-V validation failed' }
    & python (Join-Path $PSScriptRoot 'gen_shader.py') $spv (Join-Path $tempDir 'shader_spv.h')
    if ($LASTEXITCODE -ne 0) { throw 'Shader header generation failed' }
    $built = Join-Path $tempDir 'libvulkanhostprobe.so'
    & $clang '--target=aarch64-linux-android26' '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' `
        '-fPIC' '-fvisibility=hidden' '-shared' '-Wl,-z,max-page-size=16384' `
        '-Wl,-z,common-page-size=16384' '-Wl,--no-undefined' '-Wl,-soname,libvulkanhostprobe.so' `
        '-I' $tempDir (Join-Path $PSScriptRoot 'adapter.c') (Join-Path $PSScriptRoot 'jni.c') `
        '-lvulkan' '-o' $built
    if ($LASTEXITCODE -ne 0) { throw "NDK build failed: $LASTEXITCODE" }
    $outDir = Join-Path $root 'app/src/debug/jniLibs/arm64-v8a'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    Copy-Item -LiteralPath $built -Destination (Join-Path $outDir 'libvulkanhostprobe.so') -Force
    Write-Output "Built $(Join-Path $outDir 'libvulkanhostprobe.so')"
} finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force
}
