# Contract for Player 2's typed diagnostics. It proves the snapshot is a fixed, typed schema — not
# an mpv-style generic string-keyed property bag — and that the declared schema and the emitted JSON
# stay in lockstep. Run: powershell -NoProfile -File tests/player2/player2_diagnostics_contract.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Join-Path $root 'native/player2/diagnostics/PlaybackDiagnostics.cpp'
$header = Join-Path $root 'native/player2/diagnostics/PlaybackDiagnostics.h'

if (-not (Test-Path $source)) { throw "PlaybackDiagnostics.cpp is missing at $source" }
if (-not (Test-Path $header)) { throw "PlaybackDiagnostics.h is missing at $header" }

$sourceText = Get-Content -Raw $source

# No generic property-bag lookup: diagnostics must come from typed fields, never a string-keyed
# runtime property query (the mpv anti-pattern this engine replaces).
$forbidden = @('mpvProperty', 'getProperty', 'property\(QString', 'observeProperty', 'setProperty\(')
foreach ($pattern in $forbidden) {
    if ($sourceText -match $pattern) {
        throw "PlaybackDiagnostics uses a generic property lookup ($pattern); diagnostics must be typed"
    }
}

# schemaKeys() and toJson() must be present and agree on the key count. Both list keys as
# QStringLiteral("...") entries — one per key in each function body.
function Get-FunctionBody([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature)
    if ($start -lt 0) { throw "missing function: $signature" }
    $brace = $text.IndexOf('{', $start)
    $depth = 0
    for ($i = $brace; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $text.Substring($brace, $i - $brace + 1) } }
    }
    throw "unbalanced braces in $signature"
}

$schemaBody = Get-FunctionBody $sourceText 'PlaybackDiagnostics::schemaKeys'
$jsonBody = Get-FunctionBody $sourceText 'PlaybackDiagnostics::toJson'

$schemaCount = ([regex]::Matches($schemaBody, 'QStringLiteral\(')).Count
$jsonKeyCount = ([regex]::Matches($jsonBody, '\{QStringLiteral\(')).Count

if ($schemaCount -lt 20) { throw "schemaKeys() has too few keys ($schemaCount); expected the full snapshot" }
if ($schemaCount -ne $jsonKeyCount) {
    throw "schema drift: schemaKeys() has $schemaCount keys but toJson() emits $jsonKeyCount"
}

# The device/recovery fields the task requires must be part of the schema.
foreach ($required in @('deviceLostReason', 'videoDeviceLost', 'audioDeviceLost', 'colorConversion', 'avP95Ms')) {
    if ($schemaBody -notmatch [regex]::Escape($required)) {
        throw "schema is missing a required diagnostics field: $required"
    }
}

Write-Output "player2_diagnostics_contract: PASS ($schemaCount typed keys, no generic property lookup)"
