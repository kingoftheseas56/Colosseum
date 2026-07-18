# Next Up rows P0 (spec 2026-07-18): behavioral harness for the pure derivations
# + grep contracts for the world/Main wiring the harness can't reach.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }

# 1. the derivations behave (exit code IS the verdict).
$env:QT_QPA_PLATFORM = 'offscreen'
& $qmlExe (Join-Path $root 'tests/next_up_logic_harness.qml') | Out-Null
if ($LASTEXITCODE -ne 0) { throw "next_up_logic_harness FAILED (exit $LASTEXITCODE)" }

# 2. both worlds carry the row, ABOVE their Continue row.
foreach ($w in @('TheatreWorld.qml', 'TankobanWorld.qml')) {
    $src = Get-Content (Join-Path $root "qml/$w") -Raw
    $nextIdx = $src.IndexOf('title: "Next Up"')
    if ($nextIdx -lt 0) { throw "$w lost its Next Up row" }
    $contIdx = $src.IndexOf('title: "Continue')
    if ($contIdx -ge 0 -and $nextIdx -gt $contIdx) { throw "$w must place Next Up ABOVE Continue" }
}

# 3. Tankoban derives from manga + tankoban kinds only — western comics are ruled out.
$tk = Get-Content (Join-Path $root 'qml/TankobanWorld.qml') -Raw
if ($tk -notmatch 'Progress\.recent\("manga"' -or $tk -notmatch 'Progress\.recent\("tankoban"') {
    throw 'TankobanWorld Next Up must read the manga + tankoban progress kinds'
}
if ($tk -match 'nextUpRows[\s\S]{0,400}Progress\.recent\("comic"') {
    throw 'TankobanWorld Next Up must NOT read the comic kind (ruled out: not a linear catalogue)'
}

# 4. Main routes both doors: Theatre play -> openMovieSession, Tankoban read -> openComicSession.
$main = Get-Content (Join-Path $root 'qml/Main.qml') -Raw
if ($main -notmatch 'nextUpPlay\.connect\(win\.openMovieSession\)') {
    throw 'Main no longer wires the Next Up play signal to openMovieSession'
}
if ($main -notmatch 'nextUpRead\.connect\(win\.openComicSession\)') {
    throw 'Main no longer wires the Next Up read signal to openComicSession'
}

Write-Host 'test_next_up_p0: PASS (derivations + both world rows + both Main doors)'
