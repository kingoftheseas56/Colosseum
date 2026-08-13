import QtQuick
import QtQuick.Window 2.15
import QtTest 1.3

// J1-Video-Seam (Agent Visibility Phase 2) — attempts the SAME pattern J1-Manga-Seam's
// tst_journey_open_manga.qml already uses against ComicReaderShell.qml: load the production
// root directly via Qt.createComponent and drive its real state.
//
// qml/PlayerPage.qml differs from ComicReaderShell.qml in one load-bearing way: it
// `import`s the native "Colosseum.Player" QML module (MpvItem, SeekThumbnailer), hand-
// registered ONLY inside native/main.cpp's real app bootstrap — never via qt_add_qml_module
// (native/player/mpvitem.h's own header comment explains why: "Colosseum loads its QML live
// from disk ..., so the type is registered by hand in main.cpp"). The shared Qt Quick Test
// runner every tst_*.qml here goes through, tests/qml/quicktest_main.cpp, performs no such
// registration and links no MpvQt/libmpv at all. This is a pre-existing, documented, standing
// limitation, not something this slice introduced: tests/parity_load_harness.qml's own header
// comment already excludes PlayerPage.qml from ITS headless-load gate for exactly this reason
// — "PlayerPage itself imports the native Colosseum.Player module and cannot load headless —
// it is covered by the contract test + the live app smoke."
//
// initTestCase() proves that live (ground-truth, not assumed from the comment above) and
// records the exact QQmlComponent error. If the component genuinely fails to load — the
// expected, documented outcome — every case below QSKIPs with that error attached, rather
// than asserting a fabricated pass OR permanently reddening the shared `colosseum.qml` gate
// on an infrastructure gap this slice's fence has no path to close (closing it needs a change
// to tests/qml/quicktest_main.cpp — registering Colosseum.Player and linking MpvQt/libmpv into
// this runner — which is outside J1-Video-Seam's touch fence: native/player/mpvitem.{cpp,h},
// qml/PlayerPage.qml, this file, tests/auto/player/tst_journey_play_video.cpp, and the ONE
// native-test registration line in tests/CMakeLists.txt).
//
// The production decodedWidth/decodedHeight/playerReady/sourceIdentity properties
// (qml/PlayerPage.qml, "decoded-frame readiness" section, right beside the existing Lanista
// bridge proxies) are proven instead by tests/auto/player/tst_journey_play_video.cpp — a real
// MpvItem, real ffmpeg-generated fixtures, no QML engine required — plus direct human review
// of playerReady's one-line formula against that native evidence.

TestCase {
    id: testCase
    name: "JourneyPlayVideo"

    property var playerComp: null
    property string loadError: ""

    function initTestCase() {
        playerComp = Qt.createComponent("../../qml/PlayerPage.qml")
        if (playerComp.status === Component.Error)
            loadError = playerComp.errorString()
    }

    function skipIfPlayerUnloadable() {
        if (playerComp.status === Component.Error) {
            skip("qml/PlayerPage.qml cannot load under colosseum_qml_tests (see this file's " +
                 "header comment + tests/parity_load_harness.qml:6-9). QQmlComponent error: " +
                 loadError)
            return true
        }
        return false
    }

    // Right after the player root attaches (no source loaded yet), playerReady must be
    // false and decodedWidth/decodedHeight must be zero — attaching the page is not itself
    // readiness (mirrors the native route_is_not_ready case).
    function test_ready_false_at_attach() {
        if (skipIfPlayerUnloadable())
            return
        var player = createTemporaryObject(playerComp, testCase, {})
        verify(player !== null, "player createTemporaryObject must succeed")
        compare(player.decodedWidth, 0)
        compare(player.decodedHeight, 0)
        compare(player.playerReady, false)
    }

    // Once mpv reports a real decoded frame (decodedWidth/decodedHeight both positive) on an
    // otherwise-loaded session, playerReady must become true — the real false->true
    // transition (mirrors the native decoded_fixture_reports_exact_dimensions case).
    function test_ready_true_after_dimensions() {
        if (skipIfPlayerUnloadable())
            return
        skip("component loaded, but this runner links no MpvQt/libmpv, so no real decode can " +
             "happen here — needs tests/qml/quicktest_main.cpp changes outside this slice's fence")
    }

    // Loading a fresh source into the same player must reset playerReady to false and
    // decodedWidth/decodedHeight to zero until the NEW file's own frame decodes — a
    // same-size reload must not read "ready" from a stale prior value (mpvitem.cpp's
    // issueLoadFile reset, exercised here at the QML/session layer).
    function test_ready_resets_on_unload() {
        if (skipIfPlayerUnloadable())
            return
        skip("component loaded, but this runner links no MpvQt/libmpv, so no real decode/reload " +
             "can happen here — needs tests/qml/quicktest_main.cpp changes outside this slice's fence")
    }
}
