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
    function toggleArtist(id) { worldData.togglePickerArtist(id) }
    Item {
        id: header
        width: parent.width
        height: 110
        Text {
            objectName: "vinylPickerQuestion"
            text: "What do you listen to?"
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
    Flickable {
        id: artistScroll
        objectName: "vinylPickerScroll"
        y: header.height+30
        width: parent.width
        height: parent.height-y
        contentWidth: width
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Grid {
            id: grid
            columns: root.width>=1300 ? 7 : 6
            columnSpacing: 24
            rowSpacing: 28
            Repeater {
                model: root.offered
                delegate: KeyboardAction {
                    required property var modelData
                    required property int index
                    objectName: "vinylPickerArtist_"+index
                    width: (root.width-(grid.columns-1)*24)/grid.columns
                    height: width+label.implicitHeight+18
                    accessibleName: modelData.title
                    onTriggered: root.toggleArtist(modelData.id)
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
        ScrollBar.vertical: HouseScrollBar { flick: artistScroll }
    }
}
