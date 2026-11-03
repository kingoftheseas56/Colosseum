pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root
    objectName: "overviewPage"
    required property var store
    signal collectionRequested(string collectionId)
    signal createRequested(var opener)
    signal backRequested()
    property alias searchText: searchField.text
    property int visibleLimit: 36
    readonly property int pageMargin: width < 650 ? 22 : 48
    readonly property int shelfColumns: width >= 1130 ? 3 : width >= 650 ? 2 : 1
    readonly property var shownCollections: { root.store.revision; return root.store.filteredCollections(searchField.text); }
    readonly property bool hasCollections: { root.store.revision; return root.store.collections.length > 0; }
    Theme { id: theme }

    function reveal(item) {
        const point = item.mapToItem(content, 0, 0)
        if (point.y < scroller.contentY)
            scroller.contentY = point.y
        else if (point.y + item.height > scroller.contentY + scroller.height)
            scroller.contentY = Math.min(scroller.contentHeight - scroller.height, point.y + item.height - scroller.height)
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height + 24
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: content
            x: root.pageMargin
            y: 22
            width: scroller.width - root.pageMargin * 2
            spacing: 24
            BackAction {
                id: overviewBack
                objectName: "collectionsOverviewBack"
                label: qsTr("Back")
                onTriggered: root.backRequested()
                onAutomationActiveFocusChanged: if (overviewBack.automationActiveFocus) root.reveal(overviewBack)
            }
            GridLayout {
                width: parent.width
                columns: root.width < 900 ? 1 : 2
                rowSpacing: 18
                columnSpacing: 30
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Your Collections")
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: root.width < 650 ? 33 : 40
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The things you want to keep together.")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }
                }
                RowLayout {
                    Layout.fillWidth: root.width < 900
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    spacing: 12
                    CollectionSearchField {
                        id: searchField
                        objectName: "overviewSearch"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 228
                        Layout.minimumWidth: 100
                        placeholderText: qsTr("Find a collection")
                        Accessible.name: qsTr("Find a collection")
                        onTextChanged: { scroller.contentY = 0; root.visibleLimit = 36 }
                        onActiveFocusChanged: if (activeFocus) root.reveal(searchField)
                    }
                    CollectionActionButton {
                        id: newCollection
                        objectName: "newCollectionButton"
                        text: qsTr("New collection")
                        iconName: "plus"
                        emphasis: "primary"
                        enabled: root.store.healthy
                        onClicked: root.createRequested(newCollection)
                        onActiveFocusChanged: if (activeFocus) root.reveal(newCollection)
                    }
                }
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Qt.rgba(1, 1, 1, 0.10)
            }
            Grid {
                id: shelves
                width: parent.width
                columns: root.shelfColumns
                columnSpacing: 26
                rowSpacing: 24
                visible: root.shownCollections.length > 0
                Repeater {
                    model: root.shownCollections.slice(0, root.visibleLimit)
                    delegate: CollectionShelfCard {
                        id: shelf
                        required property var modelData
                        width: (shelves.width - shelves.columnSpacing * (shelves.columns - 1)) / shelves.columns
                        store: root.store
                        collection: shelf.modelData
                        onClicked: root.collectionRequested(shelf.modelData.id)
                        onActiveFocusChanged: if (activeFocus) root.reveal(shelf)
                    }
                }
            }
            CollectionActionButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.visibleLimit < root.shownCollections.length
                text: qsTr("Show more collections")
                onClicked: root.visibleLimit += 36
            }
            ColumnLayout {
                width: parent.width
                height: Math.max(310, scroller.height - 230)
                visible: root.shownCollections.length === 0
                spacing: 16
                Item { Layout.fillHeight: true }
                CollectionIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    name: root.hasCollections ? "search" : "collections"
                    strokeColor: theme.inkDimmer
                }
                Text {
                    Layout.fillWidth: true
                    text: root.hasCollections ? qsTr("No collections by that name") : qsTr("Make a little room for what you love")
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 27
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: root.hasCollections ? qsTr("Try another name, or clear your search.") : qsTr("Start with a name. Add titles whenever you find them.")
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                CollectionActionButton {
                    id: emptyAction
                    objectName: "overviewEmptyAction"
                    Layout.alignment: Qt.AlignHCenter
                    text: root.hasCollections ? qsTr("Clear search") : qsTr("New collection")
                    emphasis: root.hasCollections ? "neutral" : "primary"
                    iconName: root.hasCollections ? "close" : "plus"
                    enabled: root.hasCollections || root.store.healthy
                    onClicked: {
                        if (root.hasCollections) {
                            searchField.clear()
                            searchField.forceActiveFocus()
                        } else {
                            root.createRequested(emptyAction)
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
