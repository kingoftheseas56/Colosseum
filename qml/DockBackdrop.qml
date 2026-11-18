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
        hideSource: true
        live: false
        textureSize: Qt.size(Math.max(1, Math.ceil(width / 2)), Math.max(1, Math.ceil(height / 2)))
        onSourceItemChanged: if (sourceItem) scheduleUpdate()
        onTextureSizeChanged: if (sourceItem) scheduleUpdate()
    }
    MultiEffect {
        id: blur
        anchors.fill: parent
        source: snapshot
        autoPaddingEnabled: false
        blurEnabled: true
        blur: 1
        blurMax: 64
    }
    // The scene is a still image while the picker is open. Reuse the blurred
    // result while decks move instead of running the full-window effect again.
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: blur
        hideSource: true
        live: false
        Component.onCompleted: scheduleUpdate()
        onWidthChanged: scheduleUpdate()
        onHeightChanged: scheduleUpdate()
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
