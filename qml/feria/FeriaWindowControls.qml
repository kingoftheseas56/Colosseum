import QtQuick
import QtQuick.Controls
import ".." as Colosseum

Row {
    id: controls
    required property var controller
    property string namePrefix: "feria"
    property bool sessionControls: false
    property bool sessionFullscreen: false
    property bool compactSessionTile: false
    readonly property bool shellWindowed: sessionControls ? !sessionFullscreen
        : typeof WindowMode === "undefined" || WindowMode.shellWindowed
    spacing: compactSessionTile ? 0 : 4
    height: 44

    Colosseum.KeyboardSpatialNavigator { id: navigation; root: controls }
    Keys.onPressed: function(event) { navigation.handle(event) }

    component WindowAction: Item {
        property url source
        property string label
        signal triggered()
        width: 44
        height: width
        Image {
            anchors.centerIn: parent
            width: controls.compactSessionTile ? 18 : 22; height: width
            source: parent.source
            sourceSize: Qt.size(width, height)
            fillMode: Image.PreserveAspectFit
            opacity: action.interactionActive ? 1 : 0.72
        }
        Colosseum.KeyboardAction {
            id: action
            anchors.fill: parent
            accessibleName: parent.label
            focusRadius: 6
            onTriggered: parent.triggered()
        }
        ToolTip.visible: action.hovered
        ToolTip.delay: 600
        ToolTip.text: label
    }
    WindowAction {
        objectName: controls.namePrefix + "Minimize"
        source: Qt.resolvedUrl(controls.compactSessionTile ? "../../assets/icons/feria/session-minimize.svg" : "../../assets/icons/minimize.svg")
        label: controls.sessionControls ? "Minimize session" : "Minimize"
        onTriggered: controls.controller.minimizeClicked()
    }
    WindowAction {
        objectName: controls.namePrefix + "Fullscreen"
        source: Qt.resolvedUrl(controls.compactSessionTile
                              ? (controls.shellWindowed ? "../../assets/icons/feria/session-maximize.svg" : "../../assets/icons/feria/session-restore.svg")
                              : (controls.shellWindowed ? "../../assets/icons/fullscreen.svg" : "../../assets/icons/fullscreen-exit.svg"))
        label: controls.sessionControls
            ? (controls.shellWindowed ? "Maximize app" : "Restore app")
            : (controls.shellWindowed ? "Enter fullscreen" : "Exit fullscreen")
        onTriggered: controls.controller.fullscreenClicked()
    }
    WindowAction {
        objectName: controls.namePrefix + "Close"
        source: Qt.resolvedUrl(controls.compactSessionTile ? "../../assets/icons/feria/session-close.svg" : controls.sessionControls ? "../../assets/icons/reader2/close.svg"
                              : "../../assets/icons/power.svg")
        label: controls.sessionControls ? "Close session" : "Quit Colosseum"
        onTriggered: controls.controller.powerClicked()
    }
}
