# AF2 Home — WorldsBar contracts (SHAPE not behavior). The glass top menu that recedes.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$b = Get-Content (Join-Path $root "qml/HomeWorldsBar.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $b 'property real recede'  "WorldsBar must take a 0..1 recede input (fades/lifts on scroll)."
Has $b 'backdrop-filter'.Replace('backdrop-filter','layer.enabled') "WorldsBar must be frosted glass (MultiEffect blur behind)."
Has $b 'signal worldPicked'   "WorldsBar must emit worldPicked(string world) for routing."
foreach($w in @('Home','Tankoban','Theatre','Biblio')){ Has $b "`"$w`"" "WorldsBar must list the world: $w" }
Has $b 'signal searchRequested' "WorldsBar must expose search."
Write-Host "home worlds bar ok"
