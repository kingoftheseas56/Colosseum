param(
    [Parameter(Mandatory = $true)][string]$LogPath,
    [Parameter(Mandatory = $true)][string]$RespondingCsv,
    [int]$StartupBudgetMs = 500,
    [int]$InteractionBudgetMs = 250,
    [int]$InteractiveStartMs = 12000
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $LogPath) -or
    -not (Test-Path -LiteralPath $RespondingCsv)) {
    Write-Host "FAIL: missing evidence; log=$LogPath responding=$RespondingCsv"
    exit 2
}
if ($StartupBudgetMs -lt 1 -or $InteractionBudgetMs -lt 1 -or $InteractiveStartMs -lt 0) {
    Write-Host 'FAIL: invalid responsiveness budget arguments'
    exit 2
}

$offenses = New-Object 'System.Collections.Generic.List[string]'
$maxStartup = 0
$maxInteraction = 0
$stallPattern = 'GUI_STALL_PROBE HIT atMs=(\d+) blockedMs=(\d+)'

foreach ($line in (Get-Content -LiteralPath $LogPath)) {
    if ($line -notmatch $stallPattern) { continue }
    $atMs = [int64]$Matches[1]
    $blockedMs = [int]$Matches[2]
    if ($atMs -lt $InteractiveStartMs) {
        if ($blockedMs -gt $maxStartup) { $maxStartup = $blockedMs }
        if ($blockedMs -gt $StartupBudgetMs) {
            $offenses.Add("startup budget exceeded: ${blockedMs}ms > ${StartupBudgetMs}ms :: $line") | Out-Null
        }
    } else {
        if ($blockedMs -gt $maxInteraction) { $maxInteraction = $blockedMs }
        if ($blockedMs -gt $InteractionBudgetMs) {
            $offenses.Add("interaction budget exceeded: ${blockedMs}ms > ${InteractionBudgetMs}ms :: $line") | Out-Null
        }
    }
}

$rows = @(Import-Csv -LiteralPath $RespondingCsv)
if ($rows.Count -eq 0) {
    Write-Host 'FAIL: missing evidence; Responding CSV has no samples'
    exit 2
}
$falseRows = @($rows | Where-Object { ([string]$_.responding).Trim().ToLowerInvariant() -eq 'false' })
if ($falseRows.Count -gt 0) {
    $first = $falseRows[0]
    $offenses.Add("Windows Not Responding sample(s): $($falseRows.Count); first elapsedMs=$($first.elapsedMs)") | Out-Null
}

if ($offenses.Count -gt 0) {
    foreach ($offense in $offenses) { Write-Host "FAIL: $offense" }
    exit 1
}

Write-Host "RESPONSIVENESS_BUDGET_OK startupMaxMs=$maxStartup interactionMaxMs=$maxInteraction respondingSamples=$($rows.Count)"
exit 0
