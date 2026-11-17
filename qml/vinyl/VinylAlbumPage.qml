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
    // Options for non-music callers (the audiobook book page). Every default
    // keeps the Vinyl album page exactly as it was.
    property real coverMaxSize: 440
    property bool shuffleVisible: true
    property bool saveVisible: true
    property bool collectionsVisible: true
    property bool factsVisible: true
    property bool discSwitchEnabled: true
    // A non-empty label turns the first action into one wide labelled primary
    // button that emits primaryActionRequested() instead of playing.
    property string primaryActionLabel: ""
    property bool primaryActionEnabled: true
    // Extra lines under the artist: [{key, text, actionable, strong}]
    property var metaLines: []
    property string trackListHeading: ""
    property Component trackAside: null
    property string editionsTitle: "Editions"
    property string editionsNoun: "editions"
    property bool editionDetailFromData: false
    property bool editionsSortByYear: true
    property string extraSectionTitle: ""
    property Component extraSectionComponent: null
    property string sidebarWorldName: "Vinyl"
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
    readonly property bool discSwitchVisible: discSwitchEnabled && pageData.shownEdition.discs.length > 1
    readonly property string currentSectionKey: sidebar.currentIndex >= 0 ? sidebar.visibleRows[sidebar.currentIndex].key : ""
    readonly property real inset: Math.max(54, (width - 1280) / 2)
    readonly property real usable: width - 2 * inset
    readonly property real albumMetadataHeight: albumTitle.height + albumArtist.height
        + albumFacts.height + albumActionsRow.height + albumMeta.height + albumInfoColumn.spacing * 4
    readonly property real coverSize: Math.min(coverMaxSize, albumInfoColumn.width)
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
    readonly property bool personalTrackActionsVisible: trackSaveAvailable || trackPlaylistAvailable
    readonly property real personalTrackActionsWidth: personalTrackActionsVisible ? 80 : 0
    readonly property var albumActionModel: {
        var actions = ["Play"];
        if (shuffleVisible) actions.push("Shuffle");
        if (saveVisible) actions.push("Save");
        if (queueActionsEnabled && pageData && typeof pageData.enqueueAlbum === "function")
            actions.push("Next", "Queue");
        if (albumPlaylistAvailable) actions.push("Playlist");
        return actions;
    }
    signal videoRequested(int track)
    signal songRequested(int track)
    signal primaryActionRequested()
    signal metaLineRequested(string key)
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
            Column {
                id: albumMeta
                objectName: "vinylAlbumMeta"
                visible: root.metaLines.length > 0
                width: parent.width
                spacing: 6
                Repeater {
                    model: root.metaLines
                    delegate: KeyboardAction {
                        id: metaLine
                        required property var modelData
                        focusOnPointer: false
                        objectName: "vinylAlbumMeta_" + modelData.key
                        width: parent.width
                        height: metaText.implicitHeight + 4
                        enabled: modelData.actionable === true
                        accessibleName: modelData.text
                        onTriggered: root.metaLineRequested(modelData.key)
                        Text {
                            id: metaText
                            width: parent.width
                            text: metaLine.modelData.text
                            color: metaLine.interactionActive && metaLine.enabled ? theme.ink
                                : metaLine.modelData.strong === true ? theme.inkDim : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: metaLine.modelData.strong === true ? 16 : 14
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            Text {
                id: albumFacts
                objectName: "vinylAlbumFacts"
                visible: root.factsVisible
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
                    visible: root.collectionsVisible
                    objectName: "vinylAlbumAddToCollections"
                    reference: CollectionsReferences.album(root.pageData)
                }
                Repeater {
                    id: albumActions
                    model: root.albumActionModel
                    delegate: KeyboardAction {
                        focusOnPointer: false
                        required property string modelData
                        readonly property bool labelled: modelData === "Play" && root.primaryActionLabel.length > 0
                        objectName: labelled ? "vinylAlbumPrimaryAction" : "vinylAlbum" + modelData
                        tooltipText: modelData === "Save" ? (root.pageData.saved ? qsTr("Unsave album") : qsTr("Save album")) : labelled ? "" : accessibleName
                        width: labelled ? Math.max(150, primaryLabel.implicitWidth + 44) : 40
                        height: labelled ? 46 : 36
                        accessibleName: labelled ? root.primaryActionLabel
                            : modelData === "Save" && root.pageData.saved ? "Saved"
                            : modelData === "Playlist" ? "Add album to playlist"
                            : modelData === "Next" ? "Play album next"
                            : modelData === "Queue" ? "Add album to queue" : modelData
                        enabled: labelled ? root.primaryActionEnabled
                            : modelData === "Play" ? root.trackCount > 0
                            : modelData === "Shuffle" ? root.shuffleEnabled
                            : (modelData === "Next" || modelData === "Queue")
                                ? root.trackCount > 0 && typeof root.pageData.enqueueAlbum === "function"
                            : modelData === "Playlist" ? root.pageData.personalEnabled
                                && !root.partialPlaylist && root.albumRowsForPlaylist().length > 0
                            : root.saveEnabled
                        opacity: enabled ? 1 : 0.35
                        onTriggered: {
                            if (labelled) {
                                root.primaryActionRequested();
                            } else if (modelData === "Play") {
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
                            radius: parent.labelled ? 12 : 10
                            color: parent.modelData === "Play" ? theme.gold : theme.glassTint
                            border.color: theme.edge
                        }
                        Text {
                            id: primaryLabel
                            anchors.centerIn: parent
                            visible: parent.labelled
                            text: root.primaryActionLabel
                            color: theme.biblioWashBottom
                            font.family: theme.ui
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        VinylControlIcon {
                            visible: !parent.labelled
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
            Loader {
                id: trackAsideLoader
                objectName: "vinylTrackAside"
                visible: root.trackAside !== null
                width: trackList.width
                height: visible && item ? item.implicitHeight : 0
                sourceComponent: root.trackAside
            }
            Text {
                objectName: "vinylTrackListHeading"
                visible: root.trackListHeading.length > 0
                height: visible ? implicitHeight + 14 : 0
                text: root.trackListHeading
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 27
            }
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
                        tooltipText: accessibleName
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
                        tooltipText: accessibleName
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
                        && root.personalTrackActionsVisible
                    readonly property real trackActionWidth: root.personalTrackActionsWidth
                    height: baseTrackHeight + (compactTrackActions ? 40 : 0)
                    VinylTrackRow {
                        id: trackVisual
                        trackIndex: parent.index
                        pageData: root.pageData
                        width: parent.compactTrackActions ? parent.width : Math.max(0, parent.width - parent.trackActionWidth)
                        height: parent.baseTrackHeight
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
                    tooltipText: accessibleName
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
        editionsTitle: root.editionsTitle
        editionsNoun: root.editionsNoun
        editionDetailFromData: root.editionDetailFromData
        editionsSortByYear: root.editionsSortByYear
        extraTitle: root.extraSectionTitle
        extraComponent: root.extraSectionComponent
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
        worldName: root.sidebarWorldName
        automationPrefix: "vinylRowIndex"
        rows: sections.sectionRows
        currentOffset: 12
        onRowRequested: target => {
            root.cancelFlick();
            root.contentY = Math.max(0, target.mapToItem(root.contentItem, 0, 0).y - sidebar.pinLine);
        }
    }
}
