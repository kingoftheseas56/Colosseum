import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/vinyl"

Item {
    id: testRoot

    width: 640
    height: 480
    visible: true

    QtObject {
        id: viewedTrack
        property string title: "Viewed song"
        property bool available: true
    }

    QtObject {
        id: globalPlaybackTrack
        property string title: "Currently playing song"
        property bool available: true
    }

    QtObject {
        id: otherViewedTrack
        property string title: "Another viewed song"
        property bool available: true
    }

    QtObject {
        id: unsupportedTrack
        property string title: "Unsupported song"
        property bool available: true
    }

    QtObject {
        id: viewedSong
        property var playingTrack: viewedTrack
        property bool hasVideo: false
        property bool videoShown: false
        property bool videoBusy: false
        property bool videoSwitchCancellable: false
        property var playingCredits: []
        property bool personalEnabled: true
        property bool saved: false
        property int toggles: 0
        property int playlists: 0
        property int queueCalls: 0
        property bool lastPlayNext: false

        function songIsSaved() { return saved }
        function togglePlayingSongSaved() {
            toggles += 1
            saved = !saved
        }
        function addPlayingSongToPlaylist() { playlists += 1 }
        function enqueuePlayingSong(playNext) {
            queueCalls += 1
            lastPlayNext = playNext
        }
    }

    QtObject {
        id: playingSong
        property var playingTrack: globalPlaybackTrack
        property bool personalEnabled: true
        property bool saved: true
        property int toggles: 0
        property int playlists: 0
        property int queueCalls: 0
        property bool lastPlayNext: false

        function songIsSaved() { return saved }
        function togglePlayingSongSaved() {
            toggles += 1
            saved = !saved
        }
        function addPlayingSongToPlaylist() { playlists += 1 }
        function enqueuePlayingSong(playNext) {
            queueCalls += 1
            lastPlayNext = playNext
        }
    }

    QtObject {
        id: otherSong
        property var playingTrack: otherViewedTrack
        property bool personalEnabled: true
        property bool saved: false
        property int toggles: 0
        property int playlists: 0
        property int queueCalls: 0
        property bool lastPlayNext: false

        function songIsSaved() { return saved }
        function togglePlayingSongSaved() {
            toggles += 1
            saved = !saved
        }
        function addPlayingSongToPlaylist() { playlists += 1 }
        function enqueuePlayingSong(playNext) {
            queueCalls += 1
            lastPlayNext = playNext
        }
    }

    QtObject {
        id: unsupportedSong
        property var playingTrack: unsupportedTrack
        property bool personalEnabled: true
    }

    Item {
        id: scene
        width: 640
        height: 480

        VinylSongActionMenu {
            id: control
            x: 20
            y: 400
            songData: viewedSong
            showPlay: true
            contextKey: "viewed"
        }
    }

    SignalSpy {
        id: videoSpy
        target: control
        signalName: "videoRequested"
    }

    SignalSpy {
        id: fullscreenSpy
        target: control
        signalName: "videoFullscreenRequested"
    }

    SignalSpy {
        id: creditsSpy
        target: control
        signalName: "creditsRequested"
    }

    SignalSpy {
        id: playSpy
        target: control
        signalName: "playRequested"
    }

    TestCase {
        name: "VinylSongActionMenu"
        when: windowShown

        function action(objectName) {
            return findChild(control, objectName)
        }

        function init() {
            control.songData = viewedSong
            control.showPlay = true
            control.videoFullscreen = false
            control.showReaderTools = false
            control.contextKey = "viewed"
            control.visible = true
            viewedTrack.title = "Viewed song"
            viewedTrack.available = true
            globalPlaybackTrack.title = "Currently playing song"
            globalPlaybackTrack.available = true
            otherViewedTrack.title = "Another viewed song"
            otherViewedTrack.available = true
            unsupportedTrack.title = "Unsupported song"
            unsupportedTrack.available = true
            viewedSong.hasVideo = false
            viewedSong.videoShown = false
            viewedSong.videoBusy = false
            viewedSong.videoSwitchCancellable = false
            viewedSong.playingCredits = []
            viewedSong.personalEnabled = true
            viewedSong.saved = false
            viewedSong.toggles = 0
            viewedSong.playlists = 0
            viewedSong.queueCalls = 0
            viewedSong.lastPlayNext = false
            playingSong.toggles = 0
            playingSong.playlists = 0
            playingSong.queueCalls = 0
            otherSong.toggles = 0
            otherSong.playlists = 0
            otherSong.queueCalls = 0
            playSpy.clear()
            videoSpy.clear()
            fullscreenSpy.clear()
            creditsSpy.clear()
        }

        function test_onlySupportedActionsAndPersonalGate() {
            control.showPlay = true
            control.songData = unsupportedSong
            compare(control.hasActions, true)
            compare(action("vinylSongPlay").visible, true)
            compare(action("vinylSongSave").visible, false)
            compare(action("vinylSongPlaylist").visible, false)
            compare(action("vinylSongNext").visible, false)
            compare(action("vinylSongQueue").visible, false)

            control.showPlay = false
            control.songData = viewedSong
            viewedSong.personalEnabled = false
            compare(action("vinylSongSave").visible, false)
            compare(action("vinylSongPlaylist").visible, false)
            compare(action("vinylSongNext").visible, true)
            compare(action("vinylSongQueue").visible, true)
            compare(control.hasActions, true)

            viewedSong.personalEnabled = true
            compare(action("vinylSongSave").visible, true)
            compare(action("vinylSongPlaylist").visible, true)
        }

        function test_playVisibilityAvailabilityAndSignal() {
            control.showPlay = false
            compare(action("vinylSongPlay").visible, false)

            control.showPlay = true
            viewedTrack.available = false
            compare(action("vinylSongPlay").visible, true)
            compare(action("vinylSongPlay").enabled, false)
            action("vinylSongPlay").activate()
            compare(playSpy.count, 0)

            viewedTrack.available = true
            compare(action("vinylSongPlay").enabled, true)
            action("vinylSongPlay").activate()
            compare(playSpy.count, 1)
        }

        function test_unavailableNestedTrackDisablesQueueButAllowsSave() {
            control.songData = viewedSong
            viewedTrack.available = false
            compare(control.canEnqueue, false)
            compare(action("vinylSongNext").visible, true)
            compare(action("vinylSongNext").enabled, false)
            compare(action("vinylSongQueue").visible, true)
            compare(action("vinylSongQueue").enabled, false)
            compare(action("vinylSongSave").enabled, true)
            action("vinylSongQueue").activate()
            compare(viewedSong.queueCalls, 0)
        }

        function test_saveLabelAndSelectedSong() {
            control.songData = viewedSong
            viewedSong.saved = false
            playingSong.saved = true
            compare(action("vinylSongSave").accessibleName, "Save")
            action("vinylSongSave").activate()
            compare(viewedSong.toggles, 1)
            compare(viewedSong.saved, true)
            compare(playingSong.toggles, 0)
            compare(action("vinylSongSave").accessibleName, "Unsave")
        }

        function test_playlistAndQueueUseViewedSong() {
            control.songData = viewedSong
            action("vinylSongPlaylist").activate()
            compare(viewedSong.playlists, 1)
            compare(playingSong.playlists, 0)

            action("vinylSongNext").activate()
            compare(viewedSong.queueCalls, 1)
            compare(viewedSong.lastPlayNext, true)
            compare(playingSong.queueCalls, 0)

            action("vinylSongQueue").activate()
            compare(viewedSong.queueCalls, 2)
            compare(viewedSong.lastPlayNext, false)
            compare(playingSong.queueCalls, 0)
        }

        function test_contextSwitchRebindsDirectActions() {
            control.songData = viewedSong
            control.contextKey = "album-a"
            control.contextKey = "album-b"
            control.songData = otherSong
            compare(action("vinylSongPlaylist").accessibleDescription, "For Another viewed song")
            mouseClick(action("vinylSongPlaylist"))
            compare(otherSong.playlists, 1)
            compare(viewedSong.playlists, 0)
        }

        function test_readerToolsAvailabilityAndSignals() {
            control.showPlay = false
            control.showReaderTools = true
            control.songData = unsupportedSong
            compare(control.hasActions, true)
            compare(action("vinylBarVideo").visible, true)
            compare(action("vinylBarVideo").enabled, false)
            compare(action("vinylBarCredits").visible, true)
            compare(action("vinylBarCredits").enabled, false)

            control.songData = viewedSong
            viewedSong.hasVideo = true
            viewedSong.videoShown = false
            viewedSong.playingCredits = []
            compare(action("vinylBarVideo").enabled, true)
            compare(action("vinylBarVideo").selected, false)
            action("vinylBarVideo").activate()
            compare(videoSpy.count, 1)

            viewedSong.videoShown = true
            compare(action("vinylBarVideo").selected, true)
            viewedSong.playingCredits = ["Artist"]
            compare(action("vinylBarCredits").enabled, true)
            action("vinylBarCredits").activate()
            compare(creditsSpy.count, 1)
        }

        function test_videoOffWithoutAvailability() {
            control.showReaderTools = true
            viewedSong.videoShown = true
            viewedSong.hasVideo = false
            const button = action("vinylBarVideo")
            verify(button.enabled)
            verify(button.selected)
            compare(button.symbol, "video")
            compare(button.accessibleName, "Turn video off and return to original song audio")
            button.activate()
            compare(videoSpy.count, 1)
        }

        function test_videoPendingCancellationAndCommittedLoad() {
            control.showReaderTools = true
            viewedSong.videoBusy = true
            viewedSong.videoSwitchCancellable = true
            const button = action("vinylBarVideo")
            verify(button.enabled)
            compare(button.accessibleName, "Cancel audio/video switch")
            compare(button.symbol, "cancel")
            verify(!button.loading)
            button.activate()
            compare(videoSpy.count, 1)
            viewedSong.videoSwitchCancellable = false
            verify(!button.enabled)
            verify(button.loading)
            compare(button.accessibleName, "Switching playback mode")
            button.activate()
            compare(videoSpy.count, 1)
            viewedSong.videoBusy = false
            verify(!button.loading)
            verify(!button.enabled)
        }

        function test_legacyFixtureWithoutNewCapability() {
            control.showReaderTools = true
            control.songData = { playingTrack: {title: "Legacy"}, hasVideo: true, videoBusy: true }
            verify(!control.videoSwitchCancellable)
            verify(!action("vinylBarVideo").enabled)
            control.songData = { playingTrack: {title: "Legacy"}, videoShown: true }
            verify(action("vinylBarVideo").enabled)
            action("vinylBarVideo").activate()
            compare(videoSpy.count, 1)
        }

        function test_fullscreenVisibilityStateAndSignal() {
            const button = action("vinylBarFullscreen")
            compare(button.visible, false)
            control.showReaderTools = true
            compare(button.visible, false)
            viewedSong.videoShown = true
            verify(button.visible)
            compare(button.accessibleName, "Enter fullscreen")
            waitForRendering(scene)
            mouseClick(button)
            compare(fullscreenSpy.count, 1)
            compare(videoSpy.count, 0)
            control.videoFullscreen = true
            compare(button.accessibleName, "Exit fullscreen")
            verify(button.selected)
            button.activate()
            compare(fullscreenSpy.count, 2)
            control.showReaderTools = false
            compare(button.visible, false)
        }

        function test_directIconsHaveNoOverflowInvoker() {
            control.showReaderTools = true
            waitForRendering(scene)
            compare(findChild(control, "vinylBarSongActionsButton"), null)
            const names = ["vinylSongPlay", "vinylSongSave", "vinylSongPlaylist",
                "vinylSongNext", "vinylSongQueue", "vinylBarVideo", "vinylBarCredits"]
            var previousRight = -1
            for (var i = 0; i < names.length; i++) {
                const button = action(names[i])
                verify(button.visible)
                verify(button.width >= 34 && button.height >= 34)
                verify(button.accessibleName.length > 0)
                const pos = button.mapToItem(control, 0, 0)
                verify(pos.x > previousRight)
                previousRight = pos.x + button.width
            }
            verify(previousRight <= control.width)
        }

        function test_hiddenActionsDoNotReserveWidth() {
            control.showPlay = false
            control.showReaderTools = true
            viewedSong.personalEnabled = false
            compare(action("vinylSongPlay").visible, false)
            compare(action("vinylSongPlay").width, 0)
            compare(action("vinylSongSave").visible, false)
            compare(action("vinylSongSave").width, 0)
            verify(action("vinylBarVideo").visible)
            verify(action("vinylBarVideo").width > 0)
        }

        function test_keyboardEnterActivatesDirectAction() {
            const testWindow = scene.Window.window
            testWindow.requestActivate()
            tryCompare(testWindow, "active", true)
            const button = action("vinylSongPlaylist")
            button.forceActiveFocus()
            tryCompare(testWindow, "activeFocusItem", button)
            keyClick(Qt.Key_Return)
            compare(viewedSong.playlists, 1)
        }
    }
}
