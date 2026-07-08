$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$hotkeys = Get-Content (Join-Path $root "qml/PlayerHotkeys.js") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

# Regex variant — PowerShell -like treats [ ] as a char class, so needles containing
# literal brackets (e.g. bindings: ["E"]) must go through -match with escaped brackets.
function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

# --- Task 2: the E hotkey exists in the registry (lands in the ? sheet automatically) ---
Assert-Contains $hotkeys 'id: "browser"' `
    "PlayerHotkeys must register the browser action."
Assert-Matches $hotkeys 'id: "browser".*bindings: \["E"\]' `
    "The browser action must bind E."

$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

# --- Task 3: plumbing ---
Assert-Contains $player "property bool browserOpen: false" `
    "PlayerPage must hold the drawer open flag."
Assert-Matches $player "property var playbackQueue: \[\]" `
    "PlayerPage must retain the traveling episode queue."
Assert-Contains $player "function jumpToEpisode(ep, startLabel, failLabel)" `
    "The Next-Episode pipeline must be generalized to any episode target."
Assert-Contains $player 'root.jumpToEpisode(ep, which === "next"' `
    "goToAdjacentEpisode must delegate to jumpToEpisode (one pipeline, not two)."
Assert-Contains $player 'case "browser":' `
    "runHotkeyAction must route the E key."
Assert-Contains $player "root.browserOpen = false" `
    "closeMenus must close the drawer."
Assert-Contains $player "|| root.browserOpen" `
    "anyMenuOpen must include the drawer (chrome stays awake)."

Write-Host "Player browser contract checks passed."
