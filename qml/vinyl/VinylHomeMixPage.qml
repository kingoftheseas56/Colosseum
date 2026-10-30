import QtQuick
import QtQuick.Controls
import ".."

Flickable {
    id: root
    objectName: "vinylHomeMixPage"
    required property var mixView
    required property var session
    required property Item backdrop
    property string savedPlaylistId: ""
    readonly property var songs: mixView.songs || []
    readonly property bool ready: mixView.status==="ready" && songs.length>0
    readonly property real inset: Math.max(54, (width-1280)/2)
    readonly property real leftWidth: Math.min(360, (width-2*inset)*0.36)
    readonly property string failure: String(mixView.error || session.browseError || "")
    signal backRequested()
    signal playRequested(int index, bool shuffle)
    signal queueRequested()
    signal saveRequested()
    signal retryRequested()
    clip: true
    contentWidth: width
    contentHeight: Math.max(height, tracks.y+tracks.height+48, info.y+info.height+48)
    boundsBehavior: Flickable.StopAtBounds
    Theme { id: theme }
    ScrollGlide { flick: root }
    ScrollBar.vertical: HouseScrollBar {}
    BackAction {
        objectName: "vinylHomeMixBack"
        x: root.inset; y: 0; height: 44
        focusOnPointer: false; label: "Back"
        onTriggered: root.backRequested()
    }
    Column {
        id: info
        x: root.inset; y: 72; width: root.leftWidth; spacing: 22
        RoundedPosterImage {
            width: parent.width; height: width; radius: 16
            sources: root.songs.length && root.songs[0].album.cover ? [root.songs[0].album.cover] : []
        }
        Text {
            width: parent.width; text: root.mixView.title || "Mix"
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            color: theme.ink; font.family: theme.display; font.pixelSize: 42
        }
        Text {
            text: root.songs.length+" songs"; color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 15; visible: root.songs.length>0
        }
        Row {
            spacing: 12
            VinylWorldButton {
                objectName: "vinylHomeMixPlay"; width: 44; height: 44
                backdrop: root.backdrop; label: "Play mix"; symbol: "play"
                enabled: root.ready
                onTriggered: root.playRequested(0,false)
            }
            VinylWorldButton {
                objectName: "vinylHomeMixShuffle"; width: 44; height: 44
                backdrop: root.backdrop; label: "Shuffle mix"; symbol: "shuffle"
                enabled: root.ready
                onTriggered: root.playRequested(0,true)
            }
            VinylWorldButton {
                objectName: "vinylHomeMixQueue"; width: 44; height: 44
                backdrop: root.backdrop; label: "Queue mix"; symbol: "queue"
                enabled: root.ready
                onTriggered: root.queueRequested()
            }
            VinylWorldButton {
                objectName: "vinylHomeMixSave"; width: 44; height: 44
                backdrop: root.backdrop; label: "Save mix to Library"; symbol: "heart"
                enabled: root.ready && !root.savedPlaylistId
                onTriggered: root.saveRequested()
            }
        }
        Text {
            width: parent.width; visible: root.mixView.status==="loading" || !!root.failure || !!root.savedPlaylistId
            text: root.savedPlaylistId ? "Saved to Library" : root.mixView.status==="loading" ? "Loading music…" : root.failure
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15; wrapMode: Text.WordWrap
        }
        VinylWorldButton {
            objectName: "vinylHomeMixRetry"; width: 38; height: 38
            backdrop: root.backdrop; label: "Retry mix"; symbol: "retry"
            visible: root.mixView.status!=="loading" && root.failure.length>0
            onTriggered: root.retryRequested()
        }
    }
    Item {
        id: trackData
        readonly property var tracks: root.songs.map(song=>song.track)
        readonly property real duration: root.session.duration || 0
        readonly property real position: root.session.position || 0
        function formatTime(seconds) { return root.session.formatTime(seconds); }
        function trackArtists(index) { return ""; }
        function isPlayingTrack(index) {
            var song=root.songs[index],row=root.session.activeQueueRow;
            return !!song && !!row && row.trackId===song.track.id;
        }
        function playTrack(index) { root.playRequested(index,false); }
    }
    Column {
        id: tracks
        x: root.inset+root.leftWidth+64; y: 72
        width: Math.max(0,root.width-root.inset-x)
        Repeater {
            model: root.songs
            delegate: VinylTrackRow {
                required property int index
                width: tracks.width; pageData: trackData; trackIndex: index
                enabled: root.ready && track.available!==false
            }
        }
    }
}
