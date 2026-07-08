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

Write-Host "Player browser contract checks passed."
