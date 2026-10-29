import QtQuick

WorldTabBar {
    objectName: "theatreTabBar"
    tabPrefix: "theatreTab"
    theatreStyle: true
    currentTab: "discover"
    tabModel: [
        { key: "discover", label: qsTr("Discover") },
        { key: "movies", label: qsTr("Movies") },
        { key: "shows", label: qsTr("Shows") },
        { key: "anime", label: qsTr("Anime") },
        { key: "library", label: qsTr("Library") }
    ]
}
