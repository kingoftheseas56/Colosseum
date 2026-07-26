Cross-model review for Colosseum's Comic Reader (requested by Agent 1, Claude/Opus). You are a
DIFFERENT model than the author — your job is to check this diff against the written Definition of
Done below, not just to read the code.

CONTEXT YOU NEED (the diff will not tell you this):

* The reader is a from-scratch native comic/manga reader that replaced a WeebCentral-HTML
  `MangaReader.qml`. It has three modes: Manga and Comic (both the "double_page" surface, direction
  differs) and Strip ("long_strip", a vertical webtoon column in a ListView).
* Every fullscreen flip in the app — reader, video Player 2, topbar, F11 — routes through ONE shared
  component, `qml/FullscreenTransitionShield.qml`, via `Main.qml: toggleFullscreenShell()`. The
  shield fades a dark cover in over 60ms, emits applyRequested (the window mode flips behind the
  cover), then lifts the cover on the window's very NEXT `frameSwapped`, with a 250ms fallback timer.
* Hemanth (the user) reported twice: "going in and out of fullscreen looks incredibly rough", then
  after a first insufficient fix, "even after your fix, it remained shaky unlike the video player's
  fullscreen transition." He later specified the reference is PLAYER 2's transition specifically.
* House rules that bear on this diff: QML paints, C++ decides (no transport/threading in QML);
  a green test suite is explicitly NOT accepted as evidence that the reader feels right; guards are
  expected to be negative-controlled; PowerShell 5.1 mis-parses non-ASCII inside quoted strings in
  BOM-less .ps1 files.

DEFINITION OF DONE (verify the diff against EACH item):

1. FULLSCREEN — the reader's column reflow (the width-driven rescale plus the contentY rescale that
   preserves reading position) must COMPLETE BEFORE the shared shield lifts its cover, on BOTH
   entering and leaving fullscreen, so the first frame presented at the new size is already settled.
2. FULLSCREEN — the fix must not degrade normal scrolling: the 16ms viewport-report throttle must
   still coalesce SCROLL reports (which arrive at 60Hz while reading). Only the width path may
   become synchronous.
3. FULLSCREEN — the invariant must be gated by an ORDERING assertion (reflow before reveal), not by
   a duration/timeout that a fast machine could satisfy accidentally, and the gate must be able to
   fail (negative control).
4. E2 — keyboard scrolling (Space / PageUp / PageDown) must feed the SAME drain accumulator as the
   mouse wheel, so a key press decelerates identically and a press mid-glide ADDS to the backlog
   rather than jumping and then continuing to slide.
5. E2 — instant, FINAL repositions (scrub-bar seek, Home, End) must still be instant AND must drop
   any in-flight glide backlog, so no leftover motion carries across the jump.
6. E2 — `manualNavigation()` means "a real WHEEL gesture happened". A keyboard/API glide must NOT
   emit it; the wheel path must still emit it.
7. E2 — removing the caller-side clamp in `_stripScroll` must be safe: the drain itself must clamp
   to [0, contentHeight-height] and zero the backlog at an edge.
8. F5 — pressing forward on the last page must announce the end instead of doing nothing silently.
   With a next chapter it must name a binding that ACTUALLY EXISTS in the input map; with no next
   entry it must say only that. A mid-book page turn must stay silent.
9. F5 — in Strip mode the announcement must fire only when the column is genuinely parked at (or
   already gliding into) the bottom — including when a glide is still in flight.
10. F2 — strip page width and gap must persist PER SERIES. A series with no record of its own
    follows the global seed; once a series is dressed it keeps its own measure. Dressing series B
    must not alter series A's stored measure, and re-opening A must restore A's measure.
11. F2 — merely OPENING a series must not create a width opinion for it, and must not rewrite the
    global seed to the opened book's measure (this was the actual leak: persistence used to live in
    an onStripWidthPctChanged handler that also fires when the shell replays a series' memory).
12. F2 — changing the reading mode must not drop a previously stored width/gap (the series record
    write must merge, not replace).
13. G1 — one command must build every native comic-reader harness from the current tree, run all
    six, run all eight QML gates, and print COMICREADER_ACCEPTANCE_OK only if all pass; it must
    refuse to report success if fewer gates ran than expected.
14. ALL — no regression to existing behaviour asserted by the surfaces/shell/chrome gates, and no
    change to any file outside the comic reader's own lane.

DIFF UNDER REVIEW (git diff eec83f0..HEAD, restricted to the reader's files):

```diff
diff --git a/qml/comicreader/ComicReaderShell.qml b/qml/comicreader/ComicReaderShell.qml
index df908d6..728a9cd 100644
--- a/qml/comicreader/ComicReaderShell.qml
+++ b/qml/comicreader/ComicReaderShell.qml
@@ -335,11 +335,25 @@ Item {
         }
         return [idx0, idx0]
     }
+    // F5: the end of a volume ANNOUNCES itself instead of going quiet. Pressing forward at the last
+    // page used to do nothing at all, which is indistinguishable from a dropped input — you press
+    // again, harder, and wonder if the reader is stuck. If there is a next entry the toast says how
+    // to reach it, using the binding that actually exists (Alt+Right / the next pill), never an
+    // invented one.
+    function _endOfVolumeToast() {
+        hud.showToast(hasNext ? "End of volume — Alt+Right for the next" : "End of volume")
+    }
     function pageNext() {
         if (mode === "double_page") {
             var t = _unitBoundsForIndex(currentPage - 1)[1] + 1
-            if (t < max) currentPage = _unitBoundsForIndex(t)[0] + 1
-        } else _stripScroll(0.9)
+            if (t < max) { currentPage = _unitBoundsForIndex(t)[0] + 1; return }
+            _endOfVolumeToast()
+        } else {
+            // Strip only announces when the column is genuinely parked at (or gliding into) the
+            // bottom, so a normal page-down mid-book stays silent.
+            if (!stripSurface.atEnd) { _stripScroll(0.9); return }
+            _endOfVolumeToast()
+        }
     }
     function pagePrev() {
         if (mode === "double_page") {
@@ -352,16 +366,22 @@ Item {
         if (mode === "double_page") p = _unitBoundsForIndex(p - 1)[0] + 1
         currentPage = p
     }
+    // Keyboard scrolling GLIDES (E2). Space/PageDown feed the same drain the wheel feeds, so they
+    // decelerate identically and a press mid-glide adds to the backlog instead of fighting it. The
+    // surface clamps the landing itself; the old raw contentY write here is what produced
+    // jump-then-slide when a key landed while a wheel glide was still running.
     function _stripScroll(screens) {
         var span = stripSurface.contentHeight - stripSurface.height
         if (span <= 0) return
-        stripSurface.contentY = Math.max(0, Math.min(span, stripSurface.contentY + screens * stripSurface.height))
+        stripSurface.smoothScrollBy(screens * stripSurface.height)
     }
+    // A scrub seek is INSTANT and FINAL — it must land where the thumb was released and carry no
+    // leftover glide across the jump, so it takes the halt door rather than the drain.
     function scrubToFraction(frac) {
         var f = Math.max(0, Math.min(1, frac))
         stripFraction = f
         var span = stripSurface.contentHeight - stripSurface.height
-        if (span > 0) stripSurface.contentY = f * span
+        if (span > 0) stripSurface.haltScrollAt(f * span)
     }
     // What page a scrub fraction actually lands on. In Strip that is a GEOMETRY question — pages
     // have different heights, so a linear pages*fraction estimate lies about where you'd land — so
@@ -377,11 +397,12 @@ Item {
         }
         return Math.max(1, Math.round(f * (Math.max(1, max) - 1)) + 1)
     }
-    function firstPageNav() { currentPage = 1; if (mode === "long_strip") stripSurface.contentY = 0 }
+    // Home/End are instant and final, like a scrub seek — the halt door, not the drain.
+    function firstPageNav() { currentPage = 1; if (mode === "long_strip") stripSurface.haltScrollAt(0) }
     function lastPageNav() {
         goToPageIndex(max)
         if (mode === "long_strip")
-            stripSurface.contentY = Math.max(0, stripSurface.contentHeight - stripSurface.height)
+            stripSurface.haltScrollAt(Math.max(0, stripSurface.contentHeight - stripSurface.height))
     }
     // reading-mode changes write the PERSISTED seams (never mode/rtl directly) so a crossing's
     // load() honors the choice; the reactions below flip the visible mode/rtl live. setReadingMode
@@ -475,12 +496,21 @@ Item {
     // Route through the strip surface while it is the live one: rescaling the column moves every
     // page, so without anchoring a Page-width tap would silently scroll you somewhere else in the
     // book. Off the strip there is no viewport to hold, so the plain call is right.
-    function setStripLayout(widthPct, gap) {
+    // `persist` defaults TRUE: every caller that is a user acting on the settings sheet wants the
+    // choice remembered. _applySeriesPrefs passes false because it is replaying memory, not making
+    // it. The persistence lives HERE rather than in an onStripWidthPctChanged handler on purpose:
+    // that handler fires for any backend change including a per-series apply, so persisting there
+    // rewrote the global seed just for opening a book.
+    function setStripLayout(widthPct, gap, persist) {
         if (!core || !core.setStripLayout) return
         if (mode === "long_strip" && stripSurface && stripSurface.active)
             stripSurface.applyLayout(widthPct, gap)
         else
             core.setStripLayout(widthPct, gap)
+        if (persist === false || !_ready) return
+        globalPrefs.stripWidthPct = widthPct       // the seed for series with no opinion yet
+        globalPrefs.stripGap = gap
+        _saveSeriesPrefs({ sw: widthPct, sg: gap })
     }
     function setMemorySaver(on) { if (core && core.setMemorySaver) core.setMemorySaver(on === true) }
 
@@ -651,6 +681,19 @@ Item {
     // last-choice, then to "" so load()'s lane default decides.
     function _applySeriesPrefs() {
         var rec = ComicReaderState.storeGet(seriesRecords.all, seriesId)
+
+        // F2 — strip measure is PER SERIES, seeded by the global. A weekly gag strip and a
+        // double-page-spread tankobon want different column widths, and re-dressing every book to
+        // match the last one you touched is the behaviour this replaces. A series that has never
+        // been dressed follows the global seed, so a width set anywhere still reaches every book
+        // you have not given an opinion about.
+        var w = (rec && rec.sw !== undefined) ? rec.sw : globalPrefs.stripWidthPct
+        var g = (rec && rec.sg !== undefined) ? rec.sg : globalPrefs.stripGap
+        // Pushed WITHOUT persisting: this is applying memory, not forming it. Writing here would
+        // stamp this series' width onto the global seed merely because you opened the book, and the
+        // next undressed series would inherit it - the exact leak per-series is meant to end.
+        if (core && core.setStripLayout) setStripLayout(w, g, false)
+
         var rm = (rec && rec.rm) ? rec.rm : globalPrefs.readingMode
         // Nothing remembered -> leave persistedMode/Direction EXACTLY as they are. The store is a
         // source of memory, not an eraser: these are also the seams a caller (or the harness) can
@@ -668,9 +711,19 @@ Item {
         persistedState = blob
     }
     // ---- save ----
-    function _saveSeriesPrefs() {
+    // MERGES rather than replaces. It used to write `{ rm }` wholesale, which was fine when the
+    // record held only a reading mode; now that a series can also carry its own strip measure
+    // (sw/sg), a mode change must not silently drop the width you set. `extra` is how a caller adds
+    // fields — absent means "record only what the record already knew, plus the current mode", so
+    // merely changing the reading mode never invents a width opinion this series did not have (and
+    // therefore never stops it following the global seed).
+    function _saveSeriesPrefs(extra) {
         if (!seriesId.length) return
-        seriesRecords.all = ComicReaderState.storePut(seriesRecords.all, seriesId, { rm: readingMode })
+        var rec = ComicReaderState.storeGet(seriesRecords.all, seriesId) || ({})
+        rec.rm = readingMode
+        if (extra)
+            for (var k in extra) rec[k] = extra[k]
+        seriesRecords.all = ComicReaderState.storePut(seriesRecords.all, seriesId, rec)
     }
     function _saveEntryBlob() {
         if (!curChapterId.length || !core || !core.persistedState) return
@@ -728,8 +781,11 @@ Item {
     // ---- reactions: every settings write goes straight back to its store ----
     onNightVeilChanged:      if (_ready) globalPrefs.nightVeil = nightVeil
     onGutterStrengthChanged: if (_ready) globalPrefs.gutterStrength = gutterStrength
-    onStripWidthPctChanged:  if (_ready) globalPrefs.stripWidthPct = stripWidthPct
-    onStripGapChanged:       if (_ready) globalPrefs.stripGap = stripGap
+    // NOTE: stripWidthPct/stripGap are deliberately ABSENT here. They are readonly readbacks of the
+    // backend, so they also change when _applySeriesPrefs replays a series' remembered measure —
+    // persisting from that signal wrote the opened book's width onto the GLOBAL seed, which is how
+    // one series' taste used to re-dress every other. Their persistence lives in setStripLayout(),
+    // the one door a user actually turns. (F2, 2026-07-26.)
     onMemorySaverChanged:    if (_ready) globalPrefs.memorySaver = memorySaver
     onSeriesIdChanged:       if (_ready) _applySeriesPrefs()
 
diff --git a/qml/comicreader/ComicReaderStripSurface.qml b/qml/comicreader/ComicReaderStripSurface.qml
index 8fd3618..7357d7c 100644
--- a/qml/comicreader/ComicReaderStripSurface.qml
+++ b/qml/comicreader/ComicReaderStripSurface.qml
@@ -86,6 +86,14 @@ Item {
     property alias contentY: list.contentY
     readonly property int rowCount: list.count
     readonly property real contentHeight: list.contentHeight
+    // Is the column parked at the bottom — or already gliding into it? The shell asks this to decide
+    // whether a page-down should scroll or announce the end of the volume. It counts the IN-FLIGHT
+    // backlog deliberately: without that, a second Space pressed while the first is still gliding
+    // would read the not-yet-arrived position and announce the end while the page is still moving.
+    readonly property bool atEnd: {
+        var span = Math.max(0, list.contentHeight - list.height)
+        return span <= 0 || (list.contentY + _pendingWheelPx) >= span - 1
+    }
     function itemAt(i) { return list.itemAtIndex(i) }
     function forceRelayout() { list.forceLayout() }
 
@@ -237,7 +245,10 @@ Item {
             // mounting or restoring never clobbers the shell's page.
             if (!root._programmatic) root._scheduleEmit()
         }
-        onWidthChanged: root._scheduleReport()
+        // A WIDTH change reflows the whole column, so it is reported SYNCHRONOUSLY — never through
+        // the throttle. See _flushViewportReportNow: deferring this by even one frame is what made
+        // Hemanth's fullscreen transition shake.
+        onWidthChanged: root._flushViewportReportNow()
         onHeightChanged: root._scheduleReport()
     }
 
@@ -252,6 +263,28 @@ Item {
         _reportPending = true
         if (!reportTimer.running) reportTimer.start()
     }
+    // Report NOW, in this same beat, bypassing the 16ms throttle.
+    //
+    // WHY (measured, tests/comicreader_fullscreen_timing_probe.qml, 2026-07-26). Every fullscreen
+    // flip in the app goes through the shared FullscreenTransitionShield: cover fades in, the window
+    // mode flips behind it, and the cover lifts on the window's very NEXT frameSwapped. That is
+    // honest for Player 2 — one textured quad that letterboxes to the new size within that same
+    // frame, which is exactly why Hemanth calls its transition the smooth one. The strip does not
+    // settle in one frame: the width change rescales the entire page column and scales contentY to
+    // hold the reading position. Routed through the throttle, that landed 2 frames after the cover
+    // began lifting entering fullscreen and 5 frames after it leaving — the probe recorded contentY
+    // jumping 12000 -> 17067 in full view. The settle was happening in FRONT of the cover instead of
+    // behind it. That is the shake, and it is why the earlier decode-cap fix (a real but separate
+    // cost) did not cure it.
+    //
+    // The throttle exists to coalesce SCROLL reports, which arrive at 60Hz while reading. A width
+    // change is rare and must never be coalesced, so only the width path is made synchronous; the
+    // scroll and height paths keep the throttle exactly as before.
+    function _flushViewportReportNow() {
+        if (!active) return
+        reportTimer.stop()
+        _flushViewportReport()
+    }
     function _flushViewportReport() {
         _reportPending = false
         if (!active || !core) return
@@ -338,12 +371,28 @@ Item {
         var dy = pixelY
         if (dy === 0) dy = angleY * 1.4
         if (dy === 0) return
+        // WHEEL-ONLY provenance, fired before the shared glide call below: manualNavigation() means
+        // "a real mouse gesture happened", which a keyboard glide must never forge.
         _userInteracted = true
         manualNavigation()
+        smoothScrollBy(-dy)                                 // wheel-down -> +contentY
+    }
+
+    // Glide by px through the SAME accumulator the wheel uses, so a keyboard press feels like one
+    // big notch rather than a teleport.
+    //
+    // WHY THIS IS SHARED (E2). A raw `contentY = ...` write bypasses both the glide and the backlog.
+    // Press Space in the middle of a wheel glide and the old code jumped you instantly AND THEN kept
+    // sliding on the leftover wheel input still in the drain — jump-then-slide, the exact tell of two
+    // scroll systems fighting. Reader 1 routes keys through smoothScrollBy and pins instant moves
+    // through haltScrollAt; this restores that discipline. Instant, final repositions (a scrub seek,
+    // Home/End) still go through haltScrollAt, which pins the position AND drops the in-flight
+    // backlog so nothing carries across the jump.
+    function smoothScrollBy(px) {
+        if (px === 0) return
         // Starting from idle: re-anchor on the real position and mark this drain FRESH.
         if (!scrollDrain.running) { _smoothY = list.contentY; _drainFresh = true }
-        _pendingWheelPx = Math.max(-_maxBacklogPx,
-                          Math.min(_maxBacklogPx, _pendingWheelPx - dy))   // wheel-down -> +contentY
+        _pendingWheelPx = Math.max(-_maxBacklogPx, Math.min(_maxBacklogPx, _pendingWheelPx + px))
         if (!scrollDrain.running) scrollDrain.running = true
     }
 
diff --git a/tests/comicreader_fullscreen_timing_probe.qml b/tests/comicreader_fullscreen_timing_probe.qml
new file mode 100644
index 0000000..290ca3c
--- /dev/null
+++ b/tests/comicreader_fullscreen_timing_probe.qml
@@ -0,0 +1,205 @@
+// Comic Reader — FULLSCREEN TRANSITION GATE.
+//
+// WHAT IT PROTECTS. Hemanth: "going in and out of fullscreen looks incredibly rough" and, after a
+// first fix, "even after your fix, it remained shaky unlike the video player's fullscreen
+// transition." He named the reference precisely: PLAYER 2's transition, the smoothest in the app.
+//
+// THE MECHANISM (measured here, 2026-07-26). Every fullscreen flip in the app — reader, Player 2,
+// topbar, F11 — routes through the same qml/FullscreenTransitionShield.qml (Main.qml:
+// toggleFullscreenShell). The shield fades a cover in over 60ms, flips the window behind it, then
+// lifts the cover on the window's very NEXT frameSwapped. That contract is honest for content that
+// settles in one frame: Player 2 is one textured quad that letterboxes to the new size immediately,
+// which is exactly why its transition looks clean.
+//
+// The strip does NOT settle in one frame unless it is made to. A width change rescales the whole
+// page column and scales contentY to hold the reading position. When that work was routed through
+// the surface's 16ms viewport-report throttle, it landed 2 frames after the cover began lifting
+// entering fullscreen and 5 frames after it leaving — this probe recorded contentY jumping
+// 12000 -> 17067 in full view. The settle happened in FRONT of the cover instead of behind it.
+//
+// THE INVARIANT THIS GATE HOLDS: the reader's reflow must complete BEFORE the shield reveals, so
+// the first frame Hemanth sees at the new size is already settled. Stated as an ordering assertion
+// rather than a duration, so it cannot be satisfied by a machine simply being fast.
+//
+// NEGATIVE CONTROL: reverting ComicReaderStripSurface's `onWidthChanged` from
+// _flushViewportReportNow() back to _scheduleReport() must make this gate FAIL on both transitions.
+//
+// The window must be VISIBLE and really change WIDTH: a hidden QQuickWindow never renders, and on a
+// 1280x720 screen a 1280-wide window is already full width so only the height moves — the strip
+// reflows on width only, and an earlier version of this probe recorded a falsely clean zero
+// reflows for exactly that reason. Hence a deliberately narrow 900px windowed state.
+//
+// Run: qml.exe tests/comicreader_fullscreen_timing_probe.qml
+// Prints the full event ledger, then COMICREADER_FULLSCREEN_OK / _FAIL.
+
+import QtQuick
+import QtQuick.Window
+import "../qml"                       // FullscreenTransitionShield — the app's shared cover
+import "../qml/comicreader"           // ComicReaderStripSurface — the real surface under test
+
+Window {
+    id: win
+    // Deliberately NARROWER than the screen, so the flip genuinely changes WIDTH. See header.
+    width: 900
+    height: 600
+    visible: true
+    visibility: Window.Windowed
+    color: "#000000"
+    title: "Comic Reader — fullscreen timing gate"
+
+    // ---------------- event ledger ----------------
+    // seq is a strict monotonic event counter. The ordering assertion uses IT, not the frame number:
+    // the reflow and the reveal legitimately land in the same frame, and "same frame" is a PASS only
+    // when the reflow came first.
+    property int seq: 0
+    property int frameNo: 0
+    property real t0: Date.now()
+    property var failures: []
+    function mark(what, detail) {
+        win.seq += 1
+        console.log("FSGATE seq=" + win.seq + " frame=" + win.frameNo
+                    + " t=" + Math.round(Date.now() - win.t0) + "ms"
+                    + " " + what + (detail ? ("  " + detail) : ""))
+        return win.seq
+    }
+    function fail(msg) { failures.push(msg); console.log("COMICREADER_FULLSCREEN_FAIL: " + msg) }
+
+    // per-transition record
+    property int applySeq: -1
+    property int reflowSeq: -1
+    property int revealSeq: -1
+    property real reflowContentY: -1
+    property int firstFrameAfterRevealSeq: -1
+    property string phase: ""          // "enter" | "leave"
+    property int transitionsChecked: 0
+
+    Connections {
+        target: win
+        function onFrameSwapped() {
+            win.frameNo += 1
+            if (!shield.transitioning && win._trailing <= 0) return
+            if (win._trailing > 0) win._trailing -= 1
+            var s = win.mark("frameSwapped", "winW=" + win.width + " listW=" + Math.round(strip.width)
+                             + " contentY=" + Math.round(strip.contentY))
+            // The FIRST frame presented after the cover starts lifting must already be settled.
+            if (win.revealSeq >= 0 && win.firstFrameAfterRevealSeq < 0) {
+                win.firstFrameAfterRevealSeq = s
+                if (win.reflowSeq < 0)
+                    win.fail(win.phase + ": no reflow had happened by the first revealed frame")
+                else if (win.reflowContentY >= 0
+                         && Math.round(strip.contentY) !== Math.round(win.reflowContentY))
+                    win.fail(win.phase + ": first revealed frame shows contentY="
+                             + Math.round(strip.contentY) + " but the reflow settled it at "
+                             + Math.round(win.reflowContentY) + " — the settle is visible to the user")
+            }
+        }
+    }
+    property int _trailing: 0
+
+    // ---------------- fake backend core (shape-parity with ComicReaderCore) ----------------
+    QtObject {
+        id: core
+        property var stripModel: stripModelA
+        property int setStripViewportWidthCalls: 0
+        signal pageReady(int page)
+        signal pageFailed(int page, string code)
+        signal stripCompensation(real delta)
+        signal entryChanged()
+        signal pairingChanged()
+        function imageUrl(page) { return "" }        // no real pixels needed to time the reflow
+        function unitForPage(page) { return { rightIndex: page, leftIndex: -1, spread: false, coverAlone: false } }
+        function pageInfo(page) { return { error: "" } }
+        function setVisible(pages) {}
+        function setStripViewport(top, height) {}
+        function stripPageTop(page) { return page * 1220 }
+        function stripPageAtCenter(top, h) { return Math.floor(top / 1220) }
+        function setStripViewportWidth(w) {
+            setStripViewportWidthCalls += 1
+            // THE MOMENT UNDER TEST: the reader's column rescale. Recorded only while a transition
+            // is live, so the initial-layout call does not masquerade as a transition reflow.
+            if (!shield.transitioning) return
+            win.reflowSeq = win.mark("REFLOW setStripViewportWidth", "w=" + Math.round(w))
+            // contentY is scaled by the CALLER immediately after this returns, so it cannot be read
+            // here — a zero-interval timer reads it once that assignment has happened.
+            settleRead.restart()
+        }
+    }
+    Timer { id: settleRead; interval: 0; repeat: false; onTriggered: win.reflowContentY = strip.contentY }
+
+    ListModel { id: stripModelA }
+    Component.onCompleted: {
+        for (var i = 0; i < 200; i++)
+            stripModelA.append({ pageIndex: i, top: i * 1220, displayWidth: 800, displayHeight: 1200,
+                                 ready: true, errorCode: 0 })
+        start.start()
+    }
+
+    ComicReaderStripSurface {
+        id: strip
+        anchors.fill: parent
+        core: core
+        active: true
+    }
+
+    FullscreenTransitionShield {
+        id: shield
+        anchors.fill: parent
+        onApplyRequested: {
+            win.applySeq = win.mark("APPLY (window mode flips)", "")
+            win.visibility = (win.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
+        }
+        onAwaitingFrameChanged: {
+            if (!shield.awaitingFrame && shield.transitioning) {
+                win.revealSeq = win.mark("REVEAL begins (cover starts lifting)", "")
+                win._trailing = 12
+                // THE ORDERING ASSERTION.
+                if (win.reflowSeq < 0)
+                    win.fail(win.phase + ": cover began lifting before the reader reflowed at all")
+                else if (win.reflowSeq > win.revealSeq)
+                    win.fail(win.phase + ": reflow (seq " + win.reflowSeq + ") landed AFTER the cover "
+                             + "began lifting (seq " + win.revealSeq + ") — the user watches it settle")
+                win.transitionsChecked += 1
+            }
+        }
+    }
+
+    function beginTransition(which) {
+        win.phase = which
+        win.applySeq = -1; win.reflowSeq = -1; win.revealSeq = -1
+        win.reflowContentY = -1; win.firstFrameAfterRevealSeq = -1
+        win.mark("BEGIN " + which + "-fullscreen", "")
+        shield.begin()
+    }
+
+    // ---------------- the run ----------------
+    // Scroll first so contentY > 0: the width-ratio rescale only runs on a scrolled strip, which is
+    // the real reading case and the one Hemanth sees.
+    Timer {
+        id: start
+        interval: 600; repeat: false
+        onTriggered: {
+            strip.contentY = 12000
+            win.mark("--- scrolled to contentY=12000 ---", "")
+            enter.start()
+        }
+    }
+    Timer { id: enter; interval: 300; repeat: false; onTriggered: win.beginTransition("enter") }
+    Timer { id: leave; interval: 1600; repeat: false; running: true; onTriggered: win.beginTransition("leave") }
+    Timer {
+        interval: 3200; repeat: false; running: true
+        onTriggered: {
+            if (win.transitionsChecked !== 2)
+                win.fail("expected 2 transitions to be checked, saw " + win.transitionsChecked
+                         + " — the probe did not exercise what it claims to")
+            if (core.setStripViewportWidthCalls < 3)
+                win.fail("expected at least 3 width reports (initial + 2 flips), saw "
+                         + core.setStripViewportWidthCalls + " — the window may not have resized")
+            if (win.failures.length === 0) {
+                console.log("COMICREADER_FULLSCREEN_OK")
+                Qt.exit(0)
+            } else {
+                Qt.exit(1)
+            }
+        }
+    }
+}
diff --git a/tests/comicreader_shell_harness.qml b/tests/comicreader_shell_harness.qml
index 8dca602..54b8ebb 100644
--- a/tests/comicreader_shell_harness.qml
+++ b/tests/comicreader_shell_harness.qml
@@ -86,7 +86,19 @@ Item {
         property var lastVisible: null
         function setVisible(pages) { lastVisible = pages }
         property var fakeUnit: null   // when set, unitForPage returns THIS regardless of page (B5 RTL test)
-        function unitForPage(page) { return fakeUnit !== null ? fakeUnit : { rightIndex: page - 1, leftIndex: -1, spread: false } }
+        // NOTE on the default: it answers `rightIndex: page - 1`, which is NOT what the real core
+        // answers for an unpaired page (ComicReaderCore returns rightIndex == page). The existing
+        // checks that use this fake only care that SOME unit comes back, so the shift is harmless to
+        // them and is left alone rather than changed underneath them. It is not harmless to anything
+        // doing forward unit arithmetic: with the shift, "the unit after this one" walks backwards,
+        // so pageNext could never advance and an end-of-book test read as a code bug for an hour.
+        // unitIdentity opts into the real core's contract for tests that need honest unit math.
+        property bool unitIdentity: false
+        function unitForPage(page) {
+            if (fakeUnit !== null) return fakeUnit
+            if (unitIdentity) return { rightIndex: page, leftIndex: -1, spread: false }
+            return { rightIndex: page - 1, leftIndex: -1, spread: false }
+        }
         // spread override spy (B5): pageInfo reports the override as absent/true/false (matches the
         // real core's PageMeta::toVariantMap — absence IS the auto state, never a third "auto" value).
         property var fakePageInfo: ({})
@@ -955,6 +967,143 @@ Item {
             harness._csShell = csShell
             harness._csArea = csArea
 
+            // -- 13. F5 END OF VOLUME: pressing forward on the last page ANNOUNCES the end instead
+            // of going silent. Silence there is indistinguishable from a dropped input — you press
+            // again, harder, and wonder whether the reader is stuck. --
+            var f5Store = fakeStoreF5
+            f5Store.pages = fivePages()
+            fakeCoreF5.unitIdentity = true      // honest unit math — see the FakeCore note
+            var f5Shell = makeShell({
+                "width": 640, "height": 480,
+                "seriesId": "s-f5", "seriesTitle": "EndOfVolume", "seriesCover": "file:///f/f5.png",
+                "core": fakeCoreF5, "progress": fakeProgF5, "pageStore": f5Store,
+                "persistedMode": "double_page",
+                "entryKind": "manga", "western": false,
+                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],     // ONLY chapter -> no next
+                "chapterId": "ch1", "chapterLabel": "Chapter 1"
+            })
+            var f5Toast = byName(f5Shell, "hudToastText")
+            ck(f5Toast !== null, "F5: the HUD toast text must be reachable (objectName 'hudToastText')")
+            ck(f5Shell.hasNext === false, "F5: a single-chapter fixture must report hasNext=false, got " + f5Shell.hasNext)
+
+            ck(f5Shell.mode === "double_page",
+               "F5: fixture must mount in double_page, got '" + f5Shell.mode + "' (max=" + f5Shell.max + ")")
+
+            // mid-book: a normal page turn must stay SILENT
+            f5Shell.currentPage = 1
+            var f5Before = f5Toast ? f5Toast.text : ""
+            f5Shell.pageNext()
+            ck(f5Shell.currentPage > 1, "F5: a mid-book pageNext must actually turn the page, got "
+               + f5Shell.currentPage + " (mode=" + f5Shell.mode + " max=" + f5Shell.max + ")")
+            ck(!f5Toast || f5Toast.text === f5Before,
+               "F5: a mid-book page turn must NOT toast, got '" + (f5Toast ? f5Toast.text : "") + "'")
+
+            // last page: announce, and with no next entry say only that
+            f5Shell.currentPage = f5Shell.max
+            f5Shell.pageNext()
+            ck(f5Shell.currentPage === f5Shell.max,
+               "F5: pageNext at the end must not move past the last page, got " + f5Shell.currentPage)
+            ck(f5Toast && f5Toast.text === "End of volume",
+               "F5: the end of a volume with no next entry must toast 'End of volume', got '"
+               + (f5Toast ? f5Toast.text : "") + "'")
+
+            // ...and when there IS a next entry, it names the binding that actually exists.
+            var f5bStore = fakeStoreF5b
+            f5bStore.pages = fivePages()
+            fakeCoreF5b.unitIdentity = true
+            var f5bShell = makeShell({
+                "width": 640, "height": 480,
+                "seriesId": "s-f5b", "seriesTitle": "EndOfVolumeNext", "seriesCover": "file:///f/f5b.png",
+                "core": fakeCoreF5b, "progress": fakeProgF5b, "pageStore": f5bStore,
+                "persistedMode": "double_page",
+                "entryKind": "manga", "western": false,
+                // chapters are NEWEST-FIRST (see the crossing checks above): "next" means index-1,
+                // toward the newest. The open entry must therefore sit LAST for a next to exist.
+                "chapters": [{ "id": "ch2", "number": "2", "name": "" },
+                             { "id": "ch1", "number": "1", "name": "" }],
+                "chapterId": "ch1", "chapterLabel": "Chapter 1"
+            })
+            var f5bToast = byName(f5bShell, "hudToastText")
+            ck(f5bShell.hasNext === true, "F5: a two-chapter fixture on the first must report hasNext=true, got " + f5bShell.hasNext)
+            f5bShell.currentPage = f5bShell.max
+            f5bShell.pageNext()
+            ck(f5bToast && f5bToast.text === "End of volume — Alt+Right for the next",
+               "F5: with a next entry the toast must name the REAL binding (Alt+Right, per "
+               + "ComicReaderInput's nextEntry), got '" + (f5bToast ? f5bToast.text : "") + "'")
+
+            // -- 14. F2 PER-SERIES STRIP MEASURE: a width set on one series must not re-dress the
+            // others. Two shells share ONE seriesRecords store and ONE globalPrefs, which is what
+            // makes this a real test of the leak rather than of two isolated objects. --
+            var f2Records = freshRecords()
+            var f2Prefs = freshPrefs({ stripWidthPct: 78, stripGap: 0 })
+            var f2StoreA = fakeStoreF2a, f2StoreB = fakeStoreF2b
+            f2StoreA.pages = fivePages(); f2StoreB.pages = fivePages()
+
+            var aShell = makeShell({
+                "width": 640, "height": 480,
+                "seriesId": "series-A", "seriesTitle": "A", "seriesCover": "file:///f/a.png",
+                "core": fakeCoreF2a, "progress": fakeProgF2a, "pageStore": f2StoreA,
+                "globalPrefs": f2Prefs, "seriesRecords": f2Records,
+                "entryKind": "manga", "western": false,
+                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
+                "chapterId": "ch1", "chapterLabel": "Chapter 1"
+            })
+            // A is dressed narrow by the user
+            aShell.setStripLayout(55, 12)
+            ck(fakeCoreF2a.lastStripLayout.w === 55 && fakeCoreF2a.lastStripLayout.g === 12,
+               "F2: setStripLayout must reach the backend, got " + JSON.stringify(fakeCoreF2a.lastStripLayout))
+            var recA = JSON.parse(f2Records.all)["series-A"]
+            ck(recA && recA.sw === 55 && recA.sg === 12,
+               "F2: the series record must remember this series' own measure, got " + JSON.stringify(recA))
+            ck(f2Prefs.stripWidthPct === 55 && f2Prefs.stripGap === 12,
+               "F2: a user-set measure must ALSO seed the global for undressed series, got "
+               + f2Prefs.stripWidthPct + "/" + f2Prefs.stripGap)
+
+            // B has no record of its own -> follows the global seed (which A just moved). That is
+            // the intended half of the coupling: a width you set still reaches books you have no
+            // opinion about.
+            var bShell = makeShell({
+                "width": 640, "height": 480,
+                "seriesId": "series-B", "seriesTitle": "B", "seriesCover": "file:///f/b.png",
+                "core": fakeCoreF2b, "progress": fakeProgF2b, "pageStore": f2StoreB,
+                "globalPrefs": f2Prefs, "seriesRecords": f2Records,
+                "entryKind": "manga", "western": false,
+                "chapters": [{ "id": "ch1", "number": "1", "name": "" }],
+                "chapterId": "ch1", "chapterLabel": "Chapter 1"
+            })
+            ck(fakeCoreF2b.lastStripLayout.w === 55 && fakeCoreF2b.lastStripLayout.g === 12,
+               "F2: a series with NO record must follow the global seed, got "
+               + JSON.stringify(fakeCoreF2b.lastStripLayout))
+            var recBopen = JSON.parse(f2Records.all)["series-B"]
+            ck(recBopen === undefined || recBopen === null || recBopen.sw === undefined,
+               "F2: merely OPENING a series must not invent a width opinion for it, got "
+               + JSON.stringify(recBopen))
+
+            // B is dressed wide by the user...
+            bShell.setStripLayout(92, 4)
+            ck(JSON.parse(f2Records.all)["series-B"].sw === 92,
+               "F2: B must record its own measure, got "
+               + JSON.stringify(JSON.parse(f2Records.all)["series-B"]))
+            // ...and A must be UNTOUCHED. This is the whole point of the task.
+            var recA2 = JSON.parse(f2Records.all)["series-A"]
+            ck(recA2.sw === 55 && recA2.sg === 12,
+               "F2: dressing series B must NOT re-dress series A, got " + JSON.stringify(recA2))
+
+            // ...and re-opening A restores A's own measure, not B's or the global's.
+            fakeCoreF2a.lastStripLayout = null
+            aShell.seriesId = "series-A"
+            aShell._applySeriesPrefs()
+            ck(fakeCoreF2a.lastStripLayout && fakeCoreF2a.lastStripLayout.w === 55,
+               "F2: re-opening A must restore A's own 55, not B's 92 or the global, got "
+               + JSON.stringify(fakeCoreF2a.lastStripLayout))
+
+            // A reading-mode change must not silently invent/drop a width record.
+            var beforeMode = JSON.parse(f2Records.all)["series-A"].sw
+            aShell._saveSeriesPrefs()
+            ck(JSON.parse(f2Records.all)["series-A"].sw === beforeMode,
+               "F2: saving series prefs without an explicit measure must PRESERVE the stored width, got "
+               + JSON.stringify(JSON.parse(f2Records.all)["series-A"]))
+
         } catch (e) {
             failures.push("exception during checks: " + e.message)
         }
@@ -1073,6 +1222,10 @@ Item {
     FakeCore { id: fakeCoreB6 }  FakeProgress { id: fakeProgB6 }  FakePageStore { id: fakeStoreB6 }
     FakeCore { id: fakeCoreVP }  FakeProgress { id: fakeProgVP }  FakePageStore { id: fakeStoreVP }
     FakeCore { id: fakeCoreCS }  FakeProgress { id: fakeProgCS }  FakePageStore { id: fakeStoreCS }
+    FakeCore { id: fakeCoreF5 }  FakeProgress { id: fakeProgF5 }  FakePageStore { id: fakeStoreF5 }
+    FakeCore { id: fakeCoreF5b } FakeProgress { id: fakeProgF5b } FakePageStore { id: fakeStoreF5b }
+    FakeCore { id: fakeCoreF2a } FakeProgress { id: fakeProgF2a } FakePageStore { id: fakeStoreF2a }
+    FakeCore { id: fakeCoreF2b } FakeProgress { id: fakeProgF2b } FakePageStore { id: fakeStoreF2b }
 
     // fires the deferred phase after the pinned 20ms record debounce has elapsed
     Timer { id: deferredTimer; interval: 150; running: false; onTriggered: harness.runDeferred() }
diff --git a/tests/comicreader_surfaces_harness.qml b/tests/comicreader_surfaces_harness.qml
index c92ef8b..e78303e 100644
--- a/tests/comicreader_surfaces_harness.qml
+++ b/tests/comicreader_surfaces_harness.qml
@@ -175,6 +175,10 @@ Item {
 
     // ---- strip user-signal capture: a PROGRAMMATIC restore must emit none of these ----
     property int stripScrolledCount: 0
+    // manualNavigation() is WHEEL provenance specifically ("a real gesture happened"), unlike the
+    // provenance-blind tracking signals above. E2 routes keyboard scrolling through the same drain
+    // as the wheel, so this counter is what proves the shared path did not forge a wheel gesture.
+    property int stripManualNavCount: 0
 
     function fillStripModel(m, n) {
         m.clear()
@@ -206,6 +210,7 @@ Item {
         if (!stripSurface) { failures.push("strip: createObject returned null"); return }
         stripSurface.scrolled.connect(function (f) { harness.stripScrolledCount += 1 })
         stripSurface.pageInView.connect(function (p) { harness.stripScrolledCount += 1 })
+        stripSurface.manualNavigation.connect(function () { harness.stripManualNavCount += 1 })
         stripSurface.forceRelayout()
 
         // --- virtualization: near window has a delegate, a far page does not ---
@@ -384,6 +389,69 @@ Item {
         ck(stripSurface._pendingWheelPx === 0, "wheel: haltScrollAt must drop the in-flight backlog, got " + stripSurface._pendingWheelPx)
         ck(stripSurface._smoothY === 1234.5, "wheel: haltScrollAt must re-anchor the float accumulator, got " + stripSurface._smoothY)
 
+        // --- E2: keyboard / API repositions ride the SAME drain as the wheel ---
+        // A raw contentY write bypasses both the glide and the backlog: press Space mid-glide and the
+        // view jumps AND THEN keeps sliding on the leftover wheel input. Reader 1 routes keys through
+        // smoothScrollBy and pins instant moves through haltScrollAt. Space should feel like one big
+        // wheel notch, not a teleport.
+        stripSurface.haltScrollAt(0)
+        stripSurface.smoothScrollBy(300)
+        ck(stripSurface._pendingWheelPx === 300,
+           "glide: smoothScrollBy must feed the SAME drain backlog as the wheel, got " + stripSurface._pendingWheelPx)
+        ck(stripSurface._drainFresh === true,
+           "glide: a glide from idle must mark the drain FRESH, exactly like a wheel intake from idle")
+
+        // An instant reposition mid-glide drops the leftover backlog — no jump-then-slide.
+        stripSurface.haltScrollAt(500)
+        ck(stripSurface._pendingWheelPx === 0 && stripSurface.contentY === 500,
+           "glide: haltScrollAt must pin AND drop in-flight backlog, got contentY=" + stripSurface.contentY
+           + " backlog=" + stripSurface._pendingWheelPx)
+
+        // PROVENANCE: manualNavigation() means "a real WHEEL gesture happened". A keyboard/API glide
+        // must not forge one — otherwise every Space press would look like a mouse gesture to any
+        // future consumer of that signal. (Tracking itself is provenance-blind; this is only the
+        // wheel-specific signal.)
+        var manualBefore = harness.stripManualNavCount
+        stripSurface.smoothScrollBy(120)
+        ck(harness.stripManualNavCount === manualBefore,
+           "glide: smoothScrollBy must NOT emit manualNavigation() — that signal is wheel provenance")
+        stripSurface.haltScrollAt(0)
+
+        // ...while the wheel path still does emit it, so the refactor did not hollow the signal out.
+        stripSurface._intakeWheel(-120, 0)
+        ck(harness.stripManualNavCount === manualBefore + 1,
+           "glide: the WHEEL path must still emit manualNavigation() after the refactor, got "
+           + harness.stripManualNavCount + " vs " + (manualBefore + 1))
+        stripSurface.haltScrollAt(0)
+
+        // --- F5 seam: atEnd, the shell's "is there anything left to scroll" question ---
+        // The span is re-read immediately before every step, never cached: contentHeight moves as
+        // the ListView relayouts, and haltScrollAt does NOT clamp — caching it once parked the
+        // column 97,000px past the end of the book and made the first draft of this test fail for
+        // its own reasons rather than the code's.
+        function spanNowF5() {
+            stripSurface.forceRelayout()
+            return stripSurface.contentHeight - stripSurface.height
+        }
+
+        stripSurface.haltScrollAt(0)
+        ck(spanNowF5() > 0, "atEnd: fixture must have a scrollable span to test against, got " + spanNowF5())
+        ck(stripSurface.atEnd === false, "atEnd: must be false at the top of a long book")
+
+        stripSurface.haltScrollAt(spanNowF5())
+        ck(stripSurface.atEnd === true, "atEnd: must be true once parked at the bottom (contentY="
+           + stripSurface.contentY + " span=" + spanNowF5() + ")")
+
+        // It counts the IN-FLIGHT backlog: a second key pressed while the first is still gliding
+        // toward the bottom must not read the not-yet-arrived position and announce the end early.
+        stripSurface.haltScrollAt(spanNowF5() - 2000)
+        ck(stripSurface.atEnd === false, "atEnd: must be false 2000px short of the bottom (contentY="
+           + stripSurface.contentY + " span=" + spanNowF5() + ")")
+        stripSurface.smoothScrollBy(5000)                 // a glide that will land past the bottom
+        ck(stripSurface.atEnd === true,
+           "atEnd: a glide already heading past the bottom must count as at-the-end (backlog aware)")
+        stripSurface.haltScrollAt(0)
+
         // --- RESTORE (B2): the surface is a PAINTER. It restores nothing itself — the shell puts the
         // column somewhere by CALLING seekToPage()/haltScrollAt(). This replaces the old bound
         // `resumeFraction` + `_resumeApplied` latch, which was a feedback loop (the surface's own
diff --git a/tests/test_comicreader_acceptance.ps1 b/tests/test_comicreader_acceptance.ps1
new file mode 100644
index 0000000..8412b53
--- /dev/null
+++ b/tests/test_comicreader_acceptance.ps1
@@ -0,0 +1,190 @@
+# Comic Reader - ACCEPTANCE.
+#
+# One command, one verdict. Builds every native comic reader harness from the CURRENT tree, runs
+# them, runs every QML gate, and prints COMICREADER_ACCEPTANCE_OK only if all of them pass.
+#
+# WHY IT EXISTS. The reader's gates had grown to fourteen separate scripts and executables, each run
+# by hand. That is how a red gate hides: one gets skipped, the rest are green, and "the suite passes"
+# becomes true-ish rather than true. It also rebuilds the native harnesses rather than trusting
+# whatever .exe happens to be lying in build-msvc, because a stale harness passing an old contract
+# is the most convincing kind of false green.
+#
+# WHAT IT IS NOT. It is not eyes-on. Every gate here can be green while the reader still feels wrong
+# in the hand - that has happened repeatedly on this project, most recently with the fullscreen
+# transition, which was invisible to the whole suite until it was measured deliberately. Hemanth's
+# eyes remain the gate this script cannot be.
+#
+# Usage:
+#   powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_comicreader_acceptance.ps1
+#   ...          -SkipBuild     run against the harness .exe files already built (fast iteration)
+#   ...          -QmlOnly       skip the native half entirely
+#
+# This file is deliberately pure ASCII: PowerShell 5.1 reading a BOM-less UTF-8 script mis-frames
+# multi-byte characters inside quoted strings and reports bogus parse errors.
+
+param(
+    [switch]$SkipBuild,
+    [switch]$QmlOnly
+)
+
+$ErrorActionPreference = "Stop"
+
+$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
+$qmlExe   = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
+$buildBat = Join-Path $repoRoot "native/build-target.bat"
+$buildDir = Join-Path $repoRoot "native/build-msvc"
+
+# The QML gates. Each is a sibling test_comicreader_*.ps1 that exits 0 on success.
+$qmlGates = @(
+    "chrome",       # HUD, toast, back action, auto-hide, positioner-anchor warnings
+    "surfaces",     # strip + double geometry, wheel drain, glide, atEnd
+    "shell",        # orchestration, persistence, crossings, end-of-volume
+    "contract",     # public surface the callers depend on
+    "overlays",     # settings sheet and friends
+    "state",        # pure store/reading-mode logic
+    "migration",    # the MangaReader -> ComicReaderShell cutover
+    "fullscreen"    # the transition ordering (needs a REAL window; see its header)
+)
+
+# The native harnesses. Target name == executable name.
+$nativeHarnesses = @(
+    "comicreader_core_harness",
+    "comicreader_pairing_harness",
+    "comicreader_cache_harness",
+    "comicreader_decode_harness",
+    "comicreader_coupling_harness",
+    "comicreader_strip_harness"
+)
+
+$failures = @()
+$results  = @()
+
+function Record($name, $ok, $detail) {
+    $script:results += [PSCustomObject]@{ Name = $name; Ok = $ok; Detail = $detail }
+    if (-not $ok) { $script:failures += ("{0}: {1}" -f $name, $detail) }
+    $tag = if ($ok) { "PASS" } else { "FAIL" }
+    Write-Host ("  {0,-34} {1}" -f $name, $tag)
+}
+
+if (!(Test-Path -LiteralPath $qmlExe)) {
+    Write-Host "COMICREADER_ACCEPTANCE_FAIL: qml.exe not found at $qmlExe"
+    exit 1
+}
+
+$env:QT_FORCE_STDERR_LOGGING = "1"
+$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = "1"
+# The Qt bin directory must lead PATH so the harnesses resolve their own Qt DLLs.
+$env:PATH = "C:\Qt\6.11.1\msvc2022_64\bin;" + $buildDir + ";" + $env:PATH
+
+$started = Get-Date
+
+# ---------------------------------------------------------------- native half
+if (-not $QmlOnly) {
+    Write-Host ""
+    Write-Host "NATIVE HARNESSES"
+    foreach ($h in $nativeHarnesses) {
+        $exe = Join-Path $buildDir "$h.exe"
+
+        if (-not $SkipBuild) {
+            if (!(Test-Path -LiteralPath $buildBat)) {
+                Record $h $false "build-target.bat not found at $buildBat"
+                continue
+            }
+            # build-target.bat needs an ABSOLUTE path invocation (house trap).
+            #
+            # ErrorActionPreference is dropped to Continue around the call ON PURPOSE. vcvars64
+            # emits a harmless "'vswhere.exe' is not recognized" line on stderr, and under "Stop"
+            # PowerShell promotes ANY native stderr output to a terminating error - so a build that
+            # printed TARGET_BUILD_OK was being reported as a failed build. The log is the source of
+            # truth here, which is the same house rule that says build exit codes lie.
+            $log = Join-Path $env:TEMP "cr_acc_build_$h.log"
+            $prevBuild = $ErrorActionPreference
+            $ErrorActionPreference = "Continue"
+            & cmd.exe /c "`"$buildBat`" $h" > $log 2>&1
+            $ErrorActionPreference = $prevBuild
+            # Backgrounded/again-shelled build exit codes lie on this toolchain - grep the log for
+            # the success marker the batch prints, and for the two failure signatures.
+            $logText = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
+            if (-not $logText -or $logText -notmatch "TARGET_BUILD_OK") {
+                $sig = ""
+                if ($logText) {
+                    $m = [regex]::Matches($logText, "(?m)^.*(error C\d+|LNK\d+|ninja: build stopped|fatal error).*$")
+                    if ($m.Count -gt 0) { $sig = $m[0].Value.Trim() }
+                }
+                Record $h $false ("build failed" + $(if ($sig) { " - $sig" } else { " (see $log)" }))
+                continue
+            }
+        }
+
+        if (!(Test-Path -LiteralPath $exe)) {
+            Record $h $false "executable missing at $exe (run without -SkipBuild)"
+            continue
+        }
+
+        $prev = $ErrorActionPreference
+        $ErrorActionPreference = "Continue"
+        $out = & $exe 2>&1 | Out-String
+        $code = $LASTEXITCODE
+        $ErrorActionPreference = $prev
+
+        if ($code -ne 0) {
+            $line = ($out -split "`n" | Where-Object { $_ -match "FAIL" } | Select-Object -First 1)
+            if (-not $line) { $line = ($out -split "`n" | Select-Object -Last 3) -join " / " }
+            Record $h $false ("exit $code - " + $line.Trim())
+        } else {
+            Record $h $true "ok"
+        }
+    }
+}
+
+# ------------------------------------------------------------------- QML half
+Write-Host ""
+Write-Host "QML GATES"
+foreach ($g in $qmlGates) {
+    $script = Join-Path $PSScriptRoot "test_comicreader_$g.ps1"
+    if (!(Test-Path -LiteralPath $script)) {
+        Record $g $false "gate script missing at $script"
+        continue
+    }
+    $prev = $ErrorActionPreference
+    $ErrorActionPreference = "Continue"
+    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $script 2>&1 | Out-String
+    $code = $LASTEXITCODE
+    $ErrorActionPreference = $prev
+
+    if ($code -ne 0) {
+        $line = ($out -split "`n" | Where-Object { $_ -match "FAIL" } | Select-Object -First 1)
+        if (-not $line) { $line = ($out -split "`n" | Select-Object -Last 3) -join " / " }
+        Record $g $false ("exit $code - " + $line.Trim())
+    } else {
+        Record $g $true "ok"
+    }
+}
+
+# --------------------------------------------------------------------- verdict
+$elapsed = [int]((Get-Date) - $started).TotalSeconds
+$total = $results.Count
+$passed = ($results | Where-Object { $_.Ok }).Count
+
+Write-Host ""
+Write-Host ("{0}/{1} gates passed in {2}s" -f $passed, $total, $elapsed)
+
+if ($failures.Count -gt 0) {
+    Write-Host ""
+    Write-Host "FAILURES:"
+    foreach ($f in $failures) { Write-Host ("  - " + $f) }
+    Write-Host ""
+    Write-Host "COMICREADER_ACCEPTANCE_FAIL"
+    exit 1
+}
+
+# A green run that exercised nothing is the failure mode this guards against: if the gate list is
+# ever emptied or every entry silently skipped, the script must not report success.
+$expected = $(if ($QmlOnly) { $qmlGates.Count } else { $qmlGates.Count + $nativeHarnesses.Count })
+if ($total -lt $expected) {
+    Write-Host ("COMICREADER_ACCEPTANCE_FAIL: expected {0} gates, only {1} ran" -f $expected, $total)
+    exit 1
+}
+
+Write-Host "COMICREADER_ACCEPTANCE_OK"
+exit 0
diff --git a/tests/test_comicreader_fullscreen.ps1 b/tests/test_comicreader_fullscreen.ps1
new file mode 100644
index 0000000..2195e9f
--- /dev/null
+++ b/tests/test_comicreader_fullscreen.ps1
@@ -0,0 +1,71 @@
+# Comic Reader - FULLSCREEN TRANSITION gate.
+#
+# Holds the invariant behind Hemanth's "going in and out of fullscreen looks incredibly rough" and
+# his follow-up after a first, insufficient fix: "even after your fix, it remained shaky unlike the
+# video player's fullscreen transition." His reference is PLAYER 2's transition, the smoothest in
+# the app.
+#
+# THE INVARIANT: the reader's column reflow must land BEFORE the shared FullscreenTransitionShield
+# lifts its cover, so the first frame shown at the new size is already settled. Player 2 satisfies
+# this for free (one textured quad, letterboxes within the frame). The strip only satisfies it
+# because its width report bypasses the 16ms viewport throttle - route it back through the throttle
+# and the reflow lands 2-5 frames late, in full view. See the harness header for the measured
+# ledger and the negative control.
+#
+# UNLIKE every sibling comicreader gate, this one CANNOT run -platform offscreen: an offscreen
+# window never really resizes and never presents frames, so the ordering under test would not exist
+# to observe. It opens a small real window for ~3 seconds and closes itself.
+#
+# This file is deliberately pure ASCII. The sibling gates carry a few non-ASCII characters in
+# comments and survive, but PowerShell 5.1 reading a BOM-less UTF-8 script mis-frames multi-byte
+# characters INSIDE quoted strings, which swallowed a closing quote here and produced a bogus
+# "Missing closing '}'" parse error. Not worth re-learning.
+
+$ErrorActionPreference = "Stop"
+
+$qmlExe = "C:/Qt/6.11.1/msvc2022_64/bin/qml.exe"
+if (!(Test-Path -LiteralPath $qmlExe)) {
+    Write-Host "FAIL: qml.exe not found at $qmlExe"
+    exit 1
+}
+
+# --- static assertion: the width path must NOT be routed through the throttle ---
+# The behavioral gate below is the real test, but it needs a visible window and a compositor. This
+# grep fails fast and unambiguously if someone reverts the one line that matters, even on a machine
+# where the windowed run is skipped or flaky.
+$stripQml = Join-Path $PSScriptRoot "../qml/comicreader/ComicReaderStripSurface.qml"
+if (!(Test-Path -LiteralPath $stripQml)) {
+    Write-Host "FAIL: ComicReaderStripSurface.qml not found at $stripQml"
+    exit 1
+}
+$widthHandler = Select-String -LiteralPath $stripQml -Pattern "onWidthChanged\s*:"
+if (!$widthHandler) {
+    Write-Host "FAIL: ComicReaderStripSurface.qml has no onWidthChanged handler at all"
+    exit 1
+}
+foreach ($h in $widthHandler) {
+    if ($h.Line -notmatch "_flushViewportReportNow") {
+        Write-Host "FAIL: the strip's onWidthChanged must report SYNCHRONOUSLY (_flushViewportReportNow)."
+        Write-Host "      Deferring a width change by even one frame puts the reflow in front of the"
+        Write-Host "      fullscreen cover instead of behind it - that is the shake Hemanth reported."
+        Write-Host ("      line " + $h.LineNumber + ": " + $h.Line.Trim())
+        exit 1
+    }
+}
+
+$env:QT_FORCE_STDERR_LOGGING = "1"
+$harness = Join-Path $PSScriptRoot "comicreader_fullscreen_timing_probe.qml"
+
+$prevEAP = $ErrorActionPreference
+$ErrorActionPreference = "Continue"
+$output = & $qmlExe $harness 2>&1 | Out-String
+$code = $LASTEXITCODE
+$ErrorActionPreference = $prevEAP
+
+if ($code -ne 0 -or ($output -notmatch "COMICREADER_FULLSCREEN_OK")) {
+    Write-Host "FAIL: comic reader fullscreen transition gate (exit $code)"
+    Write-Host $output
+    exit 1
+}
+
+Write-Host "COMICREADER_FULLSCREEN_OK"
```

YOUR REVIEW — do all four:
1. For EACH Definition-of-Done item above (1-14): state MET / NOT-MET / PARTIAL with one line of
   evidence from the diff.
2. Flag anything the diff DOES that the DoD never asked for (scope creep / unrequested behaviour
   change). Note especially: the diff changes a shared persistence path and REMOVES two property
   change handlers — judge whether that removal can lose a write on any path the DoD did not list.
3. Correctness + security pass: real bugs, regressions, races, unsafe assumptions. Pay particular
   attention to (a) whether making the width report synchronous inside `onWidthChanged` can recurse
   or fight the ListView's own layout pass, (b) whether `atEnd` can be wrong when contentHeight is
   still an estimate because pages have not decoded, and (c) whether the F2 merge-write can be
   defeated by the 800ms debounced entry-blob save or by seriesId changing mid-write.
4. Anything the DoD SHOULD have specified but didn't (a gap in the intent itself).

END with exactly one line: APPROVE or REQUEST-CHANGES, plus a one-sentence reason. Be terse;
default to REQUEST-CHANGES if any DoD item is NOT-MET or you are unsure.
