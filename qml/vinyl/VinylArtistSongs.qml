import QtQuick
import ".."

// Compact 1-column 62px numbered, cover-backed vertical track list (.track-list parity)
// as well as the legacy 5-row Popular songs shelf when compactListMode is false.
Column {
    id: root
    required property var artistData
    property bool fullView: false
    property bool compactListMode: false
    readonly property string title: compactListMode
        ? "Singles & Tracks"
        : (artistData.popularSongs.length ? "Popular songs" : "Songs")
    readonly property var shownSongs: compactListMode
        ? artistData.singlesRows
        : (fullView ? artistData.songOrder
            : (artistData.popularSongs.length ? artistData.popularSongs : artistData.songOrder).slice(0, 5))

    signal songRequested(var song)
    signal albumRequested(var album)
    signal seeAllRequested()
    signal itemFocused(var item)

    spacing: 12
    visible: shownSongs.length > 0
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
        width: parent.width
        visible: !root.fullView && !root.compactListMode
        title: root.title
        sub: root.artistData.popularSongs.length ? root.artistData.popularSource : ""
        moreLabel: "See all"
        automationId: "vinylArtistSeeAll_songs"
        onMoreClicked: root.seeAllRequested()
    }

    Rectangle {
        id: listContainer
        objectName: root.compactListMode ? "vinylArtistSinglesListContainer" : "vinylArtistSongsContainer"
        width: parent.width
        height: tableColumn.implicitHeight
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
                    width: Math.max(100, parent.width * 0.26)
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
                    anchors.rightMargin: 20
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
                id: rowRepeater
                model: root.shownSongs.length
                delegate: Item {
                    id: row
                    readonly property var modelData: root.shownSongs[index]
                    required property int index
                    property alias primaryAction: play

                    readonly property bool isCompact: root.compactListMode
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
                    readonly property var albumPayload: isCompact ? row.modelData.album : row.modelData.album

                    width: parent ? parent.width : 0
                    height: isCompact ? 62 : 56

                    Rectangle {
                        anchors.fill: parent
                        color: row.isQueued
                            ? Qt.rgba(0.94, 0.77, 0.29, 0.11)
                            : (play.interactionActive || album.interactionActive ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    }

                    KeyboardAction {
                        id: play
                        objectName: root.compactListMode
                            ? ("vinylArtistSinglesRow_" + row.index)
                            : ((root.fullView ? "vinylArtistFullSong_" : "vinylArtistSong_") + row.index)
                        width: parent.width >= 680 ? parent.width * 0.61 : parent.width - 84
                        height: parent.height
                        focusRadius: 8
                        focusInset: -4
                        spaceActivates: true
                        enabled: !root.artistData.liveMode || !row.isCompact || row.modelData.playable
                        accessibleName: row.isCompact
                            ? (row.trackTitle + ", " + String(row.modelData.year || "") + (root.artistData.liveMode ? "; play local track" : "; queue demonstration track"))
                            : ("Play " + row.trackTitle)
                        onTriggered: {
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
                            visible: row.isCompact
                            x: 16
                            width: 34
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.isCompact ? String(row.modelData.number || (row.index + 1)) : ""
                            horizontalAlignment: Text.AlignRight
                            color: row.isQueued ? theme.gold : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: row.isQueued ? Font.Bold : Font.Medium
                        }

                        // 42x42 cover sleeve with ♫ fallback (.track-art parity)
                        Rectangle {
                            x: row.isCompact ? 60 : 4
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
                            x: row.isCompact ? 114 : 62
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
                                        ? row.modelData.track.artists : row.modelData.album.artists || []).join(" · "))
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
                        id: album
                        objectName: root.compactListMode
                            ? ("vinylArtistSinglesAlbum_" + row.index)
                            : ((root.fullView ? "vinylArtistFullSongAlbum_" : "vinylArtistSongAlbum_") + row.index)
                        visible: !row.isCompact || parent.width >= 680
                        x: parent.width * 0.62
                        width: parent.width * 0.28
                        height: parent.height
                        focusRadius: 6
                        focusInset: -4
                        enabled: !row.isCompact || !!(row.albumPayload && row.albumPayload.tracks && row.albumPayload.tracks.length > 0)
                        accessibleName: "Open " + row.releaseTitle
                        onTriggered: {
                            if (row.albumPayload && row.albumPayload.tracks && row.albumPayload.tracks.length > 0)
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
                        anchors.rightMargin: row.isCompact ? 20 : 8
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property real seconds: (!row.isCompact && row.modelData.track)
                            ? Number(row.modelData.track.seconds) : 0
                        text: row.isCompact
                            ? String(row.modelData.year || "")
                            : (Number.isFinite(seconds) && seconds > 0
                                ? Math.floor(seconds / 60) + ":" + ("0" + Math.floor(seconds % 60)).slice(-2) : "")
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
                }
            }
        }
    }
}
