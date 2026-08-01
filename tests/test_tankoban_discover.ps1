$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Task 6 gate: TankobanDiscoverApi.js (the world-neutral adapter for the shared Discover
# shell) against its offscreen contract harness, PLUS static contract guards that need no
# live database and no Comic Vine / Metron runtime.
#
# qml.exe is a GUI-subsystem exe: its console.log never reaches redirected stdout unless
# QT_FORCE_STDERR_LOGGING forces it to stderr, so we set that and capture the merged
# stream, gating the harness on BOTH its OK marker AND a clean exit (0).
$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path -LiteralPath $qmlExe)) { throw "qml.exe not found at $qmlExe" }

$env:QT_FORCE_STDERR_LOGGING = "1"
$qmlInc = Join-Path $root "qml"

# (1) Adapter contract harness — drives every descriptor/normalize/merge/pin/filter path
#     through FAKE catalog objects so the contract is pinned without a live SQLite DB.
$harness = Join-Path $root "tests\tankoban_discover_api_harness.qml"
if (!(Test-Path -LiteralPath $harness)) { throw "harness not found: $harness" }
$out  = cmd /c "`"$qmlExe`" -platform offscreen -I `"$qmlInc`" `"$harness`" 2>&1" | Out-String
$code = $LASTEXITCODE
Write-Host "----- tests\tankoban_discover_api_harness.qml (exit $code) -----"
Write-Host $out
if ($code -ne 0) { throw "tankoban_discover_api_harness failed (exit $code)" }
if ($out -notlike "*TANKOBAN_DISCOVER_API_OK*") {
    throw "tankoban_discover_api_harness missing OK marker (exit $code)"
}

# (2) Static contract guards — read TankobanDiscoverApi.js directly and prove the
#     no-runtime-dependency / no-download-action invariants hold. These do not execute
#     the adapter; they are deterministic source-level checks.
$adapter = Join-Path $root "qml\TankobanDiscoverApi.js"
if (!(Test-Path -LiteralPath $adapter)) { throw "adapter not found: $adapter" }
$src = Get-Content -LiteralPath $adapter -Raw

# (2a) The adapter MUST declare its source for the static contract (the harness asserts
#      this too, but the source-level check survives even if the harness regresses).
if ($src -notmatch 'var SOURCE\s*=\s*"tankoban-discover-adapter"') {
    throw "Static contract: TankobanDiscoverApi.js must expose var SOURCE = `"tankoban-discover-adapter`""
}

# (2b) NO new Comic Vine runtime dependency (spec 3.3 / 4.4). The adapter may not call a
#      Comic Vine API endpoint directly; comics are delivered from the bundled
#      ComicsCatalog, and discovery extensions declare their own catalogues.
$comicvineHits = [regex]::Matches($src, "(?i)comicvine|comic\.vine|api\.comicvine\.com")
if ($comicvineHits.Count -gt 0) {
    throw "Static contract: TankobanDiscoverApi.js must not depend on Comic Vine (found $($comicvineHits.Count) match(es))"
}

# (2c) NO Metron runtime dependency. Same shape — the adapter does not call Metron
#      directly; a Metron-style extension would surface via the extension seam, not here.
#      The word "Metron" is allowed in comments that document this very invariant; we
#      reject an actual endpoint URL or an XMLHttpRequest.open against a Metron host.
$metronHits = [regex]::Matches($src, "(?i)api\.metron\.cloud|metron\.cloud|m\.metron|https?://[^""'\s]*metron")
if ($metronHits.Count -gt 0) {
    throw "Static contract: TankobanDiscoverApi.js must not depend on Metron (found $($metronHits.Count) endpoint match(es))"
}

# (2d) NO acquisition / download action in the adapter. Discover routes normalized cards
#      to the existing Manga/Comics series doors; it performs NO download of its own.
#      Guard against the obvious download verbs; torrent/addon/install verbs are out of scope.
$downloadPatterns = @(
    '(?i)\bdownload\s*\(.',
    '(?i)\bfetch\s*\(.*download',
    '(?i)\bacquire\s*\(',
    '(?i)\binstallAddon\s*\(',
    '(?i)\bstartDownload\s*\(',
    '(?i)\btorrent:add\b',
    '(?i)\baddon:install\b'
)
foreach ($pat in $downloadPatterns) {
    $hits = [regex]::Matches($src, $pat)
    if ($hits.Count -gt 0) {
        throw "Static contract: TankobanDiscoverApi.js must not perform download/acquisition (pattern $pat matched $($hits.Count) time(s))"
    }
}

# (2e) The Explicit gate stays sexually-explicit-only: the adapter imports
#      ExplicitContentPolicy.js (the ONE gate) and applies it via Policy.visible in both
#      fetch paths. This guards against a future edit that hides Berserk/GoT/Ecchi by
#      swapping in a broader policy.
if ($src -notmatch '\.import\s+"ExplicitContentPolicy\.js"\s+as\s+Policy') {
    throw "Static contract: TankobanDiscoverApi.js must import ExplicitContentPolicy.js as Policy (sexually-explicit-only gate)"
}
$visibleUses = [regex]::Matches($src, 'Policy\.visible\s*\(')
if ($visibleUses.Count -lt 2) {
    throw "Static contract: TankobanDiscoverApi.js must apply Policy.visible in BOTH manga and comics fetch paths (found $($visibleUses.Count))"
}

Write-Host "tankoban discover adapter contract OK (harness + static guards)"
exit 0
