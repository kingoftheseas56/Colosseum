import QtQuick
import QtQuick.Controls
import ".."
Item {
    id: root
    required property var worldData
    required property var row
    property bool home: false
    property bool cold: false
    property Item backdrop: null
    property Flickable pageFlick: null
    property bool homePageVisible: false
    property int exposureRevision: 0
    property real cardSize: 196
    property int previewCount: 8
    property int currentIndex: 0
    function reveal(index) {
        var item=cardRepeater.itemAt(index);if(!item)return;
        rail.contentX=Math.max(0,Math.min(Math.max(0,rail.contentWidth-rail.width),item.x<rail.contentX ? item.x : item.x+item.width>rail.contentX+rail.width ? item.x+item.width-rail.width : rail.contentX));
    }
    function exposureGeometryChanged() { exposureRevision++; }
    onRowChanged: exposureGeometryChanged()
    onHomePageVisibleChanged: exposureGeometryChanged()
    Keys.onPressed: event => nav.handle(event)
    KeyboardCollectionController {
        id:nav;view:root;count:cardRepeater.count;orientation:"horizontal"
        positionIndexFn:function(index){root.reveal(index);Qt.callLater(function(){var card=cardRepeater.itemAt(index);if(card)card.forceActiveFocus()})}
        onActivated:index=>root.worldData.activate(root.row.items[index])
    }
    implicitHeight: header.height + 22 + rail.height
    WidgetHeader {
        id: header
        width: Math.max(0,parent.width-16)
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
        onContentXChanged: root.exposureGeometryChanged()
        onWidthChanged: root.exposureGeometryChanged()
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: cards
            spacing: 24
            Repeater {
                id: cardRepeater
                model: root.row.items.slice(0,root.previewCount)
                delegate: VinylItemCard {
                    required property var modelData
                    required property int index
                    objectName: "vinylCard_" + (root.cold ? "cold-" : "") + root.row.key + "_" + index
                    worldData: root.worldData
                    item: modelData
                    recommendationRow: root.home ? root.row : null
                    pageFlick: root.pageFlick
                    exposureRail: rail
                    backdrop: root.backdrop
                    homePageVisible: root.homePageVisible
                    exposureRevision: root.exposureRevision
                    onActiveFocusChanged: if(activeFocus){root.currentIndex=index;root.reveal(index)}
                    Keys.onPressed: event => {if([Qt.Key_Left,Qt.Key_Right,Qt.Key_Home,Qt.Key_End].indexOf(event.key)>=0)nav.handle(event)}
                    width: root.cardSize
                    height: implicitHeight
                }
            }
        }
    }
    Connections {
        target: root.pageFlick
        ignoreUnknownSignals: true
        function onContentYChanged() { root.exposureGeometryChanged() }
        function onHeightChanged() { root.exposureGeometryChanged() }
        function onVisibleChanged() { root.exposureGeometryChanged() }
    }
}
