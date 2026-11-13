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
        property bool hasLyrics: true
        property bool hasNotes: true
        property bool lyricsShown: false
        property bool autoNotes: false
        property int manualNoteLine: -1
        function toggleLyrics() { lyricsShown = !lyricsShown }
        function toggleNotes() { autoNotes = !autoNotes; manualNoteLine = -1 }
        property bool hasVideo: false
        property bool videoShown: false
        property bool torrentAvailable: false
        property bool torrentBusy: false
        property string torrentError: ""
        property int torrentRequests: 0
        property int torrentCancels: 0
        function findTorrent() { torrentRequests += 1 }
        function cancelTorrent() { torrentCancels += 1 }
        property bool videoBusy: false
        property bool videoSwitchCancellable: false
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

    SignalSpy { id: playSpy; target: control; signalName: "playRequested" }
    SignalSpy { id: fullscreenSpy; target: control; signalName: "videoFullscreenRequested" }
    TestCase {
        name: "VinylSongActionMenu"
        when: windowShown
        function action(name) { return findChild(control,name) }
        function init() {
            control.songData=viewedSong;control.showPlay=true;control.showLyricsTools=true;control.showReaderTools=true;
            control.collectionHost=null;control.personalHost=null;control.playlistHost=null;control.videoFullscreen=false;
            viewedTrack.available=true;viewedSong.saved=false;viewedSong.toggles=0;viewedSong.playlists=0;
            viewedSong.personalEnabled=true;viewedSong.lyricsShown=false;viewedSong.autoNotes=false;viewedSong.manualNoteLine=-1;
            viewedSong.videoShown=false;otherSong.toggles=0;otherSong.playlists=0;
            playSpy.clear();fullscreenSpy.clear();
        }
        function test_viewedPersonalActionsRemainSeparateFromPlayback() {
            action("vinylSongSave").activate();compare(viewedSong.toggles,1);compare(playingSong.toggles,0);
            verify(action("vinylSongSave").selected);compare(action("vinylSongSave").accessibleName,"Unsave");
            action("vinylSongPlaylist").activate();compare(viewedSong.playlists,1);
            control.songData=otherSong;action("vinylSongSave").activate();compare(otherSong.toggles,1);compare(viewedSong.toggles,1);
            action("vinylSongPlaylist").activate();compare(otherSong.playlists,1);
        }
        function test_personalGateAndExplicitViewedPlay() {
            viewedSong.personalEnabled=false;verify(!action("vinylSongSave").visible);verify(!action("vinylSongPlaylist").visible);
            verify(action("vinylSongPlay").enabled);action("vinylSongPlay").activate();compare(playSpy.count,1);
            viewedTrack.available=false;verify(!action("vinylSongPlay").enabled);action("vinylSongPlay").activate();compare(playSpy.count,1);
            control.showPlay=false;verify(!action("vinylSongPlay").visible);
        }
        function test_readingFunctionsAndLabelsRemainIntact() {
            compare(action("vinylBarLyrics").label,"Lyrics");verify(action("vinylBarLyrics").iconSource.toString().length>0);
            action("vinylBarLyrics").activate();verify(viewedSong.lyricsShown);
            verify(action("vinylBarNotes").iconSource.toString().endsWith("genius.png"));
            action("vinylBarNotes").activate();verify(viewedSong.autoNotes);
            viewedSong.manualNoteLine=3;compare(action("vinylBarNotes").accessibleName,"Resume automatic annotations");
            action("vinylBarNotes").activate();compare(viewedSong.manualNoteLine,-1);
        }
        function test_collectionUsesCanonicalLabelAndNoTooltip() {
            const button=action("vinylSongAddToCollections");
            compare(button.labelText,"Add to Collection");verify(!button.hoverDescriptionEnabled);
            compare(button.iconOnly,true);compare(button.width,34);compare(button.height,34);
            const caption=findChild(button,"vinylSongAddToCollectionsCaption");
            verify(caption!==null);verify(!caption.visible);
            const keyboardAction=findChild(button,"vinylSongAddToCollectionsAction");
            verify(keyboardAction!==null);compare(keyboardAction.accessibleName,"Add to Collection");
            verify(action("vinylSongPlaylist").iconSource.toString().includes("svg"));
        }
        function test_noQueueOrVideoActionOnSongPage() {
            compare(action("vinylSongNext"),null);compare(action("vinylSongQueue"),null);
            compare(action("vinylSongTorrent"),null);compare(action("vinylBarVideo"),null);
        }
        function test_noCreditsAndFullscreenSignals() {
            compare(action("vinylBarCredits"),null);
            verify(!action("vinylBarFullscreen").visible);viewedSong.videoShown=true;verify(action("vinylBarFullscreen").visible);
            action("vinylBarFullscreen").activate();compare(fullscreenSpy.count,1);
            control.videoFullscreen=true;compare(action("vinylBarFullscreen").accessibleName,"Exit fullscreen");
        }
        function test_hostsSeparateSingleActionInstances() {
            const collection=Qt.createQmlObject('import QtQuick; Item { x:20; y:20 }',scene);
            const personal=Qt.createQmlObject('import QtQuick; Item { x:20; y:80 }',scene);
            const playlist=Qt.createQmlObject('import QtQuick; Item { x:150; y:80 }',scene);
            const saveAction=action("vinylSongSave"),playlistAction=action("vinylSongPlaylist"),collectionAction=action("vinylSongAddToCollections");
            control.collectionHost=collection;control.personalHost=personal;control.playlistHost=playlist;
            waitForRendering(scene);
            compare(saveAction.parent.parent,personal);compare(playlistAction.parent.parent,playlist);
            compare(collectionAction.parent.parent,collection);
            compare(action("vinylSongSave"),saveAction);compare(action("vinylSongPlaylist"),playlistAction);
            saveAction.activate();compare(viewedSong.toggles,1);
            playlistAction.activate();compare(viewedSong.playlists,1);
            control.collectionHost=null;control.personalHost=null;control.playlistHost=null;
            collection.destroy();personal.destroy();playlist.destroy();
        }
        function test_keyboardActivatesPlaylist() {
            const button=action("vinylSongPlaylist");button.forceActiveFocus();keyClick(Qt.Key_Return);compare(viewedSong.playlists,1);
        }
    }
}
