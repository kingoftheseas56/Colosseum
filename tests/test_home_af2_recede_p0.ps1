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

# ── Task 5: the Main-side Home wiring ──
# Ruling 2026-07-19 (Hemanth): the shell TopBar stays as Home's top menu + window
# controls; the separate glass worlds bar was dropped. So the Home body mounts the
# receding spotlight + a glass board of rails — NOT a HomeWorldsBar.
$main = Get-Content (Join-Path $root "qml/Main.qml") -Raw
Has $main 'HomeSpotlight'  "Home body must mount the featured spotlight."
Has $main 'HomeRail'       "Home body must mount rails."
Has $main 'homeRecede'     "Home must compute a 0..1 recede from scroll and feed the spotlight."
Has $main 'prefers.*reduced|reducedMotion|Accessibility' 'Home recede must respect reduced motion.'
foreach($w in @('Tankoban','Theatre','Biblio')){ Has $main "worldTag: `"$w`"" "Home must carry a $w rail." }
Has $main 'Progress.recent' "Continue rail must be fed by the cross-world Progress store."
Write-Host "home body wiring ok"
