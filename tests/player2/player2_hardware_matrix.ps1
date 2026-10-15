# Player 2 hardware-matrix gate (Task 16): the promotion bar requires the deterministic gates green on
# the Intel iGPU target AND at least one discrete GPU. This runner records the hardware identity, runs
# the core numeric gates on THIS machine's adapter, and appends a row to the matrix log. Run it once
# per machine/GPU; promotion needs >=2 distinct adapters in the log, one discrete.
# Run: powershell -NoProfile -File tests/player2/player2_hardware_matrix.ps1 [-SoakSeconds 60]
param(
    [int]$SoakSeconds = 60,
    [string]$MatrixLog = "$PSScriptRoot\player2_hardware_matrix_results.jsonl"
)
$ErrorActionPreference = 'Stop'

$gpus = Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
Write-Output "player2_hardware_matrix: CPU=$cpu GPUs=$($gpus -join ' | ')"

# Core numeric gates on this adapter (each throws on FAIL).
& powershell -NoProfile -File "$PSScriptRoot\player2_av_sync_gate.ps1" -SoakSeconds $SoakSeconds
& powershell -NoProfile -File "$PSScriptRoot\player2_av_sync_gate.ps1" -SoakSeconds $SoakSeconds -Speed 1.5
& powershell -NoProfile -File "$PSScriptRoot\player2_seek_soak.ps1" -SeekCount 25

# Record the row.
$row = [pscustomobject]@{
    at = (Get-Date -Format o); cpu = $cpu; gpus = $gpus
    soakSeconds = $SoakSeconds; gates = @('av_sync_1x', 'av_sync_1.5x', 'seek_soak_25'); passed = $true }
$row | ConvertTo-Json -Compress | Add-Content -Path $MatrixLog

$rows = Get-Content $MatrixLog | ForEach-Object { $_ | ConvertFrom-Json }
$adapters = $rows | ForEach-Object { $_.gpus } | Sort-Object -Unique
$hasDiscrete = [bool]($adapters | Where-Object { $_ -match 'NVIDIA|Radeon|Arc' })
Write-Output "player2_hardware_matrix: rows=$($rows.Count) adapters=$($adapters -join ' | ')"
if ($adapters.Count -ge 2 -and $hasDiscrete) {
    Write-Output "player2_hardware_matrix: PASS (Intel target + discrete GPU covered)"
} else {
    Write-Output ("player2_hardware_matrix: PARTIAL - this adapter is recorded; promotion still needs " +
                  "a discrete-GPU row (run this script on that machine)")
}
