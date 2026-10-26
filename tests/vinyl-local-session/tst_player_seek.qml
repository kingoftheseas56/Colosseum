import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylPlayerBarSeek"
    width: 900
    height: 150
    when: windowShown

    property var host: null
    property var backdrop: null
    property var pageData: null
    property var bar: null

    Component {
        id: windowFactory
        Window { width: 900; height: 150; visible: true }
    }

    Component {
        id: backdropFactory
        Rectangle { width: 900; height: 150; color: "#101014" }
    }

    Component {
        id: pageDataFactory
        QtObject {
            property real duration: 100
            property real position: 10
            property bool paused: false
            property string sourceState: "playing"
            property string sourceKind: "local"
            property string qualityLabel: ""
            property int playingIndex: 0
            property int queueIndex: 0
            property var activeQueueRow: ({queueId:"queue-a"})
            property var playingTrack: ({id:"track-a", title:"Track A"})
            property var playingEdition: ({id:"edition-a", name:"Edition A", artist:"Artist", cover:""})
            property bool canPrevious: false
            property bool canNext: true
            property bool hasLyrics: false
            property bool lyricsShown: false
            property bool hasVideo: false
            property bool videoShown: false
            property bool hasNotes: false
            property bool autoNotes: false
            property var playingCredits: []
            property var seeks: []

            function seek(seconds) {
                seeks = seeks.concat([seconds]);
                position = seconds;
            }
            function formatTime(seconds) {
                var value = Math.max(0, Math.floor(Number(seconds) || 0));
                return Math.floor(value / 60) + ":" + ("0" + value % 60).slice(-2);
            }
            function artwork(source) { return ""; }
            function next(direction) {}
            function toggleLyrics() {}
            function toggleVideo() {}
            function toggleNotes() {}
            function queueRequested() {}
        }
    }

    function createBar() {
        host = windowFactory.createObject(test);
        pageData = pageDataFactory.createObject(test);
        backdrop = backdropFactory.createObject(host.contentItem);
        var component = Qt.createComponent("../../qml/vinyl/VinylPlayerBar.qml");
        compare(component.status, Component.Ready, component.errorString());
        bar = component.createObject(host.contentItem, {
            x:0, y:35, width:900, pageData:pageData, backdrop:backdrop
        });
        verify(bar !== null);
        wait(0);
    }

    function init() { createBar(); }

    function cleanup() {
        if (bar) { bar.destroy(); bar = null; }
        if (backdrop) { backdrop.destroy(); backdrop = null; }
        if (pageData) { pageData.destroy(); pageData = null; }
        if (host) {
            host.close();
            host.destroy();
            wait(0);
            host = null;
        }
    }

    function pointer() { return findChild(bar, "vinylBarSeekPointer"); }
    function seekSurface() { return findChild(bar, "vinylBarSeek"); }

    function test_dragPreviewsUntilReleaseAndClampsToDuration() {
        var mouse = pointer();
        var seek = seekSurface();
        var elapsed = findChild(bar, "vinylBarElapsed");
        verify(mouse !== null && seek !== null && elapsed !== null);
        var focusBefore = seek.activeFocus;

        mousePress(mouse, 12, mouse.height / 2);
        tryCompare(bar, "scrubbing", true);
        compare(pageData.seeks.length, 0);
        mouseMove(mouse, mouse.width + 80, mouse.height / 2, 60);
        compare(pageData.seeks.length, 0, "Dragging previews without issuing seeks.");
        compare(elapsed.text, "1:40");
        mouseRelease(mouse, mouse.width + 80, mouse.height / 2);

        compare(pageData.seeks.length, 1);
        compare(pageData.seeks[0], 100);
        compare(bar.scrubbing, false);
        compare(seek.activeFocus, focusBefore, "Pointer scrubbing must not take keyboard focus.");
    }

    function test_clickCommitsExactlyOneSeek() {
        var mouse = pointer();
        verify(mouse !== null);
        mouseClick(mouse, mouse.width / 4, mouse.height / 2);
        compare(pageData.seeks.length, 1);
        verify(Math.abs(pageData.seeks[0] - 25) < 0.5);
        compare(bar.scrubbing, false);
    }

    function test_escapeCancelsScrubWithoutStaleReleaseSeek() {
        var mouse = pointer();
        verify(mouse !== null);
        mousePress(mouse, mouse.width * 0.2, mouse.height / 2);
        tryCompare(bar, "scrubbing", true);
        keyClick(Qt.Key_Escape);
        tryCompare(bar, "scrubbing", false);
        mouseRelease(mouse, mouse.width * 0.8, mouse.height / 2);
        compare(pageData.seeks.length, 0);
    }

    function test_disablingSeekCancelsActivePointerGrab() {
        var mouse = pointer();
        var seek = seekSurface();
        verify(mouse !== null && seek !== null);
        mousePress(mouse, mouse.width * 0.2, mouse.height / 2);
        tryCompare(bar, "scrubbing", true);
        pageData.sourceState = "finding";
        tryCompare(seek, "enabled", false);
        tryCompare(bar, "scrubbing", false);
        mouseRelease(mouse, mouse.width * 0.8, mouse.height / 2);
        compare(pageData.seeks.length, 0);
    }

    function test_losingMouseGrabCancelsWithoutStaleReleaseSeek() {
        var mouse = pointer();
        var seek = seekSurface();
        verify(mouse !== null && seek !== null);
        mousePress(mouse, mouse.width * 0.2, mouse.height / 2);
        tryCompare(bar, "scrubbing", true);
        mouse.enabled = false;
        tryCompare(bar, "scrubbing", false);
        compare(seek.enabled, true, "The seek surface remains enabled while the pointer grab is lost.");
        mouse.enabled = true;
        mouseRelease(mouse, mouse.width * 0.8, mouse.height / 2);
        compare(pageData.seeks.length, 0);
    }

    function test_trackSwitchCancelsScrubBeforeRelease() {
        var mouse = pointer();
        verify(mouse !== null);
        mousePress(mouse, mouse.width * 0.2, mouse.height / 2);
        tryCompare(bar, "scrubbing", true);

        pageData.activeQueueRow = {queueId:"queue-b"};
        pageData.queueIndex = 1;
        pageData.playingIndex = 0;
        pageData.playingEdition = {id:"edition-b", name:"Edition B", artist:"Artist", cover:""};
        pageData.playingTrack = {id:"track-b", title:"Track B"};
        tryCompare(bar, "scrubbing", false);
        mouseRelease(mouse, mouse.width * 0.8, mouse.height / 2);
        compare(pageData.seeks.length, 0);
    }

    function test_pausedLoadedPlaybackKeepsKeyboardSeekingEnabled() {
        var seek = seekSurface();
        verify(seek !== null);
        pageData.paused = true;
        pageData.position = 98;
        compare(pageData.sourceState, "playing");
        tryCompare(seek, "enabled", true);
        seek.forceActiveFocus();

        keyClick(Qt.Key_Right);
        compare(pageData.seeks[0], 100, "Right clamps at the track duration while paused.");
        keyClick(Qt.Key_Left);
        compare(pageData.seeks[1], 95);
        keyClick(Qt.Key_End);
        compare(pageData.seeks[2], 100);
        keyClick(Qt.Key_Home);
        compare(pageData.seeks[3], 0);
    }
}
