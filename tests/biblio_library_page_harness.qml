// Offscreen construct+contract proof of BiblioLibraryPage (plan 2026-08-06-biblio-library-tab-
// theatre-parity.md, Slice 2). Mirrors library_page_harness.qml + tankoban_library_page_harness.qml.
// Collection/Progress context properties are absent here — the typeof guards keep construction
// safe (allRows -> []). The action logic is exercised through handleCardAction so the harness
// drives it without a real delegate/menu. NEVER throw (hangs offscreen); collect fails,
// Qt.exit(fails.length).
import QtQuick
import "../qml" as UI

Item {
    UI.BiblioLibraryPage { id: p; objectName: "biblioLibraryPage"; width: 1280; height: 760 }
    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            // ── empty offscreen state (singletons absent) ──
            ok(p.allRows.length === 0, "allRows empty offscreen (no Collection singleton): " + p.allRows.length);
            ok(p.rowCount === 0, "rowCount === 0 offscreen: " + p.rowCount);
            ok(p.visibleCount === 0, "visibleCount === 0 offscreen: " + p.visibleCount);
            ok(p.query === "", "default query empty");
            ok(p.sortMode === "added", "default sortMode = 'added': " + p.sortMode);
            ok(p.stateFilter === "", "default stateFilter empty");

            // ── objectNames present (Lanista + harness address the page by name) ──
            ok(p.objectName === "biblioLibraryPage", "page objectName set");

            var wall = p._keyboardWallForTest;
            var wallKeys = p._keyboardControllerForTest;
            ok(wall !== null && wallKeys !== null, "Biblio wall and its keyboard owner are addressable");
            if (wall && wallKeys) {
                ok(wallKeys.identityForIndex !== null && wallKeys.indexForIdentity !== null,
                   "Biblio keyboard owner receives stable identity seams");
                ok(wallKeys.modelRevision !== undefined, "Biblio keyboard owner receives a model revision");
                ok(wall.keyboardRevealIndex !== undefined, "Biblio wall exposes its existing reveal seam for return");
            }

            // The retained Biblio wall exposes the route owner's semantic book
            // identity. This is the return contract used when a detail layer
            // closes after the selected row has moved or been removed.
            p.visibleRows = [
                { entry: { id: "book-a", title: "A" } },
                { entry: { id: "book-b", title: "B" } },
                { entry: { id: "book-c", title: "C" } }
            ];
            ok(p.keyboardIdentityAt(1) === "book-b", "Biblio wall identity resolves from the selected model row");
            ok(p.keyboardIndexForIdentity("book-c") === 2, "Biblio wall resolves a surviving book identity");
            p.visibleRows = [
                { entry: { id: "book-a", title: "A" } },
                { entry: { id: "book-c", title: "C" } }
            ];
            ok(p.keyboardIndexForIdentity("book-b") === -1,
               "Biblio wall reports a removed selected identity for nearest-peer fallback");

            // Exercise the owned GridView reveal seam with enough cards to require a
            // scroll, then reorder the selected identity to the first row. The final
            // reveal must win over the historical offset so the surviving card is visible.
            var roundTripRows = [];
            for (var cardIndex = 0; cardIndex < 16; ++cardIndex)
                roundTripRows.push({ entry: { id: "round-trip-" + cardIndex, title: "Round " + cardIndex } });
            p.visibleRows = roundTripRows;
            wall.currentIndex = 15;
            wall.contentY = 0;
            ok(wall.keyboardRevealIndex(15) === true,
               "Biblio wall reveal seam accepts a surviving offscreen identity");
            ok(wall.contentY > 0,
               "Biblio wall reveal moves the owner for a lower-row identity");
            var historicalOffset = wall.contentY;
            var reorderedRows = [roundTripRows[15]];
            for (var reorderIndex = 0; reorderIndex < roundTripRows.length - 1; ++reorderIndex)
                reorderedRows.push(roundTripRows[reorderIndex]);
            p.visibleRows = reorderedRows;
            ok(p.keyboardIndexForIdentity("round-trip-15") === 0,
               "Biblio wall resolves a surviving identity after reorder");
            wall.contentY = historicalOffset;
            var resolvedRoundTripIndex = p.keyboardIndexForIdentity("round-trip-15");
            ok(wall.keyboardRevealIndex(resolvedRoundTripIndex) === true,
               "Biblio wall reapplies the final reveal after reorder");
            ok(wall.currentIndex === 0 && wall.contentY <= historicalOffset,
               "Biblio wall leaves the reordered identity visible instead of restoring stale offset");

            // ── detailRequested signal contract: forwards the exact entry ──
            var gotDetail = null;
            p.detailRequested.connect(function (entry) { gotDetail = entry; });
            var fakeEntry = { id: "b1", title: "A Book", type: "book", world: "biblio" };
            var rowDetail = { entry: fakeEntry, canResume: false, progressRecord: null,
                              title: "A Book", author: "Anon" };
            p.handleCardAction(rowDetail, "detail");
            ok(gotDetail === fakeEntry, "handleCardAction(.,'detail') -> detailRequested fires with the exact entry");

            // ── resumeRequested signal contract: forwards the EXACT progress record ──
            var gotResume = null;
            p.resumeRequested.connect(function (record) { gotResume = record; });
            var progRecord = { id: "b1", kind: "book", progress: 0.3, updatedAt: 9000,
                               resume: { path: "C:/x.epub", book: { title: "A Book" } } };
            var rowResume = { entry: fakeEntry, canResume: true, progressRecord: progRecord,
                              title: "A Book", author: "Anon" };
            p.handleCardAction(rowResume, "resume");
            ok(gotResume === progRecord, "handleCardAction(.,'resume') -> resumeRequested fires with the EXACT progressRecord");

            // ── removeRequested signal contract: forwards the exact entry ──
            var gotRemove = null;
            p.removeRequested.connect(function (entry) { gotRemove = entry; });
            p.handleCardAction(rowDetail, "remove");
            ok(gotRemove === fakeEntry, "handleCardAction(.,'remove') -> removeRequested forwards the exact entry");

            // ── the primary-click branching: canResume true -> resume, false -> detail ──
            gotResume = null; gotDetail = null;
            p.handleCardAction(rowResume, "resume");      // a canResume row's primary action
            ok(gotResume === progRecord && gotDetail === null, "canResume row's action = resume (not detail)");
            gotResume = null; gotDetail = null;
            p.handleCardAction(rowDetail, "detail");      // an unmatched row's primary action
            ok(gotDetail === fakeEntry && gotResume === null, "unmatched row's action = detail (not resume)");

            // ── defensive: a canResume:false row never silently emits resume, even if asked ──
            gotResume = null; gotDetail = null;
            p.handleCardAction(rowDetail, "resume");      // asked for resume but canResume false
            ok(gotResume === null && gotDetail === fakeEntry,
               "asked 'resume' on a canResume:false row falls through to detail (no silent resume)");

            // ── null/undefined row is a safe no-op ──
            gotResume = null; gotDetail = null; gotRemove = null;
            p.handleCardAction(null, "detail");
            p.handleCardAction(undefined, "resume");
            ok(gotResume === null && gotDetail === null, "null/undefined row -> no-op (no signal fires)");

            // ── toggleStateFilter ──
            p.stateFilter = "";
            p.toggleStateFilter("inProgress");
            ok(p.stateFilter === "inProgress", "toggleStateFilter: '' -> 'inProgress' (set): " + p.stateFilter);
            p.toggleStateFilter("inProgress");
            ok(p.stateFilter === "", "toggleStateFilter: 'inProgress' -> '' (re-click clears): " + p.stateFilter);

            // ── openMenu / closeMenu round-trip ──
            ok(p.menuRow === null, "menu starts closed (menuRow null)");
            var menuRow = { entry: { id: "menu-id", title: "Menu" }, canResume: false };
            p.openMenu(menuRow, 100, 200);
            ok(p.menuRow === menuRow, "openMenu stores the row");
            ok(p.menuX === 100 && p.menuY === 200, "openMenu stores the anchor coords");
            ok(p.menuRowId === "menu-id", "menuRowId derives from menuRow.entry.id: " + p.menuRowId);
            p.closeMenu();
            ok(p.menuRow === null && p.menuRowId === "", "closeMenu clears menuRow + menuRowId");

            // ── sortMode is settable (the page binds visibleRows to it) ──
            p.sortMode = "lastRead";
            ok(p.sortMode === "lastRead", "sortMode settable to lastRead");
            p.sortMode = "az";
            ok(p.sortMode === "az", "sortMode settable to az");
            p.sortMode = "added";   // restore default

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("biblio_library_page_harness: ALL PASS\nBIBLIO_LIBRARY_PAGE_OK");
            Qt.exit(fails.length);
        }
    }
}
