# AF2 Home — shell contracts (SHAPE not behavior). Theme cadence tokens + the
# rail/card design surface. Pixels are Hemanth's eyes; this only guards wiring.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$theme = Get-Content (Join-Path $root "qml/Theme.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $theme 'readonly property int homePad' "Theme must expose the AF2 page gutter token (homePad)."
Has $theme 'readonly property int rowH'    "Theme must expose the AF2 view_row rail cadence token (rowH)."
Has $theme 'readonly property string displaySans' "Theme must expose a sans display face token (displaySans)."
Write-Host "home AF2 shell tokens ok"
