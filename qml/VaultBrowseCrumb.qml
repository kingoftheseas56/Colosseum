// VaultBrowseCrumb — the Vault Browse face's breadcrumb (locked design §4.5): current path
// from the selected root down to the current folder/show/season. Middle segments collapse once
// the path runs "far too many" deep; first and last are always visible (design §4.5 occupancy
// table). A non-last, non-collapsed segment is clickable to ascend directly to that level.
import QtQuick

Item {
    id: crumb
    objectName: "vaultBrowseCrumb"

    // [{key, displayTitle}, ...] from the selected root down to the current level.
    property var stack: []
    // The current folder/show/season's own key — the Lanista/Quick-Test vocabulary.
    readonly property string currentPath: crumb.stack.length ? crumb.stack[crumb.stack.length - 1].key : ""
    signal segmentClicked(int index)

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
                spacing: 8
                Text {
                    text: seg.modelData.title
                    color: seg.isLast ? theme.ink : theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: seg.isLast ? Font.DemiBold : Font.Normal
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        enabled: !seg.modelData.collapsed && !seg.isLast
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
