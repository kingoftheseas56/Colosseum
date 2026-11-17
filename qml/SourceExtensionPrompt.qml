import QtQuick

Column {
    id: prompt
    property string message: ""
    property bool actionVisible: true
    property bool androidHost: Qt.platform.os === "android"
    property bool phoneLayout: false
    property string namePrefix: "sourcePrompt"
    property color messageColor: theme.inkDim
    signal openExtensionsRequested()

    spacing: 12
    height: implicitHeight
    Theme { id: theme }

    Text {
        objectName: prompt.namePrefix + "EmptyText"
        width: parent.width
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: prompt.message
        color: prompt.messageColor
        font.family: theme.ui
        font.pixelSize: prompt.phoneLayout ? 15 : 16
    }
    Rectangle {
        id: action
        objectName: prompt.namePrefix + "ExtensionsRoute"
        visible: prompt.actionVisible
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(112, label.implicitWidth + 32)
        height: prompt.phoneLayout ? 48 : 44
        radius: height / 2
        color: keyboard.interactionActive ? theme.glassHi : "transparent"
        border.width: 1
        border.color: theme.gold
        Text {
            id: label
            objectName: prompt.namePrefix + "ExtensionsLabel"
            anchors.centerIn: parent
            text: prompt.androidHost ? qsTr("Extensions") : qsTr("Find sources by adding extensions")
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        KeyboardAction {
            id: keyboard
            objectName: prompt.namePrefix + "ExtensionsAction"
            anchors.fill: parent
            visible: action.visible
            accessibleName: label.text
            focusRadius: action.radius
            onTriggered: prompt.openExtensionsRequested()
        }
    }
}
