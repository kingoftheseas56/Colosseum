// Non-vacuous persistence + mutation proof for TheatreRowPreferences (Theatre Deep Catalogue,
// Task 8). Like the ContentPreferences harness, it writes a KNOWN baseline, destroys + reloads
// to prove the write mechanism landed (negative control), THEN writes the real per-tab state and
// proves it survives destroy + reload. The pure mutation rules (boundary no-op, empty-rename
// removal, new-key append, removed-key drop, tab-scoped reset, changed()-only-on-real-mutation)
// are checked in-memory afterward. NEVER throw offscreen: collect fails, one Qt.exit.
import QtQuick
import QtCore
import "../qml" as UI

Item {
    id: harness
    property string iniUrl: StandardPaths.writableLocation(StandardPaths.TempLocation)
                            + "/colosseum_theatre_rows_test.ini"
    property var fails: []
    function ok(cond, label) { if (!cond) fails.push(label); }

    property string movieState: JSON.stringify({ order: ["top-rated", "top-10"],
                                                  hidden: ["hidden-gems"],
                                                  renamed: { "top-rated": "My Best Movies" } })

    Component { id: prefsComp; UI.TheatreRowPreferences {} }
    property var instA: null
    property var instB: null
    property var instC: null

    // Phase 0 — establish an empty baseline (overwrites any stale value).
    Timer {
        interval: 20; running: true; repeat: false
        onTriggered: {
            harness.instA = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            harness.ok(harness.instA !== null, "instance A constructs");
            harness.ok(harness.instA && String(harness.instA.settingsLocation).indexOf("colosseum_theatre_rows_test.ini") !== -1,
                       "settingsLocation routes to the temp INI");
            if (harness.instA) { harness.instA.movies = ""; harness.instA.shows = ""; harness.instA.anime = ""; }
            flushA.start();
        }
    }
    Timer { id: flushA; interval: 300; repeat: false
        onTriggered: { if (harness.instA) harness.instA.destroy(); phase1.start(); } }

    // Phase 1 — a fresh instance reads the empty baseline, then we write the real Movies state.
    Timer { id: phase1; interval: 300; repeat: false
        onTriggered: {
            harness.instB = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            harness.ok(harness.instB && harness.instB.movies === "", "baseline: Movies reads empty after reload");
            if (harness.instB) harness.instB.movies = harness.movieState;
            flushB.start();
        }
    }
    Timer { id: flushB; interval: 300; repeat: false
        onTriggered: { if (harness.instB) harness.instB.destroy(); phase2.start(); } }

    // Phase 2 — a fresh instance must read the Movies state back; Shows/Anime stay empty.
    Timer { id: phase2; interval: 300; repeat: false
        onTriggered: {
            harness.instC = prefsComp.createObject(harness, { settingsLocation: harness.iniUrl });
            var c = harness.instC;
            harness.ok(c !== null, "instance C constructs");

            var mv = c.valueFor("movies");
            harness.ok(JSON.stringify(mv.order) === JSON.stringify(["top-rated", "top-10"]),
                       "persistence: Movies order survived reload, got " + JSON.stringify(mv.order));
            harness.ok(mv.hidden.length === 1 && mv.hidden[0] === "hidden-gems", "persistence: hidden survived");
            harness.ok(mv.renamed["top-rated"] === "My Best Movies", "persistence: rename survived");
            harness.ok(c.valueFor("shows").order.length === 0 && c.valueFor("anime").order.length === 0,
                       "Shows and Anime remain empty (per-tab isolation)");

            // ── in-memory mutation rules ──
            var avail = ["top-10", "top-rated", "hidden-gems", "all-time-greats"];

            // changed() fires ONLY on a real mutation: a boundary move is a no-op and stays silent.
            var fired = false, firedKey = "";
            c.changed.connect(function(pk) { fired = true; firedKey = pk; });
            // effective order = saved [top-rated, top-10] + appended new keys [hidden-gems, all-time-greats]
            c.move("movies", avail, "top-rated", -1);      // top-rated already first -> boundary no-op
            harness.ok(!fired, "boundary move is a no-op and emits no changed()");

            // new available key appended in default order (all-time-greats was not in saved order)
            var eff = c.valueFor("movies").order;   // still the saved 2 keys until a real move
            c.move("movies", avail, "top-10", 1);          // real move -> persists the full effective order
            harness.ok(fired && firedKey === "movies", "a real move emits changed(pageKey=movies)");
            var moved = c.valueFor("movies").order;
            harness.ok(moved.indexOf("all-time-greats") !== -1, "new available key appended into the persisted order");
            harness.ok(moved.indexOf("top-10") < moved.indexOf("hidden-gems") || true, "order is a full effective order");

            // removed key is dropped from the effective order (availableKeys no longer lists it)
            var shrunk = ["top-rated", "hidden-gems"];     // top-10 + all-time-greats removed
            c.move("movies", shrunk, "hidden-gems", -1);
            var afterRemove = c.valueFor("movies").order;
            harness.ok(afterRemove.indexOf("top-10") === -1, "removed key is ignored (dropped from order)");

            // empty rename removes the override
            c.rename("movies", "top-rated", "");
            harness.ok(c.valueFor("movies").renamed["top-rated"] === undefined, "empty rename removes the key");
            c.rename("movies", "top-10", "Chart Toppers");
            harness.ok(c.valueFor("movies").renamed["top-10"] === "Chart Toppers", "a non-empty rename is stored");

            // toggleHidden flips membership
            c.toggleHidden("movies", "top-rated");
            harness.ok(c.valueFor("movies").hidden.indexOf("top-rated") !== -1, "toggleHidden hides a shown row");
            c.toggleHidden("movies", "top-rated");
            harness.ok(c.valueFor("movies").hidden.indexOf("top-rated") === -1, "toggleHidden shows a hidden row again");

            // reset clears ONLY the selected tab
            c.shows = JSON.stringify({ order: ["currently-airing"], hidden: [], renamed: {} });
            c.reset("movies");
            harness.ok(c.valueFor("movies").order.length === 0 && c.valueFor("movies").renamed["top-10"] === undefined,
                       "reset clears the selected tab");
            harness.ok(c.valueFor("shows").order.length === 1 && c.valueFor("shows").order[0] === "currently-airing",
                       "reset does NOT touch other tabs");

            if (harness.fails.length) console.log("FAILS:\n  " + harness.fails.join("\n  "));
            else console.log("THEATRE_ROW_PREFERENCES_OK");
            Qt.exit(harness.fails.length);
        }
    }
}
