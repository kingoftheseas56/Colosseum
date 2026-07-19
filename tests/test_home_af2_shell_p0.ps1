# AF2 Home — shell contracts (SHAPE not behavior). Theme cadence tokens + the
# rail/card design surface. Pixels are Hemanth's eyes; this only guards wiring.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$theme = Get-Content (Join-Path $root "qml/Theme.qml") -Raw
function Has($t,$n,$m){ if($t -notlike "*$n*"){throw $m} }
Has $theme 'readonly property int homePad' "Theme must expose the AF2 page gutter token (homePad)."
Has $theme 'readonly property int rowH'    "Theme must expose the AF2 view_row rail cadence token (rowH)."
Has $theme 'readonly property string displaySans' "Theme must expose a sans display face token (displaySans)."

$rail = Get-Content (Join-Path $root "qml/HomeRail.qml") -Raw
$card = Get-Content (Join-Path $root "qml/HomeCard.qml") -Raw
Has $rail 'property string worldTag' "Rail must carry an optional world tag."
Has $rail 'theme.rowH'               "Rail must obey the AF2 view_row cadence (theme.rowH)."
Has $rail 'theme.homePad'            "Rail must use the AF2 page gutter (theme.homePad)."
Has $card 'shape'                    "Card must switch shape (landscape/portrait/jacket)."
foreach($sh in @('landscape','portrait','jacket')){ Has $card "`"$sh`"" "Card must support shape: $sh" }
Has $card 'theme.gold'               "Card progress bar must be gold."
Write-Host "home rail+card ok"
