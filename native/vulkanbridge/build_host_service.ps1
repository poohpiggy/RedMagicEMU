param(
    [string]$Ndk = "$env:LOCALAPPDATA\Android\Sdk\ndk\28.2.13676358",
    [string]$OutputFile = ''
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$clang = Join-Path $bin 'clang.exe'
$readelf = Join-Path $bin 'llvm-readelf.exe'
$shaderTools = Join-Path $Ndk 'shader-tools/windows-x86_64'
$glslc = Join-Path $shaderTools 'glslc.exe'
$spirvVal = Join-Path $shaderTools 'spirv-val.exe'
foreach ($tool in @($clang, $readelf, $glslc, $spirvVal)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing NDK tool: $tool" }
}
if ($OutputFile -eq '') {
    $outDir = Join-Path ([IO.Path]::GetTempPath()) ('rme-vulkan-host-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $outDir | Out-Null
    $OutputFile = Join-Path $outDir 'vulkan-host-service-arm64'
} else {
    $OutputFile = [IO.Path]::GetFullPath($OutputFile)
    if (Test-Path -LiteralPath $OutputFile) { throw "Output already exists: $OutputFile" }
    $outDir = Split-Path -Parent $OutputFile
    if (-not (Test-Path -LiteralPath $outDir -PathType Container)) { throw "Output directory does not exist: $outDir" }
}
$buildDir = Join-Path ([IO.Path]::GetTempPath()) ('rme-vulkan-host-build-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $buildDir | Out-Null
try {
    $built = Join-Path $buildDir 'vulkan-host-service-arm64'
    $shaderBinary = Join-Path $buildDir 'host_shader.spv'
    $shaderHeader = Join-Path $buildDir 'host_shader_spv.h'
    & $glslc '-fshader-stage=compute' '--target-env=vulkan1.2' (Join-Path $PSScriptRoot 'host_compute.comp') '-o' $shaderBinary
    if ($LASTEXITCODE -ne 0) { throw "Shader compile failed: $LASTEXITCODE" }
    & $spirvVal '--target-env' 'vulkan1.2' $shaderBinary
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V validation failed: $LASTEXITCODE" }
    if (-not [BitConverter]::IsLittleEndian) { throw 'A little-endian build host is required' }
    $shaderBytes = [IO.File]::ReadAllBytes($shaderBinary)
    if ($shaderBytes.Length -lt 20 -or ($shaderBytes.Length % 4) -ne 0 -or
        [BitConverter]::ToUInt32($shaderBytes, 0) -ne 0x07230203) { throw 'Invalid SPIR-V binary' }
    $shaderWords = for ($i = 0; $i -lt $shaderBytes.Length; $i += 4) {
        '0x{0:x8}u' -f [BitConverter]::ToUInt32($shaderBytes, $i)
    }
    $headerText = "#include <stdint.h>`nstatic const uint32_t host_shader_spv[] = {`n" +
        ($shaderWords -join ",`n") + "`n};`n"
    [IO.File]::WriteAllText($shaderHeader, $headerText, [Text.Encoding]::ASCII)
    & $clang '--target=aarch64-linux-android29' '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' `
        '-fPIE' '-pie' '-Wl,-z,max-page-size=16384' '-Wl,-z,common-page-size=16384' `
        '-Wl,--no-undefined' '-I' $buildDir (Join-Path $PSScriptRoot 'host_service.c') `
        (Join-Path $PSScriptRoot 'host_fixed_compute.c') `
        (Join-Path $PSScriptRoot 'gpu_select.c') (Join-Path $PSScriptRoot 'gpu_device.c') `
        (Join-Path $PSScriptRoot 'gpu_buffer.c') (Join-Path $PSScriptRoot 'gpu_submission.c') `
        (Join-Path $PSScriptRoot 'gpu_compute.c') `
        '-lvulkan' '-o' $built
    if ($LASTEXITCODE -ne 0) { throw "NDK build failed: $LASTEXITCODE" }
    $elf = & $readelf '-h' '-l' '-d' $built
    if ($LASTEXITCODE -ne 0) { throw "ELF inspection failed: $LASTEXITCODE" }
    $joined = $elf -join "`n"
    if ($joined -notmatch 'Machine:\s+AArch64' -or $joined -notmatch 'Type:\s+DYN' -or
        $joined -notmatch 'Requesting program interpreter: /system/bin/linker64' -or
        $joined -notmatch 'Shared library: \[libvulkan\.so\]') { throw 'Unexpected ELF architecture, type, interpreter or Vulkan dependency' }
    $loads = @($elf | Where-Object { $_ -match '^\s*LOAD\s' })
    if ($loads.Count -eq 0 -or @($loads | Where-Object { $_ -notmatch '0x4000\s*$' }).Count -ne 0) {
        throw 'ELF LOAD alignment is not 16 KiB'
    }
    Copy-Item -LiteralPath $built -Destination $OutputFile
    $hash = (Get-FileHash -LiteralPath $OutputFile -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output "Built $OutputFile"
    Write-Output "SHA256 $hash"
    Write-Output "Validated fixed shader SHA256 $((Get-FileHash -LiteralPath $shaderBinary -Algorithm SHA256).Hash.ToLowerInvariant())"
    Write-Output 'Android ARM64 host service built; not run on Android; no Guest interface.'
} finally {
    if (Test-Path -LiteralPath $built -PathType Leaf) { Remove-Item -LiteralPath $built -Force }
    if (Test-Path -LiteralPath $shaderBinary -PathType Leaf) { Remove-Item -LiteralPath $shaderBinary -Force }
    if (Test-Path -LiteralPath $shaderHeader -PathType Leaf) { Remove-Item -LiteralPath $shaderHeader -Force }
    Remove-Item -LiteralPath $buildDir -Force
}
