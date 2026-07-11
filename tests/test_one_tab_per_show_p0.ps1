# One-tab-per-show P0 contract (spec 2026-07-11). SHAPE ONLY — behavior rides the
# SessionStore self-test (COLOSSEUM_SESSION_SELFTEST) and Hemanth's eyes.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function MustContain($file, $needle, $why) {
    $p = Join-Path $root $file
    if (!(Select-String -Path $p -Pattern ([regex]::Escape($needle)) -Quiet)) {
        throw "MISSING in ${file}: '$needle' ($why)"
    }
}

# store: show-first key, replace semantics, replace signal
MustContain "native/SessionStore.h" 'showKey'                 "dedup key prefers the show identity"
MustContain "native/SessionStore.h" 'contentKeyFor'           "same-show-new-content detection"
MustContain "native/SessionStore.h" 'targetReplaced'          "replace announces itself to Main"

# Main: callers compute the show key from the tested pure lib
MustContain "qml/Main.qml" 'EpisodeBrowser.js'                "Main imports the pure lib for seriesRootId"
MustContain "qml/Main.qml" '"showKey": EpisodeBrowser.seriesRootId' "stream + local sessions key by show"
MustContain "qml/Main.qml" 'onTargetReplaced'                 "warm flag cleared / active surface rebuilt on replace"

Write-Host "One-tab-per-show P0 contract passed."
