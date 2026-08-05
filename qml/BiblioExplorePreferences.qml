// BiblioExplorePreferences — persisted shelf order/visibility for the Biblio Explore page
// (plan `2026-08-01-biblio-discover-explore.md`, Task 6). QtCore Settings under
// [biblioExplore] holds TWO compact JSON strings (order, hidden) — no `renamed` field, unlike
// Theatre's row preferences: Biblio shelves are not user-renamable per this task's interface.
// Reordering and hiding are keyed by STABLE row keys (see BiblioExploreRules.js), never
// display titles. `move(key, toIndex)` takes an ABSOLUTE index (not Theatre's relative delta)
// so keyboard reordering (index +/- 1) and pointer drag (an arbitrary drop index) both go
// through the same function; both are boundary-safe/clamped and silent on a genuine no-op.
// Production leaves settingsLocation unset (real QSettings store); the harness injects a
// temp INI url.
import QtQuick
import QtCore

QtObject {
    id: root

    property alias settingsLocation: store.location
    // parsed, reactive views of the persisted JSON — always arrays, never raw strings.
    property var order: _parse(store.orderJson)
    property var hidden: _parse(store.hiddenJson)
    // emitted ONLY after a real mutation.
    signal changed()

    property Settings settingsStore: Settings {
        id: store
        category: "biblioExplore"
        property string orderJson: "[]"
        property string hiddenJson: "[]"
    }

    // defensive parse — a corrupt or absent value yields an empty array, never throws.
    function _parse(raw) {
        if (!raw) return [];
        try {
            var v = JSON.parse(raw);
            return Array.isArray(v) ? v : [];
        } catch (e) { return []; }
    }

    // move(key, toIndex) — reposition `key` to an absolute index in the persisted order.
    // A key not yet present in the saved order is inserted first (so a freshly-appended
    // extension key can be dragged immediately), then repositioned. The destination index is
    // clamped into range; if the resulting position is unchanged from the starting position,
    // this is a silent no-op (no write, no changed()).
    function move(key, toIndex) {
        var arr = order.slice();
        var from = arr.indexOf(key);
        var isNew = from === -1;          // key wasn't in the saved order yet
        if (isNew) {
            arr.push(key);
            from = arr.length - 1;
        }
        var to = Math.max(0, Math.min(toIndex, arr.length - 1));
        // an EXISTING key requested at its own current position is a true boundary no-op.
        // a NEW key always represents a real membership change and must persist even when
        // its natural append position happens to equal the requested index.
        if (!isNew && to === from) return;
        if (to !== from) {
            arr.splice(from, 1);
            arr.splice(to, 0, key);
        }
        store.orderJson = JSON.stringify(arr);
        root.changed();
    }

    // setVisible(key, visible) — show/hide a row by stable key. A call that does not change
    // the current hidden membership is a silent no-op.
    function setVisible(key, visible) {
        var arr = hidden.slice();
        var idx = arr.indexOf(key);
        var isHidden = idx !== -1;
        var wantHidden = visible !== true;
        if (isHidden === wantHidden) return;   // already in the requested state -> no-op
        if (wantHidden) arr.push(key);
        else arr.splice(idx, 1);
        store.hiddenJson = JSON.stringify(arr);
        root.changed();
    }

    // reset() — restore default order/visibility (empty persisted state; BiblioExploreRules'
    // defaultRows() then governs entirely). Always emits changed(), even from an already-empty
    // state, mirroring TheatreRowPreferences.reset().
    function reset() {
        store.orderJson = "[]";
        store.hiddenJson = "[]";
        root.changed();
    }
}
