import QtQuick
import QtQuick.Controls
import ".." as Colosseum

Row {
    id: controls
    required property var controller
    property string namePrefix: "feria"
    readonly property bool shellWindowed: typeof WindowMode === "undefined" || WindowMode.shellWindowed
    spacing: 4
    height: 44

    Colosseum.KeyboardSpatialNavigator { id: navigation; root: controls }
    Keys.onPressed: function(event) { navigation.handle(event) }

    component WindowAction: Item {
        property url source
        property string label
        signal triggered()
        width: 44
        height: 44
        Image {
            anchors.centerIn: parent
            width: 22; height: 22
            source: parent.source
            sourceSize: Qt.size(22, 22)
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
        source: Qt.resolvedUrl("../../assets/icons/minimize.svg")
        label: "Minimize"
        onTriggered: controls.controller.minimizeClicked()
    }
    WindowAction {
        objectName: controls.namePrefix + "Fullscreen"
        source: Qt.resolvedUrl(controls.shellWindowed
                              ? "../../assets/icons/fullscreen.svg"
                              : "../../assets/icons/fullscreen-exit.svg")
        label: controls.shellWindowed ? "Enter fullscreen" : "Exit fullscreen"
        onTriggered: controls.controller.fullscreenClicked()
    }
    WindowAction {
        objectName: controls.namePrefix + "Close"
        source: Qt.resolvedUrl("../../assets/icons/power.svg")
        label: "Quit Colosseum"
        onTriggered: controls.controller.powerClicked()
    }
}
