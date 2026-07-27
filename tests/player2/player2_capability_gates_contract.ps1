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
    return ConvertTo-QmlCodeView (Get-Content -Raw -LiteralPath $path)
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

function Assert-CodeViewRejectsDecoys() {
    $code = ConvertTo-QmlCodeView 'readonly property bool supportsCapture: !engine.p2'
    if ($code -notmatch 'readonly\s+property\s+bool\s+supportsCapture\s*:\s*!engine\.p2') {
        $script:violations += 'QML code view must retain real declarations'
    }
    $comment = ConvertTo-QmlCodeView '// readonly property bool supportsCapture: !engine.p2'
    if ($comment -match 'readonly\s+property\s+bool\s+supportsCapture\s*:\s*!engine\.p2') {
        $script:violations += 'QML code view must reject declaration comments'
    }
    $string = ConvertTo-QmlCodeView 'property string decoy: "readonly property bool supportsCapture: !engine.p2"'
    if ($string -match 'readonly\s+property\s+bool\s+supportsCapture\s*:\s*!engine\.p2') {
        $script:violations += 'QML code view must reject declaration string literals'
    }
    $guardString = ConvertTo-QmlCodeView 'property string decoy: "if (!mpv.supportsExternalSubs) return"'
    if ($guardString -match 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return') {
        $script:violations += 'QML code view must reject capability-guard string literals'
    }
}
Assert-CodeViewRejectsDecoys

function Require-Match([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { $script:violations += $message }
}

function Require-LeadingCapabilityGuard([string]$section, [string]$capability, [string]$message) {
    Require-Match $section ('\Afunction\s+\w+\s*\([^)]*\)\s*\{\s*if\s*\(\s*!' + [regex]::Escape($capability) + '\s*\)\s*return\s*;?') $message
}

$keyLabel = [regex]::Escape((Get-QmlStringToken 'label'))
$keyKind = [regex]::Escape((Get-QmlStringToken 'kind'))
$keyWhen = [regex]::Escape((Get-QmlStringToken 'when'))
$screenshot = [regex]::Escape((Get-QmlStringToken 'Screenshot'))
$gif = [regex]::Escape((Get-QmlStringToken 'gif'))

# The three flags must be BOOT facts, so each backend sees a stable, truthful capability answer.
Require-Match $engine 'readonly\s+property\s+bool\s+supportsCapture\s*:\s*!engine\.p2\b' 'supportsCapture must be !engine.p2'
Require-Match $engine 'readonly\s+property\s+bool\s+supportsLive\s*:\s*!engine\.p2\b' 'supportsLive must be !engine.p2'
Require-Match $engine 'readonly\s+property\s+bool\s+supportsExternalSubs\s*:\s*!engine\.p2\b' 'supportsExternalSubs must be !engine.p2'

# Capture/live overflow controls are absent on Player 2, not visible-but-dead.
Require-Match $page ($keyLabel + '\s*:\s*' + $screenshot + '[\s\S]{0,160}' + $keyWhen + '\s*:\s*mpv\.supportsCapture\b') 'Screenshot must gate on supportsCapture'
Require-Match $page ($keyKind + '\s*:\s*' + $gif + '[\s\S]{0,120}' + $keyWhen + '\s*:\s*mpv\.supportsCapture\b') 'GIF must gate on supportsCapture'
foreach ($kind in @('liveGuide', 'dvr', 'liveEdge')) {
    Require-Match $page ($keyKind + '\s*:\s*' + [regex]::Escape((Get-QmlStringToken $kind)) + '[\s\S]{0,180}mpv\.supportsLive\b') "$kind must gate on supportsLive"
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
Require-Match $pick ('indexOf\(' + [regex]::Escape((Get-QmlStringToken 'ext:')) + '\)[\s\S]{0,140}if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?') 'pickSubtitle must reject an external row when unsupported'
$online = Function-Section $page 'addOnlineSubtitle' 'loadSubtitleFile'
Require-Match $online 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?' 'addOnlineSubtitle must return when unsupported'
$file = Function-Section $page 'loadSubtitleFile' 'isSubtitleFile'
Require-Match $file 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s+false\s*;?' 'loadSubtitleFile must fail truthfully when unsupported'
$auto = Function-Section $page 'maybeAutoSub' 'currentShowKey'
Require-Match $auto 'if\s*\(\s*!mpv\.supportsExternalSubs\s*\)\s*return\s*;?' 'maybeAutoSub must not auto-load an unsupported external subtitle'
Require-Match $page 'DropArea\s*\{[\s\S]{0,160}enabled\s*:\s*mpv\.supportsExternalSubs\b' 'Subtitle drop target must be disabled when external subtitles are unsupported'

# A visible gate is not a safety boundary: callable functions remain reachable from automation,
# tests, or a future UI. The first action in each must decline unsupported backend work before it
# mutates state or reaches the external service/engine.
Require-LeadingCapabilityGuard (Function-Section $page 'configureLiveChannel' 'openLiveGuide') 'mpv.supportsLive' 'configureLiveChannel must reject unsupported live setup'
Require-LeadingCapabilityGuard (Function-Section $page 'openLiveGuide' 'switchLiveChannel') 'mpv.supportsLive' 'openLiveGuide must reject unsupported live setup'
Require-LeadingCapabilityGuard (Function-Section $page 'switchLiveChannel' 'startDvrRecording') 'mpv.supportsLive' 'switchLiveChannel must reject unsupported live setup'
Require-LeadingCapabilityGuard (Function-Section $page 'startDvrRecording' 'stopDvrRecording') 'mpv.supportsLive' 'startDvrRecording must reject unsupported DVR work'
Require-LeadingCapabilityGuard (Function-Section $page 'stopDvrRecording' 'goLiveEdge') 'mpv.supportsLive' 'stopDvrRecording must reject unsupported DVR work'
Require-LeadingCapabilityGuard (Function-Section $page 'goLiveEdge' 'handleWindowMinimize') 'mpv.supportsLive' 'goLiveEdge must reject unsupported live seeking'
Require-LeadingCapabilityGuard (Function-Section $page 'captureFrameGrab' 'showGifToast') 'mpv.supportsCapture' 'captureFrameGrab must reject unsupported capture'
Require-LeadingCapabilityGuard (Function-Section $page 'startGifRecording' 'stopGifRecording') 'mpv.supportsCapture' 'startGifRecording must reject unsupported capture'
Require-LeadingCapabilityGuard (Function-Section $page 'stopGifRecording' 'abortGifRecording') 'mpv.supportsCapture' 'stopGifRecording must reject unsupported capture'
Require-LeadingCapabilityGuard (Function-Section $page 'abortGifRecording' 'recordProgress') 'mpv.supportsCapture' 'abortGifRecording must reject unsupported capture'
Require-LeadingCapabilityGuard (Function-Section $menu 'runSearch' 'clampX') 'menu.supportsExternalSubs' 'runSearch must reject unsupported external subtitles before mutating search state'
Require-LeadingCapabilityGuard (Function-Section $menu 'pickOnline' 'resolvePending') 'menu.supportsExternalSubs' 'pickOnline must reject unsupported external subtitles before mutating pending state'

if ($violations.Count) {
    $violations | ForEach-Object { Write-Host "  - $_" }
    Write-Host "PLAYER2 CAPABILITY GATES: FAIL ($($violations.Count))"
    exit 1
}

Write-Host 'PLAYER2 CAPABILITY GATES: PASS'
