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
    property real tileSize:130
    property int page:0
    readonly property var items: view==="albums" ? albums : view==="artists" ? artists : songs
    readonly property int columns: view==="songs" ? 3 : Math.max(1,Math.min(8,Math.floor(width/tileSize)))
    readonly property int pageSize: view==="songs" ? 12 : columns
    readonly property int pageCount: Math.ceil((items ? items.length : 0)/pageSize)
    readonly property var pageItems: items ? items.slice(page*pageSize,(page+1)*pageSize) : []
    readonly property int currentPage:page
    implicitHeight:header.height+(grid.height>0 ? 20+grid.height : 0)
    signal activateRequested(var item)
    signal openRequested(var item)
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
    Grid {
        id:grid
        y:header.height+20
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
                width:root.view==="songs" ? Math.max(0,(root.width-40)/3) : root.tileSize
                height:item ? item.implicitHeight : 0
                sourceComponent:root.view==="songs" ? songRow : widget
                Component {
                    id:songRow
                    KeyboardAction {
                        id:song
                        objectName:"vinylSpinSong_"+entry.itemIndex
                        implicitHeight:54; focusRadius:10
                        enabled:entry.modelData.available!==false && entry.modelData.unavailable!==true
                        accessibleName:entry.modelData.title
                        onTriggered:root.activateRequested(entry.modelData)
                        Rectangle { anchors.fill:parent; radius:10; color:song.hovered ? "#12ffffff" : "transparent" }
                        RoundedPosterImage { x:5; y:5; width:44; height:44; radius:7; sources:[root.artwork(entry.modelData.cover)] }
                        Column {
                            x:60; y:(parent.height-height)/2; width:Math.max(0,parent.width-x-94); spacing:4
                            Text { width:parent.width; text:entry.modelData.title; textFormat:Text.PlainText; color:theme.ink; font.family:theme.ui; font.pixelSize:14; font.weight:Font.Bold; elide:Text.ElideRight }
                            Text { width:parent.width; text:Presentation.artist(entry.modelData)+(entry.modelData.albumTitle ? " · "+entry.modelData.albumTitle : ""); textFormat:Text.PlainText; color:theme.inkDimmer; font.family:theme.ui; font.pixelSize:12; elide:Text.ElideRight }
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
                        QtObject { id:actions; property var session:root.worldData && root.worldData.session ? root.worldData.session : null; function artwork(value){return root.artwork(value)} function activate(item){root.activateRequested(entry.modelData)} }
                        VinylItemCard { id:card; objectName:"vinylSpinTile_"+entry.itemIndex; width:parent.width; height:implicitHeight; item:entry.modelData; worldData:actions; backdrop:root.backdrop; pageFlick:root.pageFlick }
                        VinylFeedbackBadge { objectName:"vinylSpinOpen_"+entry.itemIndex; x:(parent.width-width)/2; y:(root.tileSize-height)/2; visible:card.enabled; revealed:tileHover.hovered || card.activeFocus || activeFocus; symbol:"show"; accessibleName:qsTr("Open %1").arg(entry.modelData.title); onTriggered:root.openRequested(entry.modelData) }
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
