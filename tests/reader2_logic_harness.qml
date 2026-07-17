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

            // 8. tocRowState — Contents row dimming (index-based, from relocated.tocIndex).
            check(L.tocRowState(0, 2) === "read", "tocRowState: before current -> read")
            check(L.tocRowState(2, 2) === "current", "tocRowState: at current -> current")
            check(L.tocRowState(3, 2) === "unread", "tocRowState: after current -> unread")
            check(L.tocRowState(0, -1) === "unread", "tocRowState: unknown current (-1) -> all unread")
            check(L.tocRowState(5, undefined) === "unread", "tocRowState: undefined current -> unread")

            // 9. bookmarkRow — tolerant of the old-reader shape AND reader2's write shape.
            var bmNew = L.bookmarkRow({ id: "b1", locator: { cfi: "epubcfi(/6/4)", href: "h", fraction: 0.2 },
                                       label: "Loomings", snippet: "Page 4 of 18", page: 4 })
            check(bmNew.id === "b1", "bookmarkRow: id")
            check(bmNew.cfi === "epubcfi(/6/4)", "bookmarkRow: cfi from locator")
            check(bmNew.where === "Loomings", "bookmarkRow: where = label")
            check(bmNew.snippet === "Page 4 of 18", "bookmarkRow: snippet detail line")
            // old-reader record where label === snippet: don't render the same string twice.
            var bmOld = L.bookmarkRow({ id: "b2", locator: { cfi: "c" }, label: "Ch 1 · 41%", snippet: "Ch 1 · 41%" })
            check(bmOld.where === "Ch 1 · 41%" && bmOld.snippet === "", "bookmarkRow: label==snippet -> no duplicate snippet")
            var bmEmpty = L.bookmarkRow(null)
            check(bmEmpty.cfi === "" && bmEmpty.where === "" && bmEmpty.snippet === "", "bookmarkRow: null-safe")

            // 10. highlightRow — old annotations.json shape + a value/locator variant.
            var hl = L.highlightRow({ id: "a1", cfi: "epubcfi(/6/8)", text: "Call me Ishmael.",
                                     color: "#FEF3BD", note: "famous line", chapterLabel: "Loomings" })
            check(hl.id === "a1", "highlightRow: id")
            check(hl.cfi === "epubcfi(/6/8)", "highlightRow: cfi")
            check(hl.where === "Loomings", "highlightRow: where = chapterLabel")
            check(hl.text === "Call me Ishmael.", "highlightRow: text quote")
            check(hl.note === "famous line", "highlightRow: note")
            check(hl.color === "#FEF3BD", "highlightRow: color")
            var hlValue = L.highlightRow({ id: "a2", value: "epubcfi(/6/9)", text: "t" })
            check(hlValue.cfi === "epubcfi(/6/9)", "highlightRow: cfi from value field")
            var hlEmpty = L.highlightRow(null)
            check(hlEmpty.cfi === "" && hlEmpty.text === "" && hlEmpty.note === "", "highlightRow: null-safe")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
