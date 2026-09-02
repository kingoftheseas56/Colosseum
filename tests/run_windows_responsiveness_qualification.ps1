param(
    [string]$Executable,
    [string]$AppDataTag = 'ResponsivenessGate',
    [int]$InteractiveStartMs = 12000,
    [int]$InteractionDurationMs = 25000,
    [int]$TotalDurationMs = 50000,
    [int]$InputIntervalMs = 120,
    [string]$RuntimePathPrefix = $env:COLOSSEUM_RESP_RUNTIME_PATH,
    [switch]$FreshAppData
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Executable) { $Executable = Join-Path $root 'native\build-msvc\colosseum.exe' }
if (-not (Test-Path -LiteralPath $Executable)) { throw "Colosseum executable not found: $Executable" }
if ($RuntimePathPrefix) { $env:PATH = $RuntimePathPrefix + ';' + $env:PATH }

$outDir = Join-Path $PSScriptRoot 'pacing-out'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $outDir "responsiveness-gate-$stamp.log"
$csv = Join-Path $outDir "responsiveness-gate-$stamp.csv"
$appDataRoot = Join-Path $env:APPDATA "Brotherhood\Colosseum-dltest-$AppDataTag"
if ($FreshAppData -and (Test-Path -LiteralPath $appDataRoot)) {
    Remove-Item -LiteralPath $appDataRoot -Recurse -Force
}

$env:COLOSSEUM_APPDATA_TAG = $AppDataTag
$env:COLOSSEUM_LANISTA_PIPE = "ColosseumRespGate-$stamp"
$env:COLOSSEUM_GUI_STALL_PROBE = '40'
$env:COLOSSEUM_PRIORITY_GOVERNOR_TRACE = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
Remove-Item -LiteralPath $log,$csv -Force -ErrorAction SilentlyContinue
'elapsedMs,responding,input' | Set-Content -LiteralPath $csv
$p = Start-Process -FilePath $Executable -ArgumentList 'qml\Main.qml' -WorkingDirectory $root `
    -RedirectStandardError $log -PassThru
$shell = New-Object -ComObject WScript.Shell
$sw = [Diagnostics.Stopwatch]::StartNew()
$nextInputMs = $InteractiveStartMs
$inputEndMs = $InteractiveStartMs + $InteractionDurationMs
$inputCount = 0

try {
    while ($sw.ElapsedMilliseconds -lt $TotalDurationMs -and -not $p.HasExited) {
        Start-Sleep -Milliseconds 100
        $proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
        if ($null -eq $proc) { break }
        $didInput = $false
        if ($sw.ElapsedMilliseconds -ge $nextInputMs -and $sw.ElapsedMilliseconds -lt $inputEndMs) {
            if ($shell.AppActivate($p.Id)) {
                $shell.SendKeys($(if (($inputCount % 2) -eq 0) { '{DOWN}' } else { '{UP}' }))
                $inputCount++
                $didInput = $true
            }
            $nextInputMs += $InputIntervalMs
        }
        "$($sw.ElapsedMilliseconds),$($proc.Responding),$didInput" | Add-Content -LiteralPath $csv
    }
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host "RESPONSIVENESS_EVIDENCE log=$log csv=$csv inputs=$inputCount"
$gate = Join-Path $PSScriptRoot 'responsiveness_budget_gate.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gate `
    -LogPath $log -RespondingCsv $csv -InteractiveStartMs $InteractiveStartMs
exit $LASTEXITCODE
