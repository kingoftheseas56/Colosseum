$ErrorActionPreference = "Stop"

# Task 6 RED contract: Tankoban Continue projections must carry chapter, volume,
# and western-comic progress, with one global recency cap.
$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path -LiteralPath $p)) { throw "MISSING FILE: $rel" }
    return (Get-Content -LiteralPath $p -Raw) -replace '\s+', ' '
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}
function Assert-Matches($text, $pattern, $message) {
    if ($text -notmatch $pattern) { throw $message }
}

$world = Read-File "qml/TankobanWorld.qml"
foreach ($kind in @("manga", "tankoban", "comic")) {
    Assert-Contains $world ('Progress.recent("{0}", 12)' -f $kind) `
        "TankobanWorld Continue projection must read $kind progress with limit 12."
}
Assert-Matches $world `
    'var\s+a\s*=\s*Progress\.recent\("manga",\s*12\)\s*\.concat\s*\(\s*Progress\.recent\("tankoban",\s*12\)\s*\)\s*\.concat\s*\(\s*Progress\.recent\("comic",\s*12\)\s*\).*?a\.sort\s*\(\s*function\s*\(\s*x\s*,\s*y\s*\)\s*\{\s*return\s*\(\s*y\.updatedAt\s*\|\|\s*0\s*\)\s*-\s*\(\s*x\.updatedAt\s*\|\|\s*0\s*\)\s*\}\s*\).*?a\.slice\s*\(\s*0\s*,\s*12\s*\)' `
    "TankobanWorld must globally recency-sort the three-kind merge before slicing 12."

$page = Read-File "qml/ContinueSeeAllPage.qml"
Assert-Matches $page `
    'scope\s*===\s*"tankoban"\s*\)\s*rawItems\s*=\s*Progress\.recent\("manga",\s*0\)\s*\.concat\s*\(\s*Progress\.recent\("tankoban",\s*0\)\s*\)\s*\.concat\s*\(\s*Progress\.recent\("comic",\s*0\)' `
    "Tankoban See All scope must read manga+tankoban+comic with uncapped limit 0."

$js = Read-File "qml/ContinueSeeAll.js"
Assert-Matches $js `
    'function\s+matchesMedium\s*\(\s*e\s*,\s*medium\s*\).*?medium\s*===\s*"manga".*?e\.kind\s*===\s*"manga"\s*\|\|\s*e\.kind\s*===\s*"tankoban"' `
    "ContinueSeeAll.js must define manga medium as manga OR tankoban."
Assert-Contains $js 'out = out.filter(function(e) { return matchesMedium(e, medium); });' `
    "ContinueSeeAll.js apply() must use matchesMedium for medium filtering."

Write-Host "tankoban continue projection: OK"
