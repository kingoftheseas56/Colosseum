import QtQuick
import QtQuick.Effects
import ".."

Item {
    id: root
    objectName: "vinylSongPage"
    required property var pageData
    readonly property string songTitle: pageData.playingTrack.title
    readonly property bool lyricsShown: pageData.lyricsShown
    readonly property bool autoNotes: pageData.autoNotes
    readonly property bool videoShown: pageData.videoShown
    readonly property real cardHeight: Math.min(470, Window.height * 0.48)
    signal albumRequested
    Theme {
        id: theme
    }
    Item {
        id: room
        width: parent.width
        height: root.cardHeight + 82
        anchors.verticalCenter: parent.verticalCenter
        Item {
            id: poster
            objectName: "vinylSongPoster"
            x: (root.width - width) / 2
            width: root.videoShown ? root.cardHeight * 16 / 9 : root.cardHeight * (root.pageData.playingEdition.id === "mounamela" ? 0.667 : 1)
            height: root.cardHeight
            Image {
                id: cover
                anchors.fill: parent
                visible: false
                source: root.pageData.artwork(root.pageData.playingEdition.cover)
                fillMode: Image.PreserveAspectCrop
            }
            Rectangle {
                id: mask
                anchors.fill: parent
                radius: 18
                visible: false
                color: theme.ink
                layer.enabled: true
            }
            MultiEffect {
                anchors.fill: parent
                source: cover
                maskEnabled: true
                maskSource: mask
            }
            Rectangle {
                anchors.fill: parent
                radius: 18
                color: Qt.rgba(0, 0, 0, 0)
                border.color: theme.edge
            }
            KeyboardAction {
                anchors.fill: parent
                accessibleName: "Open album"
                onTriggered: root.albumRequested()
            }
            Item {
                objectName: "vinylSongVideoFrame"
                anchors.fill: parent
                visible: root.videoShown
                Rectangle {
                    anchors.fill: parent
                    color: theme.biblioWashBottom
                    opacity: 0.3
                    radius: 18
                }
                Text {
                    x: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    text: "OFFICIAL VIDEO · FIXTURE STILL"
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.letterSpacing: 1.6
                }
            }
            Column {
                y: parent.height + 18
                width: parent.width
                spacing: 6
                Text {
                    objectName: "vinylSongTitle"
                    width: parent.width
                    text: root.songTitle
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: Math.min(34, Window.height * 0.038)
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "vinylSongArtist"
                    width: parent.width
                    text: root.pageData.playingEdition.artist
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
            }
        }
        VinylLyricsColumn {
            x: Math.max(54, poster.x - width - 52)
            width: Math.min(380, poster.x - 106)
            height: root.cardHeight
            pageData: root.pageData
            visible: root.lyricsShown && !root.videoShown
        }
        VinylNotesColumn {
            x: poster.x + poster.width + 52
            width: Math.min(380, root.width - x - 54)
            height: root.cardHeight
            pageData: root.pageData
            visible: root.lyricsShown && !root.videoShown && pageData.activeNoteLine >= 0
        }
    }
}
