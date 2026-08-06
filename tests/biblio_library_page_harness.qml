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
