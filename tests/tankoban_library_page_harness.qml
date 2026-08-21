// Offscreen construct proof of TankobanLibraryTab. TB-001 covered the typeof-guarded
// construction + detailRequested contract; TB-002 adds resumeRequested + the tap branch
// (started → resume, unstarted → details). Collection/Progress context properties are
// absent here — the typeof guards keep construction safe (allRows -> []). The tap branch
// is driven through handleCardTap so the harness can exercise it without a real delegate.
// NEVER throw (hangs offscreen); collect fails, Qt.exit(fails.length).
import QtQuick
import "../qml" as UI

Item {
    UI.TankobanLibraryTab { id: p; width: 1280; height: 760 }
    Timer {
        interval: 300; running: true; repeat: false
        onTriggered: {
            var fails = [];
            function ok(cond, label) { if (!cond) fails.push(label); }

            ok(p.allRows.length === 0, "allRows empty offscreen (no Collection singleton): " + p.allRows.length);

            // ─────────────────────────────────────────────────────────────────────
            // TB-001 regression: detailRequested signal contract — fires with exactly
            // the entry it's given.
            // ─────────────────────────────────────────────────────────────────────
            var received = null;
            p.detailRequested.connect(function (entry) { received = entry; });
            var fakeEntry = { id: "naruto-id", title: "Naruto", type: "manga" };
            p.detailRequested(fakeEntry);
            ok(received === fakeEntry, "detailRequested forwards the exact entry object");

            // ─────────────────────────────────────────────────────────────────────
            // TB-002: resumeRequested signal + tap branch
            // ─────────────────────────────────────────────────────────────────────

            // resumeRequested signal contract: fires with the EXACT record it's given.
            var gotResume = null;
            var gotDetail = null;
            p.resumeRequested.connect(function (record) { gotResume = record; });
            p.detailRequested.connect(function (entry) { gotDetail = entry; });

            // --- a STARTED row's tap emits resumeRequested with the exact resumeTarget ---
            var progRecord = { id: "berserk-id", kind: "manga", title: "Berserk",
                               progress: 0.42, updatedAt: 9999,
                               resume: { chapterId: "ch-357", page: 3 } };
            var startedRow = { entry: { id: "berserk-id", title: "Berserk", type: "manga" },
                               mediaType: "manga", state: "inProgress", progress: 0.42,
                               resumeTarget: progRecord, resumeLane: "manga",
                               lastActivityAt: 9999, addedAt: 1000,
                               downloaded: false, downloadSeriesKey: "" };
            gotResume = null; gotDetail = null;
            p.handleCardTap(startedRow);
            ok(gotResume === progRecord, "started row -> resumeRequested fires with the EXACT resumeTarget record");
            ok(gotDetail === null, "started row -> detailRequested does NOT also fire");

            // --- an UNSTARTED row's tap emits detailRequested with the entry ---
            var unstartedRow = { entry: { id: "new-id", title: "New Series", type: "manga" },
                                 mediaType: "manga", state: "notStarted", progress: 0,
                                 resumeTarget: null, resumeLane: "",
                                 lastActivityAt: 0, addedAt: 2000,
                                 downloaded: false, downloadSeriesKey: "" };
            gotResume = null; gotDetail = null;
            p.handleCardTap(unstartedRow);
            ok(gotDetail === unstartedRow.entry, "unstarted row -> detailRequested fires with the exact entry");
            ok(gotResume === null, "unstarted row -> resumeRequested does NOT fire");

            // --- edge: state inProgress but resumeTarget null -> falls through to details
            //     (defensive: a malformed row never silently swallows a tap) ---
            var dangledRow = { entry: { id: "x-id", title: "X", type: "manga" },
                               mediaType: "manga", state: "inProgress", progress: 0.5,
                               resumeTarget: null, resumeLane: "manga",
                               lastActivityAt: 5000, addedAt: 3000,
                               downloaded: false, downloadSeriesKey: "" };
            gotResume = null; gotDetail = null;
            p.handleCardTap(dangledRow);
            ok(gotDetail === dangledRow.entry, "inProgress-but-null-resumeTarget -> falls through to details (no silent swallow)");
            ok(gotResume === null, "inProgress-but-null-resumeTarget -> does NOT emit resume");

            // --- edge: null/undefined row is a safe no-op ---
            gotResume = null; gotDetail = null;
            p.handleCardTap(null);
            p.handleCardTap(undefined);
            ok(gotResume === null && gotDetail === null, "null/undefined row -> no-op, no signal fires");

            // ─────────────────────────────────────────────────────────────────────
            // TB-005: removeRequested signal, filter toggle, and menu open/close
            // ─────────────────────────────────────────────────────────────────────

            // removeRequested signal contract: forwards the exact entry to the owner.
            var gotRemove = null;
            p.removeRequested.connect(function (entry) { gotRemove = entry; });
            var rmEntry = { id: "rm-id", title: "Remove Me", type: "manga" };
            p.removeRequested(rmEntry);
            ok(gotRemove === rmEntry, "removeRequested forwards the exact entry object");

            // toggleFilter: clicking an inactive filter sets it; clicking the active one clears it.
            p.filter = "";
            p.toggleFilter("inProgress");
            ok(p.filter === "inProgress", "toggleFilter: '' -> 'inProgress' (set): " + p.filter);
            p.toggleFilter("downloaded");
            ok(p.filter === "downloaded", "toggleFilter: 'inProgress' -> 'downloaded' (switch): " + p.filter);
            p.toggleFilter("downloaded");
            ok(p.filter === "", "toggleFilter: 'downloaded' -> '' (re-click clears): " + p.filter);

            // default sortMode is lastRead (the page's documented default)
            p.sortMode = "added";          // temporarily change
            ok(p.sortMode === "added", "sortMode is settable");
            p.sortMode = "lastRead";       // restore default

            // openMenu / closeMenu round-trip the menu state
            ok(p.menuRow === null, "menu starts closed (menuRow null)");
            var menuRow = { entry: { id: "menu-id", title: "Menu" } };
            p.openMenu(menuRow, 100, 200);
            ok(p.menuRow === menuRow, "openMenu stores the row");
            ok(p.menuX === 100 && p.menuY === 200, "openMenu stores the anchor coords");
            ok(p.menuRowId === "menu-id", "menuRowId derives from menuRow.entry.id: " + p.menuRowId);
            p.closeMenu();
            ok(p.menuRow === null, "closeMenu clears menuRow");
            ok(p.menuRowId === "", "closeMenu clears menuRowId");

            // menuRowId is "" when menuRow is null (no crash on null deref)
            ok(p.menuRowId === "", "menuRowId empty string when menu closed");

            if (fails.length) console.log("FAILS:\n  " + fails.join("\n  "));
            else console.log("tankoban_library_page_harness: ALL PASS");
            Qt.exit(fails.length);
        }
    }
}
