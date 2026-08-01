$ErrorActionPreference = "Stop"

# Theatre search triage contract (Hemanth 2026-07-05): fast grids, honest best match,
# capped groups. Live timings live in qml/_searchcheck.qml (run through colosseum.exe).

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    # literal match (not -like) — needles here contain [] wildcards-chars
    if ($text.IndexOf($needle) -lt 0) { throw $message }
}

# --- the IPv6-stall cure: search + genre hosts pinned to IPv4 like the poster hosts ---
$main = Read-File "native/main.cpp"
Assert-Contains $main 'QStringLiteral("v3-cinemeta.strem.io")' "Cinemeta search host must be IPv4-pinned (dead-IPv6 ~21s stall)."
Assert-Contains $main 'QStringLiteral("cinemeta-catalogs.strem.io")' "Cinemeta genre host must be IPv4-pinned."

# --- the grid never waits for the hero's synopsis ---
$ws = Read-File "qml/WorldSearch.js"
Assert-Contains $ws 'Show the list IMMEDIATELY' "searchTheatre must emit results before the hero /meta/ enrich."

# --- shared honest scoring ---
foreach ($n in @('function normTitle', 'function scoreTitle', 'function pickTopMatch',
                 'rating * 10', 'replace(/^(the|a|an) /')) {
    Assert-Contains $ws $n "WorldSearch scoring must carry: $n"
}
# Tankoban blends ranked lanes into the shared scorer. The comics/western lanes were later
# consolidated into the single local `catalogDb` lane (2026-07-09, "comics search rides the
# LOCG catalogue"). Per this check's own stated intent, assert the scorer WRAPS the ranked
# manga lane rather than pinning the exact lane list, so a lane refactor can't falsely fail it.
Assert-Contains $ws 'pickTopMatch(query, rank(manga' "Tankoban must use the shared scorer too."

# --- See more caps the group grids ---
$ss = Read-File "qml/SearchSurface.qml"
Assert-Contains $ss 'property var expandedGroups' "SearchSurface must track See-more state."
Assert-Contains $ss 'modelData.items.slice(0, secGrid.columns)' "Collapsed groups must cap at one row."
Assert-Contains $ss '"See more · "' "Overflowing groups must offer See more."
Assert-Contains $ss 'surf.expandedGroups = []   // a NEW query' "See-more state must reset per query."

Write-Host "Theatre search triage contract checks passed."
