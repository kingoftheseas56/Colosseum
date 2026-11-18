import QtQuick
import QtQuick.Effects

Item {
    id: root
    required property Item scene
    signal dismissed()

    Rectangle {
        anchors.fill: parent
        color: "#05060a"
    }
    ShaderEffectSource {
        id: snapshot
        anchors.fill: parent
        visible: false
        sourceItem: root.visible ? root.scene : null
        live: false
        textureSize: Qt.size(Math.max(1, Math.ceil(width / 2)), Math.max(1, Math.ceil(height / 2)))
        onSourceItemChanged: if (sourceItem) scheduleUpdate()
        onTextureSizeChanged: if (sourceItem) scheduleUpdate()
    }
    MultiEffect {
        anchors.fill: parent
        source: snapshot
        autoPaddingEnabled: false
        blurEnabled: true
        blur: 1
        blurMax: 64
    }
    Rectangle {
        anchors.fill: parent
        color: "#2405050a"
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: root.dismissed()
        onWheel: event => event.accepted = true
    }
}
