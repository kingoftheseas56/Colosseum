// VaultBrowseCrumb — the Vault Browse face's breadcrumb (locked design §4.5): current path
// from the selected root down to the current folder/show/season. Middle segments collapse once
// the path runs "far too many" deep; first and last are always visible (design §4.5 occupancy
// table). A non-last, non-collapsed segment is clickable to ascend directly to that level.
// Vault ux uplift S15: those same segments are keyboard-reachable — Tab cycles them, Return or
// Space ascends to that level, house focus ring only while the crumb itself has focus.
import QtQuick

Item {
    id: crumb
    objectName: "vaultBrowseCrumb"
    focus: true
    activeFocusOnTab: true

    // [{key, displayTitle}, ...] from the selected root down to the current level.
    property var stack: []
    // The current folder/show/season's own key — the Lanista/Quick-Test vocabulary.
    readonly property string currentPath: crumb.stack.length ? crumb.stack[crumb.stack.length - 1].key : ""
    signal segmentClicked(int index)
    // S15: the crumb's own arrow/accept keys (the grid's navigation law — the root is the
    // focus unit, arrows move the focused segment, Return/Space activates it, ring pinned to
    // the focused segment). Walkable by a mouse-free session.
    property int keyboardSegment: -1
    readonly property var clickableLayout: {
        const out = []
        for (let i = 0; i < crumb.displaySegments.length; i++) {
            const r = crumb.displaySegments[i]
            if (!r.collapsed && r.index !== crumb.stack.length - 1)
                out.push(r.index)
        }
        return out
    }
    function focusSegmentForward(delta) {
        const list = crumb.clickableLayout
        if (!list.length) return
        let pos = crumb.keyboardSegment === -1 ? -1 : list.indexOf(crumb.keyboardSegment)
        pos = Math.max(0, Math.min(list.length - 1, pos + delta))
        crumb.keyboardSegment = list[pos]
    }
    Keys.onLeftPressed: (event) => { crumb.focusSegmentForward(-1); event.accepted = true }
    Keys.onRightPressed: (event) => { crumb.focusSegmentForward(1); event.accepted = true }
    Keys.onReturnPressed: (event) => {
        if (crumb.keyboardSegment >= 0) crumb.segmentClicked(crumb.keyboardSegment)
        event.accepted = true
    }
    Keys.onEnterPressed: (event) => {
        if (crumb.keyboardSegment >= 0) crumb.segmentClicked(crumb.keyboardSegment)
        event.accepted = true
    }
    Keys.onSpacePressed: (event) => {
        if (crumb.keyboardSegment >= 0) crumb.segmentClicked(crumb.keyboardSegment)
        event.accepted = true
    }
    Keys.onHomePressed: (event) => {
        const list = crumb.clickableLayout
        if (list.length) crumb.keyboardSegment = list[0]
        event.accepted = list.length > 0
    }
    Keys.onEndPressed: (event) => {
        const list = crumb.clickableLayout
        if (list.length) crumb.keyboardSegment = list[list.length - 1]
        event.accepted = list.length > 0
    }

    Theme { id: theme }

    implicitHeight: row.implicitHeight
    implicitWidth: row.implicitWidth
    width: parent ? parent.width : implicitWidth

    // "Far too many" (design §4.5): collapse the middle, keep first + last two, always the very
    // first and very last segment visible. Three or fewer levels never collapse — this browse
    // face rarely drills past root/show/season in practice, but the contract is honored anyway.
    readonly property var displaySegments: {
        const n = crumb.stack.length
        const out = []
        if (n === 0)
            return out
        if (n <= 4) {
            for (let i = 0; i < n; ++i)
                out.push({ index: i, title: crumb.stack[i].displayTitle, collapsed: false })
            return out
        }
        out.push({ index: 0, title: crumb.stack[0].displayTitle, collapsed: false })
        out.push({ index: -1, title: "…", collapsed: true })
        out.push({ index: n - 2, title: crumb.stack[n - 2].displayTitle, collapsed: false })
        out.push({ index: n - 1, title: crumb.stack[n - 1].displayTitle, collapsed: false })
        return out
    }

    Row {
        id: row
        spacing: 8
        Repeater {
            id: rep
            model: crumb.displaySegments
            delegate: Row {
                id: seg
                required property var modelData
                required property int index
                readonly property bool isLast: seg.modelData.index === crumb.stack.length - 1
                readonly property bool clickable: !seg.modelData.collapsed && !seg.isLast
                spacing: 8
                Text {
                    id: segText
                    text: seg.modelData.title
                    color: seg.isLast ? theme.ink : theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: seg.isLast ? Font.DemiBold : Font.Normal
                    // S15 — the ring pins to the crumb's keyboardSegment, never to a hover.
                    // Inside the Text (a non-positioner) so the Row's own layout is untouched.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: 6
                        border.width: 2
                        border.color: theme.inkDim
                        color: "transparent"
                        visible: crumb.activeFocus && crumb.keyboardSegment === seg.modelData.index
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        enabled: seg.clickable
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: crumb.segmentClicked(seg.modelData.index)
                    }
                }
                Text {
                    visible: seg.index < rep.count - 1
                    text: "/"
                    color: theme.inkDimmer
                    opacity: 0.5
                    font.pixelSize: 13
                }
            }
        }
    }
}
