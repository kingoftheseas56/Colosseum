# AF2 Home — Spotlight + recede contracts (SHAPE not behavior). Extended in Task 5 with
# the Main-side wiring contract.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$s = Get-Content (Join-Path $root "qml/HomeSpotlight.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $s 'property real recede'  "Spotlight must take a 0..1 recede input."
Has $s 'property url logoUrl'  "Spotlight must show the metahub-derived logo."
Has $s 'root.title'            "Spotlight must fall back to a text title when no logo."
Has $s 'signal primaryRequested'   "Spotlight must emit the primary action (Play/Watch)."
Has $s 'signal secondaryRequested' "Spotlight must emit a world-appropriate secondary (Read/Details)."
Has $s 'theme.gold'           "Resume progress must use gold."
Has $s 'translateY'.Replace('translateY','transform') "Spotlight must lift on recede."
Write-Host "home spotlight ok"

# ── Task 5 (amended 2026-07-19, Hemanth): the Main-side Home wiring ──
# Rulings: (1) shell TopBar stays as Home's top menu + window controls (no glass worlds
# bar); (2) the AF2 generic rails board was PULLED — Home keeps the receding hero over
# the RESTORED familiar body: Continue (via ContinueTile's reliable posters) + the three
# world widgets (Bookshelf / Theatre film-strip / Reading Desk).
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Has $main 'HomeSpotlight'  "Home must mount the receding featured hero."
Has $main 'homeRecede'     "Home must compute a 0..1 recede from scroll for the hero."
Has $main 'prefers.*reduced|reducedMotion|Accessibility' 'Home recede must respect reduced motion.'
foreach($w in @('Bookshelf {','TheatreStrip {','ReadingDesk {')){ Has $main $w "Home must keep the world widget: $w" }
Has $main 'variant: "home"' "Home Continue must use the ContinueTile home variant (reliable posters)."
Has $main 'Progress.recent' "Home Continue + hero fed by the Progress store."
Write-Host "home body wiring ok"
