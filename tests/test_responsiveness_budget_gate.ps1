$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gate = Join-Path $PSScriptRoot 'responsiveness_budget_gate.ps1'

function Need([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

function Run-Gate([string[]]$logLines, [string[]]$csvLines) {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ('colosseum-resp-' + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $dir | Out-Null
    try {
        $log = Join-Path $dir 'stderr.log'
        $csv = Join-Path $dir 'responding.csv'
        $logLines | Set-Content -LiteralPath $log
        $csvLines | Set-Content -LiteralPath $csv
        $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate `
            -LogPath $log -RespondingCsv $csv -InteractiveStartMs 12000 2>&1
        return @{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
    } finally { Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue }
}

$green = Run-Gate @(
    'GUI_STALL_PROBE HIT atMs=1500 blockedMs=499 severity=critical eventTypeAndReceiver=1|Main operation=startup surface=qml-load',
    'GUI_STALL_PROBE HIT atMs=15000 blockedMs=250 severity=critical eventTypeAndReceiver=1|Main operation=navigate surface=Home'
) @('elapsedMs,responding','12000,True','12100,True')
Need ($green.Code -eq 0 -and $green.Text.Contains('RESPONSIVENESS_BUDGET_OK')) 'boundary values must pass'
$startupRed = Run-Gate @(
    'GUI_STALL_PROBE HIT atMs=1500 blockedMs=501 severity=critical eventTypeAndReceiver=1|Main operation=startup surface=qml-load'
) @('elapsedMs,responding','12000,True')
Need ($startupRed.Code -eq 1 -and $startupRed.Text.Contains('startup budget exceeded')) `
    '501 ms startup event must fail the 500 ms budget'

$interactiveRed = Run-Gate @(
    'GUI_STALL_PROBE HIT atMs=15000 blockedMs=251 severity=critical eventTypeAndReceiver=1|Main operation=navigate surface=Home'
) @('elapsedMs,responding','12000,True')
Need ($interactiveRed.Code -eq 1 -and $interactiveRed.Text.Contains('interaction budget exceeded')) `
    '251 ms interaction event must fail the 250 ms budget'

$respondingRed = Run-Gate -logLines @('no GUI stalls') -csvLines @('elapsedMs,responding','12000,True','12100,False')
Need ($respondingRed.Code -eq 1 -and $respondingRed.Text.Contains('Not Responding')) `
    'any Windows Responding=False sample must fail'

$missing = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate `
    -LogPath (Join-Path $env:TEMP 'does-not-exist-colosseum.log') `
    -RespondingCsv (Join-Path $env:TEMP 'does-not-exist-colosseum.csv') 2>&1
Need ($LASTEXITCODE -eq 2 -and (($missing -join "`n").Contains('missing evidence'))) `
    'missing evidence must be an invalid gate run, never a pass'

Write-Host 'RESPONSIVENESS_BUDGET_GATE_TEST_OK'
