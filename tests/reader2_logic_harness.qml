// reader2_logic_harness — headless proof of Reader2Logic.js (TASK 6): the pure
// resume-seam logic, no paper/bridge/network needed. Run:
//   qml.exe -platform offscreen tests/reader2_logic_harness.qml
// Verdict via console ("VERDICT: PASS/FAIL") + Qt.exit(0/1). Body is wrapped in
// try/catch because a thrown error HANGS offscreen instead of failing.
//
// [Agent 2 (Claude), biblio]
import QtQuick
import "../qml/reader2/Reader2Logic.js" as L

QtObject {
    Component.onCompleted: {
        var fails = 0
        function check(ok, what) {
            if (!ok) { console.log("FAIL " + what); fails++ }
            else       console.log("ok   " + what)
        }
        try {
            // 1. fresh record from empty prev — position + derived identity fields.
            var r1 = L.progressRecord({}, { cfi: "epubcfi(/6/2)", percent: 41, fraction: 0.41 }, "C:/x/y.epub")
            check(r1.locator.cfi === "epubcfi(/6/2)", "fresh: locator.cfi")
            check(r1.locator.fraction === 0.41, "fresh: locator.fraction")
            check(r1.percent === 41, "fresh: percent")
            check(r1.path === "C:/x/y.epub", "fresh: path derived from bookPath")
            check(r1.format === "epub", "fresh: format derived from extension")
            check(r1.mediaType === "book", "fresh: mediaType book")
            check(r1.finished === false, "fresh: not finished at 41%")

            // 2. read-modify-write PRESERVES unknown prev fields (bookMeta, chapter).
            var r2 = L.progressRecord({ bookMeta: { title: "T" }, chapter: 7 },
                                      { cfi: "epubcfi(/6/8)", percent: 50, fraction: 0.5 }, "C:/x/y.epub")
            check(r2.bookMeta.title === "T", "rmw: bookMeta.title preserved")
            check(r2.chapter === 7, "rmw: chapter preserved")
            check(r2.locator.cfi === "epubcfi(/6/8)", "rmw: locator.cfi updated")

            // 2b. existing locator sub-fields survive when relocated lacks them (no href in relocated).
            var r3 = L.progressRecord({ locator: { cfi: "old", href: "OEBPS/ch1.html", updatedAt: 111 } },
                                      { cfi: "epubcfi(/6/8)", percent: 10, fraction: 0.1 }, "C:/x/y.epub")
            check(r3.locator.href === "OEBPS/ch1.html", "rmw: locator.href preserved (relocated had none)")
            check(r3.locator.cfi === "epubcfi(/6/8)", "rmw: locator.cfi overwritten")

            // 2c. finished flips true at/over 100%.
            var r4 = L.progressRecord({}, { cfi: "epubcfi(/6/99)", percent: 100, fraction: 1 }, "C:/x/y.epub")
            check(r4.finished === true, "finished true at 100%")

            // 2d. caller-stamped updatedAt threads through into the locator.
            var r5 = L.progressRecord({}, { cfi: "c", percent: 5, fraction: 0.05, updatedAt: 1780000000000 }, "C:/x/y.epub")
            check(r5.locator.updatedAt === 1780000000000, "updatedAt: caller stamp preserved")

            // 3. resumeCfiOf — the position to open at.
            check(L.resumeCfiOf({ locator: { cfi: "epubcfi(/6/12)" } }) === "epubcfi(/6/12)", "resumeCfiOf: reads cfi")
            check(L.resumeCfiOf({}) === "", "resumeCfiOf: empty entry -> ''")
            check(L.resumeCfiOf({ locator: {} }) === "", "resumeCfiOf: no cfi -> ''")

            // 4. railState — pure view model for Task 7's rail.
            var rail = L.railState({ percent: 42, pageInChapter: 3, pagesInChapter: 10 }, 5)
            check(rail.fillPct === 42, "railState: fillPct = percent")
            check(rail.label.indexOf("3") >= 0 && rail.label.indexOf("10") >= 0, "railState: label carries page/total")
            var rail2 = L.railState({}, 0)
            check(rail2.fillPct === 0, "railState: empty -> fillPct 0")
            check(rail2.label === "", "railState: empty -> no label")

            // 5. revealReducer — comic-reader doctrine. The chrome wakes ONLY on a
            // deliberate reach (edge band → enterBar), the book-open beat / a toggle, or a
            // panel; there is NO "move" event, so body movement can never wake it.
            var s0 = { shown: false, lastActive: 0, pinned: false, frozen: false }

            // wake (book-open beat): shows; +1000ms tick still shown; +3500ms idle-hides.
            var sWake = L.revealReducer(s0, "wake", 0)
            check(sWake.shown === true, "reveal: wake shows the chrome")
            var sWakeT1 = L.revealReducer(sWake, "tick", 1000)     // +1000ms (< 3000)
            check(sWakeT1.shown === true, "reveal: tick +1000ms stays shown")
            var sWakeT2 = L.revealReducer(sWake, "tick", 3500)     // +3500ms (> 3000)
            check(sWakeT2.shown === false, "reveal: tick +3500ms past idle hides")

            // enterBar: shows + freezes; a tick far past idle can't hide while frozen;
            // exitBar keeps it shown but drops the freeze so the next idle tick hides.
            var sEnter = L.revealReducer(s0, "enterBar", 0)
            check(sEnter.shown === true && sEnter.frozen === true, "reveal: enterBar shows + freezes")
            var sEnterT = L.revealReducer(sEnter, "tick", 5000)    // +5000ms but frozen
            check(sEnterT.shown === true, "reveal: frozen survives a tick past idle")
            var sExit = L.revealReducer(sEnter, "exitBar", 0)
            check(sExit.shown === true && sExit.frozen === false, "reveal: exitBar keeps shown, drops freeze")
            var sExitT = L.revealReducer(sExit, "tick", 3500)      // +3500ms after leaving the band
            check(sExitT.shown === false, "reveal: after exitBar idle hides")

            // toggle: from shown → hides; from hidden → shows.
            var sTogOff = L.revealReducer(sWake, "toggle", 100)
            check(sTogOff.shown === false, "reveal: toggle from shown hides")
            var sTogOn = L.revealReducer(s0, "toggle", 100)
            check(sTogOn.shown === true, "reveal: toggle from hidden shows")

            // panelOpen pins shown (idle can't hide); panelClose unpins → idle hides.
            var sPin = L.revealReducer(s0, "panelOpen", 0)
            check(sPin.pinned === true && sPin.shown === true, "reveal: panelOpen pins shown")
            var sPinTick = L.revealReducer(sPin, "tick", 5000)     // +5000ms but pinned
            check(sPinTick.shown === true, "reveal: pinned stays shown past idle")
            var sClose = L.revealReducer(sPin, "panelClose", 5000)
            check(sClose.pinned === false, "reveal: panelClose unpins")
            var sCloseTick = L.revealReducer(sClose, "tick", 8500) // +3500ms after close
            check(sCloseTick.shown === false, "reveal: after panelClose idle hides")

            // THE REGRESSION (Hemanth's bug): a stray "move" — or any unknown event — on a
            // HIDDEN state leaves it unchanged. Body movement can NEVER wake the chrome.
            var sMove = L.revealReducer(s0, "move", 9999)
            check(sMove.shown === false && sMove.lastActive === s0.lastActive
                  && sMove.pinned === s0.pinned && sMove.frozen === s0.frozen,
                  "reveal: 'move' on a hidden state cannot wake (unchanged)")
            var sKey = L.revealReducer(sWake, "key", 9999)         // unknown event on a shown state
            check(sKey.shown === sWake.shown && sKey.lastActive === sWake.lastActive
                  && sKey.pinned === sWake.pinned && sKey.frozen === sWake.frozen,
                  "reveal: unknown/key event leaves state unchanged")

            // 6. railTicks — chapter marks: ascending fractions strictly inside (0,1).
            var tk = L.railTicks(null, 5)
            check(tk.length === 4, "railTicks: sections=5 -> 4 interior marks")
            var asc = true, inRange = true, prevF = -1
            for (var ti = 0; ti < tk.length; ti++) {
                if (!(tk[ti] > 0 && tk[ti] < 1)) inRange = false
                if (!(tk[ti] > prevF)) asc = false
                prevF = tk[ti]
            }
            check(inRange, "railTicks: all fractions in (0,1)")
            check(asc, "railTicks: strictly ascending")
            var tkToc = L.railTicks([{ fraction: 0.6 }, { fraction: 0.2 }, { fraction: 0.9 }], 0)
            check(tkToc.length === 3 && tkToc[0] === 0.2 && tkToc[2] === 0.9, "railTicks: explicit fractions sorted")
            check(L.railTicks([], 0).length === 0, "railTicks: empty -> []")

            // 7. authorText — normalize foliate metadata.author shapes.
            check(L.authorText({ author: "Herman Melville" }) === "Herman Melville", "authorText: string")
            check(L.authorText({ author: [{ name: "A" }, { name: "B" }] }) === "A, B", "authorText: array of {name}")
            check(L.authorText({ author: ["X", "Y"] }) === "X, Y", "authorText: array of strings")
            check(L.authorText({}) === "", "authorText: missing -> ''")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
