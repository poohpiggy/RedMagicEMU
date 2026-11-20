param(
    [Parameter(Mandatory)] [string] $Sdk,
    [Parameter(Mandatory)] [string] $KeyStore
)

$ErrorActionPreference = "Stop"
$buildTools = Join-Path $Sdk "build-tools/36.0.0"
$androidJar = Join-Path $Sdk "platforms/android-36/android.jar"
$asset = Join-Path $PSScriptRoot "../../app/src/main/assets/engines/n64-36/vendor#overlay#rme-nx809j-camera.apk"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("rme-camera-overlay-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null

try {
    & "$buildTools/aapt2.exe" compile --dir "$PSScriptRoot/res" -o "$temp/resources.zip"
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile camera overlay resources" }
    & "$buildTools/aapt2.exe" link -o "$temp/base.apk" --manifest "$PSScriptRoot/AndroidManifest.xml" -I $androidJar "$temp/resources.zip"
    if ($LASTEXITCODE -ne 0) { throw "Failed to link camera overlay" }
    & "$buildTools/zipalign.exe" -f 4 "$temp/base.apk" "$temp/aligned.apk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to align camera overlay" }
    & "$buildTools/apksigner.bat" sign --ks $KeyStore --ks-pass pass:android --key-pass pass:android --out "$temp/signed.apk" "$temp/aligned.apk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to sign camera overlay" }
    & "$buildTools/apksigner.bat" verify "$temp/signed.apk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to verify camera overlay signature" }
    Copy-Item -LiteralPath "$temp/signed.apk" -Destination $asset
} finally {
    Get-ChildItem -LiteralPath $temp -File | Remove-Item
    Remove-Item -LiteralPath $temp
}
