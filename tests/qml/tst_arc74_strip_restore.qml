import QtQuick
import QtTest
import "../../qml/comicreader" as Reader

TestCase {
    id: test
    name: "Arc74StripRestore"
    width: 640
    height: 480
    visible: true
    when: windowShown

    component Progress: QtObject {
        property var entry: ({kind: "manga", id: "mal:1", title: "Portuguese",
            resume: {chapterId: "tankoyomi:pt:source:chapter-47", page: 4,
                maxSeen: 4, pageFraction: 0.37, scrollFrac: 0.81}})
        function get(kind, id) { return entry }
        function record(value) { entry = JSON.parse(JSON.stringify(value)) }
    }
    component Pages: QtObject {
        function localPages(id) {
            var result = []
            for (var i = 0; i < 5; ++i)
                result.push({index: i, url: "file:///arc74/p" + i + ".png", group: -1})
            return result
        }
    }
    component Records: QtObject {
        property string all: '{"mal:1":{"layout":"long_strip"}}'
    }
    component Core: QtObject {
        property bool measured: false
        property bool showPixels: false
        property var stripModel: ListModel {}
        signal entryChanged()
        signal pageReady(int page)
        function openEntry() {
            stripModel.clear()
            for (var i = 0; i < 5; ++i)
                stripModel.append({pageIndex: i, top: i * 702,
                    displayWidth: 468, displayHeight: 702, ready: false, errorCode: 0})
            entryChanged()
        }
        function closeEntry() {}
        function setVisible() {}
        function setStripViewport() {}
        function setStripViewportWidth() {}
        function stripPageTop(page) { return page * (measured ? 832 : 702) }
        function stripPageHeight(page) { return measured ? 832 : 702 }
        function stripPageAtCenter(top, height) {
            return Math.max(0, Math.min(4, Math.floor((top + height / 2) / stripPageHeight(0))))
        }
        function pageInfo(page) {
            return measured ? {sourceWidth: 900, sourceHeight: 1600, error: "none"} : {error: "none"}
        }
        function imageUrl() {
            return showPixels ? Qt.resolvedUrl("../../assets/icons/manga.svg").toString() : ""
        }
        function learnSize() {
            measured = true
            for (var i = 0; i < 5; ++i) {
                stripModel.setProperty(i, "top", i * 832)
                stripModel.setProperty(i, "displayHeight", 832)
            }
        }
        function paint() {
            showPixels = true
            for (var i = 0; i < 5; ++i) pageReady(i)
        }
    }
    Component { id: progressComponent; Progress {} }
    Component { id: pagesComponent; Pages {} }
    Component { id: recordsComponent; Records {} }
    Component { id: coreComponent; Core {} }
    Component { id: readerComponent; Reader.ComicReaderShell {} }
    property var readers: []

    function surface(item) {
        if (item.seekToPageFraction !== undefined) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = surface(children[i])
            if (found) return found
        }
        return null
    }
    function makeReader(progress, chapter) {
        var core = createTemporaryObject(coreComponent, test)
        var shell = createTemporaryObject(readerComponent, test, {
            width: 640, height: 480, core: core, progress: progress,
            pageStore: createTemporaryObject(pagesComponent, test),
            seriesRecords: createTemporaryObject(recordsComponent, test),
            seriesId: "mal:1", seriesTitle: "Portuguese", entryKind: "manga",
            chapterId: chapter || "tankoyomi:pt:source:chapter-47",
            recordDebounceMs: 20
        })
        verify(shell !== null)
        readers.push(shell)
        var strip = surface(shell)
        verify(strip !== null)
        tryVerify(function() { return strip.contentHeight > strip.height })
        return {shell: shell, strip: strip, core: core}
    }
    function cleanup() {
        for (var i = 0; i < readers.length; ++i) {
            readers[i].shutdown()
            readers[i].progress = null
            readers[i].destroy()
        }
        readers = []
    }

    function test_restore_waits_for_real_page_dimensions() {
        var progress = createTemporaryObject(progressComponent, test)
        var f = makeReader(progress)
        f.shell._runStripRestore()
        verify(f.shell._stripRestorePending, "estimated dimensions must not complete a fractional restore")
        compare(f.shell._pendingPageFraction, 0.37)
        f.shell.goMinimize()
        compare(progress.entry.resume.pageFraction, 0.37)
    }

    function test_blank_alternate_source_cannot_replace_saved_source() {
        var progress = createTemporaryObject(progressComponent, test)
        var f = makeReader(progress, "tankoyomi:en:other:chapter-47")
        wait(450)
        f.strip._emitPresented()
        f.shell.recordProgress()
        compare(progress.entry.resume.chapterId, "tankoyomi:pt:source:chapter-47")
        compare(f.shell.readerReady, false, "geometry without image pixels is not reader readiness")
    }

    function test_three_reopens_keep_fraction_and_new_user_position() {
        var progress = createTemporaryObject(progressComponent, test)
        for (var cycle = 0; cycle < 3; ++cycle) {
            var f = makeReader(progress)
            f.shell._runStripRestore() // metadata has deliberately not arrived yet
            f.core.learnSize()
            f.shell._runStripRestore()
            f.core.paint()
            tryCompare(f.shell, "readerReady", true)
            fuzzyCompare(f.shell.presentedPageFraction, 0.37, 0.001)
            fuzzyCompare(f.strip.contentY, 3 * 832 + 0.37 * 832 - 240, 1)
            f.shell.recordProgress()
            fuzzyCompare(progress.entry.resume.pageFraction, 0.37, 0.001)
            f.shell.progress = null
            f.shell.shutdown()
            f.shell.visible = false
        }
        var moved = makeReader(progress)
        moved.core.learnSize()
        moved.shell._runStripRestore()
        moved.core.paint()
        tryCompare(moved.shell, "readerReady", true)
        moved.shell.manualActivity()
        moved.strip.contentY = 3 * 832 + 0.60 * 832 - 240
        tryVerify(function() { return Math.abs(progress.entry.resume.pageFraction - 0.60) < 0.001 })
        moved.shell.recordProgress()
        fuzzyCompare(progress.entry.resume.pageFraction, 0.60, 0.001)
    }
}
