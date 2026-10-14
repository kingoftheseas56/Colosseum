import QtQuick
import QtQuick.Controls
import ".."

Flickable {
    id: root
    objectName: "vinylArtistPage"
    required property var artistData
    required property var worldData
    signal albumRequested(var album)
    signal songRequested(var song)
    signal artistRequested(string name)
    property real gutter: width < 1100 ? 36 : 54
    readonly property real bodyWidth: width - gutter * 2
    readonly property int columns: bodyWidth < 1100 ? 4 : 6
    function jumpTo(section) { contentY = Math.min(section.y, Math.max(0, contentHeight - height)); }
    contentWidth: width
    contentHeight: body.height + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Theme { id: theme }
    ScrollGlide { flick: root }
    ScrollBar.vertical: HouseScrollBar { flick: root }

    Column {
        id: body
        x: root.gutter
        width: root.bodyWidth
        spacing: 24
        Row {
            spacing: 26
            Repeater {
                model: ["Overview", "Albums", "Singles & EPs", "Playlists", "About"]
                delegate: KeyboardAction {
                    required property string modelData
                    required property int index
                    objectName: "vinylArtistNav_" + index
                    width: navLabel.implicitWidth; height: 30
                    visible: index === 0 || index === 1 && root.artistData.releases.length > 0 || index === 2 && root.artistData.singles.length > 0 || index === 3 && root.artistData.playlists.length > 0 || index === 4 && root.artistData.context.history.length > 0
                    accessibleName: modelData
                    onTriggered: { if (index === 0) root.contentY = 0; else root.jumpTo([body, albumsSection, singlesSection, playlistsSection, about][index]); }
                    Text { id: navLabel; text: modelData; color: parent.interactionActive ? theme.gold : theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                }
            }
        }
        Row {
            width: parent.width
            spacing: 36
            Column {
                width: parent.width * 0.38 - 36
                spacing: 16
                Text { text: root.artistData.context.roles.toUpperCase(); color: theme.gold; font.family: theme.ui; font.pixelSize: 10; font.letterSpacing: 1.8 }
                Text { objectName: "vinylArtistName"; width: parent.width; text: root.artistData.record.name; color: theme.ink; font.family: theme.display; font.pixelSize: root.width < 1100 ? 48 : 64; wrapMode: Text.WordWrap }
                Text { width: parent.width; visible: text.length > 0; text: root.artistData.context.blurb; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; lineHeight: 1.35; wrapMode: Text.WordWrap }
                KeyboardAction {
                    objectName: "vinylArtistSave"; width: 120; height: 36
                    accessibleName: root.artistData.saved ? "Remove saved artist" : "Save artist"
                    onTriggered: root.artistData.toggleSaved()
                    Rectangle { anchors.fill: parent; radius: 18; color: parent.interactionActive ? theme.glassHi : theme.glassTint; border.color: root.artistData.saved ? theme.gold : theme.edge }
                    Text { anchors.centerIn: parent; text: root.artistData.saved ? "✓  Saved" : "+  Save artist"; color: root.artistData.saved ? theme.gold : theme.ink; font.family: theme.ui; font.pixelSize: 12 }
                }
                KeyboardAction {
                    width: 140; height: 28; visible: root.artistData.context.history.length > 0
                    accessibleName: "About the artist"; onTriggered: root.jumpTo(about)
                    Text { text: "The story behind the music ↗"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
        Column {
            width: parent.width * 0.62
            spacing: 8
            visible: root.artistData.songs.length > 0
            Text {
                text: "Selected songs"
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 28
            }
            Repeater {
                model: root.artistData.songs
                delegate: Row {
                    required property var modelData
                    required property int index
                    width: parent.width
                    height: root.height < 650 ? 40 : 48
                    spacing: 18
                    KeyboardAction {
                        objectName: "vinylArtistSong_" + index
                        width: parent.width * 0.59
                        height: parent.height
                        accessibleName: "Play " + modelData.track.title
                        onTriggered: root.songRequested(modelData)
                        Rectangle { anchors.fill: parent; radius: 6; color: theme.glassTint; visible: parent.interactionActive }
                        Text {
                            x: 8; anchors.verticalCenter: parent.verticalCenter
                            text: index + 1; color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 12
                        }
                        Column {
                            x: 40; width: parent.width - 52; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                            Text {
                                width: parent.width; text: modelData.track.title; elide: Text.ElideRight
                                color: theme.ink; font.family: theme.ui; font.pixelSize: 14
                            }
                            Text {
                                width: parent.width; text: (modelData.track.artists && modelData.track.artists.length ? modelData.track.artists : modelData.album.artists || []).join(" · "); elide: Text.ElideRight
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11
                            }
                        }
                    }
                    KeyboardAction {
                        objectName: "vinylArtistSongAlbum_" + index
                        width: parent.width * 0.31
                        height: parent.height
                        accessibleName: "Open " + modelData.album.title
                        onTriggered: root.albumRequested(modelData.album)
                        Text {
                            width: parent.width; anchors.verticalCenter: parent.verticalCenter
                            text: modelData.album.title; elide: Text.ElideRight
                            color: parent.interactionActive ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.floor(modelData.track.seconds / 60) + ":" + ("0" + modelData.track.seconds % 60).slice(-2)
                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                    }
                }
            }
            KeyboardAction {
                objectName: "vinylArtistAllSongs"
                width: 150; height: 34
                visible: root.artistData.record.songs.length > 5
                accessibleName: root.artistData.allSongs ? "Show fewer songs" : "Show all songs"
                onTriggered: root.artistData.allSongs = !root.artistData.allSongs
                Text {
                    anchors.verticalCenter: parent.verticalCenter; text: parent.accessibleName
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                }
            }
        }
        }
        Column {
            width: parent.width
            id: albumsSection
            spacing: 20
            visible: root.artistData.record.releases.length > 0
            Row {
                width: parent.width
                height: 36
                Text {
                    width: parent.width - filters.width - sort.width - 24
                    text: "Albums"
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 28
                }
                Row {
                    id: filters
                    spacing: 6
                    Repeater {
                        model: root.artistData.filters
                        delegate: KeyboardAction {
                            required property var modelData
                            objectName: "vinylArtistFilter_" + modelData.key
                            width: filterText.implicitWidth + 24
                            height: 34
                            accessibleName: modelData.label
                            onTriggered: root.artistData.filter = modelData.key
                            Rectangle {
                                anchors.fill: parent
                                radius: 17
                                color: root.artistData.filter === modelData.key ? theme.glassHi : "transparent"
                                border.color: root.artistData.filter === modelData.key ? theme.edge : "transparent"
                            }
                            Text {
                                id: filterText
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.artistData.filter === modelData.key ? theme.gold : theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 12
                            }
                        }
                    }
                }
                KeyboardAction {
                    id: sort
                    objectName: "vinylArtistSort"
                    width: 126
                    height: 34
                    accessibleName: root.artistData.newestFirst ? "Sort oldest first" : "Sort newest first"
                    onTriggered: root.artistData.newestFirst = !root.artistData.newestFirst
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.artistData.newestFirst ? "Newest first ↓" : "Oldest first ↑"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 12
                    }
                }
            }
            Grid {
                width: parent.width
                columns: root.columns
                columnSpacing: 22
                rowSpacing: 22
                Repeater {
                    model: root.artistData.releases
                    delegate: KeyboardAction {
                        required property var modelData
                        required property int index
                        objectName: "vinylArtistAlbum_" + index
                        width: (root.bodyWidth - (root.columns - 1) * 22) / root.columns
                        height: width + 66
                        accessibleName: modelData.album.title + ", " + modelData.album.year
                        onTriggered: root.albumRequested(modelData.album)
                        RoundedPosterImage {
                            objectName: "vinylArtistAlbumArt_" + index
                            width: parent.width
                            height: width
                            radius: 10
                            sources: [root.artistData.artwork(modelData.album.cover)]
                            hovered: parent.interactionActive
                        }
                        Text {
                            y: parent.width + 12
                            width: parent.width
                            text: modelData.album.title
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            y: parent.width + 36
                            text: modelData.album.year + (modelData.credit === "track" ? "  ·  Appears on" : "")
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
        VinylArtistReleaseShelf {
            id: singlesSection
            width: parent.width
            visible: records.length > 0
            title: "Singles & EPs"
            records: root.artistData.singles
            artistData: root.artistData
            columns: root.columns
            onAlbumRequested: album => root.albumRequested(album)
        }
        Column {
            id: playlistsSection
            width: parent.width; spacing: 18
            visible: root.artistData.playlists.length > 0
            Row {
                width: parent.width
                Text { width: parent.width - 180; text: "Playlists"; color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
                KeyboardAction {
                    objectName: "vinylArtistCreatePlaylist"; width: 180; height: 36; accessibleName: "Create playlist"
                    onTriggered: { playlistName.text = ""; createDialog.open(); }
                    Text { anchors.right: parent.right; text: "+  Create playlist"; color: theme.gold; font.family: theme.ui; font.pixelSize: 13 }
                }
            }
            Flow {
                width: parent.width; spacing: 18
                Repeater {
                    model: root.artistData.playlists
                    delegate: KeyboardAction {
                        required property var modelData
                        required property int index
                        objectName: "vinylArtistPlaylist_" + index
                        width: (root.bodyWidth - 36) / 3; height: 100; accessibleName: modelData.name
                        onTriggered: { playlistDialog.selection = modelData; playlistDialog.open(); }
                        Rectangle { anchors.fill: parent; radius: 10; color: parent.interactionActive ? theme.glassHi : theme.glassTint; border.color: theme.edge }
                        Image { x: 10; y: 10; width: 80; height: 80; source: root.artistData.artwork(modelData.songs[0].album.cover); fillMode: Image.PreserveAspectCrop }
                        Column {
                            x: 104; width: parent.width - 118; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                            Text { width: parent.width; text: modelData.name; elide: Text.ElideRight; color: theme.ink; font.family: theme.ui; font.pixelSize: 14 }
                            Text { width: parent.width; text: modelData.owner + " · " + modelData.songs.length + " songs"; wrapMode: Text.WordWrap; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
        Column {
            id: about
            objectName: "vinylArtistAbout"
            width: parent.width; spacing: 20
            visible: root.artistData.context.history.length > 0
            Text { text: "Behind the music"; color: theme.ink; font.family: theme.display; font.pixelSize: 32 }
            Row {
                width: parent.width; spacing: 44
                Column {
                    width: parent.width * 0.6 - 22; spacing: 18
                    Text { width: parent.width; text: root.artistData.context.history; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15; lineHeight: 1.45; wrapMode: Text.WordWrap }
                    Row {
                        spacing: 22
                        Repeater {
                            model: root.artistData.context.sources
                            delegate: KeyboardAction {
                                required property var modelData
                                width: sourceText.implicitWidth; height: 30; accessibleName: "Read " + modelData.title
                                onTriggered: Qt.openUrlExternally(modelData.url)
                                Text { id: sourceText; text: modelData.title + " ↗"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12 }
                            }
                        }
                    }
                }
                Column {
                    width: parent.width * 0.4 - 22; spacing: 14
                    Repeater {
                        model: root.artistData.context.facts
                        delegate: Row {
                            required property var modelData
                            width: parent.width; spacing: 14
                            Text { width: 104; text: modelData.label; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                            Text { width: parent.width - 118; text: modelData.value; wrapMode: Text.WordWrap; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                        }
                    }
                }
            }
            Row {
                width: parent.width; spacing: 24
                Repeater {
                    model: root.artistData.context.timeline
                    delegate: Column {
                        required property var modelData
                        width: (parent.width - (root.artistData.context.timeline.length - 1) * 24) / root.artistData.context.timeline.length; spacing: 10
                        Rectangle { width: parent.width; height: 1; color: theme.edge }
                        Text { text: modelData.year; color: theme.gold; font.family: theme.display; font.pixelSize: 26 }
                        Text { width: parent.width; text: modelData.title; wrapMode: Text.WordWrap; color: theme.ink; font.family: theme.ui; font.pixelSize: 14 }
                        Text { width: parent.width; text: modelData.text; wrapMode: Text.WordWrap; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; lineHeight: 1.3 }
                    }
                }
            }
        }
        Column {
            width: parent.width
            spacing: 14
            visible: root.artistData.record.collaborators.length > 0
            Text {
                text: "Shared credits"
                color: theme.ink; font.family: theme.display; font.pixelSize: 28
            }
            Flow {
                width: parent.width; spacing: 10
                Repeater {
                    model: root.artistData.record.collaborators
                    delegate: KeyboardAction {
                        required property string modelData
                        objectName: "vinylArtistCredit_" + modelData
                        width: creditText.implicitWidth + 28; height: 38
                        accessibleName: modelData
                        onTriggered: root.artistRequested(modelData)
                        Rectangle { anchors.fill: parent; radius: 19; color: parent.interactionActive ? theme.glassHi : theme.glassTint }
                        Text {
                            id: creditText
                            anchors.centerIn: parent; text: modelData
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: createDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        title: "Create playlist"
        modal: true
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.base: "#22232a"
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        palette.highlight: theme.gold
        palette.highlightedText: "#06070b"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.artistData.createPlaylist(playlistName.text)
        Column {
            width: parent.width; spacing: 14
            Label { width: parent.width; text: "Start with the displayed songs. This prototype keeps playlists for this session."; wrapMode: Text.WordWrap }
            TextField { id: playlistName; objectName: "vinylArtistPlaylistName"; width: parent.width; placeholderText: "Playlist name"; maximumLength: 80 }
        }
        onOpened: playlistName.forceActiveFocus()
    }
    Dialog {
        id: playlistDialog
        property var selection: ({name:"",songs:[]})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 520; height: Math.min(500, parent.height - 80)
        title: selection.name
        modal: true
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.base: "#22232a"
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        palette.highlight: theme.gold
        palette.highlightedText: "#06070b"
        standardButtons: Dialog.Close
        contentItem: ListView {
            clip: true; model: playlistDialog.selection.songs; spacing: 4
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                objectName: "vinylArtistPlaylistSong_" + index
                width: ListView.view.width
                text: modelData.track.title
                onClicked: { root.songRequested(modelData); playlistDialog.close(); }
            }
        }
    }
}
