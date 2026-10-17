// Android has no desktop taskbar. Keep the shared shell interface without
// instantiating desktop chrome or its desktop-only world dependencies.
import QtQuick

Item {
    objectName: "colosseumTaskbar"
    property bool open: false
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
    signal vaultClicked()
    signal downloadsClicked()
    signal extensionsClicked()
    signal historyStatsClicked()
    function reveal() {}
    function focusHistoryStatsAction() {}
}
