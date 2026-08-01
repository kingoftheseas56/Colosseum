# Lanista bridge contract gate.
#   1. GREP SHAPE - load-bearing wiring strings exist (presence, not behaviour).
#   2. HARNESS    - the C++ harness self-checks (LANISTA_OK).
#   3. SCENARIOS  - self_smoke + self_visual green against a serving harness.
# The real-app scenario (app_home.json) is Task 12 / eyes-on territory - it
# needs the actual app booted and is NOT run here.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "native/build-msvc"
$qtBin = "C:/Qt/6.11.1/msvc2022_64/bin"

function Read-RepoFile([string]$rel) { Get-Content -Raw -LiteralPath (Join-Path $root $rel) }
function Assert-Contains([string]$text, [string]$needle, [string]$msg) {
    if (-not $text.Contains($needle)) { Write-Host "FAIL: $msg"; exit 1 }
}

$server = Read-RepoFile "native/devtools/LanistaServer.cpp"
$mainc  = Read-RepoFile "native/main.cpp"
Assert-Contains $mainc  "new LanistaServer" "the bridge must be constructed in main"
Assert-Contains $server "DRIVE_DISABLED" "driving must be gated"
Assert-Contains $server "WRITE_DISABLED" "mutation must be gated"
Assert-Contains $server "COLOSSEUM_LANISTA_PIPE" "the pipe name must be overridable"
Assert-Contains $server "grabToImage" "item grabs must render the full item"
Assert-Contains $server "not on the invoke-read allowlist" "organ reads must be allowlisted"

foreach ($exe in @("lanista_harness.exe", "lanista.exe")) {
    if (!(Test-Path -LiteralPath (Join-Path $build $exe))) {
        Write-Host "FAIL: $exe missing - build native first"; exit 1
    }
}

$env:PATH = "$qtBin;$env:PATH"
$prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"

# 2. harness self-check
$out = & (Join-Path $build "lanista_harness.exe") 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $out -notmatch "LANISTA_OK") {
    Write-Host "FAIL: harness self-check"; Write-Host $out; exit 1
}

# 3. scenarios against a serving harness (fresh process, no DRIVE env)
$serve = Start-Process -FilePath (Join-Path $build "lanista_harness.exe") `
    -ArgumentList "--serve" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
try {
    Push-Location $root
    foreach ($sc in @("tests/lanista_scenarios/self_smoke.json",
                      "tests/lanista_scenarios/self_visual.json")) {
        & (Join-Path $build "lanista.exe") --pipe ColosseumLanistaTest run $sc 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: scenario $sc"; exit 1 }
    }
    Pop-Location
} finally {
    Stop-Process -Id $serve.Id -Force -ErrorAction SilentlyContinue
}
$ErrorActionPreference = $prev
Write-Host "lanista bridge: OK"
