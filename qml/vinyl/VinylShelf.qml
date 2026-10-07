import QtQuick
import QtQuick.Controls
import ".."
Item {
    id: root
    required property var worldData
    required property var row
    property bool home: false
    property bool cold: false
    property real cardSize: 196
    property int previewCount: 8
    implicitHeight: header.height + 22 + rail.height
    WidgetHeader {
        id: header
        width: parent.width
        title: root.row.title
        moreLabel: "See all"
        automationId: (root.home ? "vinylHomeSeeAll_" : "vinylShelfSeeAll_") + root.row.key
        onMoreClicked: root.worldData.openList(root.row.key,root.home)
    }
    Flickable {
        id: rail
        y: header.height + 22
        width: parent.width
        height: cards.implicitHeight + 14
        contentWidth: cards.implicitWidth
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: cards
            spacing: 24
            Repeater {
                model: root.row.items.slice(0,root.previewCount)
                delegate: VinylItemCard {
                    required property var modelData
                    required property int index
                    objectName: "vinylCard_" + (root.cold ? "cold-" : "") + root.row.key + "_" + index
                    worldData: root.worldData
                    item: modelData
                    width: root.cardSize
                    height: implicitHeight
                }
            }
        }
        ScrollBar.horizontal: HouseScrollBar { orientation: Qt.Horizontal; flick: rail }
    }
}
