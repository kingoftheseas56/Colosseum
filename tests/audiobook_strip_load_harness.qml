import QtQuick
import QtQuick.Window

// Headless load + contract gate for AudiobookStrip.qml (the reader's SECOND remote
// onto the shared AudiobookSession, Task 4.4 2026-07-13). The strip mounts inside
// BookReader.qml, which hosts a WebEngineView and can NOT load under qml.exe
// (QtWebEngine needs initialize() before app creation) — so the strip gets its own
// harness against a stubbed session, mirroring audiobook_player_load_harness.qml.
// Contract asserts (throw HANGS qml.exe — verdict rides Qt.exit via try/catch):
//   1. openFor on a ready session → strip live
//   2. hide() drops the strip WITHOUT touching the session (remote contract)
//   3. the engine moving to ANOTHER book hides the strip (honest, no re-label)
//   4. pauseForTts() pauses the stream and yields the bottom edge
Window {
    id: root
    visible: true
    width: 500; height: 300

    // stub of the shared engine — every property/function the strip binds or calls
    QtObject {
        id: audioSessionStub
        property string activePairKey: ""
        property var book: ({})
        property var files: []
        property int currentIndex: 0
        property bool ready: false
        property bool multiFile: false
        property real position: 12
        property real duration: 3600
        property bool paused: true
        property real speed: 1.0
        function openFor(pk, b) {
            if (ready && activePairKey === pk) return   // idempotent, like the real engine
            activePairKey = pk
            book = b || ({})
            files = ["a.mp3"]
            ready = true
        }
        function togglePlay() { paused = !paused }
        function seekRel(d) { position = Math.max(0, position + d) }
        function seekTo(t) { position = t }
        function setRate(r) { speed = r }
    }

    Loader {
        id: l
        width: 500; height: 96
        source: "../qml/AudiobookStrip.qml"
        onStatusChanged: {
            if (status === Loader.Error) { console.log("[diag] LOADER ERROR — see warnings above"); Qt.exit(1) }
            if (status === Loader.Ready) {
                console.log("[diag] LOADER READY — audiobook strip created fine")
                root.runContract()
            }
        }
    }

    function runContract() {
        try {
            var s = l.item
            s.session = audioSessionStub

            // 1. summon → live (headless: Audiobooks is undefined, so the
            //    isDownloaded pre-gate is skipped and the stub decides readiness)
            s.openFor("dune|frank herbert", { title: "Dune", author: "Frank Herbert" })
            if (!s.live) throw "openFor on a ready session did not go live"

            // 2. hide() = drop the remote, NEVER stop the stream
            s.hide()
            if (s.live) throw "hide() left the strip live"
            if (!audioSessionStub.ready || audioSessionStub.activePairKey !== "dune|frank herbert")
                throw "hide() disturbed the session (remote contract breach)"

            // 3. another book takes the engine → this book's strip hides
            s.openFor("dune|frank herbert", { title: "Dune" })
            if (!s.live) throw "re-summon did not go live"
            audioSessionStub.activePairKey = "other|book"
            if (s.live) throw "strip stayed live after the engine moved to another book"
            audioSessionStub.activePairKey = "dune|frank herbert"

            // 4. TTS opens → pause + yield (one engine per ear)
            audioSessionStub.paused = false
            s.pauseForTts()
            if (!audioSessionStub.paused) throw "pauseForTts() did not pause the session"
            if (s.live) throw "pauseForTts() left the strip up"

            console.log("[diag] STRIP CONTRACT OK")
            Qt.exit(0)
        } catch (e) {
            console.log("[diag] STRIP CONTRACT FAIL: " + e)
            Qt.exit(1)
        }
    }

    // watchdog: a hang (uncaught throw, missed signal) must FAIL, not idle forever
    Timer { interval: 5000; running: true; onTriggered: { console.log("[diag] WATCHDOG — harness hung"); Qt.exit(2) } }
}
