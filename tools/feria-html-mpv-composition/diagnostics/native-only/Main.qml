import QtQuick
import QtQuick.Window
import Baseline.Player 1.0
Window {
    width: 960; height: 540; visible: true
    color: "#141a20"
    title: "Native-only unchanged MpvItem | no WebEngine loaded"
    MpvItem { id: player; anchors.fill: parent }
    Component.onCompleted: baseline.bind(player)
}
