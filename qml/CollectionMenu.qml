import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import "TouchMetrics.js" as TouchMetrics
import "PhoneOverlayRegistry.js" as Overlays

Rectangle {
    id: menu
    property bool touch: Qt.platform.os === "android" || !!(Window.window && Window.window.androidHost)
    property var items: []
    property rect viewport: Qt.rect(0, 0, parent ? parent.width : 0, parent ? parent.height : 0)
    property real anchorX: 0
    property real anchorY: 0
    property string itemPrefix: ""
    property string scrollObjectName: ""
    property bool centeredLabels: false
    property real labelLeft: 11
    property real rowSpacing: 0
    property color panelColor: Qt.rgba(0.043, 0.047, 0.075, 0.98)
    property color warningColor: "#e08a8a"
    property int currentIndex: 0
    property var focusReturnItem: null
    readonly property var actions: items.filter(item => !item.separator)
    readonly property real rowHeight: touch ? TouchMetrics.minimumTargetSize : 38
    signal picked(string action)
    signal closeRequested(bool restoreFocus)
    function close() { closeRequested(focusReturnItem !== null) }

    width: Math.min(200, Math.max(0, viewport.width - 16))
    implicitHeight: column.implicitHeight + 12
    height: touch ? Math.min(implicitHeight, Math.max(0, viewport.height - 16)) : implicitHeight
    x: Math.max(viewport.x + 8, Math.min(anchorX - width, viewport.x + viewport.width - width - 8))
    y: Math.max(viewport.y + 8, Math.min(anchorY, viewport.y + viewport.height - height - 8))
    radius: 13
    color: touch ? Qt.rgba(panelColor.r, panelColor.g, panelColor.b, 1) : panelColor
    border.width: 1
    border.color: theme.edge
    focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
    onVisibleChanged: {
        if (visible) focusReturnItem = Overlays.claim(menu, Window.window)
        else Overlays.release(menu)
    }
    Component.onDestruction: Overlays.release(menu)
    Keys.onPressed: function(event) {
        const n = actions.length
        if (!n) return
        let delta = 0
        if (event.key === Qt.Key_Down || (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier))) delta = 1
        else if (event.key === Qt.Key_Up || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) delta = -1
        if (delta) currentIndex = (currentIndex + delta + n) % n
        else if (event.key === Qt.Key_Home) currentIndex = 0
        else if (event.key === Qt.Key_End) currentIndex = n - 1
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) picked(actions[currentIndex].id)
        else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) closeRequested(true)
        else return
        event.accepted = true
    }
    Theme { id: theme }
    MouseArea { anchors.fill: parent }
    Flickable {
        objectName: menu.scrollObjectName
        anchors.fill: parent
        anchors.margins: 6
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        interactive: menu.touch && contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            width: parent.width
            spacing: menu.rowSpacing
            Repeater {
                model: menu.items
                delegate: Item {
                    id: row
                    required property var modelData
                    readonly property string actionId: modelData.id || ""
                    readonly property string label: modelData.label || ""
                    objectName: menu.itemPrefix + (modelData.objectId || modelData.id || "separator")
                    width: column.width
                    height: modelData.separator ? 1 : menu.rowHeight
                    readonly property bool selected: !!(menu.activeFocus && !modelData.separator
                        && menu.actions[menu.currentIndex] && menu.actions[menu.currentIndex].id === modelData.id
                        )
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: row.modelData.separator ? 0 : (menu.centeredLabels ? 0 : 2)
                        radius: menu.centeredLabels ? 8 : 9
                        color: row.modelData.separator ? theme.edge
                            : input.containsMouse || row.selected ? Qt.rgba(1,1,1,menu.centeredLabels ? 0.08 : 0.09) : "transparent"
                        border.width: row.selected ? 2 : 0
                        border.color: theme.gold
                        visible: !row.modelData.separator
                    }
                    Rectangle { visible: !!row.modelData.separator; width: parent.width - 16; x:8; height:1; color:theme.edge }
                    Row {
                        visible: !row.modelData.separator
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: menu.centeredLabels ? parent.horizontalCenter : undefined
                        x: menu.centeredLabels ? 0 : menu.labelLeft
                        spacing: row.modelData.icon ? 10 : 0
                        Item {
                            width: row.modelData.icon ? 14 : 0
                            height: 14
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !!row.modelData.icon
                            Image { id: glyph; anchors.fill:parent; source: row.modelData.icon ? Qt.resolvedUrl("../assets/icons/lucide/"+row.modelData.icon+".svg") : ""; sourceSize:Qt.size(28,28); visible:false }
                            MultiEffect { anchors.fill:parent; source:glyph; colorization:1; colorizationColor:row.modelData.warn ? menu.warningColor : theme.inkDim }
                        }
                        Text {
                            text: row.modelData.label || ""
                            color: row.modelData.warn ? menu.warningColor : theme.ink
                            font.family: theme.ui
                            font.pixelSize: theme.fontSize(menu.centeredLabels ? 14 : 13)
                        }
                    }
                    MouseArea {
                        id: input
                        anchors.fill: parent
                        enabled: !row.modelData.separator
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: menu.picked(row.modelData.id)
                    }
                }
            }
        }
    }
}
