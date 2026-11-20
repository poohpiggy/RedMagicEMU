param(
    [Parameter(Mandatory)] [string] $Sdk,
    [Parameter(Mandatory)] [string] $KeyStore,
    [switch] $Rear1080p60
)

$ErrorActionPreference = "Stop"
$buildTools = Join-Path $Sdk "build-tools/36.0.0"
$androidJar = Join-Path $Sdk "platforms/android-36/android.jar"
$engines = Join-Path $PSScriptRoot "../../app/src/main/assets/engines"
$layer = if ($Rear1080p60) { "n64-36-camera-1080p60" } else { "n64-36" }
$asset = Join-Path $engines "$layer/vendor#overlay#rme-nx809j-camera.apk"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("rme-camera-overlay-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null

try {
    & "$buildTools/aapt2.exe" compile --dir "$PSScriptRoot/res" -o "$temp/resources.zip"
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile camera overlay resources" }
    $resources = @("$temp/resources.zip")
    if ($Rear1080p60) {
        & "$buildTools/aapt2.exe" compile --dir "$PSScriptRoot/res-1080p60" -o "$temp/rear1080p60.zip"
        if ($LASTEXITCODE -ne 0) { throw "Failed to compile the 1080p60 camera resources" }
        $resources += @("-R", "$temp/rear1080p60.zip")
    }
    & "$buildTools/aapt2.exe" link -o "$temp/base.apk" --manifest "$PSScriptRoot/AndroidManifest.xml" -I $androidJar @resources
    if ($LASTEXITCODE -ne 0) { throw "Failed to link camera overlay" }
    & "$buildTools/zipalign.exe" -f 4 "$temp/base.apk" "$temp/aligned.apk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to align camera overlay" }
    & "$buildTools/apksigner.bat" sign --ks $KeyStore --ks-pass pass:android --key-pass pass:android --out "$temp/signed.apk" "$temp/aligned.apk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to sign camera overlay" }
    & "$buildTools/apksigner.bat" verify "$temp/signed.apk"
    if ($LASTEXITCODE -ne 0) { throw "Failed to verify camera overlay signature" }
    if ($Rear1080p60) {
        $profileName = "vendor#etc#media_profiles_V1_0.xml"
        $profiles = Get-Content -Raw -LiteralPath (Join-Path $engines "n64-36/$profileName")
        $rearStart = '    <CamcorderProfiles cameraId="0">'
        $avcCap = [regex] '(?s)    <VideoEncoderCap name="h264"[^>]+/>'
        if (!$profiles.Contains($rearStart) -or $avcCap.Matches($profiles).Count -ne 1) {
            throw "Base camera profile layout has changed"
        }
        $newLine = if ($profiles.Contains([char]13)) { [string][char]13 + [char]10 } else { [string][char]10 }
        $rear1080p = @'

        <EncoderProfile quality="1080p" fileFormat="mp4" duration="60">
            <Video codec="h264"
                   bitRate="20000000"
                   width="1920"
                   height="1080"
                   frameRate="60" />
            <Audio codec="aac"
                   bitRate="128000"
                   sampleRate="48000"
                   channels="2" />
        </EncoderProfile>

'@
        $verifiedAvcCap = @'
    <VideoEncoderCap name="h264" enabled="true"
        minBitRate="64000" maxBitRate="20000000"
        minFrameWidth="176" maxFrameWidth="1920"
        minFrameHeight="144" maxFrameHeight="1080"
        minFrameRate="15" maxFrameRate="60" />
'@
        $profiles = $profiles.Replace($rearStart, $rearStart + ($rear1080p -replace '\r?\n', $newLine))
        $profiles = $avcCap.Replace($profiles, ($verifiedAvcCap -replace '\r?\n', $newLine))
        New-Item -ItemType Directory -Path (Join-Path $engines $layer) -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $engines "$layer/$profileName"), $profiles, [Text.UTF8Encoding]::new($false))
    }
    Copy-Item -LiteralPath "$temp/signed.apk" -Destination $asset
} finally {
    Get-ChildItem -LiteralPath $temp -File | Remove-Item
    Remove-Item -LiteralPath $temp
}
