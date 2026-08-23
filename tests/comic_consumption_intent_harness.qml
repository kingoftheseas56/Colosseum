// comic_consumption_intent_harness.qml — Arc 19 deterministic consume-vs-acquire gate.
// Copy beside Colosseum/tests and run with test_comic_consumption_intent.ps1 after adoption.
// No network or disk library is used: ComicSeries.qml runs in baked mode against fake services.
import QtQuick
import QtQuick.Window

Window {
    id: harness
    width: 1400
    height: 900
    visible: true

    component FakeComics: QtObject {
        property var states: ({})
        property var pageMap: ({})
        property int downloadCalls: 0
        property int cancelCalls: 0
        property int deleteCalls: 0
        signal progress(string issueId, real done, real total)
        signal finished(string issueId)
        signal failed(string issueId, string reason)
        signal removed(string issueId)

        function _copy(source) {
            var out = ({})
            for (var k in source) out[k] = source[k]
            return out
        }
        function setState(id, state, withPages) {
            var next = _copy(states)
            next[String(id)] = { "state": String(state), "done": state === "done" ? 100 : 0,
                                 "total": state === "done" ? 100 : 0 }
            states = next
            var pages = _copy(pageMap)
            pages[String(id)] = withPages ? [ { "index": 0, "url": "file:///arc19/page.jpg" } ] : []
            pageMap = pages
        }
        function statusOf(id) {
            return states[String(id)] !== undefined
                ? states[String(id)] : { "state": "none", "done": 0, "total": 0 }
        }
        function localPages(id) { return pageMap[String(id)] || [] }
        function downloadIssue(id, url, seriesId, seriesTitle, label, expectedBytes) {
            downloadCalls += 1
            setState(String(id), "resolving", false)
        }
        function cancelDownload(id) {
            cancelCalls += 1
            setState(String(id), "none", false)
            removed(String(id))
        }
        function deleteIssue(id) {
            deleteCalls += 1
            setState(String(id), "none", false)
            removed(String(id))
        }
        function finish(id) {
            setState(String(id), "done", true)
            finished(String(id))
        }
        function fail(id, reason) {
            setState(String(id), "none", false)
            failed(String(id), String(reason || "network failure"))
        }
    }

    component FakeProgress: QtObject {
        property int revision: 1
        property var record: ({
            "id": "gcd:19", "kind": "comic", "progress": 0.48,
            "resume": { "chapterId": "r1", "page": 48, "maxSeen": 48, "finished": false }
        })
        function get(kind, id) {
            return kind === "comic" && String(id) === String(record.id) ? record : ({})
        }
    }

    FakeComics { id: comics }
    FakeProgress { id: progress }

    property var page: null
    property var r1: ({ id: "r1", name: "Issue #1", url: "https://example/r1", cover: "file:///cover1", sizeMB: 10, year: 2026, collection: false })
    property var r2: ({ id: "r2", name: "Issue #2", url: "https://example/r2", cover: "file:///cover2", sizeMB: 10, year: 2026, collection: false })
    property var r3: ({ id: "r3", name: "Issue #3", url: "https://example/r3", cover: "file:///cover3", sizeMB: 10, year: 2026, collection: false })
    property var r4: ({ id: "r4", name: "Issue #4", url: "https://example/r4", cover: "file:///cover4", sizeMB: 10, year: 2026, collection: false })
    property var r5: ({ id: "r5", name: "Issue #5", url: "https://example/r5", cover: "file:///cover5", sizeMB: 10, year: 2026, collection: false })
    property var r6: ({ id: "r6", name: "Issue #6", url: "https://example/r6", cover: "file:///cover6", sizeMB: 10, year: 2026, collection: false })
    property var r7: ({ id: "r7", name: "Issue #7", url: "https://example/r7", cover: "file:///cover7", sizeMB: 10, year: 2026, collection: false })
    property var r8: ({ id: "r8", name: "Issue #8", url: "https://example/r8", cover: "file:///cover8", sizeMB: 10, year: 2026, collection: false })
    property var r9: ({ id: "r9", name: "Issue #9", url: "https://example/r9", cover: "file:///cover9", sizeMB: 10, year: 2026, collection: false })
    property var extra: ({ id: "extra", name: "Sketchbook", url: "https://example/extra", cover: "file:///coverx", sizeMB: 10, year: 2026, collection: false, packRole: "extra" })
    property var main: ({ id: "main", name: "Volume 1", url: "https://example/main", cover: "file:///coverm", sizeMB: 10, year: 2026, collection: false, packRole: "main" })

    // HOUSE HARNESS PATTERN: a thrown error HANGS qml.exe offscreen. ck NEVER throws — it
    // collects failures; the run prints exactly ONE COMIC_CONSUMPTION_INTENT_OK when clean,
    // else one COMIC_CONSUMPTION_INTENT_FAIL: <msg> per failure and Qt.exit(1).
    property var failures: []
    function ck(value, message) {
        if (!value) failures.push(message)
    }
    function report() {
        if (failures.length === 0) {
            console.log("COMIC_CONSUMPTION_INTENT_OK")
            Qt.exit(0)
        } else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMIC_CONSUMPTION_INTENT_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    function makePage() {
        var comp = Qt.createComponent("../qml/ComicSeries.qml")
        if (comp.status === Component.Error) throw new Error(comp.errorString())
        var rows = [r1, r2, r3, r4, r5, r6, r7, r8, r9]
        var p = comp.createObject(harness, {
            "width": harness.width, "height": harness.height,
            "seriesTitle": "Arc 19 Comics", "poster": "file:///arc19/poster.jpg",
            "gcdId": 19, "bakedReleases": rows,
            "comicsRef": comics, "progressRef": progress
        })
        if (!p) throw new Error("ComicSeries candidate did not instantiate")
        return p
    }
    function runChecks() {
        try {
            comics.setState("r1", "done", true)
            page = makePage()

            ck(page.seriesId === "gcd:19", "baked candidate must retain the GCD progress namespace")
            ck(page.readActionLabel(r1, "done") === "Continue · 48%",
               "the exact resumed issue must expose Continue plus reading progress")
            ck(page.readStatusLine(r1, "done") === "48% read",
               "reading progress must stay distinct from acquisition progress")

            // Ready Read: opens immediately, starts no acquisition.
            var before = comics.downloadCalls
            page.openChapterId = ""
            ck(page.readRelease(r1, "done"), "ready Read must be accepted")
            ck(page.openChapterId === "r1", "ready Read must open the exact release immediately")
            ck(comics.downloadCalls === before, "ready Read must not start a download")
            ck(!page.pendingReadActive, "ready Read must leave no pending foreground intent")

            // Fresh Read: one assertion starts acquisition and remembers the exact release.
            page.openChapterId = ""
            comics.setState("r2", "none", false)
            before = comics.downloadCalls
            ck(page.readRelease(r2, "none"), "fresh Read must start or adopt acquisition")
            ck(comics.downloadCalls === before + 1, "fresh Read must start exactly one acquisition")
            ck(page.pendingReadReleaseId === "r2", "fresh Read must remember exact release identity")
            ck(page.openChapterId === "", "fresh Read must not open before the release is readable")
            // A second Read on the same already-arriving issue adopts the native job.
            before = comics.downloadCalls
            ck(page.readRelease(r2, "resolving"), "Read on an in-flight issue must be accepted")
            ck(comics.downloadCalls === before, "in-flight Read must not duplicate the download")
            ck(page.readActionLabel(r2, "resolving") === "Reading when ready",
               "an adopted foreground job must say it will read when ready")

            // Wrong completion cannot satisfy the exact pending identity.
            comics.finish("r3")
            ck(page.openChapterId === "", "a different issue finishing must never open the reader")
            ck(page.pendingReadReleaseId === "r2", "wrong completion must not clear the intended issue")

            // Exact finish opens through the same reader identity as a local Read.
            comics.finish("r2")
            ck(page.openChapterId === "r2", "exact foreground completion must open the reader")
            ck(!page.pendingReadActive, "successful foreground completion must consume the intent once")

            // Duplicate finish after the intent fired must be inert.
            page.openChapterId = ""
            comics.finished("r2")
            ck(page.openChapterId === "", "duplicate finish must not re-open an already-consumed intent")

            // Explicit Download is acquire-only and must never auto-open.
            comics.setState("r3", "none", false)
            before = comics.downloadCalls
            ck(page.downloadRelease(r3, "none"), "explicit Download must start acquisition")
            ck(comics.downloadCalls === before + 1, "explicit Download must start exactly one job")
            ck(!page.pendingReadActive, "explicit Download must not create a consume intent")
            comics.finish("r3")
            ck(page.openChapterId === "", "explicit Download completion must stay on the series page")
            // Read can attach to a job that was started earlier from Download.
            comics.setState("r4", "downloading", false)
            before = comics.downloadCalls
            ck(page.readRelease(r4, "downloading"), "Read must adopt an existing download")
            ck(comics.downloadCalls === before, "adoption must not start a second job")
            comics.finish("r4")
            ck(page.openChapterId === "r4", "adopted job must open when the exact issue becomes ready")

            // Leaving the foreground context cancels auto-open, not the transport.
            page.openChapterId = ""
            comics.setState("r5", "none", false)
            page.readRelease(r5, "none")
            ck(page.pendingReadReleaseId === "r5", "cancellation case must begin with a pending Read")
            page._invalidateReadIntent()
            ck(comics.statusOf("r5").state === "resolving", "cancelling Read intent must not cancel the download")
            comics.finish("r5")
            ck(page.openChapterId === "", "a completion after navigation cancellation must not hijack focus")

            // Newer Read wins: a stale success from the superseded issue is ignored.
            comics.setState("r6", "none", false)
            comics.setState("r7", "none", false)
            page.readRelease(r6, "none")
            page.readRelease(r7, "none")
            ck(page.pendingReadReleaseId === "r7", "new Read must supersede the older generation")
            comics.finish("r6")
            ck(page.openChapterId === "", "stale generation completion must not open")
            comics.finish("r7")
            ck(page.openChapterId === "r7", "current generation completion must open")
            // Terminal unavailability is honest: no pending intent and no transport call.
            page.openChapterId = ""
            before = comics.downloadCalls
            ck(!page.readRelease(r8, "dead"), "dead source must reject Read honestly")
            ck(comics.downloadCalls === before && !page.pendingReadActive,
               "dead source must not invent acquisition or foreground intent")

            // A failure clears foreground auto-open; retry is a new user assertion/generation.
            comics.setState("r9", "none", false)
            page.readRelease(r9, "none")
            ck(page.pendingReadReleaseId === "r9", "failure case must begin with pending Read")
            comics.fail("r9", "temporary network failure")
            ck(!page.pendingReadActive && page.openChapterId === "",
               "terminal callback for a failed Read must clear auto-open without opening")

            // Pack extras retain their existing solo-reader law; mains restore the crossing chain.
            comics.setState("extra", "done", true)
            ck(page.readRelease(extra, "done"), "ready pack extra must open")
            ck(page.soloChapters && page.soloChapters.length === 1
               && page.soloChapters[0].id === "extra",
               "pack extra must remain a single-entry reader, never join the main chain")
            comics.setState("main", "done", true)
            ck(page.readRelease(main, "done"), "ready pack main must open")
            ck(page.soloChapters === null, "pack main must restore the normal crossing chain")

            report()
        } catch (e) {
            console.log("COMIC_CONSUMPTION_INTENT_FAIL: setup: " + e.message)
            Qt.exit(1)
        }
    }
    Timer {
        interval: 120
        running: true
        repeat: false
        onTriggered: Qt.callLater(harness.runChecks)
    }
    Timer {
        interval: 8000
        running: true
        repeat: false
        onTriggered: {
            console.log("COMIC_CONSUMPTION_INTENT_FAIL: timeout")
            Qt.exit(1)
        }
    }
}
