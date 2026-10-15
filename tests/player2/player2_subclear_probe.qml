import QtQuick
import QtQuick.Window
import "../../qml/player2host"

// Text-path subtitle clearing, in-app, in HEMANTH'S environment (no QSG_NO_VSYNC).
// The C++ P2_SUB_TRACE tells the story; this window only drives playback.
Window {
    id: probe
    width: 960; height: 540; visible: true; color: "black"
    title: "sub trace probe"
    Player2Page { id: page; anchors.fill: parent }
    property bool selected: false
    Component.onCompleted:
        page.playLocalFile({ "localPath": Qt.application.arguments[2], "title": "sub probe" })
    Timer {
        interval: 500; repeat: true; running: true
        onTriggered: {
            if (!probe.selected && page.sessionState() >= 3) {
                probe.selected = true
                page.sessionSelectSubtitleById("2")
                console.log("SUBCLEAR PROBE: subtitle track 2 selected; seeking to 0")
                page.sessionSeek(0)
            }
        }
    }
    Timer { interval: 15000; running: true; onTriggered: Qt.exit(0) }
}
