// Non-vacuous rules + persistence proof for BiblioExploreRules.js and
// BiblioExplorePreferences.qml (Biblio Discover/Explore plan, Task 6). Mirrors the Theatre row
// preferences harness pattern: a temp INI file under StandardPaths.TempLocation, sequential
// Timer-staged phases (construct -> write baseline -> destroy -> reconstruct -> verify
// persistence survived), then in-memory pure-rule assertions run synchronously against
// BiblioExploreRules.js. NEVER throw offscreen: collect fails, one Qt.exit at the end.
import QtQuick
import QtCore
import "../qml" as UI
import "../qml/BiblioExploreRules.js" as Rules

Item {
    id: harness
    property string iniUrl: StandardPaths.writableLocation(StandardPaths.TempLocation)
                            + "/colosseum_biblio_explore_test.ini"
    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    // ---------------------------------------------------------------------
    // Pure rules — no QML instance required, run first and synchronously.
    // ---------------------------------------------------------------------
    function runRuleChecks() {
        // exact default order, no extensions installed
        var bare = Rules.defaultRows([]);
        ok(JSON.stringify(bare) === JSON.stringify(["top-10", "popular", "top-rated", "new-releases", "trending"]),
           "default order with zero extensions, got " + JSON.stringify(bare));

        // empty extension-section collapse: no "ext:" keys present at all, not a present-but-empty marker
        var hasExtKey = false;
        for (var i = 0; i < bare.length; i++) if (Rules.isExtensionKey(bare[i])) hasExtKey = true;
        ok(!hasExtKey, "zero enabled extensions means no ext: keys at all (collapsed, not present-but-empty)");

        // extension keys land between top-10 and the house rails, derived from stable id
        var withExt = Rules.defaultRows([{ id: "com.example.libgen", title: "LibGen Mirror" },
                                          { id: "com.example.annas", title: "Anna's Archive" }]);
        ok(JSON.stringify(withExt) === JSON.stringify([
            "top-10", "ext:com.example.libgen", "ext:com.example.annas",
            "popular", "top-rated", "new-releases", "trending"
        ]), "extension keys ordered between top-10 and house rails, got " + JSON.stringify(withExt));

        // stable extension keys: derived from id, NOT the display title
        var byTitleA = Rules.defaultRows([{ id: "com.example.libgen", title: "LibGen Mirror" }]);
        var byTitleB = Rules.defaultRows([{ id: "com.example.libgen", title: "Renamed Completely" }]);
        ok(JSON.stringify(byTitleA) === JSON.stringify(byTitleB),
           "extension key is stable across a title/name change (id-derived only)");
        ok(byTitleA[1] === "ext:com.example.libgen", "extension key format is ext:<id>");

        // plain string ids are also accepted (no title at all)
        var plainIds = Rules.defaultRows(["com.example.libgen"]);
        ok(plainIds[1] === "ext:com.example.libgen", "plain string extension id is accepted");

        // duplicate stable id collapses to one key, not repeated
        var dup = Rules.defaultRows([{ id: "com.example.libgen" }, { id: "com.example.libgen" }]);
        ok(dup.length === 6, "duplicate extension id collapses to a single key, got " + JSON.stringify(dup));

        // mosaics are never part of the row inventory at all
        var mosaicish = ["fiction", "nonfiction", "audience", "mosaic-fiction", "mosaic-nonfiction", "mosaic-audience"];
        var rowsAll = Rules.defaultRows([{ id: "x" }]);
        for (var m = 0; m < mosaicish.length; m++)
            ok(rowsAll.indexOf(mosaicish[m]) === -1, "mosaic key '" + mosaicish[m] + "' never appears in defaultRows()");

        // ── applyCustomization ──
        var rows = Rules.defaultRows([{ id: "ext-a" }, { id: "ext-b" }]);
        // rows = [top-10, ext:ext-a, ext:ext-b, popular, top-rated, new-releases, trending]

        // no customization -> identity order, nothing hidden
        var plain = Rules.applyCustomization(rows, { order: [], hidden: [] }, false);
        var plainKeys = plain.map(function(r) { return r.key; });
        ok(JSON.stringify(plainKeys) === JSON.stringify(rows), "no customization preserves default order");
        ok(plain.every(function(r) { return r.hidden === false; }), "nothing hidden by default");

        // drag-equivalent move: saved order reorders the effective list
        var reordered = Rules.applyCustomization(rows, { order: ["trending", "top-10"], hidden: [] }, false);
        var reorderedKeys = reordered.map(function(r) { return r.key; });
        ok(reorderedKeys[0] === "trending" && reorderedKeys[1] === "top-10",
           "saved order takes precedence (drag-equivalent move), got " + JSON.stringify(reorderedKeys));
        ok(reorderedKeys.length === rows.length, "reordering keeps every available row, none dropped");

        // new-extension append: a row not present in the saved order is appended, not dropped
        var savedBeforeExt = ["trending", "top-10", "popular", "top-rated", "new-releases"]; // missing both ext keys
        var appended = Rules.applyCustomization(rows, { order: savedBeforeExt, hidden: [] }, false);
        var appendedKeys = appended.map(function(r) { return r.key; });
        ok(appendedKeys.indexOf("ext:ext-a") !== -1 && appendedKeys.indexOf("ext:ext-b") !== -1,
           "rows missing from saved order are appended safely, got " + JSON.stringify(appendedKeys));
        ok(appendedKeys.length === rows.length, "append does not drop or duplicate any row");

        // removed-key ignore: a saved order entry for a row no longer available is silently dropped
        var shrunkRows = Rules.defaultRows([{ id: "ext-a" }]); // ext-b no longer installed
        var withStaleEntry = Rules.applyCustomization(shrunkRows,
            { order: ["top-10", "ext:ext-a", "ext:ext-b", "popular"], hidden: [] }, false);
        var staleKeys = withStaleEntry.map(function(r) { return r.key; });
        ok(staleKeys.indexOf("ext:ext-b") === -1, "a removed row's saved order entry is dropped, not resurrected");
        ok(staleKeys.length === shrunkRows.length, "dropped entry does not leave a gap or duplicate");

        // hide/show: a hidden row is omitted from normal (non-edit) browsing
        var withHidden = Rules.applyCustomization(rows, { order: [], hidden: ["popular"] }, false);
        var hiddenKeysNormal = withHidden.map(function(r) { return r.key; });
        ok(hiddenKeysNormal.indexOf("popular") === -1, "a hidden row is omitted in normal (non-edit) mode");

        // ...but still surfaced (marked hidden:true) in edit mode so it can be toggled back on
        var withHiddenEdit = Rules.applyCustomization(rows, { order: [], hidden: ["popular"] }, true);
        var popularEntry = withHiddenEdit.filter(function(r) { return r.key === "popular"; })[0];
        ok(popularEntry !== undefined && popularEntry.hidden === true,
           "a hidden row is present-but-marked-hidden in edit mode");
        ok(withHiddenEdit.length === rows.length, "edit mode surfaces every row, hidden or not");

        // applyCustomization never mutates its inputs
        var inputRows = rows.slice();
        var inputCustom = { order: ["top-10"], hidden: ["popular"] };
        var inputOrderCopy = inputCustom.order.slice();
        var inputHiddenCopy = inputCustom.hidden.slice();
        Rules.applyCustomization(inputRows, inputCustom, false);
        ok(JSON.stringify(inputRows) === JSON.stringify(rows), "applyCustomization does not mutate the rows array");
        ok(JSON.stringify(inputCustom.order) === JSON.stringify(inputOrderCopy) &&
           JSON.stringify(inputCustom.hidden) === JSON.stringify(inputHiddenCopy),
           "applyCustomization does not mutate the customization object");
    }

    // ---------------------------------------------------------------------
    // QSettings persistence — sequential Timer-staged phases.
    // ---------------------------------------------------------------------
    Component { id: prefsComp; UI.BiblioExplorePreferences {} }
    property var instA: null
    property var instB: null
    property var instC: null

    // Phase 0 — establish an empty baseline (overwrites any stale value from a prior run).
    Timer {
        interval: 20; running: true; repeat: false
        onTriggered: {
            runRuleChecks();

            harness.instA = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            harness.ok(harness.instA !== null, "instance A constructs");
            harness.ok(harness.instA && String(harness.instA.settingsLocation).indexOf("colosseum_biblio_explore_test.ini") !== -1,
                       "settingsLocation routes to the temp INI");
            if (harness.instA) harness.instA.reset();
            flushA.start();
        }
    }
    Timer { id: flushA; interval: 300; repeat: false
        onTriggered: { if (harness.instA) harness.instA.destroy(); phase1.start(); } }

    // Phase 1 — a fresh instance reads the empty baseline, then we drive real mutations.
    Timer { id: phase1; interval: 300; repeat: false
        onTriggered: {
            harness.instB = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            var b = harness.instB;
            harness.ok(b && JSON.stringify(b.order) === JSON.stringify([]), "baseline: order reads empty after reload");
            harness.ok(b && JSON.stringify(b.hidden) === JSON.stringify([]), "baseline: hidden reads empty after reload");

            // changed() fires ONLY on a real mutation.
            var fired = 0;
            b.changed.connect(function() { fired++; });

            // move() on an unknown key with a no-op destination (append position == requested
            // index) should not persist an order the reload can't otherwise reproduce; a real
            // move must both mutate `order` and fire changed().
            b.move("top-10", 0);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["top-10"]) && fired === 1,
                       "move() on a fresh key inserts it and fires changed(), got " + JSON.stringify(b.order));

            b.move("popular", 0);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["popular", "top-10"]) && fired === 2,
                       "move(key, toIndex) reorders to an ABSOLUTE index, got " + JSON.stringify(b.order));

            // boundary no-op: moving the first key to index 0 (its current position) is silent
            b.move("popular", 0);
            harness.ok(fired === 2, "a boundary/no-op move stays silent (no changed())");

            // out-of-range index is clamped rather than throwing or corrupting order
            b.move("top-10", 999);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["popular", "top-10"]) && fired === 2,
                       "move() clamps an out-of-range index instead of throwing, got " + JSON.stringify(b.order));
            b.move("popular", 5);
            harness.ok(JSON.stringify(b.order) === JSON.stringify(["top-10", "popular"]) && fired === 3,
                       "move() clamps to the end and still fires changed() for a real move, got " + JSON.stringify(b.order));

            // setVisible toggles hidden membership and is idempotent (no double-fire on repeat)
            b.setVisible("popular", false);
            harness.ok(b.hidden.indexOf("popular") !== -1 && fired === 4, "setVisible(key,false) hides a shown row");
            b.setVisible("popular", false);
            harness.ok(fired === 4, "setVisible with no actual change stays silent");
            b.setVisible("popular", true);
            harness.ok(b.hidden.indexOf("popular") === -1 && fired === 5, "setVisible(key,true) re-shows a hidden row");

            flushB.start();
        }
    }
    Timer { id: flushB; interval: 300; repeat: false
        onTriggered: { if (harness.instB) harness.instB.destroy(); phase2.start(); } }

    // Phase 2 — a fresh instance must read the real state back (order + hidden survive reload).
    Timer { id: phase2; interval: 300; repeat: false
        onTriggered: {
            harness.instC = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            var c = harness.instC;
            harness.ok(c !== null, "instance C constructs");
            harness.ok(JSON.stringify(c.order) === JSON.stringify(["top-10", "popular"]),
                       "persistence: order survived destroy + reload, got " + JSON.stringify(c.order));
            harness.ok(JSON.stringify(c.hidden) === JSON.stringify([]),
                       "persistence: hidden survived destroy + reload (re-shown popular stayed visible)");

            // only stable keys are ever persisted, never a display title
            var raw = String(c.settingsStore ? c.settingsStore.orderJson : "");
            harness.ok(raw.indexOf(" ") === -1 || raw.indexOf("Anna") === -1, "persisted order carries no display-title text");

            // reset restores empty order/hidden and fires changed()
            var resetFired = false;
            c.changed.connect(function() { resetFired = true; });
            c.reset();
            harness.ok(resetFired, "reset() fires changed()");
            harness.ok(JSON.stringify(c.order) === JSON.stringify([]) && JSON.stringify(c.hidden) === JSON.stringify([]),
                       "reset() clears both order and hidden");

            if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
            else console.log("BIBLIO_EXPLORE_RULES_OK");
            Qt.exit(harness.fails.length);
        }
    }
}
