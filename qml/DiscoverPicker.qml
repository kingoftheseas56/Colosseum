// DiscoverPicker — one Discover selector pill: label + current value, popup option
// list. House grammar: glass pill, gold active option, click-swallower popup body
// (floating panel rule). Popup closes on outside tap or Esc.
// Options may carry section HEADERS ({header:"…"}) — the catalog picker splits its
// rows into "Cinemeta" / "Your addons" (addon attribution is the parity anatomy).
// A row option is {key, text, sub} — sub is the owning addon, right-aligned.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQml.Models

Item {
    id: picker
    property string label: ""                 // dim placeholder ("Genre") — shown when no real value
    property var options: []                  // [{key,text,sub} | {header:"…"}]
    property string currentKey: ""
    property bool open: false
    property bool wrapNavigation: false        // opt-in cyclic option navigation
    property bool openOnDown: false            // opt-in combobox-style trigger
    property bool clearable: true             // an active filter shows an ✕ that resets it
    property Item focusReturn: null
    // Lanista identity stem, world-namespaced by the host ("" = unnamed): <stem>Pill and
    // <stem>Option_<index>. Discover sidebar plan Slice 1 regression; the picker goes in Slice 3.
    property string automationName: ""
    signal picked(string key)
    signal cleared()

    // current = the selected ROW option (headers and the empty "All" key don't count as a value)
    readonly property var current: {
        for (var i = 0; i < options.length; i++)
            if (options[i].header === undefined && options[i].key === currentKey) return options[i];
        return null;
    }
    readonly property bool hasValue: !!current && current.key !== ""

    implicitWidth: Math.max(150, pillRow.implicitWidth + 38)
    implicitHeight: 40

    function selectable(index) {
        return index >= 0 && index < picker.options.length
            && picker.options[index].header === undefined
    }
    function nextSelectable(start, delta) {
        var i = start
        while (i >= 0 && i < picker.options.length) {
            if (picker.selectable(i)) return i
            i += delta
        }
        return -1
    }
    function selectedOptionIndex() {
        for (var i = 0; i < picker.options.length; ++i)
            if (picker.selectable(i) && picker.options[i].key === picker.currentKey) return i
        return picker.nextSelectable(0, 1)
    }
    onOpenChanged: {
        if (open) {
            pop.positionAtPill()
            pop.open()
            list.currentIndex = selectedOptionIndex()
            if (list.currentIndex >= 0) list.positionViewAtIndex(list.currentIndex, ListView.Contain)
            Qt.callLater(function() { if (picker.open) list.forceActiveFocus(Qt.PopupFocusReason) })
        } else {
            pop.close()
        }
    }
    onVisibleChanged: if (!visible) closePopup(false)
    // Flickable scrolling changes an ancestor contentItem's position. Dismiss rather
    // than leave an overlay detached from its trigger, without polling the scene.
    Instantiator {
        model: {
            var ancestors = []
            var item = picker
            while (item) { ancestors.push(item); item = item.parent }
            return ancestors
        }
        delegate: Connections {
            required property var modelData
            target: modelData
            function onXChanged() { if (picker.open) picker.closePopup(false) }
            function onYChanged() { if (picker.open) picker.closePopup(false) }
        }
    }
    Connections {
        target: picker.Window.window
        function onWidthChanged() { if (picker.open) picker.closePopup(true) }
        function onHeightChanged() { if (picker.open) picker.closePopup(true) }
    }

    function openPopup(invoker) {
        picker.focusReturn = invoker || pillAction
        picker.open = true
    }
    function closePopup(restoreFocus) {
        const target = picker.focusReturn
        picker.open = false
        picker.focusReturn = null
        if (restoreFocus !== false && target)
            Qt.callLater(function() { if (target.visible && target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function togglePopup(invoker) {
        if (picker.open) picker.closePopup(true)
        else picker.openPopup(invoker)
    }
    function choose(index) {
        if (!picker.selectable(index)) return
        const key = picker.options[index].key
        const target = picker.focusReturn || pillAction
        picker.open = false
        picker.focusReturn = null
        picker.picked(key)
        Qt.callLater(function() { if (target.visible && target.enabled) target.forceActiveFocus(Qt.PopupFocusReason) })
    }
    function clearSelection() {
        picker.open = false
        picker.focusReturn = null
        picker.cleared()
        Qt.callLater(function() { pillAction.forceActiveFocus(Qt.PopupFocusReason) })
    }

    Theme { id: theme }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: 13
        // dark tint, not a white wash: a near-clear pill let warm/bright wallpapers through and
        // the placeholder sank into them (2026-09-29, Hemanth's sunset wallpaper)
        color: ma.containsMouse || picker.open ? Qt.rgba(0, 0, 0, 0.46) : Qt.rgba(0, 0, 0, 0.34)
        border.width: 1
        border.color: picker.open ? theme.gold
                    : picker.hasValue ? Qt.rgba(240/255, 196/255, 74/255, 0.55)
                    : theme.edge

        Row {
            id: pillRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left; anchors.leftMargin: 15
            spacing: 8
            Text {
                visible: picker.label.length > 0 && !picker.hasValue
                text: picker.label
                color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: picker.hasValue
                text: picker.hasValue ? picker.current.text : ""
                color: theme.gold; font.family: theme.ui
                font.pixelSize: 14; font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: picker.hasValue && !!picker.current.sub
                text: picker.hasValue && picker.current.sub ? picker.current.sub : ""
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        readonly property bool showClear: picker.hasValue && picker.clearable
        Text {
            text: pill.showClear ? "✕" : "▾"
            color: pill.showClear ? theme.ink : theme.inkDim
            font.pixelSize: pill.showClear ? 12 : 11
            anchors.right: parent.right; anchors.rightMargin: 13
            anchors.verticalCenter: parent.verticalCenter
        }
        MouseArea {
            id: ma
            objectName: picker.automationName.length ? picker.automationName + "Pill" : ""
            anchors.fill: parent
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: picker.togglePopup(pillAction)
        }
        KeyboardAction {
            id: pillAction
            objectName: picker.automationName.length ? picker.automationName + "Action" : ""
            spaceActivates: true
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: picker.hasValue ? picker.current.text : picker.label
            accessibleDescription: "Open filter options"
            focusRadius: pill.radius
            onTriggered: picker.togglePopup(pillAction)
            Keys.onDownPressed: event => {
                if (picker.openOnDown) { picker.openPopup(pillAction); event.accepted = true }
                else event.accepted = false
            }
        }
        // clear hit-area — sits over the ✕ (declared after `ma`, so it wins the click there)
        MouseArea {
            id: clearMa
            visible: pill.showClear
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 36
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: picker.clearSelection()
        }
        KeyboardAction {
            id: clearAction
            visible: pill.showClear
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 36
            pointerEnabled: false
            accessibleName: "Clear " + picker.label
            focusRadius: pill.radius
            onTriggered: picker.clearSelection()
        }
    }

    // The shared popup lives in the window overlay, outside any clipped page Flickable.
    Popup {
        id: pop
        objectName: picker.automationName.length ? picker.automationName + "Popup" : ""
        parent: Overlay.overlay
        popupType: Popup.Item
        padding: 6
        modal: true
        dim: false
        margins: 8
        width: Math.min(Math.max(picker.width, 280), parent ? parent.width - 16 : 280)
        // Delegate heights are fixed. ListView.contentHeight is estimated while
        // delegates are being created, which can move a bottom-anchored popup
        // across its trigger after opening or refreshing the options.
        readonly property real desiredHeight: {
            var total = 12
            for (var i = 0; i < picker.options.length; ++i)
                total += picker.options[i].header !== undefined ? 27 : 38
            return Math.min(380, total)
        }
        property real anchorRoomHeight: 380
        height: Math.min(desiredHeight, anchorRoomHeight)
        onDesiredHeightChanged: if (picker.open) positionAtPill()
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        function positionAtPill() {
            if (!parent) return
            var point = pill.mapToItem(parent, 0, 0)
            x = Math.max(8, Math.min(point.x, parent.width - width - 8))
            var below = point.y + pill.height + 6
            var roomBelow = Math.max(0, parent.height - 8 - below)
            var roomAbove = Math.max(0, point.y - 6 - 8)
            var placeBelow = desiredHeight <= roomBelow
                || (desiredHeight > roomAbove && roomBelow >= roomAbove)
            anchorRoomHeight = placeBelow ? roomBelow : roomAbove
            var fittedHeight = Math.min(desiredHeight, anchorRoomHeight)
            y = placeBelow ? below : Math.max(8, point.y - fittedHeight - 6)
        }
        onClosed: {
            // An external host setting open=false must not steal its newly assigned focus.
            if (picker.open) picker.closePopup(true)
        }
        background: Rectangle {
            radius: 13
            color: Qt.rgba(0.045, 0.05, 0.075, 0.97)
            border.width: 1
            border.color: theme.edge
        }

        contentItem: ListView {
            id: list
            objectName: picker.automationName.length ? picker.automationName + "List" : ""
            clip: true
            // Hidden pickers must not rebuild option delegates on catalogue refresh.
            // Selection and pill text remain bound to options above.
            model: picker.open ? picker.options : []
            currentIndex: -1
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) { picker.closePopup(true); event.accepted = true; return }
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted = true; return }
                var next = list.currentIndex
                if (event.key === Qt.Key_Down) next = picker.nextSelectable(list.currentIndex + 1, 1)
                else if (event.key === Qt.Key_Up) next = picker.nextSelectable(list.currentIndex - 1, -1)
                else if (event.key === Qt.Key_Home) next = picker.nextSelectable(0, 1)
                else if (event.key === Qt.Key_End) next = picker.nextSelectable(picker.options.length - 1, -1)
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    picker.choose(list.currentIndex); event.accepted = true; return
                } else return
                if (next < 0 && picker.wrapNavigation) {
                    if (event.key === Qt.Key_Down) next = picker.nextSelectable(0, 1)
                    else if (event.key === Qt.Key_Up) next = picker.nextSelectable(picker.options.length - 1, -1)
                }
                if (next >= 0) { list.currentIndex = next; list.positionViewAtIndex(next, ListView.Contain) }
                event.accepted = true
            }
            delegate: Item {
                id: opt
                required property var modelData
                required property int index
                readonly property bool isHeader: modelData.header !== undefined
                width: list.width
                height: isHeader ? 27 : 38

                // section header (.msec) — small uppercase, dim
                Text {
                    visible: opt.isHeader
                    text: opt.isHeader ? opt.modelData.header : ""
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 10
                    font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
                    anchors.left: parent.left; anchors.leftMargin: 12
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                }

                // option row — catalog name left, owning addon dimmed right (mock .mrow)
                Rectangle {
                    visible: !opt.isHeader
                    anchors.fill: parent
                    radius: 9
                    color: opt.modelData.key === picker.currentKey ? Qt.rgba(240/255, 196/255, 74/255, 0.16)
                         : rowMa.containsMouse || (list.activeFocus && list.currentIndex === opt.index)
                           ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    border.width: list.activeFocus && list.currentIndex === opt.index ? 1 : 0
                    border.color: theme.gold
                    Text {
                        id: catText
                        text: opt.isHeader ? "" : opt.modelData.text
                        color: opt.modelData.key === picker.currentKey ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 13
                        font.weight: opt.modelData.key === picker.currentKey ? Font.DemiBold : Font.Normal
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        visible: !!opt.modelData.sub
                        text: opt.modelData.sub || ""
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                        anchors.right: parent.right; anchors.rightMargin: 12
                        anchors.left: catText.right; anchors.leftMargin: 8
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        id: rowMa
                        objectName: picker.automationName.length ? picker.automationName + "Option_" + opt.index : ""
                        anchors.fill: parent
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: picker.choose(opt.index)
                    }
                }
            }
        }
    }
    Keys.onEscapePressed: if (picker.open) picker.closePopup(true)
}
