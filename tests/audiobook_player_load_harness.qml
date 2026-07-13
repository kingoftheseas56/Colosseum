import QtQuick
import QtQuick.Window

// Headless load gate for AudiobookPlayer.qml (the audiobook REMOTE, Task 4.3 2026-07-13).
// The page sits behind a lazy Loader in Main.qml, so a creation-time QML error ships
// invisibly (the DownloadsPage 12.5px lesson). Before the shared-session refactor this
// page imported the native Colosseum.Player module and could not load headless; now the
// MpvItem lives in AudiobookSession (window root) and the page only binds to the session
// id — which this harness stubs. AudiobookSession itself still imports Colosseum.Player
// and stays covered by the live boot smoke (house rule).
Window {
    visible: true
    width: 300; height: 200

    // stub of the shared engine — every property/function the remote binds or calls
    QtObject {
        id: audioSession
        property string activePairKey: ""
        property var book: ({ title: "Stub Book", author: "Stub Author", cover: "" })
        property var files: []
        property int currentIndex: 0
        property bool ready: false
        property bool multiFile: false
        property var chapterModel: []
        property real position: 0
        property real duration: 0
        property bool paused: true
        property real speed: 1.0
        property int sleepMinutes: 0
        function openFor(pk, b) {}
        function playIndex(i) {}
        function togglePlay() {}
        function seekRel(d) {}
        function seekTo(t) {}
        function setRate(r) {}
        function stop() {}
        function recordProgress() {}
        function captureState() { return ({}) }
        function restoreState(st) {}
    }

    Loader {
        id: l
        source: "../qml/AudiobookPlayer.qml"
        onStatusChanged: {
            if (status === Loader.Error) console.log("[diag] LOADER ERROR — see warnings above")
            if (status === Loader.Ready) console.log("[diag] LOADER READY — audiobook remote created fine")
        }
    }
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
}
