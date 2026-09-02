// VaultBrowseEmpty — the Vault Browse face's empty-state family (locked design §4.5, execution
// plan Slice 9). Four distinct causes, each with its OWN copy — the design's own requirement is
// that they never share wording, because only one of the four is actually a problem. The CAUSE is
// a fact the C++ projection computes (VaultLibrary::browseEmptyCause) — this component only
// paints it, never infers it. Copy verbatim from the approved mock
// (Brotherhood/agents/colosseum-vault-browse-face-mock.html, plate 6). No taglines.
//
// The fourth cause ("filtered") had no production trigger until vault ux uplift S13 shipped
// the Browse face's filter surface — browseEmptyCause(path, filter) now returns it from a
// real filter, and the cause's own "clear the filter" copy grew its action (the
// clearFilterRequested signal below; VaultPage wires it to resetFilters()).
import QtQuick

Item {
    id: root
    objectName: "vaultBrowseEmpty"
    // "noRoots" | "emptyFolder" | "allAway" | "filtered" | "" (nothing to show)
    property string cause: ""
    // The one count both the "allAway" and "filtered" templates need (design plate 6: "All 8
    // items live..." / "...see all 8 items again."). 0 renders the count-free fallback phrasing.
    property int itemsCount: 0
    signal addStorageRequested()
    // S13 — the filtered cause's action: clears the active filters (VaultPage's resetFilters).
    signal clearFilterRequested()

    function awayBody(n) {
        if (n <= 0)
            return "Everything here lives on a drive that is not connected. Nothing has been forgotten."
        const noun = n === 1 ? "1 item lives" : (n + " items live")
        return "All " + noun + " on a drive that is not connected. Nothing has been forgotten."
    }
    function filteredBody(n) {
        if (n <= 0)
            return "Clear the filter to see everything again."
        const noun = n === 1 ? "1 item" : (n + " items")
        return "Clear the filter to see all " + noun + " again."
    }

    readonly property var copyByCause: ({
        noRoots: {
            h: "No storage yet",
            p: "Point Vault at a folder or a drive and it will work out what is there."
        },
        emptyFolder: {
            h: "This folder is empty",
            p: "Nothing here yet. Anything you drop in will appear on its own."
        },
        allAway: {
            h: "Everything here is away",
            p: root.awayBody(root.itemsCount)
        },
        filtered: {
            h: "Nothing matches that filter",
            p: root.filteredBody(root.itemsCount)
        }
    })
    readonly property var active: root.copyByCause[root.cause] || null
    readonly property string headingText: root.active ? root.active.h : ""
    readonly property string bodyText: root.active ? root.active.p : ""

    visible: root.active !== null
    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    Theme { id: theme }

    Column {
        id: col
        anchors.centerIn: parent
        width: Math.min(360, root.width > 0 ? root.width : 360)
        spacing: 11

        Text {
            objectName: "vaultBrowseEmptyHeading"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.headingText
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Text {
            objectName: "vaultBrowseEmptyBody"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.4
            text: root.bodyText
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
        }

        Rectangle {
            id: addBtn
            objectName: "vaultBrowseEmptyAddStorage"
            visible: root.cause === "noRoots"
            anchors.horizontalCenter: parent.horizontalCenter
            width: addLabel.implicitWidth + 40
            height: 40
            radius: 11
            color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.9) : Qt.rgba(1, 1, 1, 0.08)
            border.width: 1
            border.color: addMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6) : theme.edge
            Text {
                id: addLabel
                anchors.centerIn: parent
                text: "Add storage"
                color: addMa.containsMouse ? "#141207" : theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            MouseArea {
                id: addMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.addStorageRequested()
            }
            KeyboardAction {
                anchors.fill: parent
                pointerEnabled: false
                accessibleName: "Add Vault storage"
                onTriggered: root.addStorageRequested()
            }
        }

        // S13 — the filtered cause's action: the copy always said "Clear the filter to see
        // everything again"; now the sentence has its verb.
        Item {
            objectName: "vaultBrowseEmptyClearFilter"
            visible: root.cause === "filtered"
            anchors.horizontalCenter: parent.horizontalCenter
            width: clearLabel.implicitWidth + 32
            height: 36
            Rectangle {
                anchors.fill: parent; radius: 10
                color: clearMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.9) : Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                border.color: clearMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.6) : theme.edge
            }
            Text {
                id: clearLabel
                anchors.centerIn: parent
                text: "Clear filter"
                color: clearMa.containsMouse ? "#141207" : theme.ink
                font.family: theme.ui
                font.pixelSize: 13
            }
            MouseArea {
                id: clearMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.clearFilterRequested()
            }
            KeyboardAction {
                anchors.fill: parent
                pointerEnabled: false
                accessibleName: "Clear Vault filters"
                onTriggered: root.clearFilterRequested()
            }
        }
    }
}
