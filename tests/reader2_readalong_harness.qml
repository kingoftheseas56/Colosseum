// reader2_readalong_harness — headless proof of the Reader2 audiobook read-along
// WIRING (Task 6): the pure decision logic in Reader2Logic.js, the ReaderChrome scrub
// preview/commit split + Return-to-narration + mode controls, the LeftPanel Text Sync
// controls, and the end-to-end dispatch against FAKE ReadAlong/paper/audioSession objects
// (playback → controller, every seek → one commit, double-click → commitLocation, manual
// nav → detachFollow, return → following, and the DORMANT gate: with read-along absent,
// no controller calls happen and only the direct seek fires). Run:
//   qml.exe -platform offscreen tests/reader2_readalong_harness.qml
// Verdict via console + Qt.exit(0/1). Body wrapped in try/catch (a thrown error HANGS
// offscreen instead of failing) and the exit CODE is the verdict.
//
// WHY fakes + a mirror dispatch: ReaderShell.qml can't instantiate offscreen (it needs
// the WebEngine Paper, Reader2Bridge, and a dozen context singletons). So every read-along
// DECISION lives in Reader2Logic.js pure functions — proven here directly — and this
// harness wires those SAME functions to fake objects exactly as ReaderShell does, so the
// call PATTERN (preview-no-seek / one-commit / dormant-gate) is proven at the contract
// level. The bridge-free ReaderChrome + LeftPanel are instantiated for real.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "../qml/reader2" as R
import "../qml/reader2/Reader2Logic.js" as L

Item {
    id: root
    width: 1280
    height: 720

    // ---- FAKE controller (native ReadAlongController stand-in) — records every call ----
    QtObject {
        id: fakeReadAlong
        property string followState: "following"
        property var preview: ({})
        property int setPlayheadCount: 0
        property int previewCount: 0
        property int commitTimeCount: 0
        property int commitLocationCount: 0
        property int detachCount: 0
        property int returnCount: 0
        property var lastSetPlayhead: null
        property var lastPreview: null
        property var lastCommitTime: null
        property var lastCommitLocation: null
        function setPlayhead(pid, ch, ms) { lastSetPlayhead = { pid: pid, ch: ch, ms: ms }; setPlayheadCount++ }
        function previewTime(pid, ch, ms) { lastPreview = { pid: pid, ch: ch, ms: ms }; previewCount++ }
        function commitTime(pid, ch, ms) { lastCommitTime = { pid: pid, ch: ch, ms: ms }; commitTimeCount++ }
        function commitLocation(pid, loc) { lastCommitLocation = { pid: pid, loc: loc }; commitLocationCount++ }
        function detachFollow() { detachCount++; followState = "detached" }
        function returnToNarration() { returnCount++; followState = "following" }
        function reset() {
            setPlayheadCount = 0; previewCount = 0; commitTimeCount = 0; commitLocationCount = 0
            detachCount = 0; returnCount = 0; followState = "following"
            lastSetPlayhead = null; lastPreview = null; lastCommitTime = null; lastCommitLocation = null
        }
    }

    // ---- FAKE paper — records paint/navigation commands ----
    QtObject {
        id: fakePaper
        property int paintCount: 0
        property int clearCount: 0
        property int styleCount: 0
        property int ensureVisibleCount: 0
        property int navigateCount: 0
        property var lastCue: null
        property var lastStyle: null
        property var lastLocation: null
        function paintReadAlong(cue) { lastCue = cue; paintCount++ }
        function clearReadAlong() { clearCount++ }
        function setReadAlongStyle(s) { lastStyle = s; styleCount++ }
        function ensureReadAlongVisible(loc) { lastLocation = loc; ensureVisibleCount++ }
        function navigateReadAlong(loc) { lastLocation = loc; navigateCount++ }
    }

    // ---- FAKE audiobook session — records seeks ----
    QtObject {
        id: fakeAudio
        property int currentIndex: 0
        property real position: 0        // seconds within the current stream
        property real duration: 600      // seconds
        property int seekCount: 0
        property real lastSeek: -1
        function seekTo(t) { lastSeek = t; seekCount++ }
    }

    // ---- mirror of ReaderShell state (kept in lock-step with ReaderShell.qml) ----
    property bool m_readAlongAvailable: true
    property bool m_followOn: true
    property string m_followState: "following"
    property var m_lastPlayhead: null
    property bool m_pendingJump: false
    property var m_boundsMs: null
    property string bookId: "book-key-1"

    // ReaderShell.feedPlayhead — playback → controller (mirror).
    function m_feedPlayhead() {
        if (!root.m_readAlongAvailable || !root.m_followOn) return
        if (root.m_followState !== "following") return
        var idx = fakeAudio.currentIndex
        var absMs = L.sessionToAbsMs(idx, fakeAudio.position, root.m_boundsMs)
        var next = { chapter: idx, absMs: absMs }
        if (!L.shouldEmitSetPlayhead(root.m_lastPlayhead, next)) return
        root.m_lastPlayhead = next
        fakeReadAlong.setPlayhead(root.bookId, idx, absMs)
    }
    // ReaderShell.dispatchScrub — the gold scrub rail preview/commit (mirror).
    function m_dispatchScrub(phase, f) {
        var act = L.readAlongScrubAction(phase, f, fakeAudio.duration, root.m_readAlongAvailable)
        if (act.kind === "preview") fakeReadAlong.previewTime(root.bookId, fakeAudio.currentIndex, act.timeMs)
        else if (act.kind === "commit") { fakeReadAlong.commitTime(root.bookId, fakeAudio.currentIndex, act.timeMs); root.m_pendingJump = true }
        else fakeAudio.seekTo(fakeAudio.duration * Math.max(0, Math.min(1, f)))   // dormant direct seek
    }
    // ReaderShell controller-signal handlers (mirror).
    function m_onNavigationRequested(location) {
        if (!root.m_readAlongAvailable) return
        if (root.m_pendingJump) { root.m_pendingJump = false; fakePaper.navigateReadAlong(location) }
        else fakePaper.ensureReadAlongVisible(location)
    }

    // ---- ReaderChrome instantiated for real (bridge-free) — read-along ON ----
    property real chromePreviewFrac: -1
    property real chromeCommitFrac: -1
    property int chromeReturnCount: 0
    property string chromeModePick: ""
    property real chromeScale: -1
    R.ReaderChrome {
        id: chrome
        anchors.fill: parent
        title: "Moby-Dick"
        author: "Herman Melville"
        audioAttached: true
        readAlongAvailable: true
        readAlongMode: "sentenceWord"
        readAlongWordScale: 1.0
        readAlongPreviewActive: true
        readAlongPreviewLabel: "12:34 · Ch 3"
        readAlongFollowDetached: true
        onAudioScrubPreviewed: (f) => root.chromePreviewFrac = f
        onAudioScrubCommitted: (f) => root.chromeCommitFrac = f
        onReturnToNarrationRequested: root.chromeReturnCount++
        onReadAlongModePicked: (m) => root.chromeModePick = m
        onReadAlongScaleChanged: (s) => root.chromeScale = s
    }

    // ---- a DORMANT ReaderChrome (read-along absent) — must render identically to today ----
    R.ReaderChrome {
        id: dormantChrome
        anchors.fill: parent
        title: "Moby-Dick"
        author: "Herman Melville"
        audioAttached: true
        // readAlongAvailable stays at its default (false) — dormant
    }

    // ---- LeftPanel instantiated for real — read-along ON ----
    property string panelModePick: ""
    property real panelScale: -1
    R.LeftPanel {
        id: panel
        width: 360
        height: 640
        open: true
        activeTab: "audio"
        audioAttached: true
        readAlongAvailable: true
        readAlongMode: "sentenceWord"
        readAlongWordScale: 1.0
        onReadAlongModePicked: (m) => root.panelModePick = m
        onReadAlongScaleChanged: (s) => root.panelScale = s
    }
    // ---- a DORMANT LeftPanel (read-along absent) ----
    R.LeftPanel {
        id: dormantPanel
        width: 360
        height: 640
        open: true
        activeTab: "audio"
        audioAttached: true
        // readAlongAvailable default false — dormant
    }

    Component.onCompleted: {
        var fails = 0
        function check(ok, what) { if (!ok) { console.log("FAIL " + what); fails++ } else console.log("ok   " + what) }
        try {
            // ================= 1. PURE DECISION LOGIC (Reader2Logic.js Task 6) =================

            // --- readAlongDefaults / readAlongFrom: Sentence + Word is the ratified default ---
            var rad = L.readAlongDefaults()
            check(rad.mode === "sentenceWord" && rad.wordScale === 1.0, "readAlongDefaults: sentenceWord + scale 1.0")
            check(L.readAlongFrom(L.appearanceDefaults()).mode === "sentenceWord", "readAlongFrom: default appearance -> sentenceWord")
            check(L.readAlongFrom({ readAlong: { mode: "word", wordScale: 1.5 } }).mode === "word", "readAlongFrom: stored mode read back")
            check(L.readAlongFrom({ readAlong: { mode: "word", wordScale: 1.5 } }).wordScale === 1.5, "readAlongFrom: stored scale read back")
            check(L.readAlongFrom({ readAlong: { mode: "junk" } }).mode === "sentenceWord", "readAlongFrom: junk mode -> default")
            check(L.readAlongFrom({ readAlong: { wordScale: 99 } }).wordScale === 2.0, "readAlongFrom: out-of-range scale clamps to ceiling")
            check(L.readAlongFrom(null).mode === "sentenceWord", "readAlongFrom: null-safe")

            // --- readAlongStyleFromMode: mode -> the paper style payload ---
            var sSent = L.readAlongStyleFromMode("sentence", 1.0)
            check(sSent.sentence === true && sSent.word === false, "style: Sentence -> sentence only")
            var sWord = L.readAlongStyleFromMode("word", 1.0)
            check(sWord.sentence === false && sWord.word === true, "style: Word -> word only")
            var sBoth = L.readAlongStyleFromMode("sentenceWord", 1.4)
            check(sBoth.sentence === true && sBoth.word === true, "style: Sentence + Word -> both")
            check(sBoth.wordScale === 1.4, "style: carries the word enlargement scale")
            check(L.readAlongStyleFromMode("nonsense", 5).mode === "sentenceWord", "style: unknown mode -> default sentenceWord")
            check(L.readAlongStyleFromMode("word", 5).wordScale === 2.0, "style: scale clamped to ceiling")
            check(L.readAlongStyleFromMode("word", 0.1).wordScale === 1.0, "style: scale clamped to floor")

            // --- mergeReadAlong: patch one field, keep the rest + the whole appearance ---
            var ap0 = L.appearanceDefaults()
            var apM = L.mergeReadAlong(ap0, { mode: "word" })
            check(apM.readAlong.mode === "word" && apM.readAlong.wordScale === 1.0, "mergeReadAlong: mode patched, scale kept")
            check(apM.theme === "night" && apM.font === "literata", "mergeReadAlong: appearance fields survive")
            check(ap0.readAlong.mode === "sentenceWord", "mergeReadAlong: original not mutated (pure)")
            var apS = L.mergeReadAlong(apM, { wordScale: 1.75 })
            check(apS.readAlong.mode === "word" && apS.readAlong.wordScale === 1.75, "mergeReadAlong: second patch keeps prior mode")
            check(L.mergeReadAlong(ap0, { wordScale: 99 }).readAlong.wordScale === 2.0, "mergeReadAlong: patched scale clamped")

            // --- scrubFractionToTimeMs: fraction over a duration -> ms (clamped) ---
            check(L.scrubFractionToTimeMs(0.5, 600) === 300000, "scrubFractionToTimeMs: 0.5 of 600s -> 300000ms")
            check(L.scrubFractionToTimeMs(0, 600) === 0, "scrubFractionToTimeMs: 0 -> 0")
            check(L.scrubFractionToTimeMs(1, 600) === 600000, "scrubFractionToTimeMs: 1 -> full")
            check(L.scrubFractionToTimeMs(1.5, 600) === 600000, "scrubFractionToTimeMs: over-range clamps to 1")
            check(L.scrubFractionToTimeMs(-1, 600) === 0, "scrubFractionToTimeMs: under-range clamps to 0")
            check(L.scrubFractionToTimeMs(0.5, 0) === 0, "scrubFractionToTimeMs: zero duration -> 0 (safe)")

            // --- sessionToAbsMs / audioSeekTargetSec: (index, positionSec) <-> absolute ms ---
            check(L.sessionToAbsMs(0, 12.5, null) === 12500, "sessionToAbsMs: m4b (no bounds) -> position*1000")
            check(L.sessionToAbsMs(3, 10, null) === 10000, "sessionToAbsMs: no bounds -> position only (nominal offset 0)")
            check(L.sessionToAbsMs(2, 5, [0, 1000, 2000, 3000]) === 7000, "sessionToAbsMs: real bounds add the chapter base")
            check(L.audioSeekTargetSec(0, 12500, null) === 12.5, "audioSeekTargetSec: absolute ms -> seconds")
            check(L.audioSeekTargetSec(2, 7000, [0, 1000, 2000, 3000]) === 5, "audioSeekTargetSec: subtracts the chapter base")
            check(L.audioSeekTargetSec(0, -50, null) === 0, "audioSeekTargetSec: never negative")

            // --- shouldEmitSetPlayhead: only when the playhead identity moved ---
            check(L.shouldEmitSetPlayhead(null, { chapter: 0, absMs: 0 }) === true, "shouldEmitSetPlayhead: first playhead always emits")
            check(L.shouldEmitSetPlayhead({ chapter: 0, absMs: 1000 }, { chapter: 0, absMs: 1000 }) === false, "shouldEmitSetPlayhead: unchanged -> no emit")
            check(L.shouldEmitSetPlayhead({ chapter: 0, absMs: 1000 }, { chapter: 0, absMs: 1250 }) === true, "shouldEmitSetPlayhead: time moved -> emit")
            check(L.shouldEmitSetPlayhead({ chapter: 0, absMs: 1000 }, { chapter: 1, absMs: 1000 }) === true, "shouldEmitSetPlayhead: chapter changed -> emit")

            // --- previewLabelFrom: timestamp + chapter + sync/locator flags ---
            var pl = L.previewLabelFrom({ timeMs: 754000, chapter: 2, synced: true, spineHref: "Text/ch3.xhtml" })
            check(pl.time === "12:34", "previewLabelFrom: formats the timestamp")
            check(pl.chapter === "Ch 3", "previewLabelFrom: 0-based chapter -> 1-based label")
            check(pl.line.indexOf("12:34") >= 0 && pl.line.indexOf("Ch 3") >= 0, "previewLabelFrom: line carries time + chapter")
            check(pl.synced === true && pl.located === true, "previewLabelFrom: synced + located flags")
            var pl2 = L.previewLabelFrom({ timeMs: 0, chapter: 0, synced: false })
            check(pl2.synced === false && pl2.located === false, "previewLabelFrom: unsynced + no location")
            check(L.previewLabelFrom(null).line === "", "previewLabelFrom: null-safe -> empty line")

            // --- readAlongScrubAction: available -> preview/commit; dormant -> direct seek ---
            var aPrev = L.readAlongScrubAction("preview", 0.5, 600, true)
            check(aPrev.kind === "preview" && aPrev.timeMs === 300000, "scrubAction: available preview -> preview + timeMs (NO seek)")
            var aCommit = L.readAlongScrubAction("commit", 0.5, 600, true)
            check(aCommit.kind === "commit" && aCommit.timeMs === 300000, "scrubAction: available release -> commit + timeMs")
            var aSeek = L.readAlongScrubAction("preview", 0.5, 600, false)
            check(aSeek.kind === "seek", "scrubAction: DORMANT -> direct seek (never a controller call)")
            check(L.readAlongScrubAction("commit", 0.5, 600, false).kind === "seek", "scrubAction: dormant release -> seek too")

            // --- navModeFor: a pending committed jump navigates; otherwise comfort-zone ---
            check(L.navModeFor(true) === "navigate", "navModeFor: pending committed -> navigate")
            check(L.navModeFor(false) === "ensureVisible", "navModeFor: passive follow -> ensureVisible (comfort zone)")

            // ================= 2. ReaderChrome scrub split + affordances (real instance) =================
            check(chrome !== null, "ReaderChrome instantiates with read-along inputs")
            check(chrome.readAlongAvailable === true, "ReaderChrome readAlongAvailable binds")
            // the split signals fire and carry the fraction (the rail's MouseArea calls these).
            chrome.audioScrubPreviewed(0.42); check(Math.abs(root.chromePreviewFrac - 0.42) < 1e-9, "chrome: audioScrubPreviewed carries the fraction (drag = preview)")
            chrome.audioScrubCommitted(0.66); check(Math.abs(root.chromeCommitFrac - 0.66) < 1e-9, "chrome: audioScrubCommitted carries the fraction (release = commit)")
            chrome.returnToNarrationRequested(); check(root.chromeReturnCount === 1, "chrome: returnToNarrationRequested fires")
            chrome.readAlongModePicked("word"); check(root.chromeModePick === "word", "chrome: re-emits readAlongModePicked from the panel")
            chrome.readAlongScaleChanged(1.5); check(Math.abs(root.chromeScale - 1.5) < 1e-9, "chrome: re-emits readAlongScaleChanged")
            // dormant chrome: instantiates, read-along OFF, new affordances hidden.
            check(dormantChrome !== null && dormantChrome.readAlongAvailable === false, "DORMANT ReaderChrome: read-along off by default (today's behavior)")

            // ================= 3. LeftPanel Text Sync controls (real instance) =================
            check(panel !== null && panel.readAlongAvailable === true, "LeftPanel instantiates with read-along on")
            panel.readAlongModePicked("sentence"); check(root.panelModePick === "sentence", "LeftPanel: readAlongModePicked fires")
            panel.readAlongScaleChanged(1.25); check(Math.abs(root.panelScale - 1.25) < 1e-9, "LeftPanel: readAlongScaleChanged fires")
            check(dormantPanel !== null && dormantPanel.readAlongAvailable === false, "DORMANT LeftPanel: read-along off by default")

            // ================= 4. END-TO-END DISPATCH against the fakes (mirror of ReaderShell) =================

            // (a) playback -> controller: setPlayhead on a moving playhead, no-op when still.
            fakeReadAlong.reset(); root.m_lastPlayhead = null; root.m_readAlongAvailable = true; root.m_followOn = true; root.m_followState = "following"
            fakeAudio.currentIndex = 0; fakeAudio.position = 10
            root.m_feedPlayhead()
            check(fakeReadAlong.setPlayheadCount === 1 && fakeReadAlong.lastSetPlayhead.ms === 10000, "feed: first tick -> setPlayhead(absMs)")
            root.m_feedPlayhead()   // identical position
            check(fakeReadAlong.setPlayheadCount === 1, "feed: unchanged playhead -> NO second setPlayhead")
            fakeAudio.position = 12
            root.m_feedPlayhead()
            check(fakeReadAlong.setPlayheadCount === 2, "feed: advanced position -> setPlayhead again")
            // detached (manual nav): playback no longer drives the page.
            root.m_followState = "detached"
            fakeAudio.position = 20
            root.m_feedPlayhead()
            check(fakeReadAlong.setPlayheadCount === 2, "feed: detached -> playback does NOT drive the controller")
            root.m_followState = "following"

            // (b) scrub PREVIEW updates without any seek; RELEASE commits exactly once.
            fakeReadAlong.reset(); fakeAudio.seekCount = 0
            root.m_dispatchScrub("preview", 0.25)
            root.m_dispatchScrub("preview", 0.35)
            check(fakeReadAlong.previewCount === 2 && fakeReadAlong.commitTimeCount === 0, "scrub: hover/drag -> previewTime (twice)")
            check(fakeAudio.seekCount === 0, "scrub: preview performs NO seek")
            check(fakeReadAlong.lastPreview.ms === L.scrubFractionToTimeMs(0.35, 600), "scrub: preview timeMs tracks the fraction")
            root.m_dispatchScrub("commit", 0.35)
            check(fakeReadAlong.commitTimeCount === 1, "scrub: RELEASE -> exactly ONE commitTime")
            check(fakeAudio.seekCount === 0, "scrub: the commit routes through the controller, not a direct seek")

            // (c) alignedDoubleClick -> commitLocation with the canonical location.
            fakeReadAlong.reset()
            var dblLoc = { spineHref: "Text/ch3.xhtml", canonicalStart: 40, canonicalEnd: 47 }
            // ReaderShell: pendingJump=true then commitLocation(bookId, location)
            root.m_pendingJump = true; fakeReadAlong.commitLocation(root.bookId, dblLoc)
            check(fakeReadAlong.commitLocationCount === 1 && fakeReadAlong.lastCommitLocation.loc.canonicalStart === 40, "double-click: commitLocation(location)")

            // (d) manualNavigation -> detachFollow (audio keeps playing, follow detaches).
            fakeReadAlong.reset()
            fakeReadAlong.detachFollow()
            check(fakeReadAlong.detachCount === 1 && fakeReadAlong.followState === "detached", "manualNavigation: detachFollow, state -> detached")
            // (e) returnToNarration -> following.
            fakeReadAlong.returnToNarration()
            check(fakeReadAlong.returnCount === 1 && fakeReadAlong.followState === "following", "return: returnToNarration -> following")

            // (f) navigationRequested routing: a committed jump navigates once, then comfort-zone.
            fakePaper.navigateCount = 0; fakePaper.ensureVisibleCount = 0
            root.m_pendingJump = true
            root.m_onNavigationRequested({ spineHref: "Text/ch3.xhtml", canonicalStart: 40, canonicalEnd: 47 })
            check(fakePaper.navigateCount === 1 && fakePaper.ensureVisibleCount === 0, "nav: committed jump -> navigateReadAlong once")
            root.m_onNavigationRequested({ spineHref: "Text/ch3.xhtml", canonicalStart: 60, canonicalEnd: 68 })
            check(fakePaper.ensureVisibleCount === 1 && fakePaper.navigateCount === 1, "nav: passive follow -> ensureReadAlongVisible (comfort zone)")

            // (g) THE DORMANT GATE: read-along absent -> ZERO controller calls, only a direct seek.
            fakeReadAlong.reset(); fakeAudio.seekCount = 0
            root.m_readAlongAvailable = false
            root.m_dispatchScrub("preview", 0.5)
            root.m_dispatchScrub("commit", 0.5)
            root.m_feedPlayhead()
            check(fakeReadAlong.previewCount === 0 && fakeReadAlong.commitTimeCount === 0 && fakeReadAlong.setPlayheadCount === 0,
                  "DORMANT: no previewTime / commitTime / setPlayhead calls happen")
            check(fakeAudio.seekCount >= 1, "DORMANT: the scrub falls back to a direct audio seek (today's behavior)")
            root.m_readAlongAvailable = true

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS READALONG_OK")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
