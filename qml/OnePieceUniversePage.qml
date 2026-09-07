pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    anchors.fill: parent

    property string extensionId: ""
    property string universeName: ""
    property bool reducedMotion: false
    property var installedExtensions: []
    readonly property bool atlasTransientOpen: atlas.transientOpen

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal arcRequested(var arc)
    signal mediaRequested(var entry)

    function requestEscape() {
        if (atlas && atlas.requestEscape)
            return atlas.requestEscape()
        return false
    }

    Theme { id: theme }

    Rectangle { anchors.fill: parent; color: "#07131d" }

    OnePieceEastBlueAtlas {
        id: atlas
        anchors.fill: parent
        reducedMotion: root.reducedMotion
        onArcRequested: function(arc) { root.arcRequested(arc) }
        onMediaRequested: function(entry) { root.mediaRequested(entry) }
    }

    ChromeScrim { z: 16 }
    BackAction {
        x: theme.margin
        y: 28
        z: 20
        onTriggered: root.backRequested()
    }

    Row {
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"; sourceSize.width: 22; sourceSize.height: 22; opacity: minMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image {
                anchors.fill: parent
                source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                        ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
                sourceSize.width: 22; sourceSize.height: 22
                opacity: fsMa.containsMouse ? 1.0 : 0.72
            }
            MouseArea { id: fsMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"; sourceSize.width: 22; sourceSize.height: 22; opacity: closeMa.containsMouse ? 1.0 : 0.72 }
            MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
        }
    }
}
