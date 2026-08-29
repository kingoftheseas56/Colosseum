import QtQuick

Item {
    id: harness
    width: 320
    height: 240
    visible: false

    property var failures: []
    property var shellComponent: null

    function ck(condition, message) {
        if (!condition)
            failures.push(message)
    }

    component FakeStore: QtObject {
        property var ready: ({ "vol-1": true })
        signal progress(string cid, int done, int total)
        signal finished(string cid)
        signal failed(string cid, string reason)

        function localPages(id) {
            if (!ready[String(id)]) return []
            var out = []
            for (var i = 0; i < 6; ++i)
                out.push({ index: i, url: "file:///fake/p" + i + ".png", group: -1 })
            return out
        }
        function downloadChapter() {}
        function downloadIssue() {}
    }

    component FakeProgress: QtObject {
        property var saved: ({})
        property int localRecordCount: 0
        signal syncedEntryApplied(string kind, string id)

        function get(kind, id) {
            if (String(saved.kind) !== String(kind)
                    || String(saved.id) !== String(id))
                return ({})
            return saved
        }
        function record(entry) { localRecordCount += 1 }
        function forget(kind, id) {}
    }

    Component { id: storeFactory; FakeStore {} }
    Component { id: progressFactory; FakeProgress {} }

    function resumeRecord(chapterId, page, fraction) {
        return {
            id: "series-1",
            kind: "tankoban",
            progress: Number(page) / 6,
            resume: {
                chapterId: String(chapterId),
                page: Number(page),
                pageFraction: Number(fraction),
                scrollFrac: 0,
                maxSeen: Number(page)
            }
        }
    }

    function runChecks() {
        shellComponent = Qt.createComponent(
            "../qml/comicreader/ComicReaderShell.qml")
        ck(shellComponent.status === Component.Ready,
           "shell component failed: " + shellComponent.errorString())
        if (shellComponent.status !== Component.Ready) {
            report()
            return
        }

        var store = storeFactory.createObject(harness)
        var progress = progressFactory.createObject(harness)
        progress.saved = resumeRecord("vol-1", 1, 0)

        var shell = shellComponent.createObject(harness, {
            width: 320,
            height: 240,
            pageStore: store,
            progress: progress,
            entryKind: "tankoban",
            seriesId: "series-1",
            seriesTitle: "Series One",
            chapters: [
                { id: "vol-2", number: 2, name: "Vol. 2" },
                { id: "vol-1", number: 1, name: "Vol. 1" }
            ],
            chapterId: "vol-1"
        })
        ck(shell !== null, "shell creation failed")
        if (!shell) { report(); return }

        ck(shell.currentPage === 1, "baseline open must start at imported page 1")

        var writesBeforeSameVolume = progress.localRecordCount

        // A remote winning Progress record for the already-open volume must enter
        // the shell's existing resume path and move the logical reader anchor.
        progress.saved = resumeRecord("vol-1", 4, 0.25)
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(shell.currentPage === 4,
           "matching remote Tankoban resume did not reposition active reader")
        ck(shell.curChapterId === "vol-1",
           "same-volume remote resume changed entry identity")
        ck(progress.localRecordCount === writesBeforeSameVolume,
           "same-volume remote resume was echoed as a local Progress write")

        // A remote winner for a volume that is not local yet must not blank the
        // current readable volume. The bridge holds it until the store reports ready.
        var writesBeforeUnavailable = progress.localRecordCount
        progress.saved = resumeRecord("vol-2", 3, 0.5)
        progress.syncedEntryApplied("tankoban", "series-1")
        ck(shell.curChapterId === "vol-1",
           "unavailable remote volume displaced the readable current volume")
        ck(progress.localRecordCount === writesBeforeUnavailable,
           "unavailable remote resume was echoed as a local Progress write")

        store.ready["vol-2"] = true
        store.finished("vol-2")
        ck(shell.curChapterId === "vol-2",
           "ready notification did not consume pending remote resume")
        ck(shell.currentPage === 3,
           "pending remote volume did not restore its imported page")
        ck(progress.localRecordCount === writesBeforeUnavailable,
           "ready remote resume was echoed as a local Progress write")

        shell.destroy()
        progress.destroy()
        store.destroy()
        report()
    }

    function report() {
        if (!failures.length) {
            console.log("COMICREADER_SYNC_RESUME_SHELL_OK")
            Qt.exit(0)
            return
        }
        for (var i = 0; i < failures.length; ++i)
            console.log("COMICREADER_SYNC_RESUME_SHELL_FAIL: " + failures[i])
        Qt.exit(1)
    }

    Component.onCompleted: Qt.callLater(runChecks)

    Timer {
        interval: 10000
        running: true
        onTriggered: {
            console.log("COMICREADER_SYNC_RESUME_SHELL_FAIL: timeout")
            Qt.exit(1)
        }
    }
}
