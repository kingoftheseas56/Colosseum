import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
    signal creditsRequested()
    signal artistRequested(string name)
    readonly property string songTitle: pageData.playingTrack.title
    readonly property bool lyricsShown: pageData.lyricsShown
    readonly property bool autoNotes: pageData.autoNotes
    readonly property bool videoShown: pageData.videoShown
    property alias videoSurface: videoFrame
    readonly property real centerWidth: Math.max(96, Math.min(470, Window.height * 0.48, width * 0.38))
    readonly property real videoFrameHeight: Math.max(0, Math.min(height, width * 9 / 16))
    readonly property real cardHeight: Math.max(96, Math.min(centerWidth,
        height - backAction.y - backAction.height - songMetadata.implicitHeight - 38))
    readonly property real readingHeight: Math.max(cardHeight, height - room.y - 8)
    readonly property real sideInset: Math.max(24, Math.min(54, width * 0.04))
    readonly property real sideGap: Math.max(18, Math.min(52, width * 0.04))
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
        visible: !(root.videoFullscreen && root.videoShown) && opacity > 0.01
        enabled: visible
        opacity: root.videoShown && !root.videoControlsVisible ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 140 } }
        id: backAction
        objectName: "vinylSongBack"
        x: root.videoShown ? (root.width - root.videoFrameHeight * 16 / 9) / 2 + 12 : theme.margin
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
    Item {
        id: room
        width: parent.width
        visible: !(root.videoFullscreen && root.videoShown)
        height: root.videoShown ? root.videoFrameHeight : root.cardHeight + 18 + songMetadata.implicitHeight
        y: root.videoShown ? (root.height - height) / 2
            : backAction.y + backAction.height + Math.max(12,
            (root.height - backAction.y - backAction.height - height) / 2)
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
                visible: !root.videoShown
            }
            Rectangle {
                anchors.fill: parent
                radius: 18
                color: Qt.rgba(0, 0, 0, 0)
                border.color: theme.edge
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
                y: parent.height + 18
                width: root.videoShown
                    ? Math.min(root.centerWidth * 16 / 9, Math.max(0, root.width - 2 * root.sideInset))
                    : root.centerWidth * (root.pageData.playingEdition.id === "mounamela" ? 0.667 : 1)
                spacing: 6
                Text {
                    objectName: "vinylSongTitle"
                    width: parent.width
                    text: root.songTitle
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: Math.min(34, Window.height * 0.038)
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
                TextMetrics {
                    id: artistNameMetrics
                    text: songArtists.displayedNames.join(" · ")
                    font.family: theme.ui
                    font.pixelSize: 14
                }
                VinylArtistLinks {
                    id: songArtists
                    objectName: "vinylSongArtist"
                    x: (parent.width - width) / 2
                    width: Math.min(parent.width, Math.max(24, artistNameMetrics.advanceWidth))
                    names: root.pageData.playingTrack.artists || root.pageData.playingEdition.artists || (root.pageData.playingEdition.sourceAlbum && root.pageData.playingEdition.sourceAlbum.artists) || []
                    fallbackName: root.pageData.playingEdition.artist
                    navigationEnabled: root.artistNavigationEnabled
                    fontSize: 14
                    onArtistRequested: name => root.artistRequested(name)
                }
                Item {
                    width: parent.width
                    height: 34
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
        VinylLyricsColumn {
            x: root.sideInset
            width: Math.max(0, Math.min(380, (root.width - songMetadata.width) / 2 - root.sideInset - root.sideGap))
            height: root.readingHeight
            pageData: root.pageData
            showSourceCredit: false
            visible: root.lyricsShown && pageData.hasLyrics && !root.videoShown && width > 0
        }
        VinylNotesColumn {
            id: notesColumn
            x: (root.width + songMetadata.width) / 2 + root.sideGap
            width: Math.max(0, Math.min(380, root.width - x - root.sideInset))
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
