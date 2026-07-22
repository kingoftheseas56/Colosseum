# scripts/native/fetch_onnxruntime.ps1
# Developer-time bootstrap for the shared offline-ML seam (guided comics +
# audiobook alignment). Stages ONNX Runtime CPU x64 1.25.0 into C:\tools.
# The installed app never downloads anything; this runs once per dev machine.
$ErrorActionPreference = 'Stop'
$version = '1.25.0'
$expected = 'da753f762bf2400e7191ec594086b186a7051d5af8dc886f6e2020c2403df738'
$zip = Join-Path $env:TEMP "onnxruntime-win-x64-$version.zip"
$dest = "C:\tools\onnxruntime-win-x64-$version"
if (Test-Path "$dest\lib\onnxruntime.lib") { Write-Host "ONNXRUNTIME_READY $dest (already staged)"; exit 0 }
Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-win-x64-$version.zip" -OutFile $zip
if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'ONNX Runtime checksum mismatch' }
Expand-Archive -LiteralPath $zip -DestinationPath 'C:\tools' -Force
if (!(Test-Path "$dest\lib\onnxruntime.lib")) { throw 'onnxruntime.lib missing after extraction' }
Write-Host "ONNXRUNTIME_READY $dest"
