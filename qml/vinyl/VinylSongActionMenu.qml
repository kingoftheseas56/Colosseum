import QtQuick
import QtQuick.Controls.Basic
import ".."
import "../CollectionsReferences.js" as CollectionsReferences

Item {
    id: root

    width: collectionHost ? readerActions.width : collectionActions.width + (collectionActions.width && readerActions.width ? spacing : 0) + readerActions.width
    height: 34
    property real spacing: 7
    // Reparent the single collection row visually; button instances and ownership stay unique.
    property Item collectionHost: null
    readonly property real collectionWidth: collectionActions.width
    readonly property bool videoBusy: songData && songData.videoBusy === true
    readonly property bool videoSwitchCancellable: songData && songData.videoSwitchCancellable === true

    required property var songData
    property bool videoFullscreen: false
    property bool showPlay: false
    property bool showReaderTools: false
    property string contextKey: ""

    signal playRequested()
    signal videoFullscreenRequested()
    signal videoRequested()
    signal creditsRequested()

    readonly property var playingTrack: {
        const page = root.songData
        if (!page)
            return null
        try {
            const track = page.playingTrack
            return track && (typeof track === "object" || typeof track === "function") ? track : null
        } catch (error) {
            return null
        }
    }
    readonly property string viewedTitle: {
        const track = root.playingTrack
        if (!track)
            return ""
        try {
            const title = track.title
            return title === undefined || title === null ? "" : String(title)
        } catch (error) {
            return ""
        }
    }
    readonly property bool personalEnabled: {
        const track = root.songData
        if (!track)
            return false
        try {
            return track.personalEnabled === true
        } catch (error) {
            return false
        }
    }
    readonly property bool canToggleSaved: root.personalEnabled && root.hasMethod("togglePlayingSongSaved")
    readonly property bool canAddToPlaylist: root.personalEnabled && root.hasMethod("addPlayingSongToPlaylist")
    readonly property bool hasEnqueueMethod: root.hasMethod("enqueuePlayingSong")
    readonly property bool canEnqueue: root.hasEnqueueMethod && root.playingTrackAvailable
    readonly property bool playingTrackAvailable: {
        const track = root.playingTrack
        if (!track)
            return false
        try {
            return track.available !== false
        } catch (error) {
            return false
        }
    }
    readonly property bool videoAvailable: {
        const track = root.songData
        if (!track)
            return false
        try {
            return track.hasVideo === true
        } catch (error) {
            return false
        }
    }
    readonly property bool videoShown: {
        const track = root.songData
        if (!track)
            return false
        try {
            return track.videoShown === true
        } catch (error) {
            return false
        }
    }
    readonly property bool creditsAvailable: {
        const track = root.songData
        if (!track)
            return false
        try {
            const credits = track.playingCredits
            return credits !== undefined && credits !== null
                   && typeof credits.length === "number" && credits.length > 0
        } catch (error) {
            return false
        }
    }
    readonly property bool hasActions: root.showPlay || root.canToggleSaved
                                                  || root.canAddToPlaylist || root.hasEnqueueMethod
                                                  || root.showReaderTools
    readonly property bool playEnabled: root.showPlay && root.playingTrackAvailable
    readonly property bool currentSongSaved: {
        const track = root.songData
        if (!track)
            return false
        try {
            const state = track.songIsSaved
            return typeof state === "function" ? !!state.call(track) : state === true
        } catch (error) {
            return false
        }
    }

    function hasMethod(name) {
        const track = root.songData
        if (!track)
            return false
        try {
            return typeof track[name] === "function"
        } catch (error) {
            return false
        }
    }

    function invokeCurrentSong(name, args) {
        const track = root.songData
        if (!track)
            return false
        try {
            const method = track[name]
            if (typeof method !== "function")
                return false
            method.apply(track, args || [])
            return true
        } catch (error) {
            return false
        }
    }

    Theme { id: theme }

    component Action: KeyboardAction {
        id: control
        property string symbol
        property bool selected: false
        property bool loading: false
        property url iconSource: ""
        width: visible ? 34 : 0
        height: 34
        focusOnPointer: false
        opacity: enabled ? 1 : 0.34
        accessibleDescription: root.viewedTitle.length > 0
                               ? qsTr("For %1").arg(root.viewedTitle) : ""
        Rectangle {
            anchors.fill: parent
            radius: 10
            color: control.interactionActive || control.selected ? theme.glassHi : theme.glassTint
            border.color: theme.edge
        }
        VinylControlIcon {
            objectName: control.objectName + "Glyph"
            anchors.centerIn: parent
            width: 17
            height: 17
            visible: !control.loading && !control.iconSource.toString().length
            symbol: control.symbol
            color: control.selected ? theme.gold : theme.inkDim
        }
        Image {
            anchors.centerIn: parent
            width: 17
            height: 17
            sourceSize: Qt.size(17, 17)
            visible: !control.loading && control.iconSource.toString().length > 0
            source: control.iconSource
            opacity: control.selected ? 1 : 0.75
        }
        BusyIndicator {
            anchors.centerIn: parent
            width: 22; height: 22
            running: control.loading && control.visible
            visible: running
        }
    }

    Row {
        id: collectionActions
        parent: root.collectionHost || root
        visible: root.visible
        spacing: root.spacing
        Action {
            objectName: "vinylSongPlay"
            visible: root.showPlay
            enabled: root.playEnabled
            symbol: "playViewed"
            accessibleName: qsTr("Play viewed song")
            onTriggered: root.playRequested()
        }
        Action {
            objectName: "vinylSongSave"
            visible: root.canToggleSaved
            symbol: root.currentSongSaved ? "saved" : "save"
            selected: root.currentSongSaved
            accessibleName: root.currentSongSaved ? qsTr("Unsave") : qsTr("Save")
            onTriggered: root.invokeCurrentSong("togglePlayingSongSaved")
        }
        Action {
            objectName: "vinylSongAddToCollections"
            visible: typeof CollectionsIdentity !== "undefined"
            enabled: visible && CollectionsIdentity.collectionReference(CollectionsReferences.song(root.songData)).ok
            iconSource: "../../assets/icons/lucide/folder-open.svg"
            accessibleName: qsTr("Add to Collections")
            onTriggered: {
                const window = root.Window.window
                if (window && typeof window.openNamedCollectionsPicker === "function")
                    window.openNamedCollectionsPicker(CollectionsReferences.song(root.songData), this)
            }
        }
        Action {
            objectName: "vinylSongPlaylist"
            visible: root.canAddToPlaylist
            symbol: "playlistAdd"
            accessibleName: qsTr("Add to playlist")
            onTriggered: root.invokeCurrentSong("addPlayingSongToPlaylist")
        }
        Action {
            objectName: "vinylSongNext"
            visible: root.hasEnqueueMethod
            enabled: root.canEnqueue
            symbol: "queueNext"
            accessibleName: qsTr("Play next")
            onTriggered: root.invokeCurrentSong("enqueuePlayingSong", [true])
        }
        Action {
            objectName: "vinylSongQueue"
            visible: root.hasEnqueueMethod
            enabled: root.canEnqueue
            symbol: "queueAdd"
            accessibleName: qsTr("Add to queue")
            onTriggered: root.invokeCurrentSong("enqueuePlayingSong", [false])
        }
    }
    Row {
        id: readerActions
        x: root.collectionHost || !collectionActions.width ? 0 : collectionActions.width + root.spacing
        spacing: root.spacing
        Action {
            objectName: "vinylBarVideo"
            visible: root.showReaderTools
            enabled: root.videoBusy ? root.videoSwitchCancellable : (root.videoAvailable || root.videoShown)
            selected: root.videoShown
            symbol: root.videoBusy && root.videoSwitchCancellable ? "cancel" : "video"
            loading: root.videoBusy && !root.videoSwitchCancellable
            accessibleName: root.videoBusy
                ? (root.videoSwitchCancellable ? qsTr("Cancel audio/video switch") : qsTr("Switching playback mode"))
                : root.videoShown ? qsTr("Turn video off and return to original song audio") : qsTr("Switch to video")
            onTriggered: root.videoRequested()
        }
        Action {
            objectName: "vinylBarFullscreen"
            visible: root.showReaderTools && root.videoShown
            selected: root.videoFullscreen
            iconSource: root.videoFullscreen ? "../../assets/icons/fullscreen-exit.svg"
                                             : "../../assets/icons/fullscreen.svg"
            accessibleName: root.videoFullscreen ? qsTr("Exit fullscreen") : qsTr("Enter fullscreen")
            onTriggered: root.videoFullscreenRequested()
        }
        Action {
            objectName: "vinylBarCredits"
            visible: root.showReaderTools
            enabled: root.creditsAvailable
            symbol: "credits"
            accessibleName: qsTr("Credits")
            onTriggered: root.creditsRequested()
        }
    }
}
