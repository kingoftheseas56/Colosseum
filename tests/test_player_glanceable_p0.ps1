# Glanceable truth (Tier 1+2, Hemanth-approved mock 2026-07-20): the player STATES
# what it knows — state line, ends-at clock, remaining flip, chapter names, pause
# info card (NO rating — his veto), rich track rows. Grows per task.
# Plan: docs/superpowers/plans/2026-07-20-colosseum-player-glanceable-truth.md

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

# Literal substring check (needles carry [] and {} — never -like here).
function Assert-Contains($text, $needle, $message) { if (-not $text.Contains($needle)) { throw $message } }

# ── Task 1: state line + ends-at + remaining flip ──
Assert-Contains $player 'readonly property string stateLineText' "Player must resolve one state line."
Assert-Contains $player 'property string endsAtClock' "Player must hold the ends-at wall clock."
Assert-Contains $player 'function updateEndsAt' "Ends-at must be computed (speed-aware), not bound to position churn."
Assert-Contains $player 'property bool showRemaining' "Right clock must be flippable to remaining."
Assert-Contains $player 'onClicked: root.showRemaining = !root.showRemaining' "Clicking the duration must flip it."
Assert-Contains $player 'cache-buffering-state' "Buffering state must surface a live percent."

# ── Task 2: chapter names (hover tag + crossing transient) ──
Assert-Contains $player 'function chapterAtFraction' "Player must resolve the chapter under a seek fraction."
Assert-Contains $player 'function chapterCrossWatch' "Player must watch chapter crossings to speak them."
Assert-Contains $player 'root.chapterTransient =' "Crossing a chapter must set the transient state line."

# ── Task 3: pause info card (NO rating — Hemanth veto) ──
Assert-Contains $player 'id: pauseCard' "Player must have the pause info card."
Assert-Contains $player 'property bool pauseCardShown' "Pause card must be gated by its settle state."
Assert-Contains $player 'function hydratePauseCard' "Pause card plot must be lazily hydrated."
Assert-Contains $player 'function pauseFactsLine' "Pause card must build its facts line."
Assert-Contains $player 'function pauseQualityLine' "Pause card must build its quality line."
# The facts line must NOT surface any rating field (imdbRating/rating/star).
$facts = [regex]::Match($player, 'function pauseFactsLine\(\)[\s\S]*?\n    \}').Value
if ($facts -match 'imdbRating|\.rating|Rating|star') { throw "Pause facts line must NOT include a rating (Hemanth veto 2026-07-20)." }

Write-Host "PASS: glanceable-truth contracts hold."
