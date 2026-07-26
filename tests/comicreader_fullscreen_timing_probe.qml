// Comic Reader — FULLSCREEN TRANSITION GATE.
//
// WHAT IT PROTECTS. Hemanth: "going in and out of fullscreen looks incredibly rough" and, after a
// first fix, "even after your fix, it remained shaky unlike the video player's fullscreen
// transition." He named the reference precisely: PLAYER 2's transition, the smoothest in the app.
//
// THE MECHANISM (measured here, 2026-07-26). Every fullscreen flip in the app — reader, Player 2,
// topbar, F11 — routes through the same qml/FullscreenTransitionShield.qml (Main.qml:
// toggleFullscreenShell). The shield fades a cover in over 60ms, flips the window behind it, then
// lifts the cover on the window's very NEXT frameSwapped. That contract is honest for content that
// settles in one frame: Player 2 is one textured quad that letterboxes to the new size immediately,
// which is exactly why its transition looks clean.
//
// The strip does NOT settle in one frame unless it is made to. A width change rescales the whole
// page column and scales contentY to hold the reading position. When that work was routed through
// the surface's 16ms viewport-report throttle, it landed 2 frames after the cover began lifting
// entering fullscreen and 5 frames after it leaving — this probe recorded contentY jumping
// 12000 -> 17067 in full view. The settle happened in FRONT of the cover instead of behind it.
//
// THE INVARIANT THIS GATE HOLDS: the reader's reflow must complete BEFORE the shield reveals, so
// the first frame Hemanth sees at the new size is already settled. Stated as an ordering assertion
// rather than a duration, so it cannot be satisfied by a machine simply being fast.
//
// NEGATIVE CONTROL: reverting ComicReaderStripSurface's `onWidthChanged` from
// _flushViewportReportNow() back to _scheduleReport() must make this gate FAIL on both transitions.
//
// The window must be VISIBLE and really change WIDTH: a hidden QQuickWindow never renders, and on a
// 1280x720 screen a 1280-wide window is already full width so only the height moves — the strip
// reflows on width only, and an earlier version of this probe recorded a falsely clean zero
// reflows for exactly that reason. Hence a deliberately narrow 900px windowed state.
//
// Run: qml.exe tests/comicreader_fullscreen_timing_probe.qml
// Prints the full event ledger, then COMICREADER_FULLSCREEN_OK / _FAIL.

import QtQuick
import QtQuick.Window
import "../qml"                       // FullscreenTransitionShield — the app's shared cover
import "../qml/comicreader"           // ComicReaderStripSurface — the real surface under test

Window {
    id: win
    // Deliberately NARROWER than the screen, so the flip genuinely changes WIDTH. See header.
    width: 900
    height: 600
    visible: true
    visibility: Window.Windowed
    color: "#000000"
    title: "Comic Reader — fullscreen timing gate"

    // ---------------- event ledger ----------------
    // seq is a strict monotonic event counter. The ordering assertion uses IT, not the frame number:
    // the reflow and the reveal legitimately land in the same frame, and "same frame" is a PASS only
    // when the reflow came first.
    property int seq: 0
    property int frameNo: 0
    property real t0: Date.now()
    property var failures: []
    function mark(what, detail) {
        win.seq += 1
        console.log("FSGATE seq=" + win.seq + " frame=" + win.frameNo
                    + " t=" + Math.round(Date.now() - win.t0) + "ms"
                    + " " + what + (detail ? ("  " + detail) : ""))
        return win.seq
    }
    function fail(msg) { failures.push(msg); console.log("COMICREADER_FULLSCREEN_FAIL: " + msg) }

    // per-transition record
    property int applySeq: -1
    property int reflowSeq: -1
    property int revealSeq: -1
    property real reflowContentY: -1
    property int firstFrameAfterRevealSeq: -1
    property string phase: ""          // "enter" | "leave"
    property int transitionsChecked: 0

    Connections {
        target: win
        function onFrameSwapped() {
            win.frameNo += 1
            if (!shield.transitioning && win._trailing <= 0) return
            if (win._trailing > 0) win._trailing -= 1
            var s = win.mark("frameSwapped", "winW=" + win.width + " listW=" + Math.round(strip.width)
                             + " contentY=" + Math.round(strip.contentY))
            // The FIRST frame presented after the cover starts lifting must already be settled.
            if (win.revealSeq >= 0 && win.firstFrameAfterRevealSeq < 0) {
                win.firstFrameAfterRevealSeq = s
                if (win.reflowSeq < 0)
                    win.fail(win.phase + ": no reflow had happened by the first revealed frame")
                else if (win.reflowContentY >= 0
                         && Math.round(strip.contentY) !== Math.round(win.reflowContentY))
                    win.fail(win.phase + ": first revealed frame shows contentY="
                             + Math.round(strip.contentY) + " but the reflow settled it at "
                             + Math.round(win.reflowContentY) + " — the settle is visible to the user")
            }
        }
    }
    property int _trailing: 0

    // ---------------- fake backend core (shape-parity with ComicReaderCore) ----------------
    QtObject {
        id: core
        property var stripModel: stripModelA
        property int setStripViewportWidthCalls: 0
        signal pageReady(int page)
        signal pageFailed(int page, string code)
        signal stripCompensation(real delta)
        signal entryChanged()
        signal pairingChanged()
        function imageUrl(page) { return "" }        // no real pixels needed to time the reflow
        function unitForPage(page) { return { rightIndex: page, leftIndex: -1, spread: false, coverAlone: false } }
        function pageInfo(page) { return { error: "" } }
        function setVisible(pages) {}
        function setStripViewport(top, height) {}
        function stripPageTop(page) { return page * 1220 }
        function stripPageAtCenter(top, h) { return Math.floor(top / 1220) }
        function setStripViewportWidth(w) {
            setStripViewportWidthCalls += 1
            // THE MOMENT UNDER TEST: the reader's column rescale. Recorded only while a transition
            // is live, so the initial-layout call does not masquerade as a transition reflow.
            if (!shield.transitioning) return
            win.reflowSeq = win.mark("REFLOW setStripViewportWidth", "w=" + Math.round(w))
            // contentY is scaled by the CALLER immediately after this returns, so it cannot be read
            // here — a zero-interval timer reads it once that assignment has happened.
            settleRead.restart()
        }
    }
    Timer { id: settleRead; interval: 0; repeat: false; onTriggered: win.reflowContentY = strip.contentY }

    ListModel { id: stripModelA }
    Component.onCompleted: {
        for (var i = 0; i < 200; i++)
            stripModelA.append({ pageIndex: i, top: i * 1220, displayWidth: 800, displayHeight: 1200,
                                 ready: true, errorCode: 0 })
        start.start()
    }

    ComicReaderStripSurface {
        id: strip
        anchors.fill: parent
        core: core
        active: true
    }

    FullscreenTransitionShield {
        id: shield
        anchors.fill: parent
        onApplyRequested: {
            win.applySeq = win.mark("APPLY (window mode flips)", "")
            win.visibility = (win.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
        }
        onAwaitingFrameChanged: {
            if (!shield.awaitingFrame && shield.transitioning) {
                win.revealSeq = win.mark("REVEAL begins (cover starts lifting)", "")
                win._trailing = 12
                // THE ORDERING ASSERTION.
                if (win.reflowSeq < 0)
                    win.fail(win.phase + ": cover began lifting before the reader reflowed at all")
                else if (win.reflowSeq > win.revealSeq)
                    win.fail(win.phase + ": reflow (seq " + win.reflowSeq + ") landed AFTER the cover "
                             + "began lifting (seq " + win.revealSeq + ") — the user watches it settle")
                win.transitionsChecked += 1
            }
        }
    }

    function beginTransition(which) {
        win.phase = which
        win.applySeq = -1; win.reflowSeq = -1; win.revealSeq = -1
        win.reflowContentY = -1; win.firstFrameAfterRevealSeq = -1
        win.mark("BEGIN " + which + "-fullscreen", "")
        shield.begin()
    }

    // ---------------- the run ----------------
    // Scroll first so contentY > 0: the width-ratio rescale only runs on a scrolled strip, which is
    // the real reading case and the one Hemanth sees.
    Timer {
        id: start
        interval: 600; repeat: false
        onTriggered: {
            strip.contentY = 12000
            win.mark("--- scrolled to contentY=12000 ---", "")
            enter.start()
        }
    }
    Timer { id: enter; interval: 300; repeat: false; onTriggered: win.beginTransition("enter") }
    Timer { id: leave; interval: 1600; repeat: false; running: true; onTriggered: win.beginTransition("leave") }
    Timer {
        interval: 3200; repeat: false; running: true
        onTriggered: {
            if (win.transitionsChecked !== 2)
                win.fail("expected 2 transitions to be checked, saw " + win.transitionsChecked
                         + " — the probe did not exercise what it claims to")
            if (core.setStripViewportWidthCalls < 3)
                win.fail("expected at least 3 width reports (initial + 2 flips), saw "
                         + core.setStripViewportWidthCalls + " — the window may not have resized")
            if (win.failures.length === 0) {
                console.log("COMICREADER_FULLSCREEN_OK")
                Qt.exit(0)
            } else {
                Qt.exit(1)
            }
        }
    }
}
