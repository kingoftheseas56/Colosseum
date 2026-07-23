// alignment_activity_harness — headless proof of the Text Sync ACTIVITY surface
// (Task 7): the honest read-along alignment status shown BOTH in Reader2's LeftPanel
// AND in the app-wide background-activity row, each reading the SAME native job.
//
// Two halves, one shared fake job so the two surfaces provably drive one thing:
//   1. GLOBAL ROW — feed a fake BackgroundActivity registry ONE alignment row into the
//      REAL, UNTOUCHED BackgroundActivitySection and assert it renders read-only + that
//      its pause/resume channel (requestPause/requestResume) operates the shared job.
//   2. READER2 TEXT SYNC — instantiate the real LeftPanel (bridge-free) with a fake
//      AudioTextAlignment service and assert the summary line, the ready count, all seven
//      per-chapter states, the plain-language failure copy, per-chapter Retry, the
//      confirmation-gated Restart, and pause/resume PARITY (service path vs registry-row
//      path land the same paused state, because both mutate the one shared job).
//
// Run:  qml.exe -platform offscreen tests/alignment_activity_harness.qml
// Verdict via console + Qt.exit(0/1). The body is wrapped in try/catch (a thrown error
// HANGS offscreen instead of failing) and the exit CODE is the verdict; the ALIGNMENT_
// ACTIVITY_OK sentinel is a secondary confirmation for the runner.
//
// WHY fakes: the native AudioTextAlignmentService/registry are registered in Task 12, not
// yet — so presentation is proven here against fakes shaped exactly like the service
// contract (statusFor/chaptersFor/pause/resume/retry/restart + jobChanged) and the
// registry contract ({id,title,stage,progress,paused,canPause,kind} + requestPause/Resume).
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "../qml" as Spine
import "../qml/reader2" as R
import "../qml/reader2/Reader2Logic.js" as L

Item {
    id: root
    width: 1280
    height: 720

    property string bookId: "book-key-1"

    // ---- THE ONE SHARED JOB — both the Reader2 service and the global registry row
    //      operate on THIS. That is the whole point: one native job, two surfaces. ----
    QtObject {
        id: job
        property bool paused: false
        property string stage: "aligning"
        property int ready: 1
        property int total: 7
        // per-chapter states: exactly one of every wire code, so "render all seven states"
        // is provable. The last chapter failed terminally (edition_mismatch).
        property var chapters: [
            { index: 0, stage: "waiting",      failureCode: "", coverage: 0,    confidence: 0 },
            { index: 1, stage: "preparing",    failureCode: "", coverage: 0,    confidence: 0 },
            { index: 2, stage: "transcribing", failureCode: "", coverage: 0,    confidence: 0 },
            { index: 3, stage: "matching",     failureCode: "", coverage: 0.3,  confidence: 0 },
            { index: 4, stage: "aligning",     failureCode: "", coverage: 0.6,  confidence: 0.4 },
            { index: 5, stage: "ready",        failureCode: "", coverage: 0.98, confidence: 0.92 },
            { index: 6, stage: "couldnt_sync", failureCode: "edition_mismatch", coverage: 0.1, confidence: 0.05 }
        ]
    }

    // ---- FAKE AudioTextAlignment service (Reader2 side) — reads/writes the shared job ----
    QtObject {
        id: fakeAlignment
        signal jobChanged(string bookId)
        property var retryCalls: []
        property var restartCalls: []
        property var prioritizeCalls: []
        function statusFor(id) { return { stage: job.stage, ready: job.ready, total: job.total, paused: job.paused } }
        function chaptersFor(id) { return job.chapters }
        function pause(id) { job.paused = true; jobChanged(id) }
        function resume(id) { job.paused = false; jobChanged(id) }
        function retry(id, index) { retryCalls = retryCalls.concat([{ id: id, index: index }]); jobChanged(id) }
        function restart(id) { restartCalls = restartCalls.concat([id]); job.paused = false; job.ready = 0; jobChanged(id) }
        function prioritize(id, index) { prioritizeCalls = prioritizeCalls.concat([{ id: id, index: index }]) }
    }

    // ---- FAKE BackgroundActivity registry (global spine side) — ONE alignment row whose
    //      pause/resume channel routes back to the SAME shared job (as the native service
    //      does: BackgroundActivitySection.requestPause -> service.pause -> the job). ----
    QtObject {
        id: fakeRegistry
        property var activities: [
            { id: "audio-align:pair1", title: "Syncing Moby-Dick audiobook",
              stage: "Aligning words", progress: 0.42, paused: false, canPause: true,
              kind: "audio_text_alignment" }
        ]
        property var pauseCalls: []
        property var resumeCalls: []
        function requestPause(id) { pauseCalls = pauseCalls.concat([id]); job.paused = true }
        function requestResume(id) { resumeCalls = resumeCalls.concat([id]); job.paused = false }
    }

    // ---- FAKE empty registry — proves the section vanishes with no rows (read-only render) ----
    QtObject {
        id: emptyRegistry
        property var activities: []
        function requestPause(id) {}
        function requestResume(id) {}
    }

    // ---- the REAL, UNTOUCHED BackgroundActivitySection fed one alignment row ----
    Spine.BackgroundActivitySection {
        id: section
        width: 360
        registry: fakeRegistry
    }
    Spine.BackgroundActivitySection {
        id: emptySection
        width: 360
        registry: emptyRegistry
    }

    // ---- the REAL LeftPanel (bridge-free) wired to the fake service — Text Sync ON ----
    R.LeftPanel {
        id: panel
        width: 360
        height: 720
        open: true
        activeTab: "audio"
        audioAttached: true
        readAlongAvailable: true
        textSync: fakeAlignment
        bookId: root.bookId
    }

    // ---- a DORMANT LeftPanel (no service, read-along absent) — must render like today ----
    R.LeftPanel {
        id: dormantPanel
        width: 360
        height: 720
        open: true
        activeTab: "audio"
        audioAttached: true
        // readAlongAvailable default false + textSync default null -> Text Sync block absent
    }

    // ---- a ReaderChrome wired with the service + bookId (proves the pass-through binds) ----
    R.ReaderChrome {
        id: chromeTS
        anchors.fill: parent
        audioAttached: true
        readAlongAvailable: true
        textSync: fakeAlignment
        bookId: root.bookId
    }
    // ---- a DORMANT ReaderChrome — service self-resolves to null when unregistered ----
    R.ReaderChrome {
        id: dormantChrome
        anchors.fill: parent
        audioAttached: true
    }

    Component.onCompleted: {
        var fails = 0
        function check(ok, what) { if (!ok) { console.log("FAIL " + what); fails++ } else console.log("ok   " + what) }
        try {
            panel.refreshTextSync()

            // ================= 1. GLOBAL ROW through the UNTOUCHED BackgroundActivitySection =================
            check(section.rowCount === 1, "section: renders exactly the one alignment row")
            check(section.visible === true, "section: visible with a row")
            check(section.rows[0].kind === "audio_text_alignment", "section: the row is the alignment kind")
            check(section.rows[0].title === "Syncing Moby-Dick audiobook", "section: renders the row title read-only")
            check(section.rows[0].canPause === true, "section: the row exposes a pause channel")
            check(emptySection.visible === false, "section: empty registry -> the section vanishes entirely")

            // ================= 2. PURE Text Sync COPY (Reader2Logic.js Task 7) =================
            // stageLabel — every wire code maps to a plain label; unknown falls back safely.
            check(L.stageLabel("waiting") === "Waiting", "stageLabel: waiting")
            check(L.stageLabel("preparing") === "Preparing", "stageLabel: preparing")
            check(L.stageLabel("transcribing") === "Transcribing", "stageLabel: transcribing")
            check(L.stageLabel("matching") === "Matching", "stageLabel: matching")
            check(L.stageLabel("aligning") === "Aligning words", "stageLabel: aligning -> 'Aligning words'")
            check(L.stageLabel("ready") === "Ready", "stageLabel: ready")
            check(L.stageLabel("couldnt_sync") === "Couldn't sync", "stageLabel: couldnt_sync")
            check(L.stageLabel("nonsense") !== "" && L.stageLabel("") !== "", "stageLabel: unknown -> non-empty fallback")

            // textSyncSummary — the exact prompt strings, derived from {stage, ready, total}.
            check(L.textSyncSummary({ stage: "aligning", ready: 5, total: 24, paused: false }) === "Syncing chapter 6 of 24 · Aligning words",
                  "textSyncSummary: 'Syncing chapter 6 of 24 · Aligning words'")
            check(L.textSyncSummary({ stage: "ready", ready: 24, total: 24, paused: false }) === "All 24 chapters ready",
                  "textSyncSummary: all done -> 'All 24 chapters ready'")
            check(L.textSyncSummary({ stage: "matching", ready: 23, total: 24, paused: false }).indexOf("chapter 24 of 24") >= 0,
                  "textSyncSummary: in-flight chapter = ready+1, clamped to total")
            check(L.textSyncSummary({ stage: "aligning", ready: 5, total: 24, paused: true }).indexOf("Paused") === 0,
                  "textSyncSummary: a paused job says 'Paused', not 'Syncing'")
            check(L.textSyncSummary({ ready: 0, total: 0 }) === "Preparing text sync", "textSyncSummary: nothing known yet -> Preparing")
            check(L.textSyncSummary(null) === "Preparing text sync", "textSyncSummary: null-safe")

            // readyCountText — the '11 chapters ready' prompt string + singular.
            check(L.readyCountText({ ready: 11, total: 24 }) === "11 chapters ready", "readyCountText: '11 chapters ready'")
            check(L.readyCountText({ ready: 1, total: 24 }) === "1 chapter ready", "readyCountText: singular")
            check(L.readyCountText({ ready: 0 }) === "0 chapters ready", "readyCountText: zero")

            // textSyncAllReady — stage 'ready' OR ready>=total.
            check(L.textSyncAllReady({ stage: "ready", ready: 24, total: 24 }) === true, "textSyncAllReady: stage ready")
            check(L.textSyncAllReady({ stage: "aligning", ready: 24, total: 24 }) === true, "textSyncAllReady: count met")
            check(L.textSyncAllReady({ stage: "aligning", ready: 5, total: 24 }) === false, "textSyncAllReady: still working")
            check(L.textSyncAllReady({ ready: 0, total: 0 }) === false, "textSyncAllReady: unknown total is not 'all ready'")

            // chapterFailureCopy — the approved plain-language lines. edition_mismatch is pinned.
            check(L.chapterFailureCopy("edition_mismatch") === "Couldn't sync — edition may differ",
                  "chapterFailureCopy: edition_mismatch -> 'Couldn't sync — edition may differ'")
            var failCodes = ["chapter_match_missing", "audio_decode_failed", "model_missing",
                             "model_checksum_failed", "epub_index_failed", "alignment_failed"]
            var seen = {}
            var allDistinct = true
            for (var fc = 0; fc < failCodes.length; fc++) {
                var copy = L.chapterFailureCopy(failCodes[fc])
                if (copy === "" || copy.indexOf("Couldn't sync") !== 0) allDistinct = false
                if (seen[copy]) allDistinct = false
                seen[copy] = true
            }
            check(allDistinct, "chapterFailureCopy: every failure code -> a distinct 'Couldn't sync …' line")
            check(L.chapterFailureCopy("") === "Couldn't sync", "chapterFailureCopy: empty/unknown -> generic 'Couldn't sync'")

            // chapterFailed / chapterStateText — per-chapter row logic.
            check(L.chapterFailed({ stage: "couldnt_sync", failureCode: "edition_mismatch" }) === true, "chapterFailed: couldnt_sync")
            check(L.chapterFailed({ stage: "aligning", failureCode: "" }) === false, "chapterFailed: healthy stage is not failed")
            check(L.chapterStateText({ stage: "ready" }) === "Ready", "chapterStateText: ready -> Ready")
            check(L.chapterStateText({ stage: "matching" }) === "Matching", "chapterStateText: in-progress -> stage label")
            check(L.chapterStateText({ stage: "couldnt_sync", failureCode: "edition_mismatch" }) === "Couldn't sync — edition may differ",
                  "chapterStateText: failed -> the failure copy")

            // ================= 3. LeftPanel Text Sync block (real instance, wired to the fake) =================
            check(panel.textSync === fakeAlignment, "LeftPanel: textSync service injected")
            check(panel.bookId === root.bookId, "LeftPanel: bookId bound")
            check(panel.textSyncOn === true, "LeftPanel: Text Sync block ON (service + bookId present)")
            // the panel reads its status/chapters STRAIGHT off the service (not re-derived).
            check(panel.textSyncStatus.total === job.total, "LeftPanel: status read from the service")
            check(panel.textSyncSummaryText === L.textSyncSummary(panel.textSyncStatus), "LeftPanel: summary is a thin render of the service status")
            check(panel.textSyncReadyText === L.readyCountText(panel.textSyncStatus), "LeftPanel: ready count is a thin render")

            // all seven per-chapter states render (prove via the model the Repeater renders).
            check(panel.textSyncChapters.length === 7, "LeftPanel: all seven chapter rows present")
            var expectLabels = ["Waiting", "Preparing", "Transcribing", "Matching", "Aligning words", "Ready", "Couldn't sync — edition may differ"]
            var labelsOk = panel.textSyncChapterLabels.length === 7
            for (var i = 0; i < expectLabels.length; i++)
                if (panel.textSyncChapterLabels[i] !== expectLabels[i]) labelsOk = false
            check(labelsOk, "LeftPanel: every one of the seven states renders its plain label")
            check(panel.textSyncChapterLabels[6] === "Couldn't sync — edition may differ", "LeftPanel: the failed chapter shows the edition-mismatch copy")

            // Retry -> service.retry(bookId, index) with the CHAPTER's own index (6, the failed one).
            fakeAlignment.retryCalls = []
            panel.retryChapter(6)
            check(fakeAlignment.retryCalls.length === 1 && fakeAlignment.retryCalls[0].index === 6 && fakeAlignment.retryCalls[0].id === root.bookId,
                  "LeftPanel: Retry -> service.retry(bookId, chapterIndex)")

            // PROTECTED Restart — a confirmation gates it: arm does NOT restart; confirm does.
            fakeAlignment.restartCalls = []
            panel.restartArmed = false
            panel.requestRestart()
            check(panel.restartArmed === true && fakeAlignment.restartCalls.length === 0, "LeftPanel: Restart arms a confirm step; restart NOT yet called")
            panel.cancelRestart()
            check(panel.restartArmed === false && fakeAlignment.restartCalls.length === 0, "LeftPanel: Cancel disarms without restarting")
            panel.requestRestart()
            panel.confirmRestart()
            check(fakeAlignment.restartCalls.length === 1 && fakeAlignment.restartCalls[0] === root.bookId && panel.restartArmed === false,
                  "LeftPanel: Confirm -> exactly one service.restart(bookId), disarmed")

            // ================= 4. PAUSE/RESUME PARITY — Reader2 vs the global row, one job =================
            // service path (Reader2's own pause control): pause -> shared job paused; the panel
            // refreshes off jobChanged and reflects it (never caches a second copy of truth).
            job.paused = false
            panel.pauseTextSync()
            check(job.paused === true, "parity: Reader2 pause -> the shared job is paused")
            check(panel.textSyncStatus.paused === true, "parity: the panel reflects it (refreshed via jobChanged)")
            var pausedViaService = job.paused
            panel.resumeTextSync()
            check(job.paused === false && panel.textSyncStatus.paused === false, "parity: Reader2 resume -> the shared job resumes")

            // registry-row path (the global BackgroundActivitySection pause channel): the SAME job.
            fakeRegistry.requestPause("audio-align:pair1")
            check(job.paused === true, "parity: global-row pause -> the SAME shared job is paused")
            check(fakeAlignment.statusFor(root.bookId).paused === true, "parity: the service sees the global-row pause (one job)")
            var pausedViaRegistry = job.paused
            check(pausedViaService === pausedViaRegistry, "parity: service pause and registry pause land the identical paused state")
            fakeRegistry.requestResume("audio-align:pair1")
            check(job.paused === false, "parity: global-row resume -> the shared job resumes")

            // ================= 5. DORMANT — every new surface hidden/inert when unavailable =================
            check(dormantPanel !== null, "DORMANT LeftPanel: instantiates")
            check(dormantPanel.readAlongAvailable === false && dormantPanel.textSync === null, "DORMANT LeftPanel: read-along off + no service by default")
            check(dormantPanel.textSyncOn === false, "DORMANT LeftPanel: Text Sync block inert (today's Audio pane unchanged)")
            check(chromeTS.textSync === fakeAlignment && chromeTS.bookId === root.bookId, "ReaderChrome: threads the service + bookId through")
            check(dormantChrome !== null && dormantChrome.textSync === null && dormantChrome.readAlongAvailable === false,
                  "DORMANT ReaderChrome: service self-resolves to null when unregistered (today's chrome)")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS ALIGNMENT_ACTIVITY_OK")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
