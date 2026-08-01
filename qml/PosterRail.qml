// PosterRail - lightweight horizontal Theatre rail. Uses the shared CataloguePosterCard so
// its tiles hover, lift, and reveal year + `★ rating` exactly like Discover; the header shows
// only the title, optional factual source attribution, and See all. No blurb, no rating line
// under the posters. Top 10 keeps its oversized rank numerals.

import QtQuick

pragma ComponentBehavior: Bound

Column {
    id: rail

    property string title: ""
    property var items: []
    property bool ranked: false
    property int itemLimit: ranked ? 10 : 20
    // factual source attribution — "" for house shelves, the extension name for extension rows.
    property string sourceLabel: ""
    property string sourceKind: "house"
    // the See-all descriptor for this shelf; a null pin means the rail is not navigable.
    property var seeAllPin: null
    property var visibleItems: {
        var out = [];
        var count = Math.min(items.length, itemLimit);
        for (var i = 0; i < count; i++)
            out.push(items[i]);
        return out;
    }
    signal itemRequested(var item)
    signal seeAllRequested(var pin)

    width: parent ? parent.width : 900
    spacing: 14
    visible: visibleItems.length > 0

    Theme { id: theme }

    WidgetHeader {
        width: parent.width
        title: rail.title
        // source attribution is metadata, never promotional copy — shown only for extensions.
        sub: (rail.sourceKind !== "house" && rail.sourceLabel.length > 0) ? ("via " + rail.sourceLabel) : ""
        moreLabel: "See all"
        navigable: rail.seeAllPin !== null
        onMoreClicked: rail.seeAllRequested(rail.seeAllPin)
    }

    ListView {
        id: list
        width: parent.width
        height: 226
        orientation: ListView.Horizontal
        spacing: rail.ranked ? 30 : 18
        clip: true
        reuseItems: true
        cacheBuffer: width * 0.75
        boundsBehavior: Flickable.StopAtBounds
        model: rail.visibleItems
        leftMargin: 6
        rightMargin: 26

        delegate: Item {
            id: cell
            required property var modelData
            required property int index

            width: rail.ranked ? 184 : 132
            height: list.height

            Text {
                id: rankNum
                visible: rail.ranked
                text: cell.index + 1
                color: Qt.rgba(1, 1, 1, 0.16)
                font.family: theme.display
                font.bold: true
                font.pixelSize: 132
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -8
            }

            CataloguePosterCard {
                width: 132
                height: list.height
                anchors.left: rail.ranked ? rankNum.right : parent.left
                anchors.leftMargin: rail.ranked ? -32 : 0
                anchors.top: parent.top
                item: cell.modelData
                onActivated: (it) => rail.itemRequested(it)
            }
        }
    }
}
