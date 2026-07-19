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
