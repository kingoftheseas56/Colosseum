[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Lanista,
    [Parameter(Mandatory=$true)][string]$Colosseum,
    [string]$RepoRoot = "",
    [string]$Scenario = "",
    [string]$OutputRoot = "",
    [string]$Seed = "",
    [int]$Takes = 10,
    [int]$CaptureWidth = 1920,
    [int]$CaptureHeight = 1080
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Fail([string]$Message) { throw "Arc63-D3 replay: $Message" }

if ($Takes -ne 10) { Fail "canonical replay requires exactly 10 takes" }

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
} else {
    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
}
if ([string]::IsNullOrWhiteSpace($Scenario)) {
    $Scenario = Join-Path $RepoRoot "tests\lanista_capture\arc63_d3_readiness_replay.json"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
    $OutputRoot = Join-Path $RepoRoot ("artifacts\arc63-d3-replay\" + $stamp)
}

foreach ($path in @($Lanista, $Colosseum, $Scenario)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { Fail "required file missing: $path" }
}
if ($Seed -and !(Test-Path -LiteralPath $Seed -PathType Container)) {
    Fail "seed directory missing: $Seed"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$records = @()

for ($take = 1; $take -le $Takes; $take++) {
    $id = "{0:D2}" -f $take
    $takeRoot = Join-Path $OutputRoot ("take-" + $id)
    $captureRoot = Join-Path $takeRoot "capture"
    New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null
    $log = Join-Path $takeRoot "lanista.log"

    $args = @(
        "session", "run", $Scenario,
        "--exe", $Colosseum,
        "--qml", (Join-Path $RepoRoot "qml\Main.qml"),
        "--drive",
        "--tag", ("arc63-d3-take-" + $id),
        "--ready-ms", "60000",
        "--capture-width", [string]$CaptureWidth,
        "--capture-height", [string]$CaptureHeight,
        "--capture-out", $captureRoot
    )
    if ($Seed) { $args += @("--seed", $Seed) }

    Write-Host "=== ARC63_D3_REPLAY take $id/$Takes ==="
    $output = & $Lanista @args 2>&1
    $exit = $LASTEXITCODE
    @($output | ForEach-Object { [string]$_ }) | Set-Content -LiteralPath $log -Encoding UTF8
    $output | ForEach-Object { Write-Host $_ }

    if ($exit -ne 0) { Fail "take $id failed with Lanista exit $exit" }

    $clips = @(Get-ChildItem -LiteralPath $captureRoot -File -Filter *.mp4)
    if ($clips.Count -ne 2) {
        Fail "take $id produced $($clips.Count) MP4 files; expected exactly 2 readiness-gated clips"
    }

    $clipRecords = @()
    foreach ($clip in $clips | Sort-Object Name) {
        if ($clip.Length -lt 1000) { Fail "take $id clip is unexpectedly small: $($clip.FullName)" }
        $clipRecords += [pscustomobject]@{
            name = $clip.Name
            bytes = [int64]$clip.Length
            sha256 = (Get-FileHash -LiteralPath $clip.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }

    $records += [pscustomobject]@{
        take = $take
        tag = ("arc63-d3-take-" + $id)
        exitCode = $exit
        log = $log
        clips = $clipRecords
    }
}

$result = [ordered]@{
    schema = "colosseum.arc63.d3.replay.v1"
    createdAt = (Get-Date).ToString("o")
    takes = $Takes
    scenario = [IO.Path]::GetFullPath($Scenario)
    capture = [ordered]@{ width = $CaptureWidth; height = $CaptureHeight }
    records = $records
    visualComparison = "REQUIRED_MANUAL_GATE"
}
$resultPath = Join-Path $OutputRoot "result.json"
$result | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $resultPath -Encoding UTF8

Write-Host "ARC63_D3_REPLAY_OK takes=$Takes"
Write-Host "result=$resultPath"
Write-Host "NOTE: MP4 hashes are evidence receipts only. Visual equality must be judged separately."
