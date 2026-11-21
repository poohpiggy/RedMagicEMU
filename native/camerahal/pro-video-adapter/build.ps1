param(
    [Parameter(Mandatory)] [string] $Sdk,
    [Parameter(Mandatory)] [string] $Jdk
)

$ErrorActionPreference = 'Stop'
$work = Join-Path ([IO.Path]::GetTempPath()) ('rme-camera-adapter-' + [guid]::NewGuid())
$asset = Join-Path $PSScriptRoot '../../../app/src/main/assets/engines/n64-36-camera-adapter'
New-Item -ItemType Directory -Path "$work/classes", "$work/dex", $asset -Force | Out-Null
$env:JAVA_HOME = $Jdk
$android = Join-Path $Sdk 'platforms/android-36/android.jar'
try {
    & "$Jdk/bin/javac.exe" --release 11 -classpath $android -d "$work/classes" "$PSScriptRoot/AdapterSingleZteRedMagic11.java"
    if ($LASTEXITCODE -ne 0) { throw 'Camera adapter compilation failed' }
    & "$Jdk/bin/jar.exe" --create --file "$work/classes.jar" -C "$work/classes" .
    if ($LASTEXITCODE -ne 0) { throw 'Camera adapter class archive failed' }
    & "$Sdk/build-tools/36.0.0/d8.bat" --lib $android --min-api 36 --output "$work/dex" "$work/classes.jar"
    if ($LASTEXITCODE -ne 0) { throw 'Camera adapter DEX build failed' }
    & "$Jdk/bin/jar.exe" --create --no-manifest --date '2020-01-01T00:00:00Z' --file "$asset/rme-camera-adapter.jar" -C "$work/dex" classes.dex
    if ($LASTEXITCODE -ne 0) { throw 'Camera adapter archive failed' }
} finally {
    $resolved = (Resolve-Path -LiteralPath $work).Path
    $temp = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).Path
    if ((Split-Path -Parent $resolved) -eq $temp -and (Split-Path -Leaf $resolved) -match '^rme-camera-adapter-[a-f0-9-]{36}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
