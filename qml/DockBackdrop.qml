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
        // The captured scene already fills the opaque backdrop.
        hideSource: true
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
        // Keep the picker open when a primary click lands on the glass.
        onClicked: mouse => {
            if (mouse.button !== Qt.LeftButton)
                root.dismissed()
        }
        onWheel: event => event.accepted = true
    }
}
