import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// Construction probe for the production Player 2 page (Task 17). Boot-smoking the app is not enough:
// the player Loader stays inactive until something plays, so a broken import or an unresolved type in
// Player2Page/ColosseumHostServices would stay hidden until the first play - the worst moment to find
// it. This instantiates the page directly, against the real app binary's registered types and context
// properties, and prints one line so a runner can tell construction from silence.
//
// Run: colosseum.exe tests/player2/player2_page_probe.qml   (from the repo root)
Window {
    id: probe
    width: 640
    height: 360
    visible: true
    title: "Player 2 page probe"

    Player2Page {
        id: page
        anchors.fill: parent
    }

    Component.onCompleted: {
        var missing = []
        var required = ["playTorrent", "playLocalFile", "playRemoteUrl", "stop",
                        "captureState", "restoreState", "suspendForMinimize", "resumeFromMinimize"]
        for (var i = 0; i < required.length; i++) {
            if (typeof page[required[i]] !== "function")
                missing.push(required[i])
        }
        if (missing.length)
            console.log("PLAYER2 PAGE PROBE: FAIL missing=" + missing.join(","))
        else
            console.log("PLAYER2 PAGE PROBE: PASS constructed, interface complete")
        Qt.callLater(function() { Qt.quit() })
    }
}
