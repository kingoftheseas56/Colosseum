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

# --- Task 4: the drawer ---
$drawerPath = Join-Path $root "qml/BrowserDrawer.qml"
if (!(Test-Path $drawerPath)) { throw "qml/BrowserDrawer.qml must exist." }
$drawer = Get-Content $drawerPath -Raw

Assert-Contains $drawer 'import "EpisodeBrowser.js" as EpisodeBrowser' `
    "Drawer must derive everything through the harness-tested store."
Assert-Contains $drawer 'import "TheatreApi.js" as TheatreApi' `
    "Drawer must fetch seasons through the same meta call the series page uses."
Assert-Contains $drawer "Magnet.linkFor" `
    "Source rows must keep the copy affordance."
Assert-Contains $drawer "swallow" `
    "Drawer body must swallow clicks (panel doctrine)."
Assert-Contains $drawer "Couldn't load other seasons" `
    "Season fetch failure must be honest, with a retry."
Assert-Contains $player 'icon: "browser"' `
    "Control bar must carry the drawer button."
Assert-Contains $player 'kind === "browser"' `
    "IconGlyph must draw the browser glyph."
Assert-Contains $player "BrowserDrawer {" `
    "PlayerPage must instantiate the drawer."
Assert-Contains $player 'root.playStreamAt(index, "switch")' `
    "A source tap must switch in place (position carries via 41f5635)."
Assert-Contains $player "root.jumpToEpisode(target" `
    "An episode tap must ride the generalized jump pipeline."

Write-Host "Player browser contract checks passed."
