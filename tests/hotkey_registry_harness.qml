// Headless behavioral harness for the pure PlayerHotkeys.js registry.
// Proves the registry PARSES and its lookups actually work (grep contracts only prove strings
// exist). Driven by qml.exe; throws on any failure (non-zero exit), Qt.quit() on success.
import QtQuick
import "../qml/PlayerHotkeys.js" as PlayerHotkeys

QtObject {
    // Numeric Qt::Key codes (stable ABI) — actionForEvent is driven by these so the pure
    // .pragma library needs no QML `Qt` global, and synthetic events can exercise it here.
    readonly property int kSpace: 0x20
    readonly property int kEscape: 0x01000000
    readonly property int kLeft: 0x01000012
    readonly property int kRight: 0x01000014
    readonly property int kUp: 0x01000013
    readonly property int kQuestion: 0x3f
    readonly property int kSlash: 0x2f
    readonly property int k5: 0x35
    readonly property int kS: 0x53
    readonly property int kC: 0x43
    readonly property int kE: 0x45
    readonly property int kMenu: 0x01000055
    readonly property int kF10: 0x01000039
    readonly property int kF: 0x46

    Component.onCompleted: {
        // Wrap everything: an uncaught throw in onCompleted would HANG qml.exe (no Qt.quit),
        // not exit non-zero — so the exit CODE, set explicitly here, is the reliable verdict
        // (console output is not guaranteed to flush before exit).
        try {
            runChecks()
            Qt.exit(0)
        } catch (e) {
            console.log("HARNESS FAIL: " + e.message)
            Qt.exit(2)
        }
    }

    function runChecks() {
        // --- no duplicate active bindings across different actions ---
        var conflicts = PlayerHotkeys.detectConflicts(PlayerHotkeys.actions())
        if (conflicts.length !== 0)
            throw new Error("Expected no duplicate hotkeys, got " + JSON.stringify(conflicts))

        // --- string-binding lookup ---
        function assertBinding(binding, id) {
            var action = PlayerHotkeys.actionForBinding(binding)
            if (!action || action.id !== id)
                throw new Error("Expected " + binding + " -> " + id + ", got " + JSON.stringify(action))
        }
        assertBinding("Space", "space")
        assertBinding("Esc", "escape")
        assertBinding("Left", "seekBack")
        assertBinding("Right", "seekForward")
        assertBinding("?", "shortcuts")
        assertBinding("S", "cycleSubtitle")
        assertBinding("C", "cycleSubtitle")
        assertBinding("E", "browser")
        assertBinding("Menu", "contextMenu")
        assertBinding("Shift+F10", "contextMenu")
        assertBinding("Shift+Z", "subtitleDelayDown")   // shifted letter normalizes to the letter
        if (PlayerHotkeys.actionForBinding("F") !== null)
            throw new Error("F must not be a player fullscreen toggle.")

        // --- event lookup (runtime path): synthetic {key, modifiers} ---
        function assertEvent(key, mods, id) {
            var action = PlayerHotkeys.actionForEvent({ key: key, modifiers: mods || 0 })
            if (!action || action.id !== id)
                throw new Error("event key=" + key + " -> expected " + id + ", got " + JSON.stringify(action))
        }
        assertEvent(kSpace, 0, "space")
        assertEvent(kEscape, 0, "escape")
        assertEvent(kLeft, 0, "seekBack")
        assertEvent(kRight, 0, "seekForward")
        assertEvent(kUp, 0, "volumeUp")            // modifier-independent action resolution
        assertEvent(k5, 0, "seekPercent")          // any digit -> seekPercent
        assertEvent(kQuestion, 0, "shortcuts")
        assertEvent(kSlash, 0x02000000, "shortcuts")  // Shift+/ also yields ?
        assertEvent(kS, 0, "cycleSubtitle")
        assertEvent(kC, 0, "cycleSubtitle")
        assertEvent(kE, 0, "browser")
        assertEvent(kMenu, 0, "contextMenu")
        assertEvent(kF10, 0x02000000, "contextMenu")
        if (PlayerHotkeys.actionForEvent({ key: kF10, modifiers: 0 }) !== null)
            throw new Error("F10 without Shift must not open More controls.")
        if (PlayerHotkeys.actionForEvent({ key: kF, modifiers: 0 }) !== null)
            throw new Error("F key event must resolve to no action (fullscreen-only).")

        // --- groups() feeds the sheet: non-empty, each group has items ---
        var groups = PlayerHotkeys.groups()
        if (!groups || groups.length === 0)
            throw new Error("groups() must return grouped shortcut records.")
        for (var g = 0; g < groups.length; g++)
            if (!groups[g].items || groups[g].items.length === 0)
                throw new Error("group " + JSON.stringify(groups[g].group) + " has no items.")

        console.log("Player hotkey registry logic checks passed.")
    }
}
