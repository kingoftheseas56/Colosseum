// VaultBrowseState — Vault Browse face session memory (execution plan Slice 5).
// A `.pragma library` module's top-level state survives across the vaultLayer Loader's
// repeated destroy/recreate cycles (leaving Vault via the taskbar and returning deactivates
// then reactivates the Loader, destroying VaultPage.qml's item each time) for as long as the
// PROCESS lives — exactly the "within a session" half of the locked design's persistence
// contract (§4.8). It deliberately does NOT survive an app restart (module state resets with
// the process); current folder + rail-expanded ARE required to survive a restart, and those
// live in VaultPage.qml's own `Settings { category: "vaultBrowseV1" }` block (registry-backed,
// tag-isolated under COLOSSEUM_APPDATA_TAG the same way every other Colosseum store is).
.pragma library

var _scrollByPath = ({})

// Remember the GridView's contentY for the folder/level being LEFT, keyed by its browse path
// (a real filesystem path, a show-sentinel key, or the "hidden:" pseudo-path).
function rememberScroll(path, y) {
    if (!path)
        return
    _scrollByPath[path] = y
}

// The remembered scroll for a path, or 0 for a level never visited this session.
function scrollFor(path) {
    if (!path)
        return 0
    var y = _scrollByPath[path]
    return (y === undefined) ? 0 : y
}
