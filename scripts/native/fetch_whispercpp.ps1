# scripts/native/fetch_whispercpp.ps1
# Developer-time bootstrap for the audiobook read-along's COARSE English transcriber
# (Agent 2, Task 9). Stages a PINNED PREBUILT CPU whisper.cpp v1.9.1 (whisper-cli.exe
# + whisper.dll + the auto-selecting ggml-cpu-*.dll set) into the app's
# tools/whisper/ directory, beside the executable, where CoarseTranscriber resolves it
# as <appDir>/tools/whisper/whisper-cli.exe.
#
# DEVIATION FROM THE PLAN ("build whisper.cpp CPU-static"): we fetch the official
# pinned prebuilt CPU binary instead of building from source. It is simpler,
# deterministic (SHA-pinned), portable, and needs no native toolchain — the bundled
# ffmpeg's own whisper filter was unusable (Adreno-only OpenCL kernels that
# heap-corrupt on ordinary desktop GPUs with no CPU fallback), so the coarse stage
# drives this standalone CPU binary. The installed app never downloads anything; this
# runs once per dev machine and the staged tools are packaged with the app.
$ErrorActionPreference = 'Stop'

$version  = '1.9.1'
$expected = '7d8be46ecd31828e1eb7a2ecdd0d6b314feafd82163038ab6092594b0a063539'

# Stage into the MSVC build's tools/whisper (beside the harness/app exe). The shipped
# app carries the same tools/whisper next to its executable.
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dest = Join-Path $repoRoot 'native\build-msvc\tools\whisper'

if (Test-Path (Join-Path $dest 'whisper-cli.exe')) {
    Write-Host "WHISPERCPP_READY $dest (already staged)"
    exit 0
}

$zip = Join-Path $env:TEMP "whisper-bin-x64-$version.zip"
$url = "https://github.com/ggml-org/whisper.cpp/releases/download/v$version/whisper-bin-x64.zip"
Invoke-WebRequest $url -OutFile $zip
if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'whisper.cpp checksum mismatch'
}

$extract = Join-Path $env:TEMP "whisper-bin-x64-$version"
if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force

New-Item -ItemType Directory -Force -Path $dest | Out-Null

# Runtime essentials only: the CLI, the whisper runtime, and the ggml core + the
# auto-selecting CPU backend variants (ggml.dll picks the right ggml-cpu-*.dll for the
# host CPU features at load). Copy whichever of these the release archive carries.
$wanted = @('whisper-cli.exe', 'whisper.dll', 'ggml.dll', 'ggml-base.dll')
foreach ($name in $wanted) {
    $src = Get-ChildItem -Path $extract -Recurse -Filter $name -File | Select-Object -First 1
    if (-not $src) { throw "expected $name missing from whisper-bin-x64 archive" }
    Copy-Item -LiteralPath $src.FullName -Destination (Join-Path $dest $name) -Force
}
foreach ($cpu in Get-ChildItem -Path $extract -Recurse -Filter 'ggml-cpu-*.dll' -File) {
    Copy-Item -LiteralPath $cpu.FullName -Destination (Join-Path $dest $cpu.Name) -Force
}

if (-not (Test-Path (Join-Path $dest 'whisper-cli.exe'))) {
    throw 'whisper-cli.exe missing after staging'
}
Write-Host "WHISPERCPP_READY $dest"
