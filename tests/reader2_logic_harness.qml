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

            // 5. revealReducer — pointer-driven reveal; keys NEVER wake the chrome.
            var s0 = { awake: false, lastMove: 0, pinned: false }
            var sMove = L.revealReducer(s0, "move", 1000)
            check(sMove.awake === true && sMove.lastMove === 1000, "reveal: move wakes + stamps lastMove")
            var sTick1 = L.revealReducer(sMove, "tick", 2000)      // +1000ms (< 1800) still awake
            check(sTick1.awake === true, "reveal: tick +1000ms stays awake")
            var sTick2 = L.revealReducer(sMove, "tick", 3000)      // +2000ms (> 1800) hides
            check(sTick2.awake === false, "reveal: tick +2000ms past idle hides")
            var sPin = L.revealReducer(sMove, "panelOpen", 1200)
            check(sPin.pinned === true && sPin.awake === true, "reveal: panelOpen pins awake")
            var sPinTick = L.revealReducer(sPin, "tick", 6200)     // +5000ms but pinned
            check(sPinTick.awake === true, "reveal: pinned stays awake past idle")
            var sClose = L.revealReducer(sPin, "panelClose", 6200)
            check(sClose.pinned === false, "reveal: panelClose unpins")
            var sCloseTick = L.revealReducer(sClose, "tick", 8200) // +2000ms after close
            check(sCloseTick.awake === false, "reveal: after panelClose hides on idle")
            var sKey = L.revealReducer(sMove, "key", 9999)         // a key event is NOT routed here
            check(sKey.awake === sMove.awake && sKey.lastMove === sMove.lastMove
                  && sKey.pinned === sMove.pinned, "reveal: unknown/key event leaves state unchanged")

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
