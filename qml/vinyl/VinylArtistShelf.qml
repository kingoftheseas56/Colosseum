import QtQuick
import QtQuick.Controls
import ".."

// Native Vinyl row: the same WidgetHeader / horizontal-scroll / artwork language
// used by VinylShelf, with artist-specific activation kept in this page.
Column {
    id: root
    required property string title
    required property var records
    required property var artistData
    required property string category
    property string subtitle: ""
    property string prefix: "vinylArtistItem"
    property string shape: "square"
    property string badge: ""
    property Component toolbar: null
    property int previewCount: 10
    readonly property real cardWidth: shape === "wide" ? 286 : 182
    readonly property var shownRecords: records.slice(0, previewCount)
    signal activated(var record)
    signal seeAllRequested()
    signal itemFocused(var item)
    spacing: 12
    visible: records.length > 0

    WidgetHeader {
        width: parent.width
        title: root.title
        sub: root.subtitle
        moreLabel: "See all"
        automationId: "vinylArtistSeeAll_" + root.category
        onMoreClicked: root.seeAllRequested()
    }
    Loader {
        width: parent.width
        active: root.toolbar !== null
        sourceComponent: root.toolbar
        height: item ? item.implicitHeight : 0
        visible: active
    }
    Flickable {
        id: rail
        objectName: root.prefix + "Rail"
        width: parent.width
        height: items.implicitHeight + 12
        contentWidth: items.implicitWidth
        contentHeight: height
        clip: true
        interactive: contentWidth > width
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: items
            spacing: 24
            Repeater {
                model: root.shownRecords.length
                delegate: VinylArtistCard {
                    id: tile
                    readonly property var modelData: root.shownRecords[index]
                    required property int index
                    objectName: root.prefix + "_" + index
                    artObjectName: root.prefix + "Art_" + index
                    width: root.cardWidth
                    height: implicitHeight
                    record: modelData
                    artistData: root.artistData
                    shape: root.shape
                    badge: root.badge
                    onCardRequested: record => root.activated(record)
                    onActiveFocusChanged: if (activeFocus) {
                        rail.contentX = Math.max(0, Math.min(rail.contentWidth - rail.width,
                            tile.x - (rail.width - tile.width) / 2))
                        root.itemFocused(tile)
                    }
                }
            }
        }
        ScrollGlide { flick: rail }
    }
}
