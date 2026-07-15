// Headless BEHAVIORAL harness for the SubtitleMenu selection state machine (click-through fix,
// Agent 0 on A4's behalf 2026-07-15). This is interaction coverage — not source-text: it
// instantiates the real SubtitleMenu, drives its pick methods, and asserts state transitions.
//
// Verdict rides the EXIT CODE — Qt.exit(0) pass, non-zero fail — because console output is not
// guaranteed to flush and an uncaught onCompleted throw HANGS qml.exe (house rule).
import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    width: 400; height: 300

    // A representative track model: two embedded tracks.
    property var demoTracks: [
        { id: "1", label: "English", lang: "eng", external: false },
        { id: "2", label: "Spanish", lang: "spa", external: false }
    ]

    Loader {
        id: ld
        source: "../qml/SubtitleMenu.qml"
        onStatusChanged: {
            if (status === Loader.Error) { console.log("HARNESS FAIL: SubtitleMenu did not load"); Qt.exit(4) }
        }
        onLoaded: Qt.callLater(win.run)   // let bindings settle before driving
    }

    function check(cond, msg) { if (!cond) throw new Error(msg) }

    function run() {
        try {
            var m = ld.item
            if (!m) { console.log("HARNESS FAIL: no SubtitleMenu item"); Qt.exit(5); return }
            m.tracks = win.demoTracks
            m.selectedId = "1"

            // instrument the outward signals
            var trackPicks = 0, offPicks = 0, onlinePicks = 0
            m.trackPicked.connect(function(id) { trackPicks++ })
            m.offPicked.connect(function() { offPicks++ })
            m.onlinePicked.connect(function(u, t, l) { onlinePicks++ })

            // ---- 1. a row pick emits EXACTLY ONE request and does NOT close ----
            m.panelOpen = true
            m.pickTrack("2")
            check(trackPicks === 1, "a row pick must emit exactly one trackPicked, got " + trackPicks)
            check(m.panelOpen === true, "popup must stay OPEN while the selection is pending")
            check(m.pending === true, "menu.pending must be true while awaiting confirmation")

            // ---- 2. popup closes only after mpv CONFIRMS the requested track ----
            m.selectedId = "2"   // simulate mpv.subtitleTrack settling on the request
            check(m.panelOpen === false, "popup must close once the requested track is confirmed")
            check(m.pending === false, "pending must clear on confirmation")

            // ---- 3. a FAILED / timed-out selection does NOT silently close; shows an error ----
            m.selectedId = "2"
            m.panelOpen = true
            m.pickTrack("1")
            check(m.panelOpen === true, "popup must stay open while the new pick is pending")
            m.failPending()      // simulate the confirmation timeout firing
            check(m.panelOpen === true, "a failed selection must NOT silently close the popup")
            check(m.selectionError.length > 0, "a failed selection must expose a visible error")

            // ---- 4. picking the ALREADY-selected track closes immediately (no dangling pending) ----
            m.selectedId = "1"
            m.panelOpen = true
            m.pickTrack("1")
            check(m.panelOpen === false, "re-picking the current track closes at once (already confirmed)")

            // ---- 5. OFF confirms when mpv reports no subtitle track ----
            m.selectedId = "2"
            m.panelOpen = true
            m.pickOff()
            check(offPicks === 1, "Off must emit exactly one offPicked, got " + offPicks)
            check(m.panelOpen === true, "Off must stay open until mpv confirms subtitles are off")
            m.selectedId = ""
            check(m.panelOpen === false, "Off closes once mpv confirms an empty subtitle track")

            // ---- 6. ONLINE pick confirms when a NEW track becomes selected ----
            m.selectedId = "2"
            m.panelOpen = true
            m.pickOnline("http://example/sub.srt", "OpenSubtitles", "en")
            check(onlinePicks === 1, "online pick must emit exactly one onlinePicked, got " + onlinePicks)
            check(m.panelOpen === true, "online pick must stay open until the added track loads")
            m.selectedId = "7"   // the freshly added subtitle becomes the selected track
            check(m.panelOpen === false, "online pick closes once a new track is confirmed selected")

            // ---- 7. closing the panel clears any lingering pending + error ----
            m.panelOpen = true
            m.pickTrack("2")
            m.panelOpen = false
            check(m.pending === false, "closing the panel clears pending state")
            check(m.selectionError.length === 0, "closing the panel clears any error")

            console.log("SubtitleMenu interaction harness PASSED")
            Qt.exit(0)
        } catch (e) {
            console.log("HARNESS FAIL: " + e.message)
            Qt.exit(2)
        }
    }

    Timer { interval: 8000; running: true; onTriggered: { console.log("HARNESS FAIL: timed out"); Qt.exit(3) } }
}
