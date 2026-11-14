import QtQuick
import QtQuick.Controls

FocusScope {
    id: root
    property string glyph: "›"
    property string tooltipText: ""
    property bool longPressTooltipEnabled: Qt.platform.os === "android"
    TouchTooltip {
        id: tooltipState
        owner: root
        longPressTooltipEnabled: root.longPressTooltipEnabled
    }
    ToolTip.visible: root.tooltipText.length > 0
        && (root.longPressTooltipEnabled ? tooltipState.active : hover.hovered)
    ToolTip.text: root.tooltipText
    ToolTip.delay: root.longPressTooltipEnabled ? 0 : 350
    signal triggered()

    width: 44
    height: 44
    activeFocusOnTab: true

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
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

    HoverHandler { id: hover }
    TapHandler {
        id: pointerTap
        longPressThreshold: root.longPressTooltipEnabled
            ? tooltipState.holdThreshold : undefined
        onPressedChanged: {
            if (pressed)
                tooltipState.pressStarted()
            else
                tooltipState.pressReleased()
        }
        onLongPressed: tooltipState.longPressRecognized()
        onCanceled: tooltipState.pressCanceled()
        onTapped: {
            root.forceActiveFocus()
            root.triggered()
        }
    }

    Keys.onReturnPressed: root.triggered()
    Keys.onEnterPressed: root.triggered()
    Keys.onSpacePressed: root.triggered()
}
