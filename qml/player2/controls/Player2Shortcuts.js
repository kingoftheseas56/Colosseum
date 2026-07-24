.pragma library

// The Player 2 keyboard map — the SINGLE SOURCE OF TRUTH for the shortcuts sheet. Player 2 is a clean
// re-implementation with its OWN (smaller) binding set, so the CONTENT is Player 2's real bindings
// (Player2Shell.qml Keys.onPressed), while the SHAPE mirrors the production PlayerHotkeys.groups()
// registry — same group vocabulary (Playback/Seeking/Sound/Tools/Help), same item shape
// {label, keys, note} — so ShortcutsSheet renders identically to the current player's sheet. Two
// invariants keep it honest:
//   1. player2_shortcuts_harness.qml ties every visible key glyph to a Qt token in coveredQtKeys().
//   2. player2_shortcuts_contract.ps1 ties coveredQtKeys() to the shell's actual switch cases.
// A binding cannot drift here without failing the build, in either direction.

// The Qt key tokens the sheet documents, in lockstep with Player2Shell.qml's Keys.onPressed switch.
function coveredQtKeys() {
    return ["Space", "Escape", "F", "Left", "Right", "Comma", "Period", "M", "D", "E", "Question"]
}

// Map a visible key glyph (as shown on a key cap) to its Qt.Key_ token. Keep in sync with the glyphs
// used in groups().
function qtKeyForGlyph(glyph) {
    switch (glyph) {
    case "Space": return "Space"
    case "Esc":   return "Escape"
    case "F":     return "F"
    case "Left":  return "Left"
    case "Right": return "Right"
    case ",":     return "Comma"
    case ".":     return "Period"
    case "M":     return "M"
    case "D":     return "D"
    case "E":     return "E"
    case "?":     return "Question"
    default:      return ""
    }
}

// Grouped records for the sheet, mirroring PlayerHotkeys.groups(): {group, items:[{label, keys, note}]}.
// Group order by first appearance. Keys are the production-style binding strings (words, not glyphs).
function groups() {
    return [
        { group: "Playback", items: [
            { label: "Play / pause",        keys: ["Space"], note: "" },
            { label: "Fullscreen",          keys: ["F"],     note: "" },
            { label: "Close menu / overlay", keys: ["Esc"],  note: "" }
        ] },
        { group: "Seeking", items: [
            { label: "Seek back",     keys: ["Left"],  note: "10s" },
            { label: "Seek forward",  keys: ["Right"], note: "10s" },
            { label: "Frame back",    keys: [","],     note: "" },
            { label: "Frame forward", keys: ["."],     note: "" }
        ] },
        { group: "Sound", items: [
            { label: "Mute", keys: ["M"], note: "" }
        ] },
        { group: "Tools", items: [
            { label: "Playback stats",     keys: ["D"], note: "" },
            { label: "Episodes & sources", keys: ["E"], note: "" }
        ] },
        { group: "Help", items: [
            { label: "Show shortcuts", keys: ["?"], note: "" }
        ] }
    ]
}

// Every key glyph used in groups(), flattened — for the harness's glyph↔token parity check.
function catalogGlyphs() {
    var out = []
    var gs = groups()
    for (var g = 0; g < gs.length; ++g)
        for (var i = 0; i < gs[g].items.length; ++i) {
            var ks = gs[g].items[i].keys
            for (var k = 0; k < ks.length; ++k)
                out.push(ks[k])
        }
    return out
}
