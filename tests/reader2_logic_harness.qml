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

            // 3b. staleRelocate — cross-book generation guard (Part B1, hardened to gate
            // ready/error/searchResults/footnote too, not just relocated). STRICTLY-OLDER = stale:
            // a NEWER gen is a book switch to be ADOPTED (a fresh 'ready'), never dropped; a
            // missing / non-finite gen is NEVER stale (defensive: never suppress a real event).
            check(L.staleRelocate(5, 5) === false, "staleRelocate: same gen -> not stale")
            check(L.staleRelocate(4, 5) === true, "staleRelocate: older gen -> stale (drop)")
            check(L.staleRelocate(6, 5) === false, "staleRelocate: NEWER gen -> not stale (a 'ready' book switch is adopted)")
            check(L.staleRelocate(undefined, 5) === false, "staleRelocate: missing gen -> never stale (defensive)")
            check(L.staleRelocate(NaN, 5) === false, "staleRelocate: non-finite gen -> never stale")
            check(L.staleRelocate(0, -1) === false, "staleRelocate: first open vs initial -1 -> adopt (not stale)")
            check(L.staleRelocate(1, -1) === false, "staleRelocate: first ready (gen 1) vs initial -1 -> adopt")
            // Stale-event drops (Fix 1): a late event from a SUPERSEDED open (older gen) is dropped
            // — this is the ONE pure gate ReaderShell now applies to relocated AND ready/error/
            // searchResults/footnote. Book A's in-flight event (gen 1) after switching to B (gen 2)
            // is stale; B's own event (gen 2) is kept.
            check(L.staleRelocate(1, 2) === true, "staleRelocate: book A event (gen 1) after switch to B (gen 2) -> drop")
            check(L.staleRelocate(2, 2) === false, "staleRelocate: current book B's own event (gen 2) -> keep")

            // 3c. acceptBookEvent — the PRE-READY window gate (Codex re-review fix). Between
            // openBook(B) and B's 'ready', currentGen holds B's ISSUED gen (QML issues gens now),
            // so an old-book event gen-drops; unstamped events drop on bookReady alone:
            // NO book-scoped display/save event is acceptable until the current open's 'ready'.
            check(L.acceptBookEvent(1, 1, true) === true,  "acceptBookEvent: current book, ready -> accept")
            check(L.acceptBookEvent(1, 1, false) === false, "acceptBookEvent: same-gen event pre-ready -> drop (out of order)")
            check(L.acceptBookEvent(1, 2, true) === false, "acceptBookEvent: superseded gen after B's ready -> drop (gen guard)")
            check(L.acceptBookEvent(1, 2, false) === false, "acceptBookEvent: A's event after openBook(B) issued gen 2, pre-ready -> drop")
            check(L.acceptBookEvent(undefined, 5, true) === true, "acceptBookEvent: unstamped event while ready -> accept (defensive)")
            check(L.acceptBookEvent(undefined, 5, false) === false, "acceptBookEvent: unstamped event pre-ready -> drop (no book on screen)")

            // 3d. acceptReady — 'ready' adoption is DEAD (re-review #2: a queued intermediate
            // A 'ready' with a gen newer than anything adopted was itself adopted, restoring
            // bookReady mid-switch). QML now ISSUES the gen per open (openAtResume) and the glue
            // echoes it, so the only acceptable 'ready' is EXACTLY the open we asked for.
            check(L.acceptReady(2, 2) === true,  "acceptReady: the ready we asked for (issued gen) -> accept")
            check(L.acceptReady(1, 2) === false, "acceptReady: THE RACE — queued A ready (gen 1) after openBook(B) issued gen 2 -> drop")
            check(L.acceptReady(3, 2) === false, "acceptReady: unexpected future gen -> drop (never adopt)")
            check(L.acceptReady(undefined, 2) === true, "acceptReady: unstamped ready -> accept (defensive, pre-gen glue)")

            // 3e. errorDisposition (v2, QML-issued gens) — a failed OPEN never reaches 'ready',
            // so its error must SURFACE while bookReady is false; but only for the open we
            // actually asked for. currentGen is set at ISSUE time, so:
            //   gen !== currentGen  -> 'drop'        (superseded book's error, any window)
            //   gen === currentGen  -> pre-ready 'open-fail', post-ready 'operational'
            //   no finite gen       -> pre-ready 'open-fail' (boot failure), ready 'operational'
            check(L.errorDisposition(2, 2, false) === "open-fail", "errorDisposition: B's own failed open (issued gen) -> surface")
            check(L.errorDisposition(1, 1, false) === "open-fail", "errorDisposition: first-ever open fails -> surface")
            check(L.errorDisposition(1, 1, true) === "operational", "errorDisposition: current book op error (ready) -> trace")
            check(L.errorDisposition(1, 2, false) === "drop", "errorDisposition: THE RACE — A's error after openBook(B) issued gen 2 -> drop")
            check(L.errorDisposition(1, 2, true) === "drop", "errorDisposition: superseded error post-ready -> drop")
            check(L.errorDisposition(3, 2, false) === "drop", "errorDisposition: unexpected future gen -> drop (QML never issued it)")
            check(L.errorDisposition(undefined, 1, false) === "open-fail", "errorDisposition: unstamped pre-ready (boot failure) -> surface")
            check(L.errorDisposition(undefined, 1, true) === "operational", "errorDisposition: unstamped while ready -> trace")

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
            // ALL-OR-NOTHING: a MIXED toc (some entries resolve a fraction, some don't) must
            // NOT draw the sparse resolved subset — the whole set falls back to even spacing.
            var tkMixed = L.railTicks([{ fraction: 0.3 }, { label: "x" }, { fraction: 0.7 }], 0)
            check(tkMixed.length === 2 && Math.abs(tkMixed[0] - 1 / 3) < 1e-9 && Math.abs(tkMixed[1] - 2 / 3) < 1e-9,
                  "railTicks: mixed toc -> even spacing for the whole set (not the resolved subset)")
            // an out-of-range fraction (>=1) counts as a gap → whole set falls back too.
            var tkBad = L.railTicks([{ fraction: 0.5 }, { fraction: 1.0 }], 0)
            check(tkBad.length === 1 && Math.abs(tkBad[0] - 0.5) < 1e-9, "railTicks: out-of-range fraction -> even fallback")
            // EVERY entry resolves → the true-fraction fast-path (sorted).
            var tkAll = L.railTicks([{ fraction: 0.25 }, { fraction: 0.5 }, { fraction: 0.75 }], 0)
            check(tkAll.length === 3 && tkAll[0] === 0.25 && tkAll[2] === 0.75, "railTicks: every entry has fraction -> real ticks")

            // 6b. selectionMenuPos — clamp the popover card inside the frame; above else below.
            // above the selection when there's room (sel high on the page):
            var pA = L.selectionMenuPos({ x: 500, y: 400, w: 120, h: 20 }, 1280, 720, 180, 44, 12, 10)
            check(pA.y === 400 - 44 - 12, "selMenuPos: placed ABOVE when room (y = sy - cardH - gap)")
            check(pA.x === 500 + 60 - 90, "selMenuPos: horizontally centered on the selection")
            // no room above (sel near the top) → placed BELOW.
            var pB = L.selectionMenuPos({ x: 500, y: 4, w: 120, h: 20 }, 1280, 720, 180, 44, 12, 10)
            check(pB.y === 4 + 20 + 12, "selMenuPos: placed BELOW when no room above")
            // far-right selection → x clamped so the card never spills past the right margin.
            var pC = L.selectionMenuPos({ x: 1270, y: 400, w: 8, h: 20 }, 1280, 720, 180, 44, 12, 10)
            check(pC.x === 1280 - 180 - 10, "selMenuPos: right edge clamps x to frameW - cardW - margin")
            // far-left selection → x clamped to the left margin.
            var pD = L.selectionMenuPos({ x: 0, y: 400, w: 8, h: 20 }, 1280, 720, 180, 44, 12, 10)
            check(pD.x === 10, "selMenuPos: left edge clamps x to margin")
            var pNull = L.selectionMenuPos(null, 1280, 720, 180, 44, 12, 10)
            check(Number.isFinite(pNull.x) && Number.isFinite(pNull.y), "selMenuPos: null-safe (finite x,y)")

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

            // 11. stripTags — HTML → plain text (dict definitions + footnote fragments).
            check(L.stripTags("<b>hello</b> <i>world</i>") === "hello world", "stripTags: drops tags")
            check(L.stripTags("a&amp;b &lt;c&gt; &quot;d&quot;") === 'a&b <c> "d"', "stripTags: decodes entities")
            check(L.stripTags("  spaced   \n out  ") === "spaced out", "stripTags: collapses whitespace")
            check(L.stripTags("caf&#233;") === "café", "stripTags: numeric entity")
            check(L.stripTags(null) === "", "stripTags: null-safe")
            check(L.stripTags("<a href='x'>link</a> text") === "link text", "stripTags: anchor with attrs")
            check(L.stripTags("suddenly<style>.mw-parser-output .defdate{font-size:smaller}</style>") === "suddenly",
                  "stripTags: drops <style> block AND its CSS content (Wiktionary defdate)")
            check(L.stripTags("a<script>var x=1<3;</script>b") === "a b",
                  "stripTags: drops <script> block AND its content")

            // 12. firstWord — the single word to define (Wiktionary REST is single-word).
            check(L.firstWord("whale") === "whale", "firstWord: single word")
            check(L.firstWord("  the great whale ") === "the", "firstWord: first of many, trimmed")
            check(L.firstWord("“whale,”") === "whale", "firstWord: strips curly quotes + comma")
            check(L.firstWord("(portentous)") === "portentous", "firstWord: strips parens")
            check(L.firstWord("well-being of") === "well-being", "firstWord: keeps internal hyphen")
            check(L.firstWord("don't") === "don't", "firstWord: keeps internal apostrophe")
            check(L.firstWord("   ") === "", "firstWord: whitespace-only -> ''")
            check(L.firstWord(null) === "", "firstWord: null-safe")

            // 13. dictParse — Wiktionary REST JSON → normalized entries (HTML stripped).
            var djson = JSON.stringify({
                en: [
                    { partOfSpeech: "Noun", definitions: [
                        { definition: "A very large <a href='x'>marine mammal</a>." },
                        { definition: "" },
                        { definition: "Something &amp; big." } ] },
                    { partOfSpeech: "Verb", definitions: [ { definition: "To hunt whales." } ] }
                ]
            })
            var parsed = L.dictParse(djson)
            check(parsed.length === 2, "dictParse: two POS entries")
            check(parsed[0].partOfSpeech === "Noun", "dictParse: partOfSpeech")
            check(parsed[0].definitions.length === 2, "dictParse: drops empty definition")
            check(parsed[0].definitions[0] === "A very large marine mammal.", "dictParse: strips HTML in definition")
            check(parsed[0].definitions[1] === "Something & big.", "dictParse: decodes entity in definition")
            check(parsed[1].definitions[0] === "To hunt whales.", "dictParse: second entry")
            // accepts an already-parsed object too, not just a JSON string.
            check(L.dictParse({ en: [{ partOfSpeech: "Noun", definitions: [{ definition: "x" }] }] }).length === 1,
                  "dictParse: accepts a parsed object")
            check(L.dictParse("not json").length === 0, "dictParse: bad JSON -> []")
            check(L.dictParse({ fr: [] }).length === 0, "dictParse: no 'en' -> []")
            check(L.dictParse(null).length === 0, "dictParse: null -> []")
            // an entry whose definitions are all empty is dropped entirely.
            check(L.dictParse({ en: [{ partOfSpeech: "Noun", definitions: [{ definition: "" }] }] }).length === 0,
                  "dictParse: entry with only-empty definitions dropped")

            // 14. appearance model (Task 10) — pure knobs over the paper.
            // defaults: the ratified starting point (Literata is the default typeface now).
            var ad = L.appearanceDefaults()
            check(ad.theme === "night", "appearanceDefaults: theme night")
            check(ad.font === "literata", "appearanceDefaults: font literata")
            check(ad.sizePct === 100 && ad.marginPx === 72, "appearanceDefaults: size + margins")
            check(ad.lineHeight === 1.6 && ad.justify === true, "appearanceDefaults: lineHeight + justify")
            check(ad.rulerOn === false && ad.rulerHeightPx === 92 && ad.rulerDimPct === 42,
                  "appearanceDefaults: ruler controls")

            // themeColors: each of the four names → the mock's bg/fg.
            check(L.themeColors("paper").bg === "#e9e4d8" && L.themeColors("paper").fg === "#3a362c", "themeColors: paper")
            check(L.themeColors("sepia").bg === "#e5d5b8" && L.themeColors("sepia").fg === "#4a3f2c", "themeColors: sepia")
            check(L.themeColors("slate").bg === "#232830" && L.themeColors("slate").fg === "#c6cdd8", "themeColors: slate")
            check(L.themeColors("night").bg === "#111013" && L.themeColors("night").fg === "#eee9de", "themeColors: night")
            check(L.themeColors("nonsense").bg === "#111013", "themeColors: unknown -> night")

            // fontFamilyFor: name → the CSS family the glue applies.
            check(L.fontFamilyFor("literata") === "Literata", "fontFamilyFor: literata -> Literata")
            check(L.fontFamilyFor("fraunces") === "Fraunces", "fontFamilyFor: fraunces -> Fraunces")
            check(L.fontFamilyFor("inter") === "Inter", "fontFamilyFor: inter -> Inter")
            check(L.fontFamilyFor("book") === "book", "fontFamilyFor: book stays 'book'")
            check(L.fontFamilyFor("weird") === "book", "fontFamilyFor: unknown -> book")

            // appearanceToPaper: shapes the glue payload + CLAMPS the numeric fields.
            var pPay = L.appearanceToPaper({ theme: "sepia", font: "literata", sizePx: 20, lineHeight: 1.8, marginPx: 100, justify: false })
            check(pPay.theme.bg === "#e5d5b8" && pPay.theme.fg === "#4a3f2c", "appearanceToPaper: theme -> colors")
            check(pPay.font === "Literata", "appearanceToPaper: font -> family")
            check(pPay.sizePx === 20 && pPay.lineHeight === 1.8 && pPay.marginPx === 100, "appearanceToPaper: passes through in-range")
            check(pPay.justify === false, "appearanceToPaper: justify bool")
            // clamps: below-min and above-max on every numeric field.
            var pLo = L.appearanceToPaper({ theme: "night", font: "book", sizePx: 4, lineHeight: 0.5, marginPx: 5 })
            check(pLo.sizePx === 12 && pLo.lineHeight === 1.2 && pLo.marginPx === 24, "appearanceToPaper: clamps to the floors")
            var pHi = L.appearanceToPaper({ theme: "night", font: "book", sizePx: 99, lineHeight: 9, marginPx: 999 })
            check(pHi.sizePx === 26 && pHi.lineHeight === 2.2 && pHi.marginPx === 160, "appearanceToPaper: clamps to the ceilings")

            // flow (2026-07-20): 'scrolled' passes through; anything else — including the
            // legacy stored appearance with NO flow key — normalizes to 'paginated'.
            check(L.appearanceDefaults().flow === "paginated", "flow: default is paginated")
            check(L.appearanceToPaper({ theme: "night", flow: "scrolled" }).flow === "scrolled", "flow: scrolled passes through")
            check(L.appearanceToPaper({ theme: "night" }).flow === "paginated", "flow: missing key -> paginated")
            check(L.appearanceToPaper({ theme: "night", flow: "banana" }).flow === "paginated", "flow: junk -> paginated")

            // 15. appearance PARITY (2026-07-24) — store shape, migration, per-book merge.
            var d2 = L.appearanceDefaults()
            check(d2.sizePct === 100 && d2.sizePx === undefined, "defaults: sizePct 100, sizePx gone")
            check(d2.fontWeight === 400 && d2.wordSpacing === 0 && d2.letterSpacing === 0, "defaults: weight + spacings")
            check(d2.paraSpacing === 0 && d2.paraIndent === "book", "defaults: para spacing + indent")
            check(d2.maxLineWidthPx === 960 && d2.hyphens === false && d2.columns === "single", "defaults: measure + hyphens + columns")
            check(d2.customPage === "#111214" && d2.customInk === "#c9c5bc" && d2.customCss === "", "defaults: custom theme fields")
            check(d2.invertImages === true, "defaults: invertImages on")

            // migrateAppearance: legacy sizePx converts to sizePct (18px -> 100%), 5%-quantized.
            check(L.migrateAppearance({ sizePx: 18 }).sizePct === 100, "migrate: 18px -> 100%")
            check(L.migrateAppearance({ sizePx: 22 }).sizePct === 120, "migrate: 22px -> 120% (quantized)")
            check(L.migrateAppearance({ sizePx: 12 }).sizePct === 65, "migrate: 12px -> 65%")
            check(L.migrateAppearance({ sizePct: 150, sizePx: 18 }).sizePct === 150, "migrate: existing sizePct wins")
            check(L.migrateAppearance({ theme: "sepia" }).maxLineWidthPx === 960, "migrate: new keys filled from defaults")
            check(L.migrateAppearance({ theme: "sepia" }).theme === "sepia", "migrate: old keys preserved")

            // appearanceStore: the three store births.
            var stFresh = L.appearanceStore({})
            check(stFresh.defaults.theme === "night" && JSON.stringify(stFresh.books) === "{}", "store: fresh -> defaults + empty books")
            var stLegacy = L.appearanceStore({ reader2: { theme: "sepia", sizePx: 22, marginPx: 100 } })
            check(stLegacy.defaults.theme === "sepia" && stLegacy.defaults.sizePct === 120, "store: legacy flat migrates into defaults")
            check(stLegacy.defaults.marginPx === 100 && stLegacy.defaults.fontWeight === 400, "store: legacy keeps old, gains new")
            var stNew = L.appearanceStore({ reader2: { defaults: { theme: "paper", sizePct: 110 }, books: { b1: { justify: false } } } })
            check(stNew.defaults.theme === "paper" && stNew.defaults.sizePct === 110, "store: new shape passes through")
            check(stNew.books.b1.justify === false, "store: books preserved")

            // effectiveAppearance: defaults overlaid by the book's sparse patch.
            var stE = { defaults: L.appearanceDefaults(), books: { b1: { theme: "paper", sizePct: 150 } } }
            var eff = L.effectiveAppearance(stE, "b1")
            check(eff.theme === "paper" && eff.sizePct === 150, "effective: book patch wins")
            check(eff.fontWeight === 400, "effective: untouched keys fall back to defaults")
            check(L.effectiveAppearance(stE, "b2").theme === "night", "effective: unpatched book == defaults")
            check(L.effectiveAppearance(stE, "").theme === "night", "effective: empty bookId == defaults")

            // applyStorePatch: book key -> books[bookId]; GLOBAL key -> defaults.
            var stP = L.applyStorePatch({ defaults: L.appearanceDefaults(), books: {} }, "b1", "sizePct", 130)
            check(stP.books.b1.sizePct === 130, "patch: book key lands in books[b1]")
            check(stP.defaults.sizePct === 100, "patch: defaults untouched by a book edit")
            var stG = L.applyStorePatch(stP, "b1", "rulerOn", true)
            check(stG.defaults.rulerOn === true && stG.books.b1.rulerOn === undefined, "patch: GLOBAL key lands in defaults")
            var stG2 = L.applyStorePatch(stG, "b1", "invertImages", false)
            check(stG2.defaults.invertImages === false, "patch: invertImages is GLOBAL")
            check(L.isGlobalAppearanceKey("readAlong") === true && L.isGlobalAppearanceKey("theme") === false, "isGlobalAppearanceKey")
            var stNoBook = L.applyStorePatch({ defaults: L.appearanceDefaults(), books: {} }, "", "sizePct", 130)
            check(stNoBook.defaults.sizePct === 130 && JSON.stringify(stNoBook.books) === "{}", "patch: empty bookId -> defaults, no book patch")

            // useAsDefaultStore: effective becomes defaults; this book's patch clears; others keep theirs.
            var stU = { defaults: L.appearanceDefaults(), books: { b1: { theme: "paper", sizePct: 150 }, b2: { justify: false } } }
            var stU2 = L.useAsDefaultStore(stU, "b1")
            check(stU2.defaults.theme === "paper" && stU2.defaults.sizePct === 150, "useAsDefault: effective -> defaults")
            check(stU2.books.b1 === undefined, "useAsDefault: this book's patch cleared")
            check(stU2.books.b2.justify === false, "useAsDefault: other books keep their tuning")

            // resetBookStore: delete this book's patch only.
            var stR = L.resetBookStore(stU, "b1")
            check(stR.books.b1 === undefined && stR.books.b2.justify === false, "resetBook: only b1 cleared")
            check(stR.defaults.theme === "night", "resetBook: defaults untouched")

            // initialAppearance still works for old callers (routes through the store now).
            check(L.initialAppearance({ reader2: { theme: "sepia" } }).theme === "sepia", "initialAppearance: legacy compat kept")

            // mergeAppearance: patches ONE key, keeps the rest (pure new object).
            var mBase = L.appearanceDefaults()
            var mNext = L.mergeAppearance(mBase, { theme: "paper" })
            check(mNext.theme === "paper", "mergeAppearance: patched key applied")
            check(mNext.font === "literata" && mNext.sizePct === 100 && mNext.marginPx === 72, "mergeAppearance: other keys intact")
            check(mBase.theme === "night", "mergeAppearance: original not mutated (pure)")
            check(L.mergeAppearance(null, { sizePx: 22 }).sizePx === 22, "mergeAppearance: null prev -> patch only")

            // initialAppearance: reads back settings.reader2, else courtesy-seeds theme from the
            // old flat `theme`, else the ratified default — the store-boundary logic.
            var iRead = L.initialAppearance({ theme: "sepia", reader2: { theme: "slate", sizePx: 22 } })
            check(iRead.theme === "slate" && iRead.sizePct === 120, "initialAppearance: reader2 sub-object wins")
            check(iRead.font === "literata", "initialAppearance: reader2 merged over defaults (missing key filled)")
            var iSeed = L.initialAppearance({ theme: "sepia" })      // old flat key only, first run
            check(iSeed.theme === "sepia", "initialAppearance: first run courtesy-seeds theme from old flat key")
            check(iSeed.font === "literata" && iSeed.sizePct === 100, "initialAppearance: seed keeps the rest of the defaults")
            check(L.initialAppearance({ theme: "not-a-theme" }).theme === "night", "initialAppearance: unknown old theme -> default night")
            check(L.initialAppearance({}).theme === "night", "initialAppearance: empty settings -> default night")
            check(L.initialAppearance(null).theme === "night", "initialAppearance: null-safe -> default night")

            // appearanceDefaults now carries the ruler band POSITION (Task 11).
            check(ad.rulerYPct === 40, "appearanceDefaults: rulerYPct default 40")

            // 15. search-row shaping (Task 11) — escape + gold-mark the excerpt pieces.
            var ex = { pre: "the great ", match: "whale", post: " himself" }
            var se = L.searchExcerpt(ex)
            check(se.pre === "the great " && se.match === "whale" && se.post === " himself", "searchExcerpt: passes {pre,match,post}")
            var seBare = L.searchExcerpt("just a string")
            check(seBare.pre === "" && seBare.match === "just a string" && seBare.post === "", "searchExcerpt: bare string -> all in match")
            var seNull = L.searchExcerpt(null)
            check(seNull.pre === "" && seNull.match === "" && seNull.post === "", "searchExcerpt: null-safe")
            // searchRowStyled: order preserved, match wrapped in the passed-in gold color.
            var styled = L.searchRowStyled(ex, "#F0C24A")
            check(styled === 'the great <font color="#F0C24A">whale</font> himself', "searchRowStyled: pre + gold match + post")
            // HTML in any piece is ESCAPED so it can't break the StyledText markup.
            var styledEsc = L.searchRowStyled({ pre: "a < b & ", match: "<x>", post: ' "y"' }, "#F0C24A")
            check(styledEsc === 'a &lt; b &amp; <font color="#F0C24A">&lt;x&gt;</font> &quot;y&quot;', "searchRowStyled: escapes < & > \" in every piece")
            // gold color comes from the caller (QML passes Theme.gold); default when absent.
            check(L.searchRowStyled({ pre: "", match: "m", post: "" }).indexOf('color="#F0C24A"') >= 0, "searchRowStyled: default gold when no color passed")
            check(L.searchRowStyled({ pre: "", match: "m", post: "" }, "#abcdef") === '<font color="#abcdef">m</font>', "searchRowStyled: uses the passed color")

            // searchCountText: "N results" / singular / "300+ results" when capped.
            check(L.searchCountText(28, false) === "28 results", "searchCountText: plural")
            check(L.searchCountText(1, false) === "1 result", "searchCountText: singular")
            check(L.searchCountText(0, false) === "0 results", "searchCountText: zero")
            check(L.searchCountText(300, true) === "300+ results", "searchCountText: capped -> N+ results")

            // 16. rulerGeometry (Task 11) — band + scrim layout, clamped on-screen.
            var g1 = L.rulerGeometry(40, 92, 720)
            check(Math.abs(g1.bandTop - 288) < 1e-9, "rulerGeometry: bandTop = yPct% of height (40% of 720)")
            check(g1.bandHeight === 92, "rulerGeometry: bandHeight = heightPx")
            check(Math.abs(g1.topScrimH - 288) < 1e-9, "rulerGeometry: topScrimH = bandTop")
            check(Math.abs(g1.botScrimH - (720 - 288 - 92)) < 1e-9, "rulerGeometry: botScrimH fills below the band")
            // top scrim + band + bottom scrim == full height (no gaps/overlap).
            check(Math.abs(g1.topScrimH + g1.bandHeight + g1.botScrimH - 720) < 1e-9, "rulerGeometry: pieces tile the whole height")
            // yPct near the bottom clamps the band fully on-screen (bandTop = H - heightPx).
            var g2 = L.rulerGeometry(100, 92, 720)
            check(g2.bandTop === 720 - 92 && g2.botScrimH === 0, "rulerGeometry: yPct=100 clamps band to the bottom")
            // yPct at the top → no top scrim.
            var g3 = L.rulerGeometry(0, 92, 720)
            check(g3.bandTop === 0 && g3.topScrimH === 0, "rulerGeometry: yPct=0 -> band at top, no top scrim")
            // a band taller than the overlay is clamped to the overlay height.
            var g4 = L.rulerGeometry(40, 5000, 720)
            check(g4.bandHeight === 720 && g4.bandTop === 0, "rulerGeometry: band taller than overlay clamps to full height")
            // zero / non-finite height is null-safe (no NaN).
            var g5 = L.rulerGeometry(40, 92, 0)
            check(Number.isFinite(g5.bandTop) && Number.isFinite(g5.botScrimH), "rulerGeometry: zero height is finite-safe")

            // 17. chapterFor (Task 13) — read-along chapter matching, three tiers.
            var bToc = [ { label: "Chapter 1" }, { label: "Chapter 2" },
                         { label: "Chapter 3" }, { label: "Chapter 4" } ]
            var aCh  = [ { label: "1. Loomings" }, { label: "2. The Carpet-Bag" },
                         { label: "3. The Spouter-Inn" }, { label: "4. The Counterpane" } ]
            // exact chapter-NUMBER title match: book "Chapter 3" ↔ audio "3. The Spouter-Inn".
            check(L.chapterFor(2, bToc, aCh) === 2, "chapterFor: number/title match (Chapter 3 -> '3. ...')")
            check(L.chapterFor(0, bToc, aCh) === 0, "chapterFor: number/title match (Chapter 1 -> '1. ...')")
            // TEXT title match — audio order swapped, paired by normalized title (no numbers).
            var bToc2 = [ { label: "Loomings" }, { label: "The Spouter-Inn" } ]
            var aCh2  = [ { label: "The Spouter-Inn" }, { label: "Loomings" } ]
            check(L.chapterFor(0, bToc2, aCh2) === 1, "chapterFor: text title match finds swapped order (Loomings)")
            check(L.chapterFor(1, bToc2, aCh2) === 0, "chapterFor: text title match (The Spouter-Inn -> idx 0)")
            // ORDINAL fallback — titles share nothing, counts equal → same index.
            var bToc3 = [ { label: "Alpha" }, { label: "Beta" }, { label: "Gamma" } ]
            var aCh3  = [ { label: "Track One" }, { label: "Track Two" }, { label: "Track Three" } ]
            check(L.chapterFor(1, bToc3, aCh3) === 1, "chapterFor: ordinal fallback (no title match, same index)")
            check(L.chapterFor(2, bToc3, aCh3) === 2, "chapterFor: ordinal fallback (last index)")
            // PROPORTIONAL fallback — book has MORE chapters than the audiobook.
            var bToc4 = []; for (var bi = 0; bi < 10; bi++) bToc4.push({ label: "C" + bi })
            var aCh4  = [ { label: "P1" }, { label: "P2" }, { label: "P3" }, { label: "P4" } ]
            check(L.chapterFor(8, bToc4, aCh4) === 3, "chapterFor: proportional (book 8/10 -> audio 3/4)")
            check(L.chapterFor(3, bToc4, aCh4) === 3, "chapterFor: ordinal wins while index is in audio range (book 3 -> audio 3)")
            check(L.chapterFor(4, bToc4, aCh4) === 1, "chapterFor: proportional once past audio range (book 4/10 -> audio 1/4)")
            // ROMAN numerals normalize: book "Chapter IV" ↔ audio "4 — ...".
            check(L.chapterFor(0, [ { label: "Chapter IV" } ],
                               [ { label: "1" }, { label: "2" }, { label: "3" }, { label: "4 — The Counterpane" } ]) === 3,
                  "chapterFor: roman 'Chapter IV' -> audio '4 ...'")
            // string entries (not objects) are tolerated.
            check(L.chapterFor(1, [ "Chapter 1", "Chapter 2" ], [ "1 a", "2 b", "3 c" ]) === 1,
                  "chapterFor: bare-string entries pair by number")
            // degenerate / empty inputs are safe.
            check(L.chapterFor(0, bToc, []) === -1, "chapterFor: no audio chapters -> -1")
            check(L.chapterFor(-1, bToc, aCh) === -1, "chapterFor: unknown book chapter (-1) -> -1")
            check(L.chapterFor(0, null, aCh) === 0, "chapterFor: null bookToc -> ordinal (index 0)")
            check(L.chapterFor(5, [], aCh) === 3, "chapterFor: empty toc, i beyond audio -> proportional clamps to last")

            // 18. Audio-card / transport text shapers (Task 13).
            check(L.audiobookMetaLine(135, 0) === "135 chapters", "audiobookMetaLine: chapters only when no duration")
            check(L.audiobookMetaLine(1, 0) === "1 chapter", "audiobookMetaLine: singular chapter")
            check(L.audiobookMetaLine(135, 75840) === "21 h 04 m · 135 chapters", "audiobookMetaLine: duration + chapters")
            check(L.audiobookMetaLine(3, 600) === "10 min · 3 chapters", "audiobookMetaLine: sub-hour duration")
            check(L.audiobookTimeLine("Chapter 1 — Loomings", 252, 1350) === "Chapter 1 — Loomings · 4:12 / 22:30",
                  "audiobookTimeLine: chapter + times")
            check(L.audiobookTimeLine("", 0, 0) === "", "audiobookTimeLine: empty when nothing known")
            check(L.audiobookTimeLine("Only label", 0, 0) === "Only label", "audiobookTimeLine: label alone when no duration")
            check(L.speedLabel(1) === "1.0×", "speedLabel: 1 -> 1.0×")
            check(L.speedLabel(1.5) === "1.5×", "speedLabel: 1.5 -> 1.5×")
            check(L.speedLabel(1.25) === "1.25×", "speedLabel: 1.25 -> 1.25×")

            console.log(fails ? "VERDICT: FAIL" : "VERDICT: PASS")
            Qt.exit(fails ? 1 : 0)
        } catch (e) {
            console.log("VERDICT: FAIL (threw) " + e)
            Qt.exit(1)
        }
    }
}
