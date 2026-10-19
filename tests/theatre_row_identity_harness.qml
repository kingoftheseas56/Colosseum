// Actual Qt QObject identity regression for TheatreCatalogPage's presentation owner.
// Snapshot LazyPosterShelf QObject references; count replaced hosts and mounted rails.
import QtQuick
import QtCore
import "../qml" as UI

Item {
    id: h
    width: 1100; height: 900
    property var fails: []
    property int phase: 0
    property var before: ({})
    property var rows: []
    property var lastPush: null
    property int canceled: 0
    property int totalRecreated: 0
    property int totalMountedRailReplaced: 0
    property int loaderCalls: 0
    property var retiredPush: null
    property int retiredGeneration: -1
    property var focusedList: null
    function ok(value, label) { if (!value) fails.push(label); }
    function railList(rail) {
        if (!rail) return null
        for (var i = 0; i < rail.children.length; ++i) {
            var child = rail.children[i]
            if (child && child.contentX !== undefined && child.contentWidth !== undefined)
                return child
        }
        return null
    }
    function fakeLoader(pk, options, push) {
        h.loaderCalls++
        h.lastPush = push
        return function() { h.canceled++ }
    }
    function mountedRail(shelf) {
        if (!shelf) return null
        for (var c = 0; c < shelf.children.length; ++c) {
            var child = shelf.children[c]
            if (child && child.item && child.item.title !== undefined) return child.item
        }
        return null
    }
    function controlsFor(key) {
        var index = page.mainKeys.indexOf(key)
        var shelf = index >= 0 ? page.mainShelfAt(index) : null
        if (!shelf || !shelf.parent) return null
        for (var c = 0; c < shelf.parent.children.length; ++c) {
            var child = shelf.parent.children[c]
            if (child && child.rowKey === key) return child
        }
        return null
    }
    function row(key, title, pending) {
        var cards = []
        // The first rail genuinely overflows the viewport, so its 45px scroll is real.
        if (!pending) for (var i = 0; i < (key === "a" ? 12 : 1); ++i)
            cards.push({ id: "item-" + key + "-" + i, title: "Card " + key, cover: "" })
        return { key: key, title: title, sourceKind: "house", sourceLabel: "Colosseum",
                 items: cards, pendingFacts: pending === true, liveFacts: pending === true,
                 seeAllPin: { pageKey: "movies", sourceKind: "house", rowKey: key } }
    }
    function seed() { return [row("a", "Alpha"), row("b", "Beta"), row("c", "Gamma")]; }
    function publish(list) {
        h.rows = list
        var start = Date.now()
        page._push({ generation: page.generation, loading: false, error: "", rows: list })
        console.log("PUSH_MS " + (Date.now() - start) + " count=" + list.length)
    }
    function snapshot() {
        var out = {}
        for (var i = 0; i < page.mainRows.length; ++i) {
            var key = page.mainRows[i].key, shelf = page.mainShelfAt(i)
            if (shelf) out[key] = { object: shelf, rail: mountedRail(shelf),
                                   mounted: shelf.railLoaded,
                                   title: shelf.row ? shelf.row.title : "",
                                   items: shelf.row ? shelf.row.items.length : -1,
                                   pending: shelf.row ? shelf.row.pendingFacts === true : false,
                                   savedX: shelf.savedContentX }
        }
        return out
    }
    function compare(label, oldRefs, expectedPreserved, expectedRemoved, expectedAdded) {
        var next = snapshot(), created = 0, removed = 0, rails = 0, railCreated = 0
        for (var k in oldRefs) {
            if (!next[k] || next[k].object !== oldRefs[k].object) removed++
            if (oldRefs[k].rail && (!next[k] || next[k].rail !== oldRefs[k].rail)) rails++
        }
        for (var n in next) {
            if (!oldRefs[n] || next[n].object !== oldRefs[n].object) created++
            if (next[n].rail && (!oldRefs[n] || next[n].rail !== oldRefs[n].rail)) railCreated++
        }
        h.totalRecreated += removed
        h.totalMountedRailReplaced += rails
        console.log("IDENTITY " + label + " delegatesCreated=" + created +
                    " delegatesDestroyed=" + removed + " railsCreated=" + railCreated +
                    " railsDestroyed=" + rails + " current=" + Object.keys(next).join(","))
        for (var i = 0; i < expectedPreserved.length; i++) {
            var key = expectedPreserved[i]
            ok(!!oldRefs[key] && !!next[key] && oldRefs[key].object === next[key].object,
               label + ": unchanged shelf destroyed or replaced: " + key)
            if (oldRefs[key] && oldRefs[key].rail)
                ok(!!next[key] && oldRefs[key].rail === next[key].rail,
                   label + ": mounted rail destroyed or replaced: " + key)
        }
        for (var j = 0; j < expectedRemoved.length; j++)
            ok(!next[expectedRemoved[j]], label + ": removed key still rendered: " + expectedRemoved[j])
        for (var a = 0; a < expectedAdded.length; a++)
            ok(!!next[expectedAdded[a]], label + ": added key missing: " + expectedAdded[a])
        return next
    }

    UI.TheatreRowPreferences {
        id: prefs
        settingsLocation: StandardPaths.writableLocation(StandardPaths.TempLocation)
                          + "/theatre_row_model_018_identity.ini"
    }
    UI.TheatreCatalogPage {
        id: page
        width: 1050; active: true; pageKey: "movies"
        viewportHeight: 0
        catalogLoader: h.fakeLoader; rowPreferences: prefs
    }

    Timer {
        interval: 90; repeat: true; running: true
        onTriggered: h.run()
    }
    function run() {
        var r, next
        switch (phase++) {
        case 0:
            prefs.reset("movies")
            publish(seed())
            return
        case 1:
            before = snapshot()
            ok(Object.keys(before).length === 3, "initial delegates missing")
            if (before.a) {
                before.a.object.testSetRailContentX(45)
                h.focusedList = railList(before.a.rail)
                ok(!!h.focusedList, "no real mounted ListView for the first rail")
                if (h.focusedList) {
                    h.focusedList.forceActiveFocus(Qt.OtherFocusReason)
                    ok(h.focusedList.activeFocus, "offscreen rail did not accept keyboard focus")
                }
            }
            publish(seed()) // new JS array, same semantic rows
            return
        case 2:
            compare("same-new-array", before, ["a","b","c"], [], [])
            ok(page.mainShelfAt(0).savedContentX === 45, "same semantic rows lost horizontal offset")
            ok(h.focusedList && h.focusedList.activeFocus,
               "same semantic rows lost active ListView keyboard focus")
            before = snapshot()
            r = seed(); r[1].title = "Beta Updated"
            r[1].items[0].title = "New validated card"
            publish(r)
            return
        case 3:
            next = compare("one-row-update", before, ["a","b","c"], [], [])
            ok(next.b && next.b.title === "Beta Updated" &&
               page.mainShelfAt(1).row.items[0].title === "New validated card",
               "changed one-row data not reflected")
            ok(next.b && next.b.rail && next.b.rail.title === "Beta Updated" &&
               next.b.rail.visibleItems[0].title === "New validated card",
               "mounted real PosterRail does not display the updated title/card")
            ok(next.a && next.a.savedX === 45 && next.a.rail === before.a.rail &&
               Math.abs(next.a.rail.currentContentX - 45) < 1,
               "unaffected rail lost its real horizontal scroll or QObject identity")
            ok(h.focusedList && h.focusedList.activeFocus,
               "incremental row change stole unchanged rail keyboard focus")
            before = snapshot()
            r = h.rows.slice(); r.push(row("d", "Delta"))
            publish(r)
            return
        case 4:
            compare("append", before, ["a","b","c"], [], ["d"])
            before = snapshot()
            r = [h.rows[3],h.rows[0],h.rows[1],h.rows[2]]
            publish(r)
            return
        case 5:
            compare("reorder", before, ["a","b","c","d"], [], [])
            ok(page.mainKeys.join(",") === "d,a,b,c", "reorder did not change mainKeys")
            ok(h.focusedList && h.focusedList.activeFocus,
               "reordering other rows stole active ListView keyboard focus")
            before = snapshot()
            publish([h.rows[0], h.rows[1], h.rows[3]])
            return
        case 6:
            compare("remove", before, ["a","c","d"], ["b"], [])
            before = snapshot()
            prefs.rename("movies", "a", "Renamed Alpha")
            return
        case 7:
            next = compare("rename", before, ["a","c","d"], [], [])
            ok(next.a && next.a.title === "Renamed Alpha", "rename not reflected on row")
            var controls = controlsFor("a")
            ok(controls && controls.label === "Renamed Alpha" && controls.renamed,
               "row controls did not update with renamed presentation role")
            var indexRow = page.rowIndexRows.filter(function(x) {return x.key === "a"})[0]
            ok(indexRow && indexRow.title === "Renamed Alpha" &&
               indexRow.target === next.a.object.parent,
               "row index title or QObject target stale after rename")
            before = snapshot()
            prefs.move("movies", page.mainKeys, "a", -1)
            return
        case 8:
            compare("preference-move", before, ["a","c","d"], [], [])
            ok(page.mainKeys[0] === "a", "preference move missing")
            before = snapshot()
            prefs.toggleHidden("movies", "c")
            return
        case 9:
            compare("hidden", before, ["a","d"], ["c"], [])
            before = snapshot()
            page.editMode = true
            return
        case 10:
            compare("edit-reveal-hidden", before, ["a","d"], [], ["c"])
            ok(page.mainRows.some(function(x){return x.key === "c" && x.hidden === true}),
               "edit mode did not expose flagged hidden shelf")
            var cControls = controlsFor("c")
            ok(cControls && cControls.hidden === true && cControls.rowKey === "c",
               "hidden row controls did not bind stable key and hidden state")
            page.editMode = false
            prefs.toggleHidden("movies", "c")
            prefs.reset("movies")
            before = snapshot()
            r = seed(); r[2] = row("c", "Waiting for verified facts", true)
            publish(r)
            return
        case 11:
            next = snapshot()
            ok(next.c && next.c.pending && next.c.items === 0 &&
               (!next.c.rail || (!next.c.rail.visible && next.c.rail.visibleItems.length === 0)),
               "pending facts displayed an unvalidated card")
            before = next
            r = seed(); r[2].title = "Validated"
            publish(r)
            return
        case 12:
            next = compare("pending-to-validated", before, ["a","b","c"], [], [])
            ok(next.c && !next.c.pending && next.c.items === 1 && next.c.rail &&
               next.c.rail.title === "Validated" && next.c.rail.visibleItems.length === 1,
               "validated facts did not produce one genuine mounted card")
            before = snapshot()
            h.retiredPush = h.lastPush
            h.retiredGeneration = page.generation
            page.active = false
            h.retiredPush({ generation: h.retiredGeneration,
                            rows: [row("rogue","Late Callback")], loading:false,error:"" })
            ok(!page.mainRows.some(function(x) {return x.key === "rogue";}),
               "late callback mutated inactive page")
            ok(page.generation !== h.retiredGeneration, "deactivation did not cancel generation")
            // A settled inactive page is intentionally retained. Simulate a genuine remote
            // invalidation to require a new generation on activation, not an invented reload.
            page.remoteCatalogChanged()
            page.active = true
            return
        case 13:
            ok(h.loaderCalls >= 2, "reentry did not acquire a fresh loader")
            ok(page.generation > h.retiredGeneration, "reentry did not create a fresh generation")
            publish(seed())
            h.retiredPush({ generation: h.retiredGeneration,
                            rows: [row("rogue","Late Reentry")], loading:false,error:"" })
            return
        case 14:
            ok(page.mainRows.length === 3 && page.mainKeys.join(",") === "a,b,c",
               "active reentry failed to repopulate the correct shelves")
            ok(!page.mainKeys.some(function(x){return x === "rogue";}),
               "old callback corrupted new generation")
            return
        default:
            console.log("IDENTITY_TOTAL oldDelegatesReplaced=" + totalRecreated +
                        " previouslyMountedRailLost=" + totalMountedRailReplaced)
            if (fails.length) console.log("IDENTITY_FAILS " + fails.join(" | "))
            else console.log("THEATRE_ROW_IDENTITY_OK")
            Qt.exit(fails.length ? 1 : 0)
        }
    }
}
