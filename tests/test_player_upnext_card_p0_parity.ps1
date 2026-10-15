$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$player = Get-Content (Join-Path $root "qml/PlayerPage.qml") -Raw

function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") {
        throw $message
    }
}

function Assert-NotContains($text, $needle, $message) {
    if ($text -like "*$needle*") {
        throw $message
    }
}

# Harbor parity P0: between episodes Harbor shows a visible "Up Next" countdown card
# with a cancel affordance, instead of silently jumping to the next episode on EOF.

# --- Countdown state ---
Assert-Contains $player "property bool upNextVisible" `
    "PlayerPage must track whether the Up Next card is showing."
Assert-Contains $player "property int upNextCountdownSec" `
    "PlayerPage must define the Up Next countdown length."
Assert-Contains $player "property int upNextRemainingSec" `
    "PlayerPage must expose the live remaining seconds for the countdown."

# --- Countdown control functions ---
Assert-Contains $player "function startUpNextCountdown" `
    "PlayerPage must start a visible countdown when a next episode exists."
Assert-Contains $player "function cancelUpNext" `
    "PlayerPage must let the user dismiss the Up Next card without advancing."
Assert-Contains $player "function confirmUpNext" `
    "PlayerPage must let the user (or the timer) advance to the next episode now."

# --- The card only appears when there IS a next episode ---
Assert-Contains $player 'root.hasAdjacentEpisode("next")' `
    "Up Next countdown must only start when a next episode is available."

# --- A per-second tick timer drives the countdown ---
Assert-Contains $player "id: upNextTimer" `
    "PlayerPage must drive the Up Next countdown with a repeating tick timer."

# --- EOF shows the card instead of jumping silently ---
Assert-Contains $player "root.startUpNextCountdown()" `
    "On end-of-file the player must show the Up Next card, not jump silently."
Assert-NotContains $player "root.maybeAutoNextEpisode()" `
    "The silent auto-next jump on EOF must be replaced by the Up Next countdown."

# --- Confirming advances via the real adjacent-episode path ---
Assert-Contains $player 'root.goToAdjacentEpisode("next")' `
    "Confirming Up Next must advance through the real adjacent-episode loader."

# --- The card UI surfaces title, live countdown, and both affordances ---
Assert-Contains $player "Up next" `
    "The Up Next card must be labelled for the user."
Assert-Contains $player "root.upNextRemainingSec" `
    "The Up Next card must show the live remaining seconds."
Assert-Contains $player "Play now" `
    "The Up Next card must offer an immediate play-now action."
Assert-Contains $player "Cancel" `
    "The Up Next card must offer a cancel action."

# --- A new title/session clears any stale card ---
Assert-Contains $player "root.cancelUpNext()" `
    "Starting a new title must clear any lingering Up Next card."

Write-Host "Player Up Next countdown card P0 parity contract checks passed."
