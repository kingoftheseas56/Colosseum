// RowIndexSidebar — a compact table of contents for shelf pages.
// The host supplies page-ordered descriptors: { key, title, target }. Targets are the
// real row items, so activation can use the page's existing parking/scrolling path.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: rail

    required property Flickable pageFlick
    property Item flowHost: null
    property Item backdrop: null
    property var rows: []
    property string worldName: ""
    property string automationPrefix: "rowIndex"
    property real currentOffset: 78
    property real currentEpsilon: 4
    property real fixedTopInset: 0
    property var _rowActions: []
    readonly property var visibleRows: {
        var out = []
        for (var i = 0; i < rail.rows.length; i++) {
            var descriptor = rail.rows[i]
            var target = descriptor ? descriptor.target : null
            if (target && target.visible && target.height > 0)
                out.push(descriptor)
        }
        return out
    }

    signal rowRequested(var target)

    objectName: rail.automationPrefix + "Sidebar"

    Theme { id: theme }

    property bool collapsed: true
    readonly property int openWidth: 240
    readonly property int closedWidth: 52
    readonly property real contentLeft: width + 28
    readonly property int _shellPadRight: collapsed ? 4 : 16
    readonly property int _listPadRight: collapsed ? 0 : 16
    readonly property bool _labelsShown: width > openWidth - 24
    readonly property real pinLine: 12
    readonly property real bottomGap: 92
    readonly property bool _flow: flowHost !== null
    // A flow rail can be reparented outside the page to avoid its Column layout.
    // Keep the page's visibility even though it is no longer our visual parent.
    visible: !rail._flow || rail.flowHost.visible
    readonly property real _hostTop: {
        if (!rail._flow || !rail.pageFlick)
            return 0
        rail.pageFlick.contentY
        rail.pageFlick.contentHeight
        rail.flowHost.y
        return rail.flowHost.mapToItem(rail.pageFlick.contentItem, 0, 0).y
                - rail.pageFlick.contentY
    }
    readonly property real pinnedHeight: rail.pageFlick
        ? Math.max(160, rail.pageFlick.height - pinLine - bottomGap) : 0
    readonly property real _wantedY: rail._flow ? Math.max(0, pinLine - _hostTop) : 0
    readonly property real _flowBaseY: {
        if (!rail._flow || !rail.parent)
            return 0
        // mapToItem itself has no geometry-change notification. Track the host
        // position when the surrounding Column lays out after a tab switch.
        rail.flowHost.y
        return rail.flowHost.mapToItem(rail.parent, 0, 0).y
    }
    readonly property string automationRows: {
        var names = []
        for (var i = 0; i < rail.visibleRows.length; i++)
            names.push(String(rail.visibleRows[i].title || ""))
        return names.join("\n")
    }
    readonly property int currentIndex: {
        if (!rail.pageFlick || rail.visibleRows.length === 0)
            return -1
        var maxY = Math.max(0, rail.pageFlick.contentHeight - rail.pageFlick.height)
        if (maxY > 0.5
                && rail.pageFlick.contentY >= maxY - Math.max(0.5, rail.currentOffset))
            return rail.visibleRows.length - 1
        var line = rail.pageFlick.contentY + rail.currentOffset + rail.currentEpsilon
        var current = 0
        for (var i = 0; i < rail.visibleRows.length; i++) {
            var target = rail.visibleRows[i].target
            if (!target)
                continue
            target.y
            target.height
            var top = target.mapToItem(rail.pageFlick.contentItem, 0, 0).y
            if (top <= line)
                current = i
            else
                break
        }
        return current
    }
    readonly property string automationCurrentRow: currentIndex >= 0
        && currentIndex < rail.visibleRows.length
        ? String(rail.visibleRows[currentIndex].title || "") : ""
    readonly property bool automationSidebarPinned: rail._flow
        && rail._wantedY > 0.5
        && Math.abs(rail.y - (rail._flowBaseY + rail._wantedY)) < 0.5

    function _sanitise(key) { return String(key).replace(/[^A-Za-z0-9]/g, "_") }
    function _focusRow(index) {
        if (index < 0 || index >= rowRepeater.count)
            return false
        var action = rail._rowActions[index]
        if (!action)
            return false
        action.forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    width: collapsed ? closedWidth : openWidth
    height: rail._flow
        ? Math.min(rail.pinnedHeight, rail.flowHost ? rail.flowHost.height : 0)
        : Math.max(0, (parent ? parent.height : 0) - rail.fixedTopInset)
    y: rail._flow && rail.flowHost
        ? rail._flowBaseY + Math.max(0, Math.min(rail._wantedY, rail.flowHost.height - height))
        : rail.fixedTopInset

    Behavior on width {
        NumberAnimation {
            duration: 240
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.2, 0.7, 0.2, 1, 1, 1]
        }
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => {
        if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
            return
        var focused = -1
        for (var i = 0; i < rail._rowActions.length; i++) {
            var action = rail._rowActions[i]
            if (action && action.activeFocus) {
                focused = i
                break
            }
        }
        if (focused >= 0 && rail._focusRow(focused + (event.key === Qt.Key_Down ? 1 : -1)))
            event.accepted = true
    }

    Rectangle {
        x: rail.width - 1
        y: 2
        width: 1
        height: Math.max(0, rail.height - 4)
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.10; color: Qt.rgba(1, 1, 1, 0.18) }
            GradientStop { position: 0.86; color: Qt.rgba(1, 1, 1, 0.10) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Item {
        id: brand
        x: 12
        y: 22
        width: rail.openWidth - 44
        height: rail.collapsed ? 38 : brandColumn.implicitHeight + 20
        clip: true
        opacity: rail.collapsed ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Column {
            id: brandColumn
            y: 2
            spacing: 5
            Text {
                text: "WORLD"
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 10
                font.letterSpacing: 2.8
            }
            Text {
                text: rail.worldName
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 27
                font.letterSpacing: -0.54
            }
        }
    }

    Flickable {
        id: list
        objectName: rail.automationPrefix + "List"
        x: 0
        y: brand.y + brand.height
        width: rail.width - rail._shellPadRight
        height: Math.max(0, rail.height - y - 18)
        contentHeight: rowColumn.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: list; visible: list.interactive && !rail.collapsed }

        Column {
            id: rowColumn
            width: list.width - rail._listPadRight
            spacing: 4

            Repeater {
                id: rowRepeater
                model: rail.visibleRows

                delegate: Item {
                    id: rowItem
                    required property var modelData
                    required property int index
                    readonly property bool current: rowItem.index === rail.currentIndex
                    readonly property alias action: rowAction
                    objectName: rail.automationPrefix + "Row_" + rail._sanitise(rowItem.modelData.key)
                    width: rail.collapsed ? 44 : rowColumn.width
                    height: rail.collapsed ? 46 : Math.max(46, rowLabel.implicitHeight + 20)

                    Rectangle {
                        anchors.fill: parent
                        radius: 12
                        color: rowItem.current ? Qt.rgba(1, 1, 1, 0.09)
                             : rowAction.interactionActive ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                        Behavior on color { ColorAnimation { duration: 160 } }
                    }
                    Rectangle {
                        visible: rowItem.current
                        x: (rail.collapsed ? -2 : -1) - 6
                        y: 4
                        width: 15
                        height: parent.height - 8
                        radius: 7
                        color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.14)
                    }
                    Rectangle {
                        visible: rowItem.current
                        x: rail.collapsed ? -2 : -1
                        y: 10
                        width: 3
                        height: parent.height - 20
                        radius: 4
                        color: theme.gold
                    }
                    Image {
                        objectName: rowItem.objectName + "Icon"
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        sourceSize.width: 40
                        sourceSize.height: 40
                        source: {
                            const title = String(rowItem.modelData.title || "").toLowerCase()
                            let paths = '<path d="M4 6h16M4 12h16M4 18h16"/>'
                            if (/collection|library|shelf|shelves/.test(title))
                                paths = '<path d="M4 4v16h5V4zM9 6h5v14H9zM16 4l4 15-4 1-4-15z"/>'
                            else if (/top|rated|popular|best/.test(title))
                                paths = '<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>'
                            else if (/recent|new|available|release|continue/.test(title))
                                paths = '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>'
                            else if (/explore|discover|genre/.test(title))
                                paths = '<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5z"/>'
                            else if (/edition|omnibus|omnibuses|complete|run/.test(title))
                                paths = '<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M9 7h6M9 11h6M9 15h4"/>'
                            const color = rowItem.current ? theme.gold : theme.inkDim
                            return "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + color + '" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">' + paths + '</svg>')
                        }
                    }
                    Text {
                        id: rowLabel
                        objectName: rowItem.objectName + "Label"
                        x: 42
                        anchors.verticalCenter: parent.verticalCenter
                        visible: rail._labelsShown
                        width: Math.max(1, rowColumn.width - x - 12)
                        text: String(rowItem.modelData.title || "")
                        wrapMode: Text.Wrap
                        color: (rowItem.current || rowAction.interactionActive) ? theme.ink : theme.inkDim
                        Behavior on color { ColorAnimation { duration: 160 } }
                        font.family: theme.ui
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    KeyboardAction {
                        id: rowAction
                        objectName: rowItem.objectName + "Action"
                        anchors.fill: parent
                        accessibleName: "Row " + String(rowItem.modelData.title || "row")
                        focusRadius: 12
                        focusOnPointer: false
                        onTriggered: rail.rowRequested(rowItem.modelData.target)
                        ToolTip.visible: rail.collapsed && rowAction.interactionActive
                        ToolTip.delay: 500
                        ToolTip.text: String(rowItem.modelData.title || "")
                    }
                    Component.onCompleted: rail._rowActions[rowItem.index] = rowAction
                    Component.onDestruction: {
                        if (rail._rowActions[rowItem.index] === rowAction)
                            rail._rowActions[rowItem.index] = null
                    }
                }
            }
        }
    }
    ScrollGlide { flick: list }

    Item {
        id: toggle
        objectName: rail.automationPrefix + "Collapse"
        x: rail.width - 14
        y: 18
        width: 28
        height: 28
        opacity: toggleAction.interactionActive ? 1 : 0.74

        Glass {
            anchors.fill: parent
            backdrop: rail.backdrop
            radius: 14
            tint: toggleAction.interactionActive ? 0.10 : 0.0
            scrim: toggleAction.interactionActive ? 0.0 : 0.46
            blurMax: 16
            edge: Qt.rgba(1, 1, 1, 0.16)
            track: rail.pageFlick ? rail.pageFlick.contentY : 0
        }
        Item {
            anchors.centerIn: parent
            width: 15
            height: 15
            rotation: rail.collapsed ? 180 : 0
            Behavior on rotation {
                NumberAnimation {
                    duration: 240
                    easing.type: Easing.Bezier
                    easing.bezierCurve: [0.2, 0.7, 0.2, 1, 1, 1]
                }
            }
            Rectangle {
                x: 5; y: 6.6; width: 6; height: 1.8; radius: 0.9
                rotation: -45; transformOrigin: Item.Left
                color: toggleAction.interactionActive ? theme.ink : theme.inkDim
                antialiasing: true
            }
            Rectangle {
                x: 5; y: 6.6; width: 6; height: 1.8; radius: 0.9
                rotation: 45; transformOrigin: Item.Left
                color: toggleAction.interactionActive ? theme.ink : theme.inkDim
                antialiasing: true
            }
        }
        KeyboardAction {
            id: toggleAction
            objectName: rail.automationPrefix + "CollapseAction"
            anchors.fill: parent
            accessibleName: rail.collapsed ? "Expand row index" : "Retract row index"
            focusRadius: 14
            focusOnPointer: false
            onTriggered: rail.collapsed = !rail.collapsed
        }
    }
}
