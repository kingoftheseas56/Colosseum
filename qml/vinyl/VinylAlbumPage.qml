import QtQuick
import QtQuick.Controls
import ".."
import "../collections" as CollectionsUi
import "../CollectionsReferences.js" as CollectionsReferences

Flickable {
    id: root
    objectName: "vinylAlbumPage"
    required property var pageData
    required property Item backdrop
    property bool artistNavigationEnabled: true
    property bool shuffleEnabled: true
    property bool saveEnabled: true
    property bool scrollbarVisible: true
    readonly property int trackCount: pageData.tracks.length
    readonly property var sourceAlbum: pageData && pageData.shownEdition
        ? pageData.shownEdition.sourceAlbum : null
    readonly property bool partialPlaylist: !!sourceAlbum
        && String(sourceAlbum.type || "").toLowerCase() === "playlist"
        && sourceAlbum.complete === false
    readonly property bool playlistHasMore: !!pageData
        && typeof pageData.playlistHasMore !== "undefined" && !!pageData.playlistHasMore
    readonly property bool playlistPageLoading: !!pageData
        && typeof pageData.playlistPageLoading !== "undefined" && !!pageData.playlistPageLoading
    readonly property string playlistPageError: !!pageData
        && typeof pageData.playlistPageError !== "undefined"
        ? String(pageData.playlistPageError || "") : ""
    readonly property bool showPlaylistMore: playlistHasMore || playlistPageLoading
        || playlistPageError.length > 0
    readonly property string shownEditionId: pageData.shownEditionId
    readonly property bool discSwitchVisible: pageData.shownEdition.discs.length > 1
    readonly property string currentSectionKey: sidebar.currentIndex >= 0 ? sidebar.visibleRows[sidebar.currentIndex].key : ""
    readonly property real inset: Math.max(54, (width - 1280) / 2)
    readonly property real usable: width - 2 * inset
    readonly property real coverSize: Math.min(440, albumInfoColumn.width)
    readonly property bool albumDetailsFit: albumInfoColumn.height + 48 <= height
    readonly property bool trackSaveAvailable: !!pageData
        && typeof pageData.personalEnabled === "boolean"
        && typeof pageData.shownTrackRow === "function"
        && typeof pageData.songSaved === "function"
        && typeof pageData.toggleSongSaved === "function"
    readonly property bool trackPlaylistAvailable: !!pageData
        && typeof pageData.personalEnabled === "boolean"
        && typeof pageData.shownTrackRow === "function"
        && typeof pageData.addRowsToPlaylist === "function"
    readonly property bool albumPlaylistAvailable: !!pageData
        && typeof pageData.personalEnabled === "boolean"
        && typeof pageData.shownAlbumRows === "function"
        && typeof pageData.addRowsToPlaylist === "function"
    readonly property bool trackQueueAvailable: queueActionsEnabled && !!pageData
        && typeof pageData.enqueueTrack === "function"
    readonly property bool personalTrackActionsVisible: trackSaveAvailable || trackPlaylistAvailable
    readonly property real personalTrackActionsWidth: personalTrackActionsVisible ? 80 : 0
    readonly property var albumActionModel: {
        var actions = ["Play", "Shuffle", "Save"];
        if (queueActionsEnabled && pageData && typeof pageData.enqueueAlbum === "function")
            actions.push("Next", "Queue");
        if (albumPlaylistAvailable) actions.push("Playlist");
        return actions;
    }
    signal videoRequested(int track)
    signal songRequested(int track)
    property bool songNavigationEnabled: false
    property bool queueActionsEnabled: false
    function focusPrimaryControl() {
        var play = albumActions.itemAt(0);
        if (play) play.forceActiveFocus(Qt.TabFocusReason);
    }
    function personalRowForTrack(index) {
        if (!trackSaveAvailable && !trackPlaylistAvailable) return null;
        return pageData.shownTrackRow(index);
    }
    function hasPersonalRow(row) {
        return row !== null && row !== undefined;
    }
    function trackIsSaved(row) {
        return trackSaveAvailable && hasPersonalRow(row) ? !!pageData.songSaved(row) : false;
    }
    function albumRowsForPlaylist() {
        return albumPlaylistAvailable ? pageData.shownAlbumRows() : [];
    }
    function trackCountFact() {
        var loaded = pageData.totalTracks(pageData.shownEdition);
        if (partialPlaylist) {
            var total = sourceAlbum.totalTracks;
            var totalLabel = typeof total === "number" && total >= loaded ? String(total) : "?";
            return loaded + " / " + totalLabel + " tracks";
        }
        return loaded + (loaded === 1 ? " track" : " tracks")
            + (sourceAlbum && sourceAlbum.omittedTracks > 0 ? " · " + sourceAlbum.omittedTracks + " unavailable" : "");
    }
    clip: true
    contentWidth: width
    contentHeight: sections.y + sections.height + height * 0.85
    boundsBehavior: Flickable.StopAtBounds
    Keys.onPressed: event => {
        var target = contentY;
        if (event.key === Qt.Key_PageDown) target += height * 0.8;
        else if (event.key === Qt.Key_PageUp) target -= height * 0.8;
        else if (event.key === Qt.Key_Home) target = 0;
        else if (event.key === Qt.Key_End) target = contentHeight - height;
        else return;
        cancelFlick();
        contentY = Math.max(0, Math.min(Math.max(0, contentHeight - height), target));
        event.accepted = true;
    }
    Theme {
        id: theme
    }
    ScrollGlide {
        flick: root
    }
    ScrollBar.vertical: HouseScrollBar {
        visible: root.scrollbarVisible
        flick: root
    }
    Connections {
        target: root.pageData
        function onEditionSelected() {
            root.contentY = 0;
        }
    }
    Item {
        id: hero
        x: root.inset
        width: root.usable
        height: Math.max(root.height, trackList.height + 30, albumInfoColumn.height + 48)
        Column {
            id: albumInfoColumn
            y: root.albumDetailsFit
                ? Math.min(Math.max(24, root.contentY + 24), Math.max(24, hero.height - height - 20))
                : 24
            width: root.usable * 0.4
            spacing: 14
            RoundedPosterImage {
                objectName: "vinylAlbumCover"
                width: root.coverSize
                height: width
                radius: 18
                sources: root.pageData.shownEdition.cover ? [root.pageData.artwork(root.pageData.shownEdition.cover)] : []
            }
            Text {
                id: albumTitle
                objectName: "vinylAlbumTitle"
                width: parent.width
                text: root.pageData.shownEdition.name
                color: theme.ink
                font.family: theme.display
                font.pixelSize: Math.min(44, root.height * 0.067)
                wrapMode: Text.WordWrap
            }
            VinylArtistLinks {
                id: albumArtist
                objectName: "vinylAlbumArtist"
                width: parent.width
                height: implicitHeight
                navigationEnabled: root.artistNavigationEnabled
                names: root.pageData.shownEdition.artists
                    || (root.pageData.shownEdition.sourceAlbum
                        ? root.pageData.shownEdition.sourceAlbum.artists : [])
                    || []
                fallbackName: root.pageData.shownEdition.artist
                fontSize: 16
                onArtistRequested: name => root.pageData.artistRequested(name)
            }
            Text {
                id: albumFacts
                objectName: "vinylAlbumFacts"
                text: [root.pageData.shownEdition.year, root.pageData.shownEdition.origin === "local" ? "Local release" : (root.pageData.shownEdition.sourceAlbum || {}).type || "Album", root.trackCountFact(), root.pageData.shownEdition.length].filter(function(value) { return !!value; }).join(" · ")
                width: parent.width
                wrapMode: Text.WordWrap
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
            }
            Flow {
                id: albumActionsRow
                width: parent.width
                height: implicitHeight
                spacing: 10
                CollectionsUi.AddToCollectionsButton {
                    objectName: "vinylAlbumAddToCollections"
                    reference: CollectionsReferences.album(root.pageData)
                }
                Repeater {
                    id: albumActions
                    model: root.albumActionModel
                    delegate: KeyboardAction {
                        focusOnPointer: false
                        required property string modelData
                        objectName: "vinylAlbum" + modelData
                        width: 40
                        height: 36
                        accessibleName: modelData === "Save" && root.pageData.saved ? "Saved"
                            : modelData === "Playlist" ? "Add album to playlist"
                            : modelData === "Next" ? "Play album next"
                            : modelData === "Queue" ? "Add album to queue" : modelData
                        enabled: modelData === "Play" ? root.trackCount > 0
                            : modelData === "Shuffle" ? root.shuffleEnabled
                            : (modelData === "Next" || modelData === "Queue")
                                ? root.trackCount > 0 && typeof root.pageData.enqueueAlbum === "function"
                            : modelData === "Playlist" ? root.pageData.personalEnabled
                                && !root.partialPlaylist && root.albumRowsForPlaylist().length > 0
                            : root.saveEnabled
                        opacity: enabled ? 1 : 0.35
                        onTriggered: {
                            if (modelData === "Play") {
                                if (typeof root.pageData.playShownAlbum === "function")
                                    root.pageData.playShownAlbum();
                                else
                                    root.pageData.playTrack(0);
                            } else if (modelData === "Shuffle") {
                                root.pageData.shuffle();
                            } else if (modelData === "Next") {
                                root.pageData.enqueueAlbum(root.pageData.shownAlbumId, true);
                            } else if (modelData === "Queue") {
                                root.pageData.enqueueAlbum(root.pageData.shownAlbumId,false);
                            } else if (modelData === "Playlist") {
                                root.pageData.addRowsToPlaylist(root.albumRowsForPlaylist());
                            } else {
                                root.pageData.toggleSaved();
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: parent.modelData === "Play" ? theme.gold : theme.glassTint
                            border.color: theme.edge
                        }
                        VinylControlIcon {
                            anchors.centerIn: parent
                            symbol: parent.modelData === "Play" ? "play"
                                : parent.modelData === "Shuffle" ? "shuffle"
                                : parent.modelData === "Save" ? (root.pageData.saved ? "saved" : "save")
                                : parent.modelData === "Next" ? "playNext" : "queue"
                            color: parent.modelData === "Play" ? theme.biblioWashBottom
                                : parent.modelData === "Save" && root.pageData.saved ? theme.gold
                                : theme.inkDim
                        }
                    }
                }
            }
        }
        Column {
            id: trackList
            x: root.usable * 0.46
            y: 24
            width: root.usable * 0.54
            Row {
                visible: root.discSwitchVisible
                height: visible ? 42 : 0
                spacing: 14
                Repeater {
                    model: root.pageData.shownEdition.discs.length
                    delegate: KeyboardAction {
                        focusOnPointer: false
                        required property int index
                        objectName: "vinylDisc_" + index
                        width: 75
                        height: 30
                        accessibleName: "Disc " + ((root.pageData.shownEdition.discNumbers || [])[index] || index+1)
                        onTriggered: root.pageData.selectDisc(index)
                        Text {
                            anchors.centerIn: parent
                            text: "Disc " + ((root.pageData.shownEdition.discNumbers || [])[parent.index] || parent.index+1)
                            color: root.pageData.shownDisc === parent.index ? theme.gold : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 13
                        }
                    }
                }
            }
            Repeater {
                model: root.pageData.tracks.length
                delegate: Item {
                    required property int index
                    width: trackList.width
                    KeyboardAction {
                        id: saveTrackAction
                        focusOnPointer: false
                        objectName: "vinylTrackSave_" + parent.index
                        property var personalRow: root.personalRowForTrack(parent.index)
                        x: (parent.compactTrackActions ? parent.width - parent.trackActionWidth : trackVisual.width) + 8
                        width: 32
                        y: parent.compactTrackActions ? parent.baseTrackHeight + 4 : 0
                        height: parent.compactTrackActions ? 32 : parent.height
                        visible: root.trackSaveAvailable
                        enabled: visible && trackVisual.enabled && root.pageData.personalEnabled && root.hasPersonalRow(personalRow)
                        opacity: enabled ? 1 : 0.35
                        accessibleName: root.trackIsSaved(personalRow) ? "Unsave song" : "Save song"
                        onTriggered: root.pageData.toggleSongSaved(personalRow)
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: parent.interactionActive ? theme.glassTint : "transparent"
                        }
                        VinylControlIcon {
                            anchors.centerIn: parent
                            symbol: root.trackIsSaved(saveTrackAction.personalRow) ? "saved" : "save"
                            color: theme.inkDim
                        }
                    }
                    KeyboardAction {
                        id: playlistTrackAction
                        focusOnPointer: false
                        objectName: "vinylTrackPlaylist_" + parent.index
                        property var personalRow: root.personalRowForTrack(parent.index)
                        x: (parent.compactTrackActions ? parent.width - parent.trackActionWidth : trackVisual.width) + 48
                        width: 32
                        y: parent.compactTrackActions ? parent.baseTrackHeight + 4 : 0
                        height: parent.compactTrackActions ? 32 : parent.height
                        visible: root.trackPlaylistAvailable
                        enabled: visible && trackVisual.enabled && root.pageData.personalEnabled && root.hasPersonalRow(personalRow)
                        opacity: enabled ? 1 : 0.35
                        accessibleName: "Add song to playlist"
                        onTriggered: root.pageData.addRowsToPlaylist([personalRow])
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: parent.interactionActive ? theme.glassTint : "transparent"
                        }
                        VinylControlIcon {
                            anchors.centerIn: parent
                            symbol: "queue"
                            color: theme.inkDim
                        }
                    }
                    readonly property real baseTrackHeight: root.height < 600
                        ? (root.pageData.trackArtists(index) ? 58 : 50)
                        : (root.pageData.trackArtists(index) ? 62 : 57)
                    readonly property bool compactTrackActions: trackList.width < 400
                        && (root.personalTrackActionsVisible || root.trackQueueAvailable)
                    readonly property real trackActionWidth: root.personalTrackActionsWidth
                        + (root.trackQueueAvailable ? 80 : 0)
                    height: baseTrackHeight + (compactTrackActions ? 40 : 0)
                    VinylTrackRow {
                        id: trackVisual
                        trackIndex: parent.index
                        pageData: root.pageData
                        width: parent.compactTrackActions ? parent.width : Math.max(0, parent.width - parent.trackActionWidth)
                        height: parent.baseTrackHeight
                    }
                    KeyboardAction {
                        focusOnPointer: false
                        objectName: "vinylTrackNext_" + parent.index
                        visible: root.trackQueueAvailable
                        x: parent.compactTrackActions ? parent.width - 72
                            : trackVisual.width + root.personalTrackActionsWidth + 8
                        y: parent.compactTrackActions ? parent.baseTrackHeight + 4 : 0
                        width: 32; height: parent.compactTrackActions ? 32 : parent.height
                        enabled: visible && trackVisual.enabled
                        accessibleName: "Play next: " + trackVisual.track.title
                        onTriggered: root.pageData.enqueueTrack(parent.index, true)
                        Rectangle { anchors.fill: parent; radius: 6; color: parent.interactionActive ? theme.glassTint : "transparent" }
                        VinylControlIcon { anchors.centerIn: parent; symbol: "playNext"; color: theme.inkDim }
                    }
                    KeyboardAction {
                        focusOnPointer: false
                        objectName: "vinylTrackQueue_" + parent.index
                        visible: root.trackQueueAvailable
                        x: parent.compactTrackActions ? parent.width - 32
                            : trackVisual.width + root.personalTrackActionsWidth + 48
                        y: parent.compactTrackActions ? parent.baseTrackHeight + 4 : 0
                        width: 32; height: parent.compactTrackActions ? 32 : parent.height
                        enabled: visible && trackVisual.enabled
                        accessibleName: "Add track to queue: " + trackVisual.track.title
                        onTriggered: root.pageData.enqueueTrack(parent.index, false)
                        Rectangle { anchors.fill: parent; radius: 6; color: parent.interactionActive ? theme.glassTint : "transparent" }
                        VinylControlIcon { anchors.centerIn: parent; symbol: "queue"; color: theme.inkDim }
                    }
                }
            }
            Row {
                visible: root.showPlaylistMore
                width: trackList.width
                height: visible ? Math.max(40, playlistMoreStatus.implicitHeight) + 10 : 0
                spacing: 10
                KeyboardAction {
                    id: playlistMoreAction
                    focusOnPointer: false
                    objectName: "vinylPlaylistMore"
                    width: 36
                    height: 36
                    accessibleName: "Load more tracks"
                    enabled: !root.playlistPageLoading && !!root.pageData
                        && typeof root.pageData.loadMorePlaylist === "function"
                    opacity: enabled ? 1 : 0.35
                    onTriggered: root.pageData.loadMorePlaylist()
                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: parent.interactionActive ? theme.glassTint : "transparent"
                        border.color: theme.edge
                    }
                    VinylControlIcon {
                        anchors.centerIn: parent
                        symbol: "arrowDown"
                        color: theme.inkDim
                    }
                }
                Text {
                    id: playlistMoreStatus
                    width: Math.max(0, parent.width - playlistMoreAction.width - parent.spacing)
                    anchors.verticalCenter: playlistMoreAction.verticalCenter
                    visible: root.playlistPageLoading || root.playlistPageError.length > 0
                    text: root.playlistPageLoading ? "Loading tracks…" : root.playlistPageError
                    color: root.playlistPageError.length > 0 && !root.playlistPageLoading
                        ? theme.gold : theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
    VinylAlbumSections {
        id: sections
        x: root.inset + sidebar.contentLeft
        y: hero.height + 50
        width: root.usable - sidebar.contentLeft
        pageData: root.pageData
        onVideoRequested: track => root.videoRequested(track)
    }
    RowIndexSidebar {
        id: sidebar
        parent: root.contentItem
        x: root.inset - 5
        z: 5
        pageFlick: root
        flowHost: sections
        backdrop: root.backdrop
        worldName: "Vinyl"
        automationPrefix: "vinylRowIndex"
        rows: sections.sectionRows
        currentOffset: 12
        onRowRequested: target => {
            root.cancelFlick();
            root.contentY = Math.max(0, target.mapToItem(root.contentItem, 0, 0).y - sidebar.pinLine);
        }
    }
}