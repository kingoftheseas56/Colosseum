// Android exposes feedback without loading desktop sessions.
import QtQuick
import QtQuick.Layouts
import ".."

Item {
    id: bar
    objectName: "colosseumTaskbar"
    anchors.fill: parent
    property bool open: false
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
    signal feedbackClicked()
    signal vaultClicked()
    signal downloadsClicked()
    signal extensionsClicked()
    signal historyStatsClicked()
    KeyboardSpatialNavigator { id: spatialNav; root: bar }
    Keys.onPressed: (event) => spatialNav.handle(event)
    function reveal() { open = true }
    function focusHistoryStatsAction() { homeInput.forceActiveFocus(Qt.TabFocusReason) }
    function focusCollectionsAction() { homeInput.forceActiveFocus(Qt.TabFocusReason) }

    Rectangle {
        id: dock
        objectName: "taskbarDock"
        x: bar.leftEdge
        y: bar.height - height - bar.bottomGap
        width: bar.open ? 126 : bar.closedWidth
        height: bar.closedSize
        radius: 22
        color: "#ed08090d"
        border.color: "#29ffffff"
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
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
                    tooltipText: accessibleName
                    focusRadius: 14
                    onTriggered: bar.open = !bar.open
                }
            }
            FeedbackButton {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignVCenter
                visible: bar.open
                onClicked: {
                    bar.feedbackClicked()
                    bar.open = false
                }
            }
        }
    }
}
