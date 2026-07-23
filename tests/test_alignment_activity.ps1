# Offscreen gate for the Text Sync ACTIVITY surface (Task 7): the honest read-along
# alignment status in Reader2's LeftPanel AND the app-wide background-activity row, both
# reading the SAME native job.
#
# Drives tests/alignment_activity_harness.qml under qml.exe -platform offscreen and
# requires the ALIGNMENT_ACTIVITY_OK sentinel plus a clean exit code (the exit code IS the
# verdict — a thrown JS error HANGS the offscreen process, so the harness signals via
# Qt.exit(0/1)). This pins: the summary/ready copy, all seven per-chapter states, the
# plain-language failure copy, per-chapter Retry, the confirmation-gated Restart, and
# pause/resume PARITY between Reader2 and the global row driving one shared fake job.
#
# Plus SHAPE contracts (grep, not behavior — pixels are Hemanth's eyes): ReaderChrome
# threads the service + bookId into its LeftPanel, and LeftPanel drives the service
# directly for pause/resume/retry/restart. The visual read-along polish is Task 14 eyes-on.
#
# [Agent 2 (Claude), biblio]

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) {
    Write-Host "FAIL: qml.exe not found at $qmlExe"
    exit 1
}

$env:QT_FORCE_STDERR_LOGGING = "1"
$harness = Join-Path $PSScriptRoot "alignment_activity_harness.qml"

# qml.exe emits benign warnings (font dir) on stderr; don't let ErrorActionPreference=Stop
# turn a native-command stderr line into a terminating error before we read the verdict.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$output = & $qmlExe -platform offscreen $harness 2>&1 | Out-String
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($code -ne 0 -or ($output -notmatch "ALIGNMENT_ACTIVITY_OK")) {
    Write-Host "FAIL: alignment activity offscreen harness (exit $code)"
    Write-Host $output
    exit 1
}

# ---- SHAPE contracts (grep, not behavior) ----
$chrome = Get-Content (Join-Path $root "qml\reader2\ReaderChrome.qml") -Raw
if ($chrome -notmatch "textSync:\s*chrome\.textSync") { Write-Host "FAIL: ReaderChrome must pass textSync into its LeftPanel"; exit 1 }
if ($chrome -notmatch "bookId:\s*chrome\.bookId") { Write-Host "FAIL: ReaderChrome must pass bookId into its LeftPanel"; exit 1 }
if ($chrome -notmatch "typeof AudioTextAlignment") { Write-Host "FAIL: ReaderChrome must guard the AudioTextAlignment context property (dormant-safe)"; exit 1 }

$panel = Get-Content (Join-Path $root "qml\reader2\LeftPanel.qml") -Raw
if ($panel -notmatch "textSync\.pause\(") { Write-Host "FAIL: LeftPanel must drive service.pause directly"; exit 1 }
if ($panel -notmatch "textSync\.resume\(") { Write-Host "FAIL: LeftPanel must drive service.resume directly"; exit 1 }
if ($panel -notmatch "textSync\.retry\(") { Write-Host "FAIL: LeftPanel must drive service.retry directly"; exit 1 }
if ($panel -notmatch "textSync\.restart\(") { Write-Host "FAIL: LeftPanel must drive service.restart directly"; exit 1 }
if ($panel -notmatch "onJobChanged") { Write-Host "FAIL: LeftPanel must refresh on the service jobChanged signal"; exit 1 }

Write-Host "alignment activity: OK"
