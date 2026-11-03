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
    function reveal() { open = true }
    function focusHistoryStatsAction() { homeInput.forceActiveFocus(Qt.TabFocusReason) }
    function focusCollectionsAction() { homeInput.forceActiveFocus(Qt.TabFocusReason) }
    function focusUtilityAction(action) {
        open = true
        Qt.callLater(function() { utilities.focusAction(action) })
    }
    TaskbarUtilities {
        id: utilities
        x: bar.leftEdge
        y: dock.y - height - 10
        width: Math.min(bar.width - bar.leftEdge * 2, 440)
        visible: bar.open
        backdrop: bar.backdrop
        worldMode: bar.worldMode
        androidHost: true
        trackersActive: bar.trackersActive
        updateAvailable: bar.updateAvailable
        updateUnseen: bar.updateUnseen
        reducedMotion: bar.reducedMotion
        onTriggered: (action, anchorRight, anchorBottom) => {
            bar.open = action === "sharing"
            bar.utilityTriggered(action, anchorRight, dock.mapToItem(null, 0, 0).y)
        }
    }
    Rectangle {
        id: dock
        x: bar.leftEdge
        y: bar.height - height - bar.bottomGap
        width: bar.closedWidth
        height: bar.closedSize
        radius: 22
        color: "#ed08090d"
        border.color: "#29ffffff"
        RowLayout {
            anchors { fill: parent; margins: 8 }
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

        }
    }
}
