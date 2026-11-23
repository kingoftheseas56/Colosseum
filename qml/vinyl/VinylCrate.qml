import QtQuick
import ".."
import "VinylCardPresentation.js" as Presentation

Item {
    id: root
    objectName: "vinylCrate"
    property var items: []
    property var worldData: null
    property Item backdrop: null
    property Flickable pageFlick: null
    property real tileSize: width>=760 ? (width-126)/8 : 130
    property bool compact: false
    property int page: 0
    readonly property int count: items ? items.length : 0
    readonly property int columns: Math.max(1, Math.min(8, Math.floor(width / tileSize)))
    readonly property int pageSize: columns * 2
    readonly property int pageCount: Math.ceil(count / pageSize)
    readonly property var pageItems: items ? items.slice(page * pageSize, (page + 1) * pageSize) : []
    readonly property int currentPage: page
    implicitHeight: header.height + (tiles.height > 0 ? 12 + tiles.height : 0)
    signal activateRequested(var item)
    signal openRequested(var item)
    signal radioRequested(var item)
    signal pinRequested(var item, bool pinned)
    onPageCountChanged: clampPage()
    onPageChanged: clampPage()
    function clampPage() { var bounded=Math.max(0,Math.min(page, pageCount-1)); if(page!==bounded) page=bounded; }
    function artwork(value) { return worldData && typeof worldData.artwork === "function" ? worldData.artwork(value) : String(value || ""); }
    function presentation(item) {
        var copy=Object.assign({},item);
        copy.unavailable=item.available===false || item.unavailable===true;
        if(Presentation.kind(item)==="albums" && Number.isInteger(item.resumeTrack)
                && Number.isInteger(item.totalTracks) && item.resumeTrack>0 && item.totalTracks>=item.resumeTrack)
            copy.reason=qsTr("Resume · track %1 of %2").arg(item.resumeTrack).arg(item.totalTracks);
        return copy;
    }
    Theme { id: theme }
    component PageArrow: KeyboardAction {
        id: arrow
        property string glyph: ""
        implicitWidth: 30; implicitHeight: 30
        focusRadius: 15
        Rectangle {
            anchors.fill: parent; radius: 15
            color: arrow.interactionActive ? theme.glassHi : "#0dffffff"
            border.width: 1; border.color: "#1affffff"
        }
        Text {
            anchors.centerIn: parent; anchors.verticalCenterOffset: -1
            text: arrow.glyph; textFormat: Text.PlainText; color: theme.inkDim
            opacity: arrow.enabled ? 1 : 0.35
            font.family: theme.ui; font.pixelSize: 18
        }
    }
    Item {
        id: header
        width: parent.width; height: root.compact ? 30 : 38
        Text { anchors.verticalCenter: parent.verticalCenter; text:qsTr("The Crate"); textFormat:Text.PlainText; color:theme.ink; font.family:theme.display; font.pixelSize:24 }
        Row {
            id: arrows
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing:6
            PageArrow { objectName:"vinylCratePrevious"; glyph:"\u2039"; accessibleName:qsTr("Previous"); enabled:root.page>0; onTriggered:root.page-- }
            PageArrow { objectName:"vinylCrateNext"; glyph:"\u203a"; accessibleName:qsTr("Next"); enabled:root.page+1<root.pageCount; onTriggered:root.page++ }
        }
    }
    Grid {
        id: tiles
        y: header.height+12
        columns: root.columns
        columnSpacing: 18
        rowSpacing: 12
        Repeater {
            model: Math.max(0,Math.min(root.pageSize,(root.items || []).length-root.page*root.pageSize))
            delegate: Item {
                id: tile
                readonly property var modelData: root.items[itemIndex]
                required property int index
                readonly property int itemIndex: root.page*root.pageSize+index
                readonly property string kind: Presentation.kind(modelData)
                readonly property bool pinned: modelData.pinned===true
                readonly property var resumeTrack: modelData.resumeTrack
                readonly property bool available: modelData.available!==false && modelData.unavailable!==true
                objectName: "vinylCrateTile_"+itemIndex
                width: root.tileSize; height: card.implicitHeight
                HoverHandler { id: tileHover }
                QtObject {
                    id: actions
                    property var session: root.worldData && root.worldData.session ? root.worldData.session : null
                    function artwork(value) { return root.artwork(value); }
                    function activate(item) {
                        if (tile.kind === "artists") root.openRequested(tile.modelData);
                        else root.activateRequested(tile.modelData);
                    }
                }
                VinylItemCard {
                    id: card
                    y: tileHover.hovered ? -4 : 0
                    objectName: tile.objectName+"_card"
                    width: parent.width; height: implicitHeight
                    worldData: actions; item: root.presentation(tile.modelData)
                    artistRadioAction: function() { root.radioRequested(tile.modelData); }
                    compactCaption: root.compact
                    backdrop: root.backdrop; pageFlick:root.pageFlick
                    opacity: tile.available ? 1 : 0.38
                }
                Rectangle { x:parent.width-width-5; y:5+card.y; width:13; height:13; radius:7; color:theme.gold; border.width:2; border.color:"#06070b"; visible:tile.pinned }
                VinylFeedbackBadge {
                    id: pin
                    objectName: "vinylCrateTilePin_"+tile.itemIndex
                    x:parent.width-width-7; y:7; z:2
                    symbol: tile.pinned ? "check" : "playlistAdd"
                    enabled: tile.available || tile.pinned
                    revealed: tileHover.hovered || card.activeFocus || activeFocus || (tile.pinned && !tile.available)
                    accessibleName: (tile.pinned ? qsTr("Unpin %1") : qsTr("Pin %1")).arg(tile.modelData.title)
                    onTriggered: root.pinRequested(tile.modelData,!tile.pinned)
                }
            }
        }
    }
}
