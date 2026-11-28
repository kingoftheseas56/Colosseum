import QtQuick
import ".."

// Compact 1-column 62px numbered, cover-backed vertical track list (.track-list parity)
// as well as the legacy 5-row Popular songs shelf when compactListMode is false.
Column {
    id: root
    required property var artistData
    property bool fullView: false
    property bool compactListMode: false
    property bool inlinePopular: false
    readonly property bool reservePopularRows: inlinePopular && !!artistData.liveMode && !!artistData.online
    readonly property int previewRows: 5
    readonly property int popularRowHeight: 56
    readonly property string title: compactListMode
        ? "Singles & Tracks"
        : (inlinePopular || artistData.popularSongs.length ? "Popular Songs" : "Songs")
    readonly property var shownSongs: compactListMode
        ? artistData.singlesRows
        : inlinePopular ? (artistData.allSongs ? artistData.popularSongs : artistData.popularSongs.slice(0, previewRows))
        : (fullView ? artistData.songOrder
            : (artistData.popularSongs.length ? artistData.popularSongs : artistData.songOrder).slice(0, 5))
    readonly property bool compactQueueAvailable: root.compactListMode && !!root.artistData
        && typeof root.artistData.enqueueDemoSong === "function"

    signal songRequested(var song)
    signal albumRequested(var album)
    signal seeAllRequested()
    signal itemFocused(var item)

    spacing: 12
    visible: shownSongs.length > 0 || reservePopularRows
    Theme { id: theme }

    function focusRowAt(targetIndex) {
        if (targetIndex < 0 || targetIndex >= rowRepeater.count)
            return false;
        var item = rowRepeater.itemAt(targetIndex);
        if (item && item.primaryAction) {
            item.primaryAction.forceActiveFocus(Qt.TabFocusReason);
            return true;
        }
        return false;
    }

    WidgetHeader {
        id: songHeader
        width: parent.width
        visible: !root.fullView && !root.compactListMode
            && (!root.reservePopularRows || root.artistData.loading || root.shownSongs.length > 0)
        title: root.title
        sub: root.artistData.popularSongs.length ? root.artistData.popularSource : ""
        navigable: !root.inlinePopular || root.artistData.popularSongs.length > root.previewRows
        moreLabel: root.inlinePopular ? (root.artistData.allSongs ? "Show less" : "Show more") : "See all"
        automationId: "vinylArtistSeeAll_songs"
        onMoreClicked: root.seeAllRequested()
    }

    Rectangle {
        id: listContainer
        objectName: root.compactListMode ? "vinylArtistSinglesListContainer" : "vinylArtistSongsContainer"
        width: parent.width
        // Keep Discography at the same document position even when the service
        // returns fewer than five songs (or no songs at all).
        height: Math.max(tableColumn.implicitHeight, root.reservePopularRows
            ? root.previewRows * root.popularRowHeight + (songHeader.visible ? 0 : songHeader.height + root.spacing) : 0)
        radius: root.compactListMode ? 13 : 0
        color: root.compactListMode ? Qt.rgba(0.024, 0.027, 0.043, 0.78) : "transparent"
        border.width: root.compactListMode ? 1 : 0
        border.color: theme.edge
        clip: true

        Column {
            id: tableColumn
            width: parent.width

            // Compact Singles table header (.track-head parity)
            Item {
                width: parent.width
                height: 44
                visible: root.compactListMode
                Text {
                    x: 16
                    width: 34
                    anchors.verticalCenter: parent.verticalCenter
                    text: "#"
                    horizontalAlignment: Text.AlignRight
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                }
                Text {
                    x: 114
                    width: Math.max(120, parent.width * 0.46)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "TRACK"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 1.8
                }
                Text {
                    visible: parent.width >= 680
                    x: parent.width * 0.62
                    width: root.compactQueueAvailable
                        ? Math.max(0, Math.min(parent.width * 0.26, parent.width - x - 134))
                        : Math.max(100, parent.width * 0.26)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "RELEASE"
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 1.8
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: root.compactQueueAvailable ? 88 : 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "YEAR"
                    horizontalAlignment: Text.AlignRight
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 1.8
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.edge
                }
            }

            Repeater {
                model: root.reservePopularRows && root.artistData.loading
                    ? Math.max(0, root.previewRows - root.shownSongs.length) : 0
                delegate: Item {
                    width: tableColumn.width
                    height: root.popularRowHeight
                    Rectangle {
                        x: 60; width: 42; height: 42; radius: 5
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.glassTint
                    }
                    Rectangle {
                        x: 114; width: Math.max(0, Math.min(200, parent.width - x - 16)); height: 12; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.glassTint
                    }
                }
            }

            Repeater {
                id: rowRepeater
                model: root.shownSongs.length
                delegate: Item {
                    id: row
                    readonly property var modelData: root.shownSongs[index]
                    required property int index
                    property alias primaryAction: play

                    readonly property bool isCompact: root.compactListMode
                    readonly property bool showRank: isCompact || root.inlinePopular
                    readonly property bool isQueued: isCompact && root.artistData.queuedSongIndex === row.index
                    readonly property string trackTitle: isCompact
                        ? String(row.modelData.title || "")
                        : String(row.modelData.track.title || "")
                    readonly property string releaseTitle: isCompact
                        ? String(row.modelData.albumTitle || row.modelData.subtitle || "")
                        : String(row.modelData.album.title || "")
                    readonly property string coverPath: isCompact
                        ? String(row.modelData.cover || "")
                        : String(row.modelData.album.cover || "")
                    readonly property var songPayload: isCompact ? row.modelData.song : row.modelData
                    readonly property var albumPayload: row.modelData.album
                    readonly property var session: root.inlinePopular ? root.artistData.session || null : null
                    readonly property var personalRow: session && typeof session.albumTrackRow === "function"
                        ? session.albumTrackRow(albumPayload, songPayload.index, songPayload.editionId) : null
                    readonly property bool saved: !!personalRow && session.songSaved(personalRow)

                    width: parent ? parent.width : 0
                    height: isCompact ? 62 : root.popularRowHeight

                    Rectangle {
                        objectName: root.inlinePopular ? "vinylArtistPopularHover_" + row.index : ""
                        anchors.fill: parent
                        color: row.isQueued ? Qt.rgba(0.94, 0.77, 0.29, 0.11)
                            : (root.inlinePopular ? rowPointer.containsMouse
                                : play.interactionActive || album.interactionActive
                                    || nextAction.interactionActive || queueAction.interactionActive)
                                ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                    }

                    KeyboardAction {
                        focusOnPointer: false
                        id: play
                        objectName: root.compactListMode
                            ? ("vinylArtistSinglesRow_" + row.index)
                            : ((root.fullView ? "vinylArtistFullSong_" : "vinylArtistSong_") + row.index)
                        width: root.inlinePopular ? Math.max(0, parent.width - (row.session ? 124 : 52))
                            : parent.width >= 680 ? parent.width * 0.61
                            : parent.width - (root.compactQueueAvailable ? 124 : 84)
                        height: parent.height
                        focusRadius: 8
                        focusInset: -4
                        spaceActivates: true
                        enabled: !root.artistData.liveMode || !row.isCompact || row.modelData.playable || (row.albumPayload && row.albumPayload.origin==="youtube-music")
                        accessibleName: row.isCompact
                            ? (row.trackTitle + ", " + String(row.modelData.year || "") + (row.albumPayload && row.albumPayload.origin==="youtube-music" ? "; open catalogue release" : root.artistData.liveMode ? "; play local track" : "; queue demonstration track"))
                            : ("Play " + row.trackTitle)
                        onTriggered: {
                            if(row.isCompact && row.albumPayload && row.albumPayload.origin==="youtube-music") {root.albumRequested(row.albumPayload);return;}
                            if (row.isCompact) {
                                root.artistData.queueDemoSong(row.index);
                                if (row.modelData.playable && row.songPayload)
                                    root.songRequested(row.songPayload);
                            } else {
                                root.songRequested(row.songPayload);
                            }
                        }
                        onActiveFocusChanged: if (activeFocus) root.itemFocused(play)
                        Keys.onUpPressed: event => {
                            if (root.focusRowAt(row.index - 1))
                                event.accepted = true;
                        }
                        Keys.onDownPressed: event => {
                            if (root.focusRowAt(row.index + 1))
                                event.accepted = true;
                        }

                        // 2-digit tabular track number in compact mode (.track-num parity)
                        Text {
                            visible: row.showRank
                            x: 16
                            width: 34
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.inlinePopular ? String(row.modelData.rank || row.index + 1)
                                : row.isCompact ? String(row.modelData.number || row.index + 1) : ""
                            horizontalAlignment: Text.AlignRight
                            color: row.isQueued ? theme.gold : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: row.isQueued ? Font.Bold : Font.Medium
                        }

                        // 42x42 cover sleeve with ♫ fallback (.track-art parity)
                        Rectangle {
                            x: row.showRank ? 60 : 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 42
                            height: 42
                            radius: 5
                            color: "#1b1e29"
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.12)
                            Text {
                                anchors.centerIn: parent
                                text: "♫"
                                color: theme.gold
                                font.family: theme.ui
                                font.pixelSize: 15
                            }
                            RoundedPosterImage {
                                anchors.fill: parent
                                radius: 5
                                sources: root.artistData.artwork(row.coverPath)
                                    ? [root.artistData.artwork(row.coverPath)] : []
                            }
                        }

                        // Track title + QUEUED badge (compact mode) or artist line (legacy mode)
                        Column {
                            x: row.showRank ? 114 : 62
                            width: parent.width - x - 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: row.isCompact ? 2 : 5

                            Row {
                                width: parent.width
                                spacing: 10
                                Text {
                                    width: Math.min(implicitWidth, parent.width - (queuedBadge.visible ? queuedBadge.width + 14 : 0))
                                    text: row.trackTitle
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: row.isQueued ? theme.gold : theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: row.isCompact ? 15 : 14
                                    font.weight: row.isCompact ? Font.DemiBold : Font.Normal
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    id: queuedBadge
                                    objectName: row.isCompact ? ("vinylArtistSinglesQueued_" + row.index) : ""
                                    visible: row.isQueued
                                    width: queuedText.implicitWidth + 14
                                    height: 20
                                    radius: 10
                                    color: Qt.rgba(0.94, 0.77, 0.29, 0.14)
                                    border.width: 1
                                    border.color: Qt.rgba(0.94, 0.77, 0.29, 0.55)
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text {
                                        id: queuedText
                                        anchors.centerIn: parent
                                        text: "QUEUED"
                                        color: theme.gold
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                        font.letterSpacing: 1.2
                                    }
                                }
                            }

                            Text {
                                visible: !row.isCompact
                                width: parent.width
                                text: !row.isCompact && row.modelData.track
                                    ? ((row.modelData.track.artists && row.modelData.track.artists.length
                                        ? row.modelData.track.artists : row.modelData.album.artists || []).concat(root.inlinePopular && row.modelData.releaseTitle
                                            ? [row.modelData.releaseTitle] : []).join(" · "))
                                    : ""
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                            }
                        }
                    }

                    // Release / Album column (.track-album parity)
                    KeyboardAction {
                        focusOnPointer: false
                        id: album
                        objectName: root.compactListMode
                            ? ("vinylArtistSinglesAlbum_" + row.index)
                            : ((root.fullView ? "vinylArtistFullSongAlbum_" : "vinylArtistSongAlbum_") + row.index)
                        visible: !root.inlinePopular && (!row.isCompact || parent.width >= 680)
                        x: parent.width * 0.62
                        width: root.compactQueueAvailable
                            ? Math.max(0, Math.min(parent.width * 0.28, parent.width - x - 134))
                            : parent.width * 0.28
                        height: parent.height
                        focusRadius: 6
                        focusInset: -4
                        enabled: !row.isCompact || !!(row.albumPayload && (row.albumPayload.origin==="youtube-music" || (row.albumPayload.tracks && row.albumPayload.tracks.length > 0)))
                        accessibleName: "Open " + row.releaseTitle
                        onTriggered: {
                            if (row.albumPayload && (row.albumPayload.origin==="youtube-music" || (row.albumPayload.tracks && row.albumPayload.tracks.length > 0)))
                                root.albumRequested(row.albumPayload);
                        }
                        onActiveFocusChanged: if (activeFocus) root.itemFocused(album)
                        Text {
                            width: parent.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.releaseTitle
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: parent.interactionActive ? theme.gold : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: row.isCompact ? 13 : 12
                        }
                    }

                    // Year (compact mode) or duration (legacy mode)
                    Text {
                        anchors.right: parent.right
                        objectName: root.inlinePopular ? "vinylArtistPopularDuration_" + row.index : ""
                        anchors.rightMargin: row.isCompact
                            ? (root.compactQueueAvailable ? 88 : 20) : row.session ? 88 : 8
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property real seconds: (!row.isCompact && row.modelData.track)
                            ? Number(row.modelData.track.seconds) : 0
                        text: row.isCompact
                            ? String(row.modelData.year || "")
                            : (Number.isFinite(seconds) && seconds > 0
                                ? Math.floor(seconds / 60) + ":" + ("0" + Math.floor(seconds % 60)).slice(-2)
                                : root.inlinePopular ? "—" : "")
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: row.isCompact ? 13 : 12
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: theme.edge
                        opacity: row.isCompact ? 0.55 : 0.4
                    }

                    KeyboardAction {
                        id: nextAction
                        focusOnPointer: false
                        objectName: row.isCompact ? "vinylArtistSinglesNext_" + row.index : ""
                        visible: row.isCompact && root.compactQueueAvailable
                        x: parent.width - 76
                        y: (parent.height - 32) / 2
                        width: 32; height: 32
                        enabled: visible && row.modelData.playable !== false
                        accessibleName: "Play next: " + row.trackTitle
                        onActiveFocusChanged: if (activeFocus) root.itemFocused(nextAction)
                        onTriggered: root.artistData.enqueueDemoSong(row.index, true)
                        Rectangle { anchors.fill: parent; radius: 7; color: parent.interactionActive ? theme.glassTint : "transparent" }
                        VinylControlIcon { anchors.centerIn: parent; symbol: "playNext"; color: theme.inkDim }
                    }

                    KeyboardAction {
                        id: saveAction
                        focusOnPointer: false
                        objectName: root.inlinePopular ? "vinylArtistPopularSave_" + row.index : ""
                        visible: !!row.session
                        enabled: visible && !!row.session.personalEnabled && !!row.personalRow
                        opacity: enabled ? 1 : 0.35
                        x: parent.width - 76
                        y: (parent.height - height) / 2
                        width: 32; height: 32
                        accessibleName: row.saved ? qsTr("Unsave song") : qsTr("Save song")
                        onActiveFocusChanged: if (activeFocus) root.itemFocused(saveAction)
                        onTriggered: row.session.toggleSongSaved(row.personalRow)
                        Rectangle { anchors.fill: parent; radius: 6; color: parent.interactionActive ? theme.glassTint : "transparent" }
                        VinylControlIcon { anchors.centerIn: parent; symbol: row.saved ? "saved" : "save"; color: theme.inkDim }
                    }

                    KeyboardAction {
                        id: queueAction
                        focusOnPointer: false
                        objectName: row.isCompact ? "vinylArtistSinglesQueue_" + row.index
                            : root.inlinePopular ? "vinylArtistPopularQueue_" + row.index : ""
                        visible: row.isCompact ? root.compactQueueAvailable : !!row.session
                        x: parent.width - 40
                        y: (parent.height - 32) / 2
                        width: 32; height: 32
                        enabled: visible && (row.isCompact ? row.modelData.playable !== false
                            : typeof row.session.enqueueSong === "function" && row.songPayload.track.available !== false)
                        opacity: enabled ? 1 : 0.35
                        accessibleName: qsTr("Add to queue: %1").arg(row.trackTitle)
                        onActiveFocusChanged: if (activeFocus) root.itemFocused(queueAction)
                        onTriggered: {
                            if (row.isCompact) root.artistData.enqueueDemoSong(row.index, false);
                            else row.session.enqueueSong(row.albumPayload, row.songPayload.index, row.songPayload.editionId, false);
                        }
                        Rectangle { anchors.fill: parent; radius: 7; color: parent.interactionActive ? theme.glassTint : "transparent" }
                        VinylControlIcon { anchors.centerIn: parent; symbol: "queue"; color: theme.inkDim }
                    }
                    // Observe the whole row without accepting any clicks; the
                    // individual keyboard actions still own activation.
                    MouseArea {
                        id: rowPointer
                        anchors.fill: parent
                        visible: root.inlinePopular
                        acceptedButtons: Qt.NoButton
                        hoverEnabled: true
                    }
                }
            }
        }
    }
}
