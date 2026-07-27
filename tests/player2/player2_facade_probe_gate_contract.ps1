# Contract for player2_facade_probe_gate.ps1.  The fake executable is deliberately below the
# gate boundary: this exercises the real process/output/exit handling without launching the app.
# Run: powershell -NoProfile -File tests/player2/player2_facade_probe_gate_contract.ps1

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$gate = Join-Path $PSScriptRoot 'player2_facade_probe_gate.ps1'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('player2-facade-gate-contract-' + [guid]::NewGuid().ToString('N'))
$fake = Join-Path $scratch 'fake-probe.cmd'
$media = Join-Path $scratch 'media.mkv'
$failures = @()

$fakeBody = @'
@echo off
echo qml: FACADE PROBE: PASS smoke
if "%PLAYER2_GATE_CONTRACT_CASE%"=="missing-result" goto done
echo qml: FACADE PROBE: mode=contract RESULT PASS
if "%PLAYER2_GATE_CONTRACT_CASE%"=="nonzero" goto nonzero
if "%PLAYER2_GATE_CONTRACT_CASE%"=="unexpected-qml" goto unexpectedQml
if "%PLAYER2_GATE_CONTRACT_CASE%"=="unexpected-qt" goto unexpectedQt
if "%PLAYER2_GATE_CONTRACT_CASE%"=="measured-benign" goto measuredBenign
:done
exit /b 0
:nonzero
exit /b 7
:unexpectedQml
echo qml: unexpected QML runtime diagnostic 1>&2
exit /b 0
:unexpectedQt
echo qt.sql.qsqldatabase: unexpected warning 1>&2
exit /b 0
:measuredBenign
echo No QSGTexture provided from updateSampledImage^(^). This is wrong. 1>&2
echo qt.sql.qsqldatabase: QSqlDatabase requires a QCoreApplication 1>&2
exit /b 0
'@

function Invoke-GateCase([string]$name, [bool]$shouldPass, [string]$requiredText) {
    $env:PLAYER2_GATE_CONTRACT_CASE = $name
    $logDir = Join-Path $scratch ("logs-" + $name)
    $output = & "$PSHOME\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $gate `
        -Media $media -Mode contract -ProbeExecutable $fake -LogDir $logDir 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    Write-Host "${name}: gate exit=$exitCode"

    if ($output -match 'A parameter cannot be found that matches parameter name') {
        $script:failures += "${name}: gate has no injectable probe/log seam; cannot exercise its real process handling."
        return
    }
    if ($shouldPass -and $exitCode -ne 0) {
        $script:failures += "${name}: expected gate PASS, exit=$exitCode. Output: $output"
    }
    if (-not $shouldPass -and $exitCode -eq 0) {
        $script:failures += "${name}: gate accepted a probe it must reject. Output: $output"
    }
    if ($requiredText -and $output -notmatch [regex]::Escape($requiredText)) {
        $script:failures += "${name}: missing required gate evidence '$requiredText'. Output: $output"
    }
}

try {
    New-Item -ItemType Directory -Path $scratch | Out-Null
    [IO.File]::WriteAllText($fake, $fakeBody, [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($media, '', [Text.Encoding]::ASCII)

    # Each negative names the production break it catches; the last case proves only measured,
    # exact benign diagnostics are allowlisted.
    Invoke-GateCase 'nonzero' $false 'unexpected process exit 7'
    Invoke-GateCase 'missing-result' $false 'missing final RESULT PASS'
    Invoke-GateCase 'unexpected-qml' $false 'unexpected QML/Qt runtime diagnostic'
    Invoke-GateCase 'unexpected-qt' $false 'unexpected QML/Qt runtime diagnostic'
    Invoke-GateCase 'measured-benign' $true 'FACADE PROBE GATE: PASS'
    if ($failures.Count) {
        $failures | ForEach-Object { Write-Host "  - $_" }
        Write-Host "FACADE PROBE GATE CONTRACT: FAIL ($($failures.Count))"
        exit 1
    }
    Write-Host 'FACADE PROBE GATE CONTRACT: PASS'
}
finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item Env:\PLAYER2_GATE_CONTRACT_CASE -ErrorAction SilentlyContinue
}
