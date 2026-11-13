import QtQuick
import QtQuick.Controls
import ".."

// Welcome-only compact choice. The shared AccountChoice intentionally elides titles.
Control {
    id: control

    property string title: ""
    property string detail: ""
    property bool compact: false
    signal chosen()

    Accessible.role: Accessible.Button
    Accessible.name: control.title
    Accessible.description: control.detail
    Accessible.onPressAction: if (control.enabled) control.chosen()

    implicitHeight: Math.max(control.compact ? adaptive.targetSize(adaptive.welcomeChoiceHeight) : 76,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 18
    rightPadding: 18
    topPadding: control.compact ? adaptive.welcomeChoicePadding : 16
    bottomPadding: control.compact ? adaptive.welcomeChoicePadding : 16
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Theme { id: theme }
    AdaptiveLayout { id: adaptive }

    contentItem: Column {
        spacing: control.compact ? adaptive.welcomeSmallGap : 5

        Text {
            width: parent.width
            text: control.title
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: control.compact ? adaptive.welcomeChoiceTitleSize : 15
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: control.detail
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: control.compact ? adaptive.welcomeChoiceDetailSize : 12
            wrapMode: Text.WordWrap
        }
    }

    background: Rectangle {
        radius: 14
        color: control.hovered
            ? Qt.rgba(1, 1, 1, 0.09)
            : Qt.rgba(1, 1, 1, 0.045)
        border.width: 1
        border.color: control.activeFocus ? theme.gold : theme.edge
    }

    TapHandler {
        enabled: control.enabled
        onTapped: control.chosen()
    }

    Keys.onReturnPressed: chosen()
    Keys.onEnterPressed: chosen()
    Keys.onSpacePressed: chosen()
}
