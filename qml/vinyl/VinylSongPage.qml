import QtQuick
import QtQuick.Effects
import ".."

Item {
    id: root
    objectName: "vinylSongPage"
    required property var pageData
    property bool artistNavigationEnabled: false
    property string backLabel: "Back"
    property bool browseMode: false
    signal playRequested()
    signal creditsRequested()
    signal artistRequested(string name)
    readonly property string songTitle: pageData.playingTrack.title
    readonly property bool lyricsShown: pageData.lyricsShown
    readonly property bool autoNotes: pageData.autoNotes
    readonly property bool videoShown: pageData.videoShown
    readonly property real cardHeight: Math.max(96, Math.min(470, Window.height * 0.48, height - 140, width * 0.48))
    signal backRequested
    signal albumRequested
    function focusBackControl() { backAction.focusControl(Qt.TabFocusReason); }
    Theme {
        id: theme
    }
    BackAction {
        focusOnPointer: false
        id: backAction
        objectName: "vinylSongBack"
        x: theme.margin
        y: 4
        label: root.backLabel
        width: Math.max(92, implicitWidth)
        height: 44
        Keys.onReturnPressed: root.backRequested()
        Keys.onEnterPressed: root.backRequested()
        onTriggered: root.backRequested()
    }
    Row {
        objectName: "vinylSongBrowseActions"
        anchors.right: parent.right; anchors.rightMargin: theme.margin; y: 4; spacing: 8
        VinylWorldButton { objectName: "vinylSongPlay"; visible: root.browseMode; label: "Play"; symbol: "play"; onTriggered: root.playRequested() }
        VinylWorldButton { objectName: "vinylSongLyrics"; label: "Lyrics"; symbol: "lyrics"; enabled: root.pageData.hasLyrics; selected: root.pageData.lyricsShown; onTriggered: root.pageData.lyricsShown=!root.pageData.lyricsShown }
        VinylWorldButton { objectName: "vinylSongNotes"; label: "Notes"; symbol: "notes"; enabled: root.pageData.hasNotes; selected: root.pageData.autoNotes; onTriggered: {root.pageData.autoNotes=!root.pageData.autoNotes;root.pageData.lyricsShown=true;} }
        VinylWorldButton { objectName: "vinylSongCredits"; label: "Credits"; symbol: "credits"; enabled: root.pageData.playingCredits.length>0; onTriggered: root.creditsRequested() }
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
                focusOnPointer: false
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
                    text: "Video artwork"
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
                VinylArtistLinks {
                    objectName: "vinylSongArtist"
                    width: parent.width
                    names: root.pageData.playingTrack.artists || root.pageData.playingEdition.artists || (root.pageData.playingEdition.sourceAlbum && root.pageData.playingEdition.sourceAlbum.artists) || []
                    fallbackName: root.pageData.playingEdition.artist
                    navigationEnabled: root.artistNavigationEnabled
                    fontSize: 14
                    onArtistRequested: name => root.artistRequested(name)
                }
            }
        }
        VinylLyricsColumn {
            x: Math.max(54, poster.x - width - 52)
            width: Math.max(0, Math.min(380, poster.x - 106))
            height: root.cardHeight
            pageData: root.pageData
            visible: root.width >= 1000 && root.lyricsShown && !root.videoShown
        }
        VinylNotesColumn {
            x: poster.x + poster.width + 52
            width: Math.max(0, Math.min(380, root.width - x - 54))
            height: root.cardHeight
            pageData: root.pageData
            visible: root.width >= 1000 && root.lyricsShown && !root.videoShown && pageData.activeNoteLine >= 0
        }
    }
    Rectangle {
        objectName: "vinylCompactLyrics"
        visible: (root.width < 1000 || !root.pageData.hasLyrics) && root.lyricsShown
        x: theme.margin; y: 60; width: root.width-2*x; height: Math.max(0,root.height-y)
        color: theme.biblioWashBottom; radius: 16
        Flickable {
            id: compactScroll
            anchors.fill: parent; anchors.margins: 18; clip: true
            contentHeight: compactLines.height; boundsBehavior: Flickable.StopAtBounds
            ScrollGlide { flick: compactScroll }
            Column {
                id: compactLines; width: parent.width; spacing: 16
                Repeater {
                    model: !root.pageData.hasLyrics ? Object.keys(root.pageData.notes) : []
                    delegate: Column {
                        required property string modelData
                        width:compactLines.width;spacing:12
                        Repeater {
                            model:root.pageData.notes[parent.modelData] || []
                            delegate: Text { required property var modelData; width:compactLines.width; text:modelData.text || "";color:theme.ink;font.family:theme.ui;font.pixelSize:16;wrapMode:Text.WordWrap }
                        }
                    }
                }
                Repeater {
                    model: root.pageData.lyrics
                    delegate: Column {
                        required property var modelData; required property int index
                        width: compactLines.width; spacing: 8
                        Text { width: parent.width; text: parent.modelData.text; color: theme.ink; font.family: theme.ui; font.pixelSize: 17; wrapMode: Text.WordWrap }
                        Repeater {
                            model: root.pageData.notes[index] || []
                            delegate: Text { required property var modelData; width: compactLines.width; text: modelData.text || ""; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
        }
    }
}
