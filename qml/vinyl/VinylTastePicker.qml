import QtQuick
import QtQuick.Controls
import ".."
Item {
    id: root
    objectName: "vinylTastePicker"
    required property var worldData
    required property Item backdrop
    required property Flickable pageFlick
    readonly property string query: worldData.pickerQuery
    readonly property var selected: worldData.pickerDraft
    readonly property var offered: worldData.artistOptions(query)
    // The picker owns one page viewport: the header with the question, the
    // search field and Skip/Done stays pinned while the artists scroll inside.
    implicitHeight: pageFlick ? pageFlick.height : header.height + 30 + grid.implicitHeight
    Theme { id: theme }
    property int currentIndex: 0
    Keys.onPressed: event=>nav.handle(event)
    KeyboardCollectionController {
        id:nav;view:root;count:root.offered.length;orientation:"grid";columns:grid.columns
        positionIndexFn:function(index){var item=artistCards.itemAt(index);if(!item)return;artistScroll.contentY=Math.max(0,Math.min(Math.max(0,artistScroll.contentHeight-artistScroll.height),item.y));Qt.callLater(function(){var card=artistCards.itemAt(index);if(card)card.forceActiveFocus()})}
        onActivated:index=>root.toggleArtist(root.offered[index].id)
    }
    function toggleArtist(id) { worldData.togglePickerArtist(id) }
    Item {
        id: header
        width: parent.width
        height: 140
        Text {
            objectName: "vinylPickerQuestion"
            text: "What do you listen to?"
            width: parent.width
            elide: Text.ElideRight
            fontSizeMode: Text.Fit
            minimumPixelSize: 24
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 40
        }
        Glass {
            y: 64
            width: Math.min(430,parent.width-220)
            height: 40
            backdrop: root.backdrop
            radius: 12
            TextField {
                id: search
                objectName: "vinylPickerSearch"
                anchors.fill: parent
                anchors.rightMargin: 36
                text: root.query
                onTextEdited: root.worldData.setPickerQuery(text)
                placeholderText: "Search artists"
                color: theme.ink
                placeholderTextColor: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 14
                background: Item {}
            }
            KeyboardAction {
                width: 32; height: 40
                anchors.right: parent.right
                visible: root.query.length>0
                accessibleName: "Clear artist search"
                onTriggered: {root.worldData.setPickerQuery("");search.forceActiveFocus()}
                Text { anchors.centerIn: parent; text:"×";color:theme.ink;font.family:theme.ui;font.pixelSize:20 }
            }
        }
        Row {
            anchors.right: parent.right
            y: 64
            spacing: 10
            VinylWorldButton {
                objectName: "vinylPickerSkip"
                label: "Skip"
                backdrop: root.backdrop
                onTriggered: root.worldData.skipPicker()
            }
            VinylWorldButton {
                objectName: "vinylPickerDone"
                label: "Done"
                selected: true
                backdrop: root.backdrop
                onTriggered: root.worldData.answerPicker(root.selected)
            }
        }
    }
    Text {
        y: 112; width: parent.width
        text: root.worldData.tasteNotice !== undefined ? root.worldData.tasteNotice : ""
        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
    }
    Text {
        visible:root.offered.length===0;y:header.height+30;width:parent.width
        text:root.query ? "No local artists match." : "Open local music to choose artists, or skip for now."
        color:theme.inkDim;font.family:theme.ui;font.pixelSize:14;wrapMode:Text.WordWrap
    }
    Flickable {
        id: artistScroll
        objectName: "vinylPickerScroll"
        y: header.height+30
        width: parent.width
        height: Math.max(0,parent.height-y)
        contentWidth: width
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Grid {
            id: grid
            columns: Math.max(2,Math.min(7,Math.floor((root.width+24)/150)))
            columnSpacing: 24
            rowSpacing: 28
            Repeater {
                id:artistCards
                model: root.offered
                delegate: KeyboardAction {
                    required property var modelData
                    required property int index
                    objectName: "vinylPickerArtist_"+index
                    width: (root.width-(grid.columns-1)*24)/grid.columns
                    height: width+label.implicitHeight+18
                    accessibleName: modelData.title
                    onTriggered: root.toggleArtist(modelData.id)
                    onActiveFocusChanged:if(activeFocus)root.currentIndex=index
                    Keys.onPressed:event=>{if([Qt.Key_Left,Qt.Key_Right,Qt.Key_Up,Qt.Key_Down,Qt.Key_Home,Qt.Key_End].indexOf(event.key)>=0)nav.handle(event)}
                    RoundedPosterImage {
                        width: parent.width; height: width; radius: width/2
                        sources: [root.worldData.artwork(modelData.cover)]
                        hovered: root.selected.indexOf(modelData.id)>=0
                    }
                    Text {
                        id: label
                        y: parent.width+12
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: parent.modelData.title
                        wrapMode: Text.WordWrap
                        color: root.selected.indexOf(parent.modelData.id)>=0 ? theme.gold : theme.ink
                        font.family: theme.ui; font.pixelSize: 14
                    }
                }
            }
        }
    }
}
