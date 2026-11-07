import QtQuick
import QtQuick.Controls.Basic
import ".."
import "../collections"
import "../CollectionsReferences.js" as CollectionsReferences

Item {
    id: root

    width: inlineActions.implicitWidth
    height: 34
    property real spacing: 7
    property Item collectionHost: null
    property Item personalHost: null
    property Item playlistHost: null
    property Item lyricsHost: null
    property Item notesHost: null
    readonly property real collectionWidth: collectionActions.width
    readonly property real personalWidth: personalActions.width
    readonly property real playlistWidth: playlistActions.width
    readonly property real lyricsWidth: lyricsActions.width
    readonly property real notesWidth: notesActions.width
    required property var songData
    property bool videoFullscreen: false
    property bool showPlay: false
    property bool showReaderTools: false
    property bool showLyricsTools: false
    property string contextKey: ""

    signal playRequested()
    signal videoFullscreenRequested()
    signal videoRequested()

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
    readonly property bool videoShown: songData && songData.videoShown === true
    readonly property bool hasActions: root.showPlay || root.canToggleSaved
                                                  || root.canAddToPlaylist
                                                  || root.showReaderTools || root.showLyricsTools
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
        property string symbol: ""
        property string label: ""
        property bool selected: false
        property bool loading: false
        property url iconSource: ""
        width: visible ? (control.label.length ? Math.max(34, caption.implicitWidth + (control.iconSource.toString().length || control.symbol.length ? 40 : 22)) : 34) : 0
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
            x: control.label.length ? 10 : (parent.width - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 17
            height: 17
            visible: !control.loading && !control.iconSource.toString().length && control.symbol.length > 0
            symbol: control.symbol
            color: control.selected ? theme.gold : theme.inkDim
        }
        Image {
            x: control.label.length ? 10 : (parent.width - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 17
            height: 17
            sourceSize: Qt.size(17, 17)
            visible: !control.loading && control.iconSource.toString().length > 0
            source: control.iconSource
            opacity: control.selected ? 1 : 0.75
        }
        Text {
            id: caption
            objectName: control.objectName + "Caption"
            anchors.verticalCenter: parent.verticalCenter
            x: control.iconSource.toString().length || control.symbol.length ? 34 : 11
            text: control.label
            color: control.selected ? theme.gold : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
        }
        BusyIndicator {
            anchors.centerIn: parent
            width: 22; height: 22
            running: control.loading && control.visible
            visible: running
        }
    }

    Row {
        id: inlineActions
        spacing: root.spacing
        Row {
            id: personalActions
            parent: root.personalHost || inlineActions
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
        }
        Row {
            id: playlistActions
            parent: root.playlistHost || inlineActions
            visible: root.visible
            Action {
                objectName: "vinylSongPlaylist"
                visible: root.canAddToPlaylist
                iconSource: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#c7c0b4" stroke-width="1.7" stroke-linecap="round"><path d="M3 5h13M3 10h9M3 15h6M17 12v9M12.5 16.5h9"/></svg>')
                accessibleName: qsTr("Add to playlist")
                onTriggered: root.invokeCurrentSong("addPlayingSongToPlaylist")
            }
        }
        Row {
            id: collectionActions
            parent: root.collectionHost || inlineActions
            visible: root.visible
            AddToCollectionsButton {
                objectName: "vinylSongAddToCollections"
                reference: CollectionsReferences.song(root.songData)
                labelText: qsTr("Add to Collection")
                iconOnly: true
                hoverDescriptionEnabled: false
                width: implicitWidth
                height: 34
            }
        }
        Row {
            id: readerActions
            spacing: root.spacing
            Row {
                id: lyricsActions
                parent: root.lyricsHost || readerActions
                visible: root.visible
                Action {
                    objectName: "vinylBarLyrics"
                    visible: root.showLyricsTools
                    iconSource: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#c7c0b4" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4h16v12H9l-5 4zM7 8h10M7 12h7"/></svg>')
                    label: qsTr("Lyrics")
                    accessibleName: !root.songData.hasLyrics && root.songData.lyricsError ? qsTr("Retry lyrics") : qsTr("Lyrics")
                    enabled: !!root.songData.hasLyrics || !!root.songData.lyricsError
                    selected: !!root.songData.lyricsShown
                    onTriggered: root.invokeCurrentSong("toggleLyrics")
                }
            }
            Row {
                id: notesActions
                parent: root.notesHost || readerActions
                visible: root.visible
                Action {
                    objectName: "vinylBarNotes"
                    visible: root.showLyricsTools
                    iconSource: "../../assets/icons/providers/genius.png"
                    label: qsTr("Annotations")
                    accessibleName: root.songData.manualNoteLine >= 0 ? qsTr("Resume automatic annotations") : qsTr("Automatic annotations")
                    enabled: !!root.songData.hasNotes
                    selected: !!root.songData.autoNotes && root.songData.manualNoteLine < 0 && !!root.songData.lyricsShown
                    onTriggered: root.invokeCurrentSong("toggleNotes")
                }
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
        }
    }
}
