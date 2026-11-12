import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "vinylSongPage"
    required property var pageData
    property bool artistNavigationEnabled: false
    property string backLabel: "Back"
    property bool browseMode: false
    property bool videoFullscreen: false
    property bool videoControlsVisible: true
    signal videoFullscreenRequested()
    signal videoInteraction()
    signal playRequested()
    signal artistRequested(string name)
    readonly property string songTitle: pageData.playingTrack.title
    readonly property bool lyricsShown: pageData.lyricsShown
    readonly property bool autoNotes: pageData.autoNotes
    readonly property bool videoShown: pageData.videoShown
    property alias videoSurface: videoFrame
    readonly property bool stackedReaders: width < 720
    readonly property real sideInset: Math.max(20, Math.min(54, width * 0.034))
    readonly property real sideGap: Math.max(18, Math.min(64, width * 0.034))
    readonly property real headerBottom: backAction.y + backAction.height
    readonly property real artworkTop: stackedReaders ? headerBottom + 18 : 4
    // The fixed metadata reservation keeps toggling reading tools from moving the cover.
    readonly property real centerWidth: Math.max(96, Math.min(540,
        stackedReaders ? width - 2 * sideInset : width * 0.36,
        stackedReaders ? height * 0.38 : height - artworkTop - 108))
    readonly property real cardHeight: centerWidth
    readonly property real videoFrameHeight: Math.max(0, Math.min(height, width * 9 / 16))
    readonly property real readingTop: stackedReaders
        ? room.y + cardHeight + 10 + songMetadata.implicitHeight + 54
        : room.y + 42
    readonly property real readingHeight: Math.max(0, height - readingTop - 8)
    readonly property real notesAreaX: stackedReaders ? width / 2 + sideGap / 2
        : (width + centerWidth) / 2 + sideGap
    readonly property real readerWidth: stackedReaders
        ? Math.max(0, (width - 2 * sideInset - sideGap) / 2)
        : Math.max(0, Math.min(380, (width - centerWidth) / 2 - sideInset - sideGap))
    readonly property var displayedGeniusNotes: {
        if (!notesColumn.visible)
            return [];
        return notesColumn.shownNotes.filter(note => note && note.source === "genius");
    }
    readonly property var annotationSources: {
        var result = [];
        for (var i = 0; i < displayedGeniusNotes.length; ++i) {
            var url = notesColumn.safeGeniusUrl(displayedGeniusNotes[i].sourceUrl);
            if (url.length && result.indexOf(url) < 0)
                result.push(url);
        }
        return result;
    }
    readonly property string annotationSourceUrl: {
        if (annotationSources.length)
            return annotationSources[0];
        if (pageData.annotationsStatus === "ready" && pageData.annotationCount > 0
                && /^[1-9][0-9]*$/.test(String(pageData.geniusSongId)))
            return "https://genius.com/songs/" + pageData.geniusSongId;
        return "";
    }
    readonly property var annotationContributors: {
        var result = [], seen = {};
        for (var i = 0; i < displayedGeniusNotes.length; ++i) {
            var authors = displayedGeniusNotes[i].authors || [];
            for (var j = 0; j < authors.length; ++j) {
                var author = authors[j];
                if (!author || typeof author.name !== "string" || !author.name.trim())
                    continue;
                var name = author.name.trim();
                var key = author.id !== undefined && author.id !== null && String(author.id).length
                    ? JSON.stringify(["id", String(author.id)]) : JSON.stringify(["name", name]);
                if (seen[key])
                    continue;
                seen[key] = true;
                result.push({ name: name, id: author.id, url: author.url || "" });
            }
        }
        return result;
    }
    readonly property bool canSavePlayingSong: !!pageData
        && typeof pageData.personalEnabled === "boolean"
        && typeof pageData.songIsSaved === "boolean"
        && typeof pageData.togglePlayingSongSaved === "function"
    readonly property bool canAddPlayingSongToPlaylist: !!pageData
        && typeof pageData.personalEnabled === "boolean"
        && typeof pageData.addPlayingSongToPlaylist === "function"
    readonly property bool canEnqueuePlayingSong: !!pageData
        && typeof pageData.enqueuePlayingSong === "function"
        && !!pageData.playingTrack && pageData.playingTrack.available !== false
    signal backRequested
    signal albumRequested
    function focusBackControl() { backAction.focusControl(Qt.TabFocusReason); }
    Theme {
        id: theme
    }
    Rectangle {
        objectName: "vinylSongVideoStage"
        anchors.fill: parent
        visible: root.videoShown
        color: "#000000"
    }
    BackAction {
        focusOnPointer: false
        visible: opacity > 0.01
        enabled: visible
        opacity: root.videoFullscreen && root.videoShown && !root.videoControlsVisible ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 140 } }
        id: backAction
        objectName: "vinylSongBack"
        x: root.videoShown ? 12 : theme.margin
        y: root.videoShown ? (root.height - root.videoFrameHeight) / 2 + 12 : 4
        z: root.videoShown ? 20 : 0
        label: root.backLabel
        width: Math.max(92, implicitWidth)
        height: 44
        Keys.onReturnPressed: root.backRequested()
        Keys.onEnterPressed: root.backRequested()
        onTriggered: root.backRequested()
        Rectangle {
            objectName: "vinylSongBackScrim"
            anchors.fill: parent
            visible: root.videoShown
            z: -1
            radius: 10
            color: "#b3000000"
        }
    }
    VinylSongActionMenu {
        id: songActions
        objectName: "vinylSongPageActions"
        x: root.width - width - (root.videoShown ? 12 : theme.margin)
        y: root.videoShown ? backAction.y : (root.width < 560 ? 56 : 9)
        z: 20
        opacity: root.videoFullscreen && root.videoShown && !root.videoControlsVisible ? 0 : 1
        visible: opacity > 0.01
        enabled: visible
        Behavior on opacity { NumberAnimation { duration: 140 } }
        collectionHost: songCollectionHost
        personalHost: songPersonalHost
        playlistHost: songPlaylistHost
        lyricsHost: lyricsHeading
        notesHost: notesHeading
        songData: root.pageData
        showPlay: root.browseMode
        showReaderTools: true
        showLyricsTools: !root.videoShown
        videoFullscreen: root.videoFullscreen
        onPlayRequested: root.playRequested()
        onVideoFullscreenRequested: { root.videoInteraction(); root.videoFullscreenRequested(); }
        onVideoRequested: { root.videoInteraction(); root.pageData.toggleVideo(); }
    }
    Item {
        id: songCollectionHost
        objectName: "vinylSongCollectionActions"
        x: root.width - width - theme.margin
        y: backAction.y + (backAction.height - height) / 2
        width: songActions.collectionWidth
        height: 34
        visible: !root.videoShown
    }
    Item {
        id: room
        width: parent.width
        visible: !(root.videoFullscreen && root.videoShown)
        height: root.videoShown ? root.videoFrameHeight : root.cardHeight + 18 + songMetadata.implicitHeight
        y: root.videoShown ? (root.height - height) / 2
            : root.artworkTop
        Item {
            id: poster
            objectName: "vinylSongPoster"
            x: (root.width - width) / 2
            width: root.videoShown ? root.videoFrameHeight * 16 / 9
                : root.cardHeight * (root.pageData.playingEdition.id === "mounamela" ? 0.667 : 1)
            height: root.videoShown ? root.videoFrameHeight : root.cardHeight
            Rectangle {
                anchors.fill: parent
                radius: 18
                color: theme.glassTint
                visible: !root.videoShown
            }
            Text {
                anchors.centerIn: parent
                text: "♪"
                font.pixelSize: parent.height * 0.24
                color: theme.inkDimmer
                visible: !root.videoShown && !root.pageData.playingEdition.cover
            }
            RoundedPosterImage {
                objectName: "vinylSongArtwork"
                anchors.fill: parent
                radius: 18
                sources: root.pageData.playingEdition.cover
                    ? [root.pageData.artwork(root.pageData.playingEdition.cover)] : []
                visible: !root.videoShown
            }
            KeyboardAction {
                focusOnPointer: false
                anchors.fill: parent
                accessibleName: "Open album"
                visible: !root.videoShown
                enabled: !root.videoShown
                onTriggered: root.albumRequested()
            }
            Item {
                id: videoFrame
                objectName: "vinylSongVideoFrame"
                anchors.fill: parent
                visible: root.videoShown
                Rectangle {
                    anchors.fill: parent
                    color: "#000000"
                }
                MouseArea {
                    objectName: "vinylSongVideoPointer"
                    anchors.fill: parent
                    z: 1000
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    onEntered: root.videoInteraction()
                    onPositionChanged: root.videoInteraction()
                    onClicked: root.videoInteraction()
                    onDoubleClicked: {
                        root.videoInteraction()
                        root.videoFullscreenRequested()
                    }
                }
            }
            Column {
                id: songMetadata
                visible: !root.videoShown
                objectName: "vinylSongMetadata"
                x: (parent.width - width) / 2
                y: parent.height + 10
                width: root.centerWidth
                spacing: 2
                Item {
                    width: parent.width
                    height: 38
                    Item {
                        id: songPersonalHost
                        objectName: "vinylSongPersonalActions"
                        width: songActions.personalWidth
                        height: 34
                        x: Math.max(0, (parent.width - songTitleText.width) / 2 - width - 12)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: songTitleText
                        objectName: "vinylSongTitle"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(implicitWidth, Math.max(0, parent.width
                            - 2 * (Math.max(songActions.personalWidth, songActions.playlistWidth) + 12)))
                        text: root.songTitle
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: root.stackedReaders ? 28 : 30
                        maximumLineCount: 1
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Item {
                        id: songPlaylistHost
                        objectName: "vinylSongPlaylistActions"
                        x: (parent.width + songTitleText.width) / 2 + 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: songActions.playlistWidth
                        height: 34
                    }
                }
                TextMetrics {
                    id: artistNameMetrics
                    text: songArtists.displayedNames.join(" · ")
                    font.family: theme.ui
                    font.pixelSize: 14
                }
                Item {
                    width: parent.width
                    height: 24
                    Row {
                        id: songIdentity
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 6
                        VinylArtistLinks {
                            id: songArtists
                            objectName: "vinylSongArtist"
                            width: Math.min(songMetadata.width * 0.55, Math.max(24, artistNameMetrics.advanceWidth))
                            names: root.pageData.playingTrack.artists || root.pageData.playingEdition.artists || (root.pageData.playingEdition.sourceAlbum && root.pageData.playingEdition.sourceAlbum.artists) || []
                            fallbackName: root.pageData.playingEdition.artist
                            navigationEnabled: root.artistNavigationEnabled
                            fontSize: 14
                            onArtistRequested: name => root.artistRequested(name)
                        }
                        Text {
                            text: "\u00b7"
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                        }
                        KeyboardAction {
                            objectName: "vinylSongAlbum"
                            width: Math.min(albumLabel.implicitWidth,
                                Math.max(0, songMetadata.width - songArtists.width - 20))
                            height: 24
                            focusOnPointer: false
                            accessibleName: qsTr("Open album %1").arg(albumLabel.text)
                            onTriggered: root.albumRequested()
                            Text {
                                id: albumLabel
                                anchors.fill: parent
                                text: root.pageData.playingEdition.title || root.pageData.playingEdition.name || ""
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Item {
                    width: parent.width
                    height: 32
                    VinylSourceAttribution {
                        objectName: "vinylSongSources"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.lyricsShown && !root.videoShown
                        lyricsSourceUrl: root.pageData.lyricsSource === "lrclib" ? "https://lrclib.net" : ""
                        annotationSourceUrl: root.annotationSourceUrl
                        annotationSources: root.annotationSources
                        contributors: root.annotationContributors
                        contextKey: JSON.stringify([root.pageData.readingContextKey,
                            root.pageData.lyricsDocumentId, notesColumn.shownLineIndex])
                    }
                }
            }
        }
        Item {
            id: lyricsHeading
            objectName: "vinylSongLyricsHeading"
            x: root.stackedReaders ? root.sideInset
                : (root.width - root.centerWidth) / 2 - root.sideGap - width
            y: root.stackedReaders ? root.readingTop - room.y - 42 : 0
            width: songActions.lyricsWidth
            height: 34
            visible: !root.videoShown
        }
        Item {
            id: notesHeading
            objectName: "vinylSongNotesHeading"
            x: root.notesAreaX
            y: lyricsHeading.y
            width: songActions.notesWidth
            height: 34
            visible: !root.videoShown
        }
        VinylLyricsColumn {
            x: root.sideInset
            y: root.readingTop - room.y
            width: root.readerWidth
            height: root.readingHeight
            pageData: root.pageData
            showSourceCredit: false
            visible: root.lyricsShown && pageData.hasLyrics && !root.videoShown && width > 0
        }
        VinylNotesColumn {
            id: notesColumn
            x: root.notesAreaX
            y: root.readingTop - room.y
            width: root.readerWidth
            height: root.readingHeight
            pageData: root.pageData
            inlineAttribution: false
            visible: root.lyricsShown && !root.videoShown
                && (pageData.activeNoteLine >= 0 || notesColumn.hasAutomaticNote) && width > 0
        }
    }
    states: State {
        name: "videoFullscreen"
        when: root.videoFullscreen && root.videoShown
        ParentChange {
            target: videoFrame
            parent: root
        }
        PropertyChanges {
            videoFrame.z: 10
        }
    }

}
