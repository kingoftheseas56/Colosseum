// The house window verbs for full-page overlays that cover the shared TopBar.
import QtQuick

Row {
    id: controls
    property string objectPrefix: "houseWindow"
    readonly property bool shellWindowed: typeof WindowMode !== "undefined" && WindowMode.shellWindowed
    signal minimizeRequested()
    signal fullscreenRequested()
    signal quitRequested()
    visible: Qt.platform.os !== "android"
    spacing: 20
    height: 22

    component WindowIcon: Item {
        id: icon
        property url source
        property string accessibleName: ""
        signal triggered()
        width: 22; height: 22
        Image {
            anchors.fill: parent
            source: icon.source
            sourceSize.width: 22; sourceSize.height: 22
            fillMode: Image.PreserveAspectFit
            opacity: action.interactionActive ? 1.0 : 0.72
        }
        KeyboardAction {
            id: action
            anchors.fill: parent
            accessibleName: icon.accessibleName
            focusRadius: 6
            onTriggered: icon.triggered()
        }
    }

    WindowIcon {
        objectName: controls.objectPrefix + "Minimize"
        source: Qt.resolvedUrl("../assets/icons/minimize.svg")
        accessibleName: qsTr("Minimize")
        onTriggered: controls.minimizeRequested()
    }
    WindowIcon {
        objectName: controls.objectPrefix + "Fullscreen"
        source: controls.shellWindowed ? Qt.resolvedUrl("../assets/icons/fullscreen.svg")
                                       : Qt.resolvedUrl("../assets/icons/fullscreen-exit.svg")
        accessibleName: controls.shellWindowed ? qsTr("Enter fullscreen") : qsTr("Exit fullscreen")
        onTriggered: controls.fullscreenRequested()
    }
    WindowIcon {
        objectName: controls.objectPrefix + "Quit"
        source: Qt.resolvedUrl("../assets/icons/power.svg")
        accessibleName: qsTr("Quit Colosseum")
        onTriggered: controls.quitRequested()
    }
}
