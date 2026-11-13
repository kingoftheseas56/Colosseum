// Shared tap-visible More menu for contextual Android actions (O1/O2/R1/R2/P1/V1/V2/D-W*).
// entries are { id, label, iconName | iconSource, enabled, busy, selected, danger }.
// Bind the array to live state and handle actionTriggered(id) with the existing action handler.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "collections" as Collections

Item {
    id: root
    property var actions: []
    property string label: qsTr("More")
    property bool showLabel: true
    property int menuWidth: 240
    readonly property bool opened: menuPopup.visible
    // Capture the current scene coordinate on open. mapToItem() alone does not
    // establish a QML binding dependency when a scrolling/positioned caller moves.
    property real _sceneX: 0
    property real _sceneY: 0
    readonly property real _spaceBelow: root.Window.window
        ? Math.max(80, root.Window.window.height - _sceneY - root.height - 8) : 380
    readonly property real _spaceAbove: root.Window.window ? Math.max(80, _sceneY - 8) : 380

    signal actionTriggered(string actionId)
    function open() {
        const scenePoint = root.Window.window ? root.mapToItem(null, 0, 0)
                                             : Qt.point(root.x, root.y)
        root._sceneX = scenePoint.x
        root._sceneY = scenePoint.y
        menuPopup.open()
    }
    function close() { menuPopup.close() }
    function rowAt(index) { return menuRepeater.itemAt(index) }

    implicitWidth: showLabel ? 86 : 40
    implicitHeight: 40

    Theme { id: theme }
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: triggerAction.interactionActive || root.opened
            ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
    }
    Row {
        anchors.centerIn: parent
        spacing: 9
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Repeater {
                model: 3
                Rectangle { width: 4; height: 4; radius: 2; color: theme.ink; opacity: 0.85 }
            }
        }
        Text {
            visible: root.showLabel
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: theme.fontSize(13)
        }
    }
    KeyboardAction {
        id: triggerAction
        objectName: "actionOverflowTrigger"
        anchors.fill: parent
        accessibleName: root.label
        tooltipText: root.showLabel ? "" : root.label
        androidTooltipPolicy: "drop"
        focusRadius: root.height / 2
        onTriggered: root.opened ? root.close() : root.open()
    }

    Popup {
        id: menuPopup
        objectName: "actionOverflowPanel"
        parent: root
        z: 100
        readonly property bool above: root._spaceBelow < Math.min(menuColumn.implicitHeight + 16, 240)
            && root._spaceAbove > root._spaceBelow
        x: root.Window.window
            ? Math.max(-root._sceneX + 8,
                       Math.min(root.width - width,
                                root.Window.window.width - root._sceneX - width - 8))
            : root.width - width
        y: above ? -height - 4 : root.height + 4
        width: root.menuWidth
        height: Math.min(menuColumn.implicitHeight + 16, 400,
                         above ? root._spaceAbove : root._spaceBelow)
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 14
            color: Qt.rgba(0.043, 0.051, 0.075, 0.98)
            border.width: 1
            border.color: theme.edge
        }
        contentItem: Flickable {
            clip: true
            contentHeight: menuColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: menuColumn
                width: parent.width
                Repeater {
                    id: menuRepeater
                    model: root.actions
                    delegate: Item {
                        id: row
                        property var entry: modelData
                        readonly property string actionId: entry && entry.id !== undefined
                            ? String(entry.id) : String(index)
                        readonly property string caption: entry && entry.label ? String(entry.label) : ""
                        readonly property string iconName: entry && entry.iconName ? String(entry.iconName) : "more"
                        readonly property url iconSource: entry && entry.iconSource ? entry.iconSource : ""
                        readonly property bool available: !entry || entry.enabled === undefined || !!entry.enabled
                        readonly property bool busy: !!(entry && entry.busy)
                        readonly property bool selected: !!(entry && entry.selected)
                        readonly property bool danger: !!(entry && entry.danger)
                        readonly property Item focusItem: rowAction
                        width: menuPopup.availableWidth
                        height: 44
                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            anchors.rightMargin: 2
                            radius: 9
                            color: row.selected ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.12)
                                : rowAction.interactionActive ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
                        }
                        Collections.CollectionIcon {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18; height: 18
                            visible: row.iconSource.toString().length === 0
                            name: row.iconName
                            strokeColor: !row.available || row.busy ? theme.inkDimmer
                                : row.selected ? theme.gold : theme.inkDim
                        }
                        Image {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18; height: 18
                            visible: row.iconSource.toString().length > 0
                            source: row.iconSource
                            fillMode: Image.PreserveAspectFit
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 42
                            anchors.right: stateMark.left
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.caption
                            textFormat: Text.PlainText
                            color: !row.available || row.busy ? theme.inkDimmer
                                : row.danger ? "#f0b4a6" : row.selected ? theme.gold : theme.ink
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(14)
                            elide: Text.ElideRight
                            opacity: !row.available || row.busy ? 0.65 : 1
                        }
                        Text {
                            id: stateMark
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            text: row.busy ? "…" : row.selected ? "✓" : ""
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(14)
                            horizontalAlignment: Text.AlignHCenter
                        }
                        KeyboardAction {
                            id: rowAction
                            anchors.fill: parent
                            enabled: row.available && !row.busy
                            accessibleName: row.caption
                            accessibleDescription: row.busy ? qsTr("Busy") : row.selected ? qsTr("Selected") : ""
                            androidTooltipPolicy: "drop"
                            focusRadius: 9
                            onTriggered: {
                                const actionId = row.actionId
                                menuPopup.close()
                                root.actionTriggered(actionId)
                            }
                            Keys.onEscapePressed: menuPopup.close()
                        }
                    }
                }
            }
        }
    }
}
