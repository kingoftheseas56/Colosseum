# player2_engine_facade_contract.ps1 - every mpv-surface member PlayerPage actually uses must be
# declared by PlayerEngine.qml. Enumerated FROM PlayerPage each run, so a new mpv.* usage in
# PlayerPage automatically fails this contract until the facade answers it. Both directions of the
# drift are fatal: a missing member is a runtime TypeError mid-playback, not a build error.
# Run: powershell -NoProfile -File tests/player2/player2_engine_facade_contract.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$page = Get-Content -Raw (Join-Path $root 'qml/PlayerPage.qml')
$enginePath = Join-Path $root 'qml/PlayerEngine.qml'
if (-not (Test-Path $enginePath)) { Write-Host 'FACADE CONTRACT: FAIL (PlayerEngine.qml missing)'; exit 1 }
$engine = Get-Content -Raw $enginePath

$members = [regex]::Matches($page, '\bmpv\.([A-Za-z]+)') | ForEach-Object { $_.Groups[1].Value } |
           Sort-Object -Unique | Where-Object { $_ -notmatch '^on[A-Z]' }  # signal handlers checked separately
$violations = @()
foreach ($m in $members) {
    if ($engine -notmatch "\b(property\s+\w+\s+$m|function\s+$m\s*\(|signal\s+$m\s*\()") {
        $violations += "PlayerEngine does not declare '$m' (PlayerPage uses mpv.$m)"
    }
}
# The signal handlers PlayerPage installs on the engine object must exist as signals.
foreach ($sig in @('durationChanged', 'chaptersChanged')) {
    if ($engine -notmatch "\b$sig\b") { $violations += "PlayerEngine must expose '$sig'" }
}
# The property keys statsValue queries must be answered by the facade's mpvProperty switch.
foreach ($k in @('video-bitrate','audio-bitrate','frame-drop-count','vo-drop-frame-count',
                 'estimated-vf-fps','container-fps','video-codec','audio-codec','hwdec-current',
                 'cache-buffering-state','width','height')) {
    if ($engine -notmatch [regex]::Escape('"' + $k + '"')) {
        $violations += "PlayerEngine's mpvProperty() does not handle '$k'"
    }
}
if ($violations.Count) { $violations | ForEach-Object { Write-Error $_ -ErrorAction Continue }
    Write-Host "FACADE CONTRACT: FAIL ($($violations.Count))"; exit 1 }
Write-Host 'FACADE CONTRACT: PASS'
