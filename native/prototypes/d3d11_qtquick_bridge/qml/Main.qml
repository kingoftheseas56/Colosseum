import QtQuick
import QtQuick.Window
import Colosseum.Bridge 1.0

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    color: "black"
    title: "Kodi-inspired D3D11 to Qt Quick bridge"

    Shortcut {
        sequence: "F11"
        onActivated: root.visibility = root.visibility === Window.FullScreen
                     ? Window.Windowed : Window.FullScreen
    }

    VideoBridgeItem {
        id: bridgeItem
        objectName: "bridgeItem"
        anchors.fill: parent
        source: bridgeSource
        file: bridgeFile
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 24
        width: Math.min(parent.width - 48, 620)
        height: telemetry.implicitHeight + 32
        radius: 8
        color: "#b0101520"
        border.color: "#80ffffff"

        Text {
            id: telemetry
            anchors.fill: parent
            anchors.margins: 16
            color: "white"
            font.family: "Consolas"
            font.pixelSize: 16
            text: bridgeItem.statusText
        }

        Timer {
            interval: 250
            repeat: true
            running: true
            onTriggered: telemetry.text = bridgeItem.statusText
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: 220
        height: 52
        radius: 26
        color: "#ccffffff"
        Text {
            anchors.centerIn: parent
            color: "#101820"
            font.pixelSize: 18
            font.bold: true
            text: "LIVE QML OVERLAY"
        }
    }
}
