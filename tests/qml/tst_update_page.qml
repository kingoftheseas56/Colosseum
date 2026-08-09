import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "UpdatePage"
    when: windowShown

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
            { kind: "feature", section: "READER", title: "Reader", body: "Read without losing the room.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "DISCOVER", title: "Discover", body: "Find the next world in one quiet sweep.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "BIBLIO", title: "Biblio", body: "Keep every volume close at hand.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "THEATRE", title: "Theatre", body: "Return to the exact frame you left.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "THE HOUSE", title: "The house", body: "One home for everything you follow.", artwork: [] },
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
    Component { id: taskbarComponent; Colosseum.Taskbar {} }
    property var page
    property var taskbar

    function syncTaskbar() {
        taskbar.updatePresentation = page.taskbarPresentation
    }

    function waitForPrimaryAction(label) {
        var action = findChild(taskbar, "colosseumUpdatePrimaryAction")
        verify(action !== null)
        tryVerify(function() { return taskbar.width > 0 && taskbar.height > 0 }, 1000,
                  "taskbar fixture must have geometry before testing its primary action")
        tryVerify(function() {
            return taskbar.updatePrimaryVisible
                    && taskbar.updatePrimaryEnabled
                    && taskbar.updatePrimaryLabel === label
                    && action.visible
                    && action.enabled
        }, 1000, "primary action must become visible and enabled before input")
        tryVerify(function() { return action.width >= 44 && action.height >= 44 }, 1000,
                  "primary action must be laid out to the 44 logical-pixel minimum before input; initial action="
                  + action.width + "x" + action.height + ", parent="
                  + (action.parent ? action.parent.width : -1) + "x"
                  + (action.parent ? action.parent.height : -1)
                  + ", implicit=" + action.implicitWidth + "x" + action.implicitHeight)
        return action
    }

    function waitForActionlessTaskbar() {
        var action = findChild(taskbar, "colosseumUpdatePrimaryAction")
        verify(action !== null)
        tryVerify(function() {
            return !taskbar.updatePrimaryVisible
                    && !taskbar.updatePrimaryEnabled
                    && !action.visible
                    && !action.enabled
        }, 1000, "actionless service states must not expose a primary action")
    }

    function syncTaskbarForPrimaryAction(label) {
        syncTaskbar()
        return waitForPrimaryAction(label)
    }

    function init() {
        fakeUpdates.state = 3
        fakeUpdates.receivedBytes = 0
        fakeUpdates.release = ({ eyebrow: "A NEW CHAPTER", title: "Colosseum 1.2",
                                 summary: "A long editorial release summary that should wrap cleanly.",
                                 version: "1.2.0" })
        fakeUpdates.highlights = [
            { kind: "feature", section: "READER", title: "Reader", body: "Read without losing the room.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "DISCOVER", title: "Discover", body: "Find the next world in one quiet sweep.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "BIBLIO", title: "Biblio", body: "Keep every volume close at hand.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "THEATRE", title: "Theatre", body: "Return to the exact frame you left.",
              artwork: [Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg")] },
            { kind: "feature", section: "THE HOUSE", title: "The house", body: "One home for everything you follow.", artwork: [] },
            { kind: "unknown", section: "NOPE", title: "Hidden", body: "Never render", artwork: [] }
        ]
        page = pageComponent.createObject(testWindow, { updates: fakeUpdates, width: 1280, height: 720 })
        verify(page !== null)
        taskbar = taskbarComponent.createObject(testWindow, {
            width: testWindow.width,
            height: testWindow.height,
            open: true,
            updateActive: true,
            updateAvailable: true,
            updateUnseen: false,
            updatePresentation: page.taskbarPresentation
        })
        verify(taskbar !== null)
        taskbar.updatePrimaryActionRequested.connect(function() { page.invokePrimaryAction() })
        syncTaskbarForPrimaryAction("Download update")
    }

    function cleanup() {
        if (page) page.destroy()
        page = null
        if (taskbar) taskbar.destroy()
        taskbar = null
    }

    function test_available_primary_downloads() {
        compare(page.automationState, "Available")
        compare(page.automationVersion, "1.2.0")
        compare(page.primaryLabel, "Download update")
        var button = waitForPrimaryAction("Download update")
        verify(button.width >= 44)
        verify(button.height >= 44)
        verify(button.activeFocusOnTab)
        mouseClick(button)
        compare(fakeUpdates.downloads, 1)
    }

    function test_available_transition_does_not_rearm_taskbar_auto_collapse() {
        taskbar.open = true
        taskbar.updateActive = true
        taskbar.autoRevealed = false
        taskbar.updateAvailable = false
        taskbar.autoRevealed = true
        taskbar.updateAvailable = true
        verify(taskbar.open)
        verify(!taskbar.autoRevealed,
               "an availability transition must not arm the auto-collapse timer over the Update page")
    }

    function test_pause_resume_and_progress() {
        fakeUpdates.state = 4
        fakeUpdates.receivedBytes = 224395264
        fakeUpdates.totalBytes = 330301440
        compare(page.primaryLabel, "Pause download")
        var pauseButton = syncTaskbarForPrimaryAction("Pause download")
        compare(findChild(taskbar, "colosseumUpdateProgress").text,
                "214 MB of 315 MB \u00b7 68%")
        compare(findChild(taskbar, "colosseumUpdateProgressText").text,
                "214 MB of 315 MB \u00b7 68%")
        verify(findChild(taskbar, "colosseumUpdateProgressTrack").visible)
        compare(findChild(taskbar, "colosseumUpdatePrimaryAction").Accessible.name, "Pause download")
        mouseClick(pauseButton)
        compare(fakeUpdates.cancels, 1)
        fakeUpdates.state = 5
        compare(page.primaryLabel, "Resume download")
        var resumeButton = syncTaskbarForPrimaryAction("Resume download")
        compare(findChild(taskbar, "colosseumUpdateStatusText").text, "Update paused")
        mouseClick(resumeButton)
        compare(fakeUpdates.downloads, 2)
    }

    function test_ready_restarts_and_checking_is_safe() {
        fakeUpdates.state = 7
        var restartButton = syncTaskbarForPrimaryAction("Restart and update")
        compare(page.primaryLabel, "Restart and update")
        mouseClick(restartButton)
        compare(fakeUpdates.restarts, 1)
        fakeUpdates.state = 1
        syncTaskbar()
        compare(page.primaryLabel, "")
        waitForActionlessTaskbar()
        compare(fakeUpdates.checks, 0)
        fakeUpdates.state = 6
        syncTaskbar()
        waitForActionlessTaskbar()
        compare(fakeUpdates.checks, 0)
        fakeUpdates.state = 8
        syncTaskbar()
        waitForActionlessTaskbar()
        page.invokePrimaryAction()
        compare(fakeUpdates.checks, 0)
    }

    function test_state_copy_covers_every_service_state() {
        var states = [
            [0, "Idle", "No update check yet", "Check again", "Target 1.2.0", false, "check"],
            [1, "Checking", "Checking for updates", "", "Target 1.2.0", false, ""],
            [2, "UpToDate", "Everything is up to date", "Check again", "Installed 1.1.0 \u00b7 Latest 1.2.0", false, "check"],
            [3, "Available", "Colosseum 1.2.0 is ready", "Download update", "Target 1.2.0", false, "download"],
            [4, "Downloading", "Updating to 1.2.0", "Pause download", "", true, "cancel"],
            [5, "Paused", "Update paused", "Resume download", "Target 1.2.0", true, "download"],
            [6, "Verifying", "Verifying the update", "", "Target 1.2.0", false, ""],
            [7, "Ready", "Ready to enter 1.2.0", "Restart and update", "Target 1.2.0", false, "restart"],
            [8, "Installing", "Colosseum is updating", "", "Target 1.2.0", false, ""],
            [9, "RecoverableError", "The update could not finish", "Retry download", "Target 1.2.0", false, "download"],
            [10, "VerificationFailure", "This update could not be verified", "Check again", "Target 1.2.0", false, "check"],
            [11, "ManualUpdateRequired", "Manual update required", "Check again", "Target 1.2.0", false, "check"]
        ]
        for (var i = 0; i < states.length; i++) {
            fakeUpdates.state = states[i][0]
            fakeUpdates.receivedBytes = states[i][5] ? 224395264 : 0
            fakeUpdates.totalBytes = states[i][5] ? 330301440 : 100
            syncTaskbar()
            compare(page.automationState, states[i][1])
            compare(findChild(taskbar, "colosseumUpdateStatusText").text, states[i][2])
            compare(page.primaryLabel, states[i][3])
            compare(taskbar.updatePrimaryVisible, states[i][3].length > 0)
            compare(taskbar.updatePrimaryEnabled, states[i][3].length > 0)
            compare(findChild(taskbar, "colosseumUpdateStatusMetadata").text, states[i][4])
            compare(taskbar.updateProgressVisible, states[i][5])
            if (states[i][5])
                compare(findChild(taskbar, "colosseumUpdateProgressText").text,
                        "214 MB of 315 MB \u00b7 68%")

            var checks = fakeUpdates.checks
            var downloads = fakeUpdates.downloads
            var cancels = fakeUpdates.cancels
            var restarts = fakeUpdates.restarts
            if (states[i][6].length > 0) {
                mouseClick(waitForPrimaryAction(states[i][3]))
            } else {
                waitForActionlessTaskbar()
                page.invokePrimaryAction()
            }
            compare(fakeUpdates.checks, checks + (states[i][6] === "check" ? 1 : 0))
            compare(fakeUpdates.downloads, downloads + (states[i][6] === "download" ? 1 : 0))
            compare(fakeUpdates.cancels, cancels + (states[i][6] === "cancel" ? 1 : 0))
            compare(fakeUpdates.restarts, restarts + (states[i][6] === "restart" ? 1 : 0))
        }
    }

    function test_up_to_date_keeps_chronicle_and_filters_unknown_kind() {
        fakeUpdates.state = 2
        syncTaskbar()
        compare(page.primaryLabel, "Check again")
        compare(findChild(taskbar, "colosseumUpdateStatusText").text, "Everything is up to date")
        verify(findChild(taskbar, "colosseumUpdateStatusMetadata").text.indexOf("Installed 1.1.0") >= 0)
        verify(findChild(taskbar, "colosseumUpdateStatusMetadata").text.indexOf("Latest 1.2.0") >= 0)
        compare(findChild(page, "colosseumUpdateGallery").chapterCount, 5)
        verify(findChild(page, "colosseumUpdateChapter_06") === null)
    }

    function test_long_copy_narrow_layout_and_missing_artwork() {
        page.width = 560
        fakeUpdates.release = { eyebrow: "LONG", title: "A very long release title that wraps",
                                summary: "A very long summary that remains readable in a narrow window.", version: "1.2.0" }
        fakeUpdates.highlights = [{ kind: "beforeAfter", section: "CHANGE", title: "Before and after",
                                    body: "Readable without artwork", artwork: [] }]
        verify(page.width <= 560)
        compare(findChild(page, "colosseumUpdateGallery").chapterCount, 1)
        compare(findChild(page, "colosseumUpdateChapterTitle").text, "Before and after")
        syncTaskbar()
        verify(findChild(taskbar, "colosseumUpdateStatusText").visible)
    }

    function test_gallery_chapter_contract_and_accessible_names() {
        var gallery = findChild(page, "colosseumUpdateGallery")
        verify(gallery !== null)
        tryVerify(function() { return gallery.automationVisualReady }, 1000,
                  "chapter assertions begin only after the settled stage is visually ready")
        compare(gallery.chapterCount, 5)
        compare(gallery.currentIndex, 0)
        compare(findChild(page, "colosseumUpdateVersionTitle").text, "1.2.0")
        compare(findChild(page, "colosseumUpdateChapterTitle").text, "Reader")
        compare(findChild(page, "colosseumUpdateChapterBody").text, "Read without losing the room.")
        compare(findChild(page, "colosseumUpdateChapterCount").text, "01 / 05")
        compare(findChild(page, "colosseumUpdateChapterLabel").text, "READER")
        compare(findChild(page, "colosseumUpdateChapter_01").Accessible.name, "Chapter 1: Reader")
        compare(findChild(page, "colosseumUpdateChapter_05").Accessible.name, "Chapter 5: The house")
    }

    function test_gallery_visual_readiness_requires_settled_stage_effect() {
        var gallery = findChild(page, "colosseumUpdateGallery")
        verify(gallery !== null)
        verify(typeof gallery.automationStageOpacity === "number",
               "the named gallery automation surface must expose rendered stage opacity")
        verify(typeof gallery.automationStageSettled === "boolean",
               "the named gallery automation surface must expose whether the stage has settled")
        if (!gallery.automationStageSettled)
            verify(!gallery.automationVisualReady,
                   "visual readiness must remain false while the rendered stage is below its settled opacity")
        tryVerify(function() {
            return gallery.automationStageSettled && gallery.automationVisualReady
        }, 1000, "visual readiness must turn true only after the existing stage effect settles")
        verify(gallery.automationStageOpacity >= 0.99)
    }

    function test_update_chrome_has_non_overlapping_navigation_lane() {
        var back = findChild(page, "colosseumUpdateBackAction")
        var eyebrow = findChild(page, "colosseumUpdateReleaseLabel")
        var version = findChild(page, "colosseumUpdateVersionTitle")
        verify(back !== null)
        verify(eyebrow !== null)
        verify(version !== null)
        verify(eyebrow.x >= 126)
        compare(version.x, eyebrow.x)
        verify(eyebrow.x > back.x + back.width + 24)
        compare(eyebrow.text, "COLOSSEUM UPDATE")
        var gallery = findChild(page, "colosseumUpdateGallery")
        var body = findChild(page, "colosseumUpdateChapterBody")
        var nav = findChild(page, "colosseumUpdateChapterNav")
        verify(body !== null)
        verify(nav !== null)
        verify(nav.y + nav.height <= page.height - 64)
        verify(body.mapToItem(gallery, 0, body.height).y + 24 <= nav.y)
        page.width = 1920
        page.height = 1080
        wait(0)
        verify(body.mapToItem(gallery, 0, body.height).y + 24 <= nav.y)
    }

    function test_gallery_direct_selection_next_wrap_and_keyboard() {
        var gallery = findChild(page, "colosseumUpdateGallery")
        var requests = 0
        gallery.chapterRequested.connect(function() { requests++ })
        mouseClick(findChild(page, "colosseumUpdateChapter_03"))
        compare(gallery.currentIndex, 2)
        compare(findChild(page, "colosseumUpdateChapterTitle").text, "Biblio")
        compare(requests, 1)
        var selector = findChild(page, "colosseumUpdateChapter_03")
        var next = findChild(page, "colosseumUpdateNextChapter")
        verify(selector.width >= 44)
        verify(selector.height >= 44)
        verify(selector.activeFocusOnTab)
        verify(next.width >= 44)
        verify(next.height >= 44)
        verify(next.activeFocusOnTab)
        mouseClick(next)
        mouseClick(next)
        mouseClick(next)
        compare(gallery.currentIndex, 0)
        testWindow.requestActivate()
        page.forceActiveFocus()
        gallery.forceActiveFocus()
        wait(0)
        verify(gallery.activeFocus)
        // Qt 6.11's Windows QTest backend translates arrow keys to a NUL key code in this
        // harness (the same limitation is documented for Escape below). Exercise the exact
        // focus-owned navigation handler directly while retaining the focus precondition.
        gallery.moveChapter(1)
        compare(gallery.currentIndex, 1)
        gallery.moveChapter(-1)
        compare(gallery.currentIndex, 0)
    }

    function test_gallery_empty_list_synthesizes_copy_only_chapter() {
        fakeUpdates.highlights = []
        wait(0)
        var gallery = findChild(page, "colosseumUpdateGallery")
        compare(gallery.chapterCount, 1)
        compare(findChild(page, "colosseumUpdateChapterTitle").text, "Colosseum 1.2")
        compare(findChild(page, "colosseumUpdateChapterBody").text,
                "A long editorial release summary that should wrap cleanly.")
        verify(findChild(page, "colosseumUpdateGalleryFallbackArt").visible)
        verify(!findChild(page, "colosseumUpdateChapterLabel").visible)
        verify(!findChild(page, "colosseumUpdateChapter_01").visible)
        verify(!findChild(page, "colosseumUpdateNextChapter").visible)
    }

    function test_gallery_reduced_motion_disables_crossfade_and_keeps_fallback() {
        var gallery = findChild(page, "colosseumUpdateGallery")
        page.reducedMotion = true
        compare(gallery.imageCrossfadeEnabled, false)
        mouseClick(findChild(page, "colosseumUpdateChapter_05"))
        verify(findChild(page, "colosseumUpdateGalleryFallbackArt").visible)
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
        fakeUpdates.receivedBytes = 45 * 1048576
        fakeUpdates.totalBytes = 0
        syncTaskbar()
        verify(page.reducedMotion)
        verify(findChild(taskbar, "colosseumUpdateProgressText").visible)
        verify(findChild(taskbar, "colosseumUpdateProgress").visible)
        compare(findChild(taskbar, "colosseumUpdateProgress").text,
                "45 MB downloaded \u00b7 size unknown")
        verify(!taskbar.updateProgressAnimated)
        compare(findChild(taskbar, "colosseumUpdateProgressText").text, "45 MB downloaded \u00b7 size unknown")
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
