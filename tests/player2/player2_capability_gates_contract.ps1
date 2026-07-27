# Task 6: a Player 2 control must be absent when its backing engine seam is absent.
# This is intentionally a source contract: the runtime facade probe proves the flags; this
# check pins every PlayerPage/SubtitleMenu entry point that can reach an unsupported seam.
# Run: powershell -NoProfile -File tests/player2/player2_capability_gates_contract.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'qml_lexer.ps1')

function Read-Qml([string]$relative) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path)) { throw "missing QML file: $relative" }
    return Remove-QmlComments (Get-Content -Raw -LiteralPath $path)
}

function Function-Section([string]$text, [string]$name, [string]$nextName) {
    $start = $text.IndexOf("function $name(")
    if ($start -lt 0) { throw "missing function: $name" }
    $end = $text.IndexOf("function $nextName(", $start + 1)
    if ($end -lt 0) { $end = $text.Length }
    return $text.Substring($start, $end - $start)
}

$engine = Read-Qml 'qml/PlayerEngine.qml'
$page = Read-Qml 'qml/PlayerPage.qml'
$menu = Read-Qml 'qml/SubtitleMenu.qml'
$violations = @()

function Require-Match([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { $script:violations += $message }
}

# The three flags must be BOOT facts, so each backend sees a stable, truthful capability answer.
Require-Match $engine 'readonly\s+property\s+bool\s+supportsCapture\s*:\s*!engine\.p2\b' 'supportsCapture must be !engine.p2'
Require-Match $engine 'readonly\s+property\s+bool\s+supportsLive\s*:\s*!engine\.p2\b' 'supportsLive must be !engine.p2'
Require-Match $engine 'readonly\s+property\s+bool\s+supportsExternalSubs\s*:\s*!engine\.p2\b' 'supportsExternalSubs must be !engine.p2'

# Capture/live overflow controls are absent on Player 2, not visible-but-dead.
Require-Match $page '"label"\s*:\s*"Screenshot"[\s\S]{0,160}"when"\s*:\s*mpv\.supportsCapture\b' 'Screenshot must gate on supportsCapture'
Require-Match $page '"kind"\s*:\s*"gif"[\s\S]{0,120}"when"\s*:\s*mpv\.supportsCapture\b' 'GIF must gate on supportsCapture'
foreach ($kind in @('liveGuide', 'dvr', 'liveEdge')) {
    Require-Match $page ('"kind"\s*:\s*"' + $kind + '"[\s\S]{0,180}mpv\.supportsLive\b') "$kind must gate on supportsLive"
}

# External-subtitle controls need both a visible gate and path guards: a P2 boot must neither
# offer an external row nor claim to load a dropped/auto-selected/online subtitle.
Require-Match $page 'supportsExternalSubs\s*:\s*mpv\.supportsExternalSubs\b' 'PlayerPage must pass supportsExternalSubs into SubtitleMenu'
Require-Match $menu 'property\s+bool\s+supportsExternalSubs\s*:\s*true\b' 'SubtitleMenu needs the external-subtitle capability property'
Require-Match $menu 'id\s*:\s*footer[\s\S]{0,220}visible\s*:\s*menu\.supportsExternalSubs\b' 'SubtitleMenu external-subtitle footer must be hidden when unsupported'
Require-Match $page 'readonly\s+property\s+var\s+subRows\s*:\s*\{[\s\S]{0,900}if\s*\(\s*mpv\.supportsExternalSubs\s*\)[\s\S]{0,700}root\.onlineSubs' 'Online subtitle rows must be omitted when external subtitles are unsupported'

$fetch = Function-Section $page 'fetchSubtitles' 'pickSubtitle'
Require-Match $fetch 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?' 'fetchSubtitles must return before requesting unsupported external subtitles'
$pick = Function-Section $page 'pickSubtitle' 'addOnlineSubtitle'
Require-Match $pick 'indexOf\("ext:"\)[\s\S]{0,140}if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?' 'pickSubtitle must reject an external row when unsupported'
$online = Function-Section $page 'addOnlineSubtitle' 'loadSubtitleFile'
Require-Match $online 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?' 'addOnlineSubtitle must return when unsupported'
$file = Function-Section $page 'loadSubtitleFile' 'isSubtitleFile'
Require-Match $file 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s+false\s*;?' 'loadSubtitleFile must fail truthfully when unsupported'
$auto = Function-Section $page 'maybeAutoSub' 'currentShowKey'
Require-Match $auto 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?' 'maybeAutoSub must not auto-load an unsupported external subtitle'
Require-Match $page 'DropArea\s*\{[\s\S]{0,160}enabled\s*:\s*mpv\.supportsExternalSubs\b' 'Subtitle drop target must be disabled when external subtitles are unsupported'

if ($violations.Count) {
    $violations | ForEach-Object { Write-Host "  - $_" }
    Write-Host "PLAYER2 CAPABILITY GATES: FAIL ($($violations.Count))"
    exit 1
}

Write-Host 'PLAYER2 CAPABILITY GATES: PASS'
