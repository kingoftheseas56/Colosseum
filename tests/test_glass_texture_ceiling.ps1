# Regression gate for Glass's GPU texture ceiling.
#
# Glass blurs by allocating TWO textures the size of the WHOLE item (a
# ShaderEffectSource grab of the backdrop, plus a layer.enabled rounded-rect mask).
# A content-sized Glass can therefore ask the driver for a texture bigger than it
# allows, and the driver does not degrade - it refuses, and the panel renders as
# garbage:
#
#   QSGRhiLayer: Unsupported size requested: [1758, 54375]. Maximum texture size: 16384
#
# Hit live 2026-07-30 by a 232-chapter list inside a Glass card (~54,000px tall).
# Glass now stops allocating past a safe bound and falls back to the tint/scrim/
# border rectangles it draws unconditionally, so the card keeps its material and
# only loses a blur that was invisible at that size anyway.
#
# ASCII only - PS 5.1 chokes on non-ASCII in a BOM-less .ps1.

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$fail = 0

function Check($name, $ok) {
    if ($ok) { Write-Host "PASS  $name" }
    else { Write-Host "FAIL  $name"; $script:fail++ }
}

# 1. GREP SHAPE - the bound and its wiring must be present. A green grep proves the
#    string exists, never that it behaves; the harness below proves behaviour.
$glass = Join-Path $repo "qml\Glass.qml"
$src = Get-Content $glass -Raw
Check "Glass exposes the blurAffordable decision" ($src -match "blurAffordable")
Check "the grab drops its sourceItem when unaffordable" ($src -match "blurAffordable \? root\.backdrop : null")
Check "the mask layer is disabled when unaffordable" ($src -match "layer\.enabled: root\.blurAffordable")

# 2. OFFSCREEN BEHAVIOUR - qml.exe drives real Glass instances and asserts the
#    decision at, below and above the bound, including a runtime resize.
$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
if (-not (Test-Path $qmlExe)) {
    Write-Host "FAIL  qml.exe not found at $qmlExe"
    exit 1
}
$harness = Join-Path $PSScriptRoot "glass_texture_ceiling_harness.qml"

# qml.exe emits benign font warnings on stderr; don't let ErrorActionPreference
# turn those into a thrown terminating error.
$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $qmlExe -platform offscreen $harness 2>&1 | Out-Null
$code = $LASTEXITCODE
$ErrorActionPreference = $prev

Check "offscreen Glass ceiling contracts pass" ($code -eq 0)

if ($fail -gt 0) { Write-Host "$fail FAILURES"; exit 1 }
Write-Host "glass texture ceiling: OK"
exit 0
