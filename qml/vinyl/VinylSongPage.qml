import QtQuick
import QtQuick.Effects
import ".."

Item {
    id: root
    objectName: "vinylSongPage"
    required property var pageData
    property bool artistNavigationEnabled: false
    property string backLabel: "Back"
    signal artistRequested(string name)
    readonly property string songTitle: pageData.playingTrack.title
    readonly property bool lyricsShown: pageData.lyricsShown
    readonly property bool autoNotes: pageData.autoNotes
    readonly property bool videoShown: pageData.videoShown
    readonly property real cardHeight: Math.max(96, Math.min(470, Window.height * 0.48, height - 140, width * 0.48))
    signal backRequested
    signal albumRequested
    function focusBackControl() { backAction.forceActiveFocus(); }
    Theme {
        id: theme
    }
    BackAction {
        id: backAction
        objectName: "vinylSongBack"
        x: theme.margin
        y: 4
        label: root.backLabel
        width: Math.max(92, implicitWidth)
        height: 44
        Keys.onReturnPressed: root.backRequested()
        Keys.onEnterPressed: root.backRequested()
        FocusRing { anchors.fill: parent; shown: backAction.activeFocus; radius: 7 }
        onTriggered: root.backRequested()
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
            Rectangle { anchors.fill: parent; radius: 18; color: theme.glassTint }
            Text { anchors.centerIn: parent; text: "♪"; font.pixelSize: parent.height * 0.24; color: theme.inkDimmer; visible: !root.pageData.playingEdition.cover }
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
                    KeyboardAction {
                        objectName: "vinylSongArtistLink"
                        anchors.fill: parent
                        visible: root.artistNavigationEnabled
                        accessibleName: parent.text
                        onTriggered: root.artistRequested(accessibleName)
                    }
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
