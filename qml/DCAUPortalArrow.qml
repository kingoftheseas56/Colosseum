import QtQuick
import QtQuick.Controls

FocusScope {
    id: root
    property string glyph: "›"
    property string tooltipText: ""
    readonly property bool androidTablet: Qt.platform.os === "android" && parent && parent.width >= 600
    ToolTip.visible: Qt.platform.os !== "android" && root.tooltipText.length > 0 && hover.hovered
    ToolTip.text: root.tooltipText
    ToolTip.delay: 350
    signal triggered()

    width: Qt.platform.os === "android" ? (androidTablet ? 176 : 132) : 44
    height: Qt.platform.os === "android" && !androidTablet ? 70 : 44
    activeFocusOnTab: true

    Theme { id: theme }

    Rectangle {
        width: 44
        height: 44
        anchors.top: parent.top
        anchors.left: root.androidTablet ? parent.left : undefined
        anchors.horizontalCenter: root.androidTablet ? undefined : parent.horizontalCenter
        radius: 22
        color: Qt.rgba(8 / 255, 10 / 255, 14 / 255, 0.76)
        border.width: 1
        border.color: hover.hovered || root.activeFocus
                      ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.62)
                      : Qt.rgba(1, 1, 1, 0.14)

        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -1
            text: root.glyph
            color: hover.hovered || root.activeFocus ? theme.gold : "#d9d9de"
            font.family: theme.ui
            font.pixelSize: 25
        }
    }

    // Slice 0 owns the Android caption. Keep this input wrapper non-interactive;
    // the existing TapHandler and focus keys still own both portal directions.
    KeyboardAction {
        anchors.top: parent.top
        anchors.left: root.androidTablet ? parent.left : undefined
        anchors.horizontalCenter: root.androidTablet ? undefined : parent.horizontalCenter
        width: root.androidTablet ? 44 : root.width
        height: 44
        pointerEnabled: false
        focusEnabled: false
        focusPolicy: Qt.NoFocus
        showFocusFrame: false
        accessibleName: root.tooltipText
        tooltipText: root.tooltipText
        androidTooltipPolicy: "caption"
        androidCaptionPlacement: root.androidTablet ? "right" : "below"
        onTriggered: root.triggered()
    }

    HoverHandler { id: hover }
    TapHandler {
        onTapped: {
            root.forceActiveFocus()
            root.triggered()
        }
    }

    Keys.onReturnPressed: root.triggered()
    Keys.onEnterPressed: root.triggered()
    Keys.onSpacePressed: root.triggered()
}
