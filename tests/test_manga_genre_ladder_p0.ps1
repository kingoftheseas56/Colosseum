$ErrorActionPreference = "Stop"

# Manga genre ladder contract (2026-07-13, the Jikan half-outage): MAL refuses Jikan's own
# servers for hours at a time while MAL itself stays up — so the genre lane must ride a
# ladder (Jikan first, Kitsu rung on ANY failure/empty) and the Jikan host must be IPv4-
# pinned (it publishes AAAA; the dead-IPv6 stall rode every call, unpinned, since birth).

$root = Split-Path -Parent $PSScriptRoot
function Read-File($rel) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "MISSING FILE: $rel" }
    return Get-Content $p -Raw
}
function Assert-Contains($text, $needle, $message) {
    if ($text -notlike "*$needle*") { throw $message }
}

$ga = Read-File "qml/GenreApi.js"
foreach ($n in @('function kitsuGenre(', 'function kitsuCard(', 'KITSU_SLUGS',
                 'kitsuGenre(name, sort, push); return;')) {
    Assert-Contains $ga $n "GenreApi must carry the Kitsu rung: $n"
}

$cpp = Read-File "native/main.cpp"
Assert-Contains $cpp 'api.jikan.moe' "main.cpp must IPv4-pin api.jikan.moe (AAAA publisher, dead-IPv6 machine)."

# Theatre's anime lane rides the same ladder (A5 cross-lane touch, Hemanth-authorized
# 2026-07-13 while A4 slept — announced in the haven's agents/chat.md). LAW: Jikan first,
# Kitsu ONLY on failure/empty.
$ta = Read-File "qml/TheatreApi.js"
foreach ($n in @('function kitsuAiring(', 'function mapKitsuAnime(',
                 'kitsuAiring(limit, done); return;', '"kitsu:" + m.id')) {
    Assert-Contains $ta $n "TheatreApi must carry the Kitsu rung: $n"
}

Write-Host "manga genre ladder p0: OK"
