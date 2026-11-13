import QtQuick
import ".."

// Compatibility presentation for standalone donor-component checks.
WorldPage {
    id: page
    objectName: "vinylBrowseShell"
    required property var worldData
    property bool navigationEnabled: false
    property bool contentVisible: false
    readonly property Item board: legacyBoard
    medium: "Vinyl"
    tabBarSource: navigationEnabled && contentVisible ? legacyBoard.flowTabs : null
    dockContentLeft: legacyBoard.dockContentLeft
    dockObjectName: "vinylDockedTabs"
    dockTabPrefix: tabsDocked ? "vinylTab" : "vinylHiddenDockedTab"

    VinylWorld {
        id: legacyBoard
        width: parent.width
        visible: page.contentVisible
        active: page.lifecycleActive && page.visible && visible
        worldData: page.worldData
        backdrop: page.backdrop
        pageFlick: page.pageFlickable
        pageShell: page
        dockedInset: page.parkDockSpace
        dockedTabBar: page.dockedTabBar
        currentTab: "library"
    }
}
