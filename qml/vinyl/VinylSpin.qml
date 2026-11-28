import QtQuick
import ".."
import "VinylCardPresentation.js" as Presentation

Item {
    id: root
    objectName:"vinylSpin"
    property var songs: []
    property var albums: []
    property var artists: []
    property string view: "songs"
    property var worldData: null
    property Item backdrop: null
    property Flickable pageFlick: null
    property VinylWallpaperFrost frost: null
    readonly property bool hasSongPanel: view === "songs" && pageItems.length > 0
    readonly property real songInsetX: 12
    readonly property real songInsetY: 10
    property real tileSize:130
    property int page:0
    readonly property var items: view==="albums" ? albums : view==="artists" ? artists : songs
    readonly property int columns: view==="songs" ? 3 : Math.max(1,Math.min(8,Math.floor(width/tileSize)))
    readonly property int pageSize: view==="songs" ? 12 : columns
    readonly property int pageCount: Math.ceil((items ? items.length : 0)/pageSize)
    readonly property var pageItems: items ? items.slice(page*pageSize,(page+1)*pageSize) : []
    readonly property int currentPage:page
    implicitHeight: header.height + (grid.height > 0
        ? 20 + grid.height + (hasSongPanel ? 2 * songInsetY : 0) : 0)
    signal activateRequested(var item)
    signal openRequested(var item)
    signal radioRequested(var item)
    signal feedbackRequested(var item,string action)
    onViewChanged: { if(["songs","albums","artists"].indexOf(view)<0) view="songs"; page=0; }
    onPageCountChanged:clampPage()
    onPageChanged:clampPage()
    function clampPage() { var bounded=Math.max(0,Math.min(page,pageCount-1)); if(page!==bounded) page=bounded; }
    function artwork(value) { return worldData && typeof worldData.artwork==="function" ? worldData.artwork(value) : String(value || ""); }
    Theme { id:theme }
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
        id:header
        width:parent.width; height:38
        Text { id:spinTitle; anchors.verticalCenter:parent.verticalCenter; text:qsTr("The Spin"); textFormat:Text.PlainText; color:theme.ink; font.family:theme.display; font.pixelSize:24 }
        Rectangle { x:switcher.x-4; y:0; width:switcher.width+8; height:switcher.height+8; radius:12; color:Qt.rgba(1,1,1,0.045); border.width:1; border.color:"#1affffff" }
        Row {
            id:switcher
            objectName:"vinylSpinSwitch"
            readonly property string view:root.view
            x:spinTitle.width+22; y:4
            spacing:6
            Repeater {
                model:[{key:"songs",label:qsTr("Songs")},{key:"albums",label:qsTr("Albums")},{key:"artists",label:qsTr("Artists")}]
                delegate:VinylWorldButton {
                    required property var modelData
                    objectName:"vinylSpinSwitch_"+modelData.key
                    label:modelData.label; selected:root.view===modelData.key; backdrop:root.backdrop
                    height:28
                    onTriggered:root.view=modelData.key
                }
            }
        }
        Row {
            anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter; spacing:6
            PageArrow { objectName:"vinylSpinPrevious"; glyph:"\u2039"; accessibleName:qsTr("Previous"); enabled:root.page>0; onTriggered:root.page-- }
            PageArrow { objectName:"vinylSpinNext"; glyph:"\u203a"; accessibleName:qsTr("Next"); enabled:root.page+1<root.pageCount; onTriggered:root.page++ }
        }
    }
    Loader {
        y: header.height + 20
        width: root.width
        height: grid.height + 2 * root.songInsetY
        active: root.hasSongPanel
        sourceComponent: VinylFrostedSurface {
            objectName: "vinylQuickPicksFrost"
            frost: root.frost
            radius: 15
        }
    }
    Grid {
        id:grid
        x: root.hasSongPanel ? root.songInsetX : 0
        y: header.height + 20 + (root.hasSongPanel ? root.songInsetY : 0)
        columns:root.columns
        columnSpacing:root.view==="songs" ? 20 : 18
        rowSpacing:8
        Repeater {
            model:Math.max(0,Math.min(root.pageSize,(root.items || []).length-root.page*root.pageSize))
            delegate:Loader {
                id:entry
                readonly property var modelData: root.items[itemIndex]
                required property int index
                readonly property int itemIndex:root.page*root.pageSize+index
                width: root.view === "songs"
                    ? Math.max(0, (root.width - 2 * root.songInsetX - 2 * grid.columnSpacing) / 3) : root.tileSize
                height:item ? item.implicitHeight : 0
                sourceComponent:root.view==="songs" ? songRow : widget
                Component {
                    id:songRow
                    KeyboardAction {
                        id:song
                        objectName:"vinylSpinSong_"+entry.itemIndex
                        implicitHeight:70; showFocusFrame:false // Hemanth 2026-11-28: "the songs in the spin are small"
                        enabled:entry.modelData.available!==false && entry.modelData.unavailable!==true
                        accessibleName:entry.modelData.title
                        onTriggered:root.activateRequested(entry.modelData)
                        Loader {
                            anchors.fill: parent
                            active: song.hovered || song.activeFocus
                            sourceComponent: Rectangle {
                                objectName: song.objectName + "_lineHighlight"
                                radius: 10
                                color: Qt.rgba(1, 1, 1, 0.06)
                                border.width: song.activeFocus ? 1 : 0
                                border.color: theme.gold
                            }
                        }
                        RoundedPosterImage { x:5; y:5; width:60; height:60; radius:8; sources:[root.artwork(entry.modelData.cover)] }
                        Column {
                            x:78; y:(parent.height-height)/2; width:Math.max(0,parent.width-x-94); spacing:5
                            Text { width:parent.width; text:entry.modelData.title; textFormat:Text.PlainText; color:theme.ink; font.family:theme.ui; font.pixelSize:16; font.weight:Font.Bold; elide:Text.ElideRight }
                            Text { width:parent.width; text:Presentation.artist(entry.modelData)+(entry.modelData.albumTitle ? " · "+entry.modelData.albumTitle : ""); textFormat:Text.PlainText; color:theme.inkDimmer; font.family:theme.ui; font.pixelSize:13; elide:Text.ElideRight }
                        }
                        Row {
                            anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter; spacing:1
                            VinylFeedbackBadge { objectName:"vinylSpinOpen_"+entry.itemIndex; revealed:song.hovered || song.activeFocus || activeFocus; symbol:"show"; accessibleName:qsTr("Open %1").arg(entry.modelData.title); onTriggered:root.openRequested(entry.modelData) }
                            VinylFeedbackBadge { objectName:"vinylSpinLike_"+entry.itemIndex; revealed:song.hovered || song.activeFocus || activeFocus; symbol:"check"; accessibleName:qsTr("More like %1").arg(entry.modelData.title); onTriggered:root.feedbackRequested(entry.modelData,"like") }
                            VinylFeedbackBadge { objectName:"vinylSpinHide_"+entry.itemIndex; revealed:song.hovered || song.activeFocus || activeFocus; symbol:"hide"; accessibleName:qsTr("Hide %1").arg(entry.modelData.title); onTriggered:root.feedbackRequested(entry.modelData,"hide") }
                        }
                    }
                }
                Component {
                    id:widget
                    Item {
                        implicitHeight:card.implicitHeight
                        HoverHandler { id:tileHover }
                        QtObject {
                            id: actions
                            property var session: root.worldData && root.worldData.session ? root.worldData.session : null
                            function artwork(value) { return root.artwork(value); }
                            function activate(item) {
                                if (card.presentationKind === "artists" || card.presentationKind === "albums") root.openRequested(item);
                                else root.activateRequested(item);
                            }
                        }
                        VinylItemCard {
                            id: card
                            objectName: "vinylSpinTile_" + entry.itemIndex
                            width: parent.width; height: implicitHeight
                            item: entry.modelData; worldData: actions
                            backdrop: root.backdrop; pageFlick: root.pageFlick
                            artistRadioAction: function(item) { root.radioRequested(item); }
                            albumPlayAction: function(item) { root.activateRequested(item); }
                        }
                        VinylFeedbackBadge { objectName:"vinylSpinLike_"+entry.itemIndex; x:7; y:7; visible:card.enabled; revealed:tileHover.hovered || card.activeFocus || activeFocus; symbol:"check"; accessibleName:qsTr("More like %1").arg(entry.modelData.title); onTriggered:root.feedbackRequested(entry.modelData,"like") }
                        VinylFeedbackBadge { objectName:"vinylSpinHide_"+entry.itemIndex; x:parent.width-width-7; y:7; visible:card.enabled; revealed:tileHover.hovered || card.activeFocus || activeFocus; symbol:"hide"; accessibleName:qsTr("Hide %1").arg(entry.modelData.title); onTriggered:root.feedbackRequested(entry.modelData,"hide") }
                    }
                }
            }
        }
    }
    Rectangle {
        visible:root.view!=="songs" && root.pageItems.length>0
        x:root.width*0.92; y:grid.y; width:root.width*0.08; height:grid.height
        gradient:Gradient { orientation:Gradient.Horizontal; GradientStop { position:0; color:"transparent" } GradientStop { position:1; color:"#06070b" } }
    }
}
