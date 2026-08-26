import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// The EXACT Continue-Watching sequence from Main.qml:activateSession, against Player2Page.
// Any QML error, any throw, any silent no-op here is the dead door.
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    Player2Page { id: page; anchors.fill: parent }
    Component.onCompleted: {
        console.log("CW PROBE: page loaded OK - facade exists:",
                    typeof page.playTorrent, typeof page.playLocalFile,
                    typeof page.playRemoteUrl, typeof page.restoreState,
                    typeof page.resumeFromMinimize)
        // A stored CW target: torrent kind, NO candidates (stores don't persist them), position saved.
        var t = { "infoHash": "0000000000000000000000000000000000000000", "fileIdx": 0,
                  "title": "cw probe", "backdrop": "", "subType": "movie", "subId": "tt1",
                  "position": 1234 }
        try {
            page.playTorrent(t.infoHash, t.fileIdx || 0, t.title, t.backdrop, t.subType, t.subId,
                             t.streamCandidates || [], t.playbackContext || ({}))
            var resumeSt = { "position": Number(t.position) || 0 }
            if (page.restoreState) page.restoreState(resumeSt)
            console.log("CW PROBE: sequence completed without throwing; state=" + page.sessionState())
        } catch (e) {
            console.log("CW PROBE: THREW -> " + e)
        }
    }
    Timer { interval: 6000; running: true; onTriggered: {
        console.log("CW PROBE: after 6s state=" + page.sessionState()
                    + " starting=" + page.loadingActive() + " errorText=[" + page.errorText + "]")
        Qt.exit(0)
    } }
}
