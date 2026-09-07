pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    anchors.fill: parent
    focus: true
    activeFocusOnTab: true

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
        UniverseChromeAction {
            accessibleName: "Minimize"
            source: "../assets/icons/minimize.svg"
            onTriggered: root.minimizeRequested()
        }
        UniverseChromeAction {
            accessibleName: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                            ? "Enter fullscreen" : "Exit fullscreen"
            source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed)
                    ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"
            onTriggered: root.fullscreenRequested()
        }
        UniverseChromeAction {
            accessibleName: "Close Colosseum"
            source: "../assets/icons/power.svg"
            onTriggered: root.closeRequested()
        }
    }
}
