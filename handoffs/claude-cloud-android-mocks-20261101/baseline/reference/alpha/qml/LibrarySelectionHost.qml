pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

Item {
    id: root
    required property Item owner
    required property LibrarySelectionState selection
    property bool pageActive: true
    property bool allowWatched: false
    property bool confirmationOpen: false
    property var removeEntry: null
    property string removalError: ""
    signal removeRequested(var entry)
    signal watchedRequested(var entry, bool watched)
    readonly property var hostWindow: owner ? owner.Window.window : null
    readonly property bool shortWindow: !!hostWindow && hostWindow.height < 500
        && hostWindow.width > hostWindow.height
    readonly property bool eligible: pageActive && owner.visible && !!selection && selection.active
    readonly property real reservedHeight: eligible ? bar.height : 0
    visible: false
    Theme { id: theme }

    function publishOwner() {
        if (!hostWindow || hostWindow["phoneLibrarySelectionOwner"] === undefined) return
        if (eligible) hostWindow.phoneLibrarySelectionOwner = root
        else if (hostWindow.phoneLibrarySelectionOwner === root) hostWindow.phoneLibrarySelectionOwner = null
    }
    function handleBack() {
        if (confirmationOpen) { confirmationOpen = false; return true }
        if (selection.active) { selection.clear(); return true }
        return false
    }
    function applyRemove() {
        // Snapshot identities before canonical owner signals synchronously refresh the rows.
        if (!selection) return
        const rows = selection.selectedRows.slice()
        confirmationOpen = false
        const failed = []
        for (const row of rows) {
            if (removeEntry) {
                try { if (removeEntry(row.entry) === false) failed.push(row) }
                catch (error) { failed.push(row) }
            } else removeRequested(row.entry)
        }
        selection.selectedKeys = failed.map(row => selection.key(row))
        removalError = failed.length ? "Some items could not be removed. Try again." : ""
        confirmationOpen = failed.length > 0
    }
    function applyWatched(watched) {
        if (!selection) return
        const rows = selection.selectedRows.slice()
        for (const row of rows) watchedRequested(row.entry, watched)
        selection.clear()
    }
    onEligibleChanged: {
        if (!eligible) confirmationOpen = false
        publishOwner()
    }
    onPageActiveChanged: if (!pageActive && selection) selection.clear()
    Connections {
        target: root.owner
        function onVisibleChanged() { if (!root.owner.visible) root.selection.clear() }
    }
    Component.onDestruction: {
        if (hostWindow && hostWindow["phoneLibrarySelectionOwner"] === root)
            hostWindow.phoneLibrarySelectionOwner = null
    }

    component Action: Rectangle {
        id: action
        property string label
        signal triggered()
        width: Math.max(48, text.implicitWidth + 24); height: 48; radius: 12
        color: input.interactionActive ? Qt.rgba(240/255,196/255,74/255,0.24) : "#26252d"
        Text { id: text; anchors.centerIn: parent; text: action.label; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(14) }
        KeyboardAction { id: input; anchors.fill: parent; accessibleName: action.label; onTriggered: action.triggered() }
    }

    Rectangle {
        id: bar
        objectName: "librarySelectionBar"
        parent: root.hostWindow ? root.hostWindow.contentItem : null
        visible: root.eligible
        z: 59
        anchors.left: parent ? parent.left : undefined
        anchors.right: parent ? parent.right : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        height: (root.shortWindow ? 72 : 128) + SafeArea.margins.bottom
        color: "#13131c"
        border.width: 1; border.color: theme.gold
        Item {
            id: barContent
            x: Math.max(12, SafeArea.margins.left)
            y: 12
            width: bar.width - x - Math.max(12, SafeArea.margins.right)
            height: root.shortWindow ? 48 : 104
            Item {
                id: summary
                width: root.shortWindow ? Math.min(300, parent.width * 0.44) : parent.width
                height: 48
                Text {
                    objectName: "librarySelectionCount"
                    anchors.left: parent.left; anchors.right: cancel.left; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.selection ? root.selection.count : 0) + " selected"
                    elide: Text.ElideRight
                    color: theme.gold; font.family: theme.ui; font.pixelSize: theme.fontSize(18); font.weight: Font.DemiBold
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Action {
                    id: cancel
                    objectName: "librarySelectionCancel"
                    anchors.right: parent.right
                    label: "Cancel"
                    onTriggered: root.selection.clear()
                }
            }
            Flickable {
                x: root.shortWindow ? summary.width + 12 : 0
                y: root.shortWindow ? 0 : 56
                width: parent.width - x; height: 48
                contentWidth: actions.width; contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                Row {
                    id: actions
                    spacing: 8
                    Action { objectName: "librarySelectAll"; label: "Select all"; onTriggered: root.selection.selectAll() }
                    Action { objectName: "libraryInvertSelection"; label: "Invert"; onTriggered: root.selection.invert() }
                    Action { objectName: "libraryRemoveSelection"; label: "Remove"; onTriggered: root.confirmationOpen = true }
                    Action { visible: root.allowWatched; label: "Mark watched"; onTriggered: root.applyWatched(true) }
                    Action { visible: root.allowWatched; label: "Mark unwatched"; onTriggered: root.applyWatched(false) }
                }
            }
        }
    }

    Rectangle {
        parent: root.hostWindow ? root.hostWindow.contentItem : null
        anchors.fill: parent
        visible: root.eligible && root.confirmationOpen
        z: 60
        // Full-window scrim: the centred dialog reads its margins here to avoid a SafeArea loop.
        readonly property var safeMargins: SafeArea.margins
        color: "#99000000"
        MouseArea { anchors.fill: parent; onClicked: root.confirmationOpen = false }
        Rectangle {
            width: Math.min(440, parent.width - Math.max(24, parent.safeMargins.left + parent.safeMargins.right))
            height: Math.min(copy.implicitHeight + 32,
                parent.height - Math.max(24, parent.safeMargins.top + parent.safeMargins.bottom))
            anchors.centerIn: parent
            radius: 18; color: "#20202a"; border.width: 1; border.color: theme.gold
            MouseArea { anchors.fill: parent }
            Flickable {
                anchors.fill: parent; anchors.margins: 16
                contentHeight: copy.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                Column {
                id: copy
                width: parent.width; spacing: 12
                Text {
                    width: parent.width; text: "Remove " + (root.selection ? root.selection.count : 0) + " from library?"
                    wrapMode: Text.WordWrap; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.fontSize(20)
                }
                Text {
                    width: parent.width
                    text: root.removalError.length ? root.removalError
                        : "Downloaded files and reading/watch progress stay on this device."
                    wrapMode: Text.WordWrap; color: theme.inkDim; font.family: theme.ui; font.pixelSize: theme.fontSize(15)
                }
                Row {
                    spacing: 12
                    Action { objectName: "libraryRemoveCancel"; label: "Keep"; onTriggered: root.confirmationOpen = false }
                    Action { objectName: "libraryRemoveConfirm"; label: "Remove"; onTriggered: root.applyRemove() }
                }
                }
            }
        }
    }
}
