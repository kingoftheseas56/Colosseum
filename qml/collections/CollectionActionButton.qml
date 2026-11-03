import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Button {
    id: root
    property string iconName: ""
    property string emphasis: "neutral"
    property bool selected: false
    property bool compact: false
    property bool destructive: false
    readonly property color foreground: !enabled ? theme.inkDimmer : destructive ? "#efb6a9" : emphasis === "primary" ? "#17140a" : selected ? theme.gold : theme.ink
    Theme { id: theme }
    implicitHeight: compact ? 36 : 44
    implicitWidth: Math.max(compact ? 36 : 44, contentItem.implicitWidth + (compact ? 24 : 32))
    leftPadding: compact ? 12 : 16
    rightPadding: leftPadding
    topPadding: 8
    bottomPadding: 8
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    font.family: theme.ui
    font.pixelSize: 14
    font.weight: Font.DemiBold
    background: Rectangle {
        radius: 10
        color: root.destructive ? (root.down ? "#45b95748" : root.hovered ? "#30b95748" : "#1ab95748") : root.emphasis === "primary" ? (root.down ? "#d9ac37" : root.hovered ? "#f7d67c" : theme.gold)
             : root.selected ? "#24f0c44a" : root.down ? "#24ffffff" : root.hovered ? "#18ffffff"
             : root.emphasis === "ghost" ? "transparent" : "#0dffffff"
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? theme.gold : root.selected ? "#60f0c44a"
                    : root.emphasis === "ghost" || root.emphasis === "primary" ? "transparent" : theme.edge
        opacity: root.enabled ? 1 : 0.45
    }
    contentItem: RowLayout {
        spacing: root.iconName && root.text ? 8 : 0
        CollectionIcon {
            visible: root.iconName.length > 0
            name: root.iconName
            strokeColor: root.foreground
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
        Text {
            visible: root.text.length > 0
            text: root.text
            textFormat: Text.PlainText
            font: root.font
            color: root.foreground
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
    HoverHandler { cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    Keys.onReturnPressed: if (root.enabled) root.clicked()
    Keys.onEnterPressed: if (root.enabled) root.clicked()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Select) {
            if (root.enabled) root.clicked()
            event.accepted = true
        }
    }

}
