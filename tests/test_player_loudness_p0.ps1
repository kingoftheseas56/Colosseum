# Loudness normalization is a user choice, defaulting to SMOOTH (no filter) after the
# 2026-07-20 stutter audit proved always-on dynamic loudnorm was the primary cause
# (about 96 percent of steady-state drops on the reference laptop). Three modes:
#   off   = no audio filter (smooth, the new default)
#   light = dynaudnorm (cheap adaptive normalizer, no 192kHz upsample)
#   full  = loudnorm EBU R128 (best consistency, the old always-on behaviour)
# Audit: docs/superpowers/specs/2026-07-20-colosseum-playback-stutter-audit.md

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$mpvcpp = Get-Content (Join-Path $root "native/player/mpvitem.cpp") -Raw
$mpvh   = Get-Content (Join-Path $root "native/player/mpvitem.h") -Raw
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) { if (-not $text.Contains($needle)) { throw $message } }
function Assert-Absent($text, $needle, $message) { if ($text.Contains($needle)) { throw $message } }

# C++: a runtime setter, and the three filter strings mapped.
Assert-Contains $mpvh 'void setAudioNormalization' "MpvItem must expose setAudioNormalization to QML."
Assert-Contains $mpvcpp 'void MpvItem::setAudioNormalization' "setAudioNormalization must be implemented."
Assert-Contains $mpvcpp 'loudnorm=I=-14:TP=-1.5:LRA=11' "Full mode must apply EBU R128 loudnorm."
Assert-Contains $mpvcpp 'dynaudnorm' "Light mode must apply the cheap dynaudnorm normalizer."

# The constructor must NOT force loudnorm on anymore (that was the stutter cause).
# loudnorm may still appear inside setAudioNormalization (the full mode), so we only
# forbid the old unconditional constructor line.
Assert-Absent $mpvcpp 'setProperty(QStringLiteral("af"), QStringLiteral("loudnorm=I=-14:TP=-1.5:LRA=11"))' "Constructor must not force loudnorm on; smooth is the default."

# QML: persisted mode (defaults smooth), applied to mpv, cycled from the overflow menu.
Assert-Contains $player 'property string loudnessMode' "playerSettings must persist the loudness mode."
Assert-Contains $player 'mpv.setAudioNormalization' "PlayerPage must apply the loudness mode to mpv."
Assert-Contains $player 'function cycleLoudness' "PlayerPage must cycle loudness from the overflow menu."
Assert-Contains $player '"kind": "loudness"' "The overflow menu must carry a loudness entry."

Write-Host "PASS: loudness normalization is a persisted 3-mode choice, smooth by default."
