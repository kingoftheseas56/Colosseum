import QtQuick

FocusScope {
    id: root
    objectName: "dcauPortal_" + String(root.title).toLowerCase().replace(/ /g, "-")
    property string title: ""
    property var imageSources: []
    property bool selected: false
    signal activated()
    signal selectionRequested()
    signal previousRequested()
    signal nextRequested()

    Theme { id: theme }

    width: 260
    height: Math.round(width * 8 / 5)
    activeFocusOnTab: true
    opacity: root.selected || hover.hovered || root.activeFocus ? 1.0 : 0.56
    scale: root.selected || hover.hovered || root.activeFocus ? 1.0 : 0.91
    transformOrigin: Item.Center
    Behavior on opacity { NumberAnimation { duration: 220 } }
    Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    RoundedPosterImage {
        anchors.fill: parent
        radius: 22
        hovered: hover.hovered
        sources: root.imageSources
    }
    Rectangle {
        anchors.fill: parent
        radius: 22
        color: "transparent"
        border.width: root.selected || root.activeFocus ? 2 : 1
        border.color: root.selected || root.activeFocus
                      ? Qt.rgba(240/255,196/255,74/255,0.62)
                      : Qt.rgba(1,1,1,0.13)
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: parent.height * 0.34; radius: 22
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(0,0,0,0.82) }
        }
    }
    Text {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 20
        text: root.title
        color: root.selected || root.activeFocus ? theme.gold : theme.ink
        font.family: theme.display
        font.pixelSize: 24
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
    HoverHandler { id: hover; onHoveredChanged: if (hovered) root.selectionRequested() }
    TapHandler { onTapped: { root.forceActiveFocus(); root.activated() } }
    onActiveFocusChanged: if (activeFocus) root.selectionRequested()
    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()
    Keys.onLeftPressed: root.previousRequested()
    Keys.onRightPressed: root.nextRequested()
}
