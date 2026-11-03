// Android exposes shared utilities without loading desktop sessions.
import QtQuick
import QtQuick.Layouts
import ".."

Item {
    id: bar
    objectName: "colosseumTaskbar"
    anchors.fill: parent
    property bool open: false
    property Item backdrop: null
    property string worldMode: ""
    property bool trackersActive: false
    property bool updateAvailable: false
    property bool updateUnseen: false
    property bool reducedMotion: false
    property bool collectionsActive: false
    readonly property int leftEdge: 18
    readonly property int bottomGap: 16
    readonly property int closedSize: 64
    readonly property int closedWidth: 64
    property bool watchPartyJoinOpen: false
    property string watchPartyJoinPhase: "idle"
    property string watchPartyJoinErrorCategory: ""
    property bool vaultActive: false
    property int downloadsBadge: 0
    property bool downloadsActive: false
    property bool extensionsActive: false
    property bool historyStatsActive: false
    property bool feriaContext: false
    signal switchRequested(string id)
    signal closeRequested(string id)
    signal startClicked()
    signal openMediaClicked()
    signal openRecentRequested()
    signal watchPartyJoinClicked()
    signal collectionsClicked()
    signal utilityTriggered(string action, real anchorRight, real anchorBottom)
    signal vaultClicked()
    signal downloadsClicked()
    signal extensionsClicked()
    signal historyStatsClicked()
    KeyboardSpatialNavigator { id: spatialNav; root: bar }
    Keys.onPressed: (event) => spatialNav.handle(event)
    onOpenChanged: if (!open) { dockScroll.cancelFlick(); dockScroll.contentX = 0 }
    function reveal() { open = true }
    function focusHistoryStatsAction() { homeInput.forceActiveFocus(Qt.TabFocusReason) }
    function focusCollectionsAction() { homeInput.forceActiveFocus(Qt.TabFocusReason) }
    Connections {
        target: bar.Window.window
        function onActiveFocusItemChanged() {
            if (!bar.open) return
            const control = bar.Window.window.activeFocusItem
            let ancestor = control
            while (ancestor && ancestor !== dockRow) ancestor = ancestor.parent
            if (!control || !ancestor) return
            const position = control.mapToItem(dockRow, 0, 0)
            if (position.x < dockScroll.contentX) dockScroll.contentX = Math.max(0, position.x)
            else if (position.x + control.width > dockScroll.contentX + dockScroll.width)
                dockScroll.contentX = Math.min(dockScroll.contentWidth - dockScroll.width,
                                               position.x + control.width - dockScroll.width)
        }
    }
    function focusUtilityAction(action) {
        open = true
        Qt.callLater(function() { utilities.focusAction(action) })
    }

    Rectangle {
        id: dock
        objectName: "taskbarDock"
        x: bar.leftEdge
        y: bar.height - height - bar.bottomGap
        width: bar.open ? Math.min(bar.width - bar.leftEdge * 2, dockRow.implicitWidth + 16) : bar.closedWidth
        height: bar.closedSize
        radius: 22
        color: "#ed08090d"
        border.color: "#29ffffff"
        Flickable {
            id: dockScroll
            objectName: "taskbarDockScroll"
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            contentWidth: Math.max(width, dockRow.implicitWidth)
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: bar.open && contentWidth > width
            clip: true
            RowLayout {
                id: dockRow
                objectName: "taskbarDockRow"
                width: dockScroll.contentWidth
                height: dockScroll.height
            spacing: 8
            Item {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Image { anchors.centerIn: parent; width: 28; height: 28; source: "../../assets/icons/colosseum.svg" }
                KeyboardAction {
                    id: homeInput
                    objectName: "colosseumTaskbarHomeButton"
                    anchors.fill: parent
                    accessibleName: bar.open ? qsTr("Close taskbar") : qsTr("Open taskbar")
                    focusRadius: 14
                    onTriggered: bar.open = !bar.open
                }
            }
            TaskbarUtilities {
                id: utilities
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open
                trackersActive: bar.trackersActive
                updateAvailable: bar.updateAvailable
                updateUnseen: bar.updateUnseen
                reducedMotion: bar.reducedMotion
                onTriggered: (action, anchorRight, anchorBottom) => {
                    bar.open = action === "sharing"
                    bar.utilityTriggered(action, anchorRight, dock.mapToItem(null, 0, 0).y)
                }
            }
            }
        }
    }
}
