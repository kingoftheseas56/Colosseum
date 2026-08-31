import QtQuick 2.15
import QtTest 1.3
import "../../qml/player2" as Player2

// Arc 26 / Function 0005 / Task 5: exercise the real Player2Shell lifecycle and host seam.
TestCase {
    id: testCase
    name: "Player2Progress"

    property var shellUnderTest: null

    QtObject {
        id: fakeSession
        property int state: 3
        property real position: 0
        property real duration: 120
        property real speed: 1
        property bool networkStalled: false
        property real volume: 1
        property bool muted: false
        property var tracks: []

        function setMuted(value) { muted = value }
        function setVolume(value) { volume = value }
        function seekRelative() {}
        function frameStep() {}
        function seekExact() {}
    }

    QtObject {
        id: fakeHost
        property var progressCalls: []
        property var events: []
        property string currentPlaybackUrl: ""
        property string mediaLocalPath: ""

        function reportProgress(mediaId, position, duration, silent) {
            progressCalls.push({
                "mediaId": mediaId,
                "position": position,
                "duration": duration,
                "silent": silent,
                "argumentCount": arguments.length,
                "activityKeyAtCall": testCase.shellUnderTest.activityActiveKey
            })
            events.push("progress")
        }
        function requestMetadata() {}
        function requestSkipSegments() {}
        function requestAdjacentEpisode() {}
        function requestDownload() {}
        function cancelDownload() {}
    }

    Component {
        id: shellComponent
        Player2.Player2Shell {}
    }

    Connections {
        target: testCase.shellUnderTest
        function onBackRequested() { fakeHost.events.push("back") }
        function onMinimizeRequested() { fakeHost.events.push("minimize") }
        function onCloseRequested() { fakeHost.events.push("close") }
    }

    function findDescendant(object, predicate) {
        if (predicate(object))
            return object
        var children = object.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findDescendant(children[i], predicate)
            if (found)
                return found
        }
        return null
    }

    function topBar() {
        return findDescendant(shellUnderTest, function (object) {
            return typeof object.backRequested === "function"
                    && typeof object.minimizeRequested === "function"
                    && typeof object.closeRequested === "function"
                    && object.nowClock !== undefined
                    && object.tight !== undefined
        })
    }

    function closeConfirm() {
        return findDescendant(shellUnderTest, function (object) {
            return typeof object.confirmed === "function"
                    && typeof object.cancelled === "function"
                    && object.open !== undefined
        })
    }

    function init() {
        fakeHost.progressCalls = []
        fakeHost.events = []
        fakeSession.state = 3
        fakeSession.position = 0
        fakeSession.duration = 120
        shellUnderTest = shellComponent.createObject(testCase, {
            "width": 1280,
            "height": 720,
            "session": fakeSession,
            "hostServices": fakeHost,
            "rootMediaId": "tt-task5"
        })
        verify(shellUnderTest !== null, "real Player2Shell could not be created")
        shellUnderTest._lastReportedSec = -1
        shellUnderTest.activityActiveKey = ""
    }

    function cleanup() {
        if (shellUnderTest) {
            shellUnderTest.destroy()
            shellUnderTest = null
        }
    }

    function assertVisibleProgress() {
        compare(fakeHost.progressCalls.length, 1)
        compare(fakeHost.progressCalls[0].argumentCount, 4)
        compare(fakeHost.progressCalls[0].silent, false)
    }

    function test_normal_five_second_cadence_is_silent() {
        fakeSession.position = 15
        shellUnderTest.reportProgress(false)
        compare(fakeHost.progressCalls.length, 1)
        compare(fakeHost.progressCalls[0].position, 15)
        compare(fakeHost.progressCalls[0].argumentCount, 4)
        compare(fakeHost.progressCalls[0].silent, true)
    }

    function test_pause_reports_one_visible_progress() {
        fakeSession.position = 18
        fakeSession.state = 4
        assertVisibleProgress()
    }

    function test_back_reports_visible_progress_before_intent() {
        fakeSession.position = 30
        var bar = topBar()
        verify(bar !== null, "real TopBar child was not found")
        bar.backRequested()
        assertVisibleProgress()
        compare(fakeHost.events, ["progress", "back"])
    }

    function test_minimize_reports_visible_progress_before_intent() {
        fakeSession.position = 30
        var bar = topBar()
        verify(bar !== null, "real TopBar child was not found")
        bar.minimizeRequested()
        assertVisibleProgress()
        compare(fakeHost.events, ["progress", "minimize"])
    }

    function test_confirmed_close_reports_visible_progress_before_intent() {
        fakeSession.state = 3
        fakeSession.position = 30
        shellUnderTest.requestClose()
        var prompt = closeConfirm()
        verify(prompt !== null, "real CloseConfirm child was not found")
        verify(prompt.open, "real shell did not open close confirmation")
        prompt.confirmed()
        assertVisibleProgress()
        compare(fakeHost.events, ["progress", "close"])
    }

    function test_ended_reports_visible_progress_before_activity_end() {
        fakeSession.position = 119
        shellUnderTest.activityActiveKey = "active-key"
        fakeSession.state = 6
        assertVisibleProgress()
        compare(fakeHost.progressCalls[0].activityKeyAtCall, "active-key")
        compare(shellUnderTest.activityActiveKey, "")
    }
}
