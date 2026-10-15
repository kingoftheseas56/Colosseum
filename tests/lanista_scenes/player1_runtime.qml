import QtQuick
import QtQuick.Window
import "../../qml"

Window {
    id: runtimeWindow
    objectName: "player1RuntimeScene"
    visible: true
    width: 1280
    height: 720
    color: "#000000"
    title: "Colosseum Player 1 Runtime Selftest"

    PlayerPage {
        id: playerPage
        anchors.fill: parent
    }
}
