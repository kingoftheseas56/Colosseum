# Function 0004 session-model source contract.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function MustContain($file, $needle, $why) {
    $path = Join-Path $root $file
    if (!(Select-String -Path $path -Pattern ([regex]::Escape($needle)) -Quiet)) {
        throw "MISSING in ${file}: '$needle' ($why)"
    }
}

function MustNotContain($file, $needle, $why) {
    $path = Join-Path $root $file
    if (Select-String -Path $path -Pattern ([regex]::Escape($needle)) -Quiet) {
        throw "FORBIDDEN in ${file}: '$needle' ($why)"
    }
}

function MustNotMatch($file, $pattern, $why) {
    $path = Join-Path $root $file
    if ((Get-Content -Raw $path) -match $pattern) {
        throw "FORBIDDEN in ${file}: '$pattern' ($why)"
    }
}

MustContain "native/SessionStore.h" 'target.value(QStringLiteral("subId"))' "Theatre episode identity prefers the exact subId"
MustContain "native/SessionStore.h" 'target.value(QStringLiteral("seriesId"))' "comic identity includes its owning series"
MustContain "native/SessionStore.h" 'target.value(QStringLiteral("chapterId"))' "comic identity includes chapter/volume content"
MustContain "native/SessionStore.h" 'target.contains(QStringLiteral("fileIdx"))' "raw torrent identity distinguishes files in one pack"
MustContain "native/SessionStore.h" 'Exact-content reopen: refresh transport/path/metadata' "exact-content descriptors refresh without discarding saved state"
MustContain "qml/Main.qml" 'rec && rec.contentKind === "movie" && win.warmPlayerSessionId === id' "close only stops the warm player when that session owns it"
MustNotMatch "qml/Main.qml" 'if \(rec && rec\.contentKind === "movie"\) \{\s+if \(playerLayer\.item\) playerLayer\.item\.stop\(\)\s+if \(win\.warmPlayerSessionId === id\)' "inactive movie close must not unconditionally stop the shared player"

MustContain "native/CMakeLists.txt" 'add_executable(session_store_harness' "native build defines the behavioral SessionStore harness"
MustContain "tests/CMakeLists.txt" 'colosseum_register_harness(session_store_harness unit;session)' "CTest registers the behavioral harness"
MustContain "tests/CMakeLists.txt" 'colosseum.session_model_contract' "CTest registers this source contract"
MustContain "tests/session_store_harness.cpp" 'same title and chapter label in another series cannot collide' "behavioral harness covers comic identity collision"
MustContain "tests/session_store_harness.cpp" 'same episode transport refresh preserves playback state' "behavioral harness covers exact-content refresh"
MustContain "tests/session_store_harness.cpp" 'raw files inside one torrent pack remain distinct sessions' "behavioral harness covers file-index fallback"

Write-Host "Function 0004 session-model source contract passed."
