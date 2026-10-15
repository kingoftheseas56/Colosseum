// Headless behavioural gate for the shortcuts-sheet registry (Player2Shortcuts.js). Grep contracts
// prove strings are present; this proves the registry is WELL-FORMED and that every visible key glyph
// maps to exactly one Qt token in coveredQtKeys() (and vice versa) — so the human-readable sheet can
// never drift from the Qt tokens the shell-parity contract checks. Run offscreen:
// qml.exe -platform offscreen <this file>. Verdict rides the EXIT CODE (console.log does not flush
// before exit); an uncaught throw in onCompleted HANGS qml.exe, so assertions live in the try/catch.
import QtQml 2.15
import "../../qml/player2/controls/Player2Shortcuts.js" as Shortcuts

QtObject {
    function check(cond, msg) { if (!cond) throw new Error(msg) }

    function runChecks() {
        // --- registry is well-formed: groups with items, non-empty labels and keys, no dup glyphs ---
        var groups = Shortcuts.groups()
        check(groups.length >= 3, "the registry has at least Playback/Seeking/Help groups")
        var seenGlyphs = {}
        for (var g = 0; g < groups.length; ++g) {
            check(String(groups[g].group).length > 0, "every group is named")
            check(groups[g].items.length > 0, "every group has at least one binding")
            for (var i = 0; i < groups[g].items.length; ++i) {
                var it = groups[g].items[i]
                check(it.keys && it.keys.length > 0, "every binding names at least one key")
                check(String(it.label).length > 0, "every binding has a non-empty label")
                for (var k = 0; k < it.keys.length; ++k) {
                    var glyph = it.keys[k]
                    check(!seenGlyphs[glyph], "key glyph '" + glyph + "' is not bound twice")
                    seenGlyphs[glyph] = true
                }
            }
        }

        // --- every glyph maps to exactly one Qt token ---
        var mapped = {}
        for (var glyphKey in seenGlyphs) {
            var tok = Shortcuts.qtKeyForGlyph(glyphKey)
            check(tok.length > 0, "key glyph '" + glyphKey + "' maps to a Qt token")
            check(!mapped[tok], "Qt token '" + tok + "' is not reached by two glyphs")
            mapped[tok] = true
        }

        // --- glyph tokens and coveredQtKeys() are the SAME set (no drift in either direction) ---
        var covered = Shortcuts.coveredQtKeys()
        var coveredSet = {}
        for (var c = 0; c < covered.length; ++c) {
            check(!coveredSet[covered[c]], "coveredQtKeys has no duplicate: " + covered[c])
            coveredSet[covered[c]] = true
        }
        for (var t in mapped)
            check(coveredSet[t], "glyph token '" + t + "' is listed in coveredQtKeys()")
        for (var cc in coveredSet)
            check(mapped[cc], "coveredQtKeys token '" + cc + "' has a visible glyph in the sheet")
    }

    Component.onCompleted: {
        try { runChecks(); Qt.exit(0) }
        catch (e) { console.warn("player2_shortcuts_logic: FAIL: " + e.message); Qt.exit(2) }
    }
}
