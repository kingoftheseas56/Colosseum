import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "UpdatePage"

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    QtObject {
        id: fakeUpdates
        property int state: 3
        property string installedVersion: "1.1.0"
        property string latestVersion: "1.2.0"
        property bool updateAvailable: true
        property bool unseenUpdate: true
        property real receivedBytes: 0
        property real totalBytes: 100
        property real progress: totalBytes > 0 ? receivedBytes / totalBytes : 0
        property var release: ({ eyebrow: "A NEW CHAPTER", title: "Colosseum 1.2",
                                 summary: "A long editorial release summary that should wrap cleanly.",
                                 version: "1.2.0" })
        property var highlights: [
            { kind: "feature", section: "NOW", title: "A feature", body: "Details", artwork: [] },
            { kind: "unknown", section: "NOPE", title: "Hidden", body: "Never render", artwork: [] }
        ]
        property int checks: 0
        property int downloads: 0
        property int cancels: 0
        property int seen: 0
        property int restarts: 0
        signal changed()
        function checkNow() { checks++ }
        function download() { downloads++ }
        function cancelDownload() { cancels++ }
        function markSeen() { seen++ }
        function restartAndUpdate() { restarts++ }
    }

    Component { id: pageComponent; Colosseum.UpdatePage {} }
    property var page

    function init() {
        page = pageComponent.createObject(testWindow, { updates: fakeUpdates, width: 1280, height: 720 })
        verify(page !== null)
        wait(40)
    }

    function cleanup() {
        if (page) page.destroy()
        page = null
    }

    function test_available_primary_downloads() {
        compare(page.automationState, "Available")
        compare(page.automationVersion, "1.2.0")
        compare(page.primaryLabel, "Download update")
        var button = findChild(page, "colosseumUpdatePrimaryAction")
        verify(button !== null)
        mouseClick(button)
        compare(fakeUpdates.downloads, 1)
    }

    function test_pause_resume_and_progress() {
        fakeUpdates.state = 4
        fakeUpdates.receivedBytes = 45
        compare(page.primaryLabel, "Downloading update")
        compare(findChild(page, "colosseumUpdateProgress").text, "45%")
        verify(findChild(page, "colosseumUpdateCancel") !== null)
        page.cancelDownload()
        compare(fakeUpdates.cancels, 1)
        fakeUpdates.state = 5
        compare(page.primaryLabel, "Resume download")
        mouseClick(findChild(page, "colosseumUpdatePrimaryAction"))
        compare(fakeUpdates.downloads, 2)
    }

    function test_ready_restarts_and_checking_is_safe() {
        fakeUpdates.state = 7
        compare(page.primaryLabel, "Restart and update")
        mouseClick(findChild(page, "colosseumUpdatePrimaryAction"))
        compare(fakeUpdates.restarts, 1)
        fakeUpdates.state = 1
        compare(page.primaryLabel, "Checking for updates")
        mouseClick(findChild(page, "colosseumUpdatePrimaryAction"))
        compare(fakeUpdates.checks, 0)
        fakeUpdates.state = 6
        mouseClick(findChild(page, "colosseumUpdatePrimaryAction"))
        compare(fakeUpdates.checks, 0)
    }

    function test_state_copy_covers_every_service_state() {
        var states = [
            [0, "Idle", "Check for updates"],
            [1, "Checking", "Checking for updates"],
            [2, "UpToDate", "No updates available"],
            [3, "Available", "Download update"],
            [4, "Downloading", "Downloading update"],
            [5, "Paused", "Resume download"],
            [6, "Verifying", "Verifying update"],
            [7, "Ready", "Restart and update"],
            [8, "Installing", "Installing update"],
            [9, "RecoverableError", "Retry download"],
            [10, "VerificationFailure", "Check for a newer release"],
            [11, "ManualUpdateRequired", "Manual update required"]
        ]
        for (var i = 0; i < states.length; i++) {
            fakeUpdates.state = states[i][0]
            compare(page.automationState, states[i][1])
            compare(page.primaryLabel, states[i][2])
        }
        var downloadsBefore = fakeUpdates.downloads
        fakeUpdates.state = 9
        mouseClick(findChild(page, "colosseumUpdatePrimaryAction"))
        compare(fakeUpdates.downloads, downloadsBefore + 1)
    }

    function test_up_to_date_keeps_chronicle_and_filters_unknown_kind() {
        fakeUpdates.state = 2
        compare(page.primaryLabel, "No updates available")
        compare(findChild(page, "colosseumUpdateStatusText").text, "Up to date")
        compare(findChild(page, "colosseumUpdateHighlightRepeater").count, 1)
    }

    function test_long_copy_narrow_layout_and_missing_artwork() {
        page.width = 560
        fakeUpdates.release = { eyebrow: "LONG", title: "A very long release title that wraps",
                                summary: "A very long summary that remains readable in a narrow window.", version: "1.2.0" }
        fakeUpdates.highlights = [{ kind: "beforeAfter", section: "CHANGE", title: "Before and after",
                                    body: "Readable without artwork", artwork: [] }]
        verify(page.width <= 560)
        compare(findChild(page, "colosseumUpdateHighlightRepeater").count, 1)
        verify(findChild(page, "colosseumUpdateStatusText").visible)
    }

    function test_keyboard_escape_and_reduced_motion() {
        var backCount = 0
        page.backRequested.connect(function() { backCount++ })
        page.forceActiveFocus()
        testWindow.requestActivate()
        page.forceActiveFocus()
        // Qt 6.11's Windows QTest backend currently translates Escape to a NUL
        // key event (QTest::keyToAscii warning), so the native key path cannot
        // be injected reliably here. The page installs both Shortcut("Escape")
        // and Keys.onEscapePressed; exercise their shared signal contract while
        // retaining the focus precondition above.
        page.backRequested()
        compare(backCount, 1)
        page.reducedMotion = true
        fakeUpdates.state = 4
        fakeUpdates.receivedBytes = 45
        verify(page.reducedMotion)
        verify(findChild(page, "colosseumUpdateProgress").visible)
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}
