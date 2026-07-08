.pragma library

// Pure player hotkey registry (Feature 7). Centralizes shortcut metadata, string/event lookup,
// conflict detection, and grouping for the shortcuts sheet. No QML `Qt` global is used: key
// events are matched by numeric Qt::Key codes (stable ABI) so this library is fully testable
// headless with synthetic events. PlayerPage owns the ACTUAL behavior; this only resolves ids.

var PLAYER_SCOPE = "player"

// Qt::Key numeric codes.
var K = {
    Space: 0x20, Escape: 0x01000000,
    Home: 0x01000010, End: 0x01000011,
    Left: 0x01000012, Up: 0x01000013, Right: 0x01000014, Down: 0x01000015,
    Comma: 0x2c, Period: 0x2e, Slash: 0x2f, Question: 0x3f,
    Zero: 0x30, Nine: 0x39,
    BracketLeft: 0x5b, BracketRight: 0x5d,
    C: 0x43, D: 0x44, I: 0x49, L: 0x4c, M: 0x4d, O: 0x4f, S: 0x53, X: 0x58, Z: 0x5a
}
var SHIFT = 0x02000000   // Qt.ShiftModifier

function actions() {
    return [
        { id: "space", group: "Playback", label: "Play / Pause", bindings: ["Space"], note: "Hold for temporary fast playback", scope: PLAYER_SCOPE },
        { id: "escape", group: "Playback", label: "Close menus / Back", bindings: ["Esc"], scope: PLAYER_SCOPE },
        { id: "speedDown", group: "Playback", label: "Speed down", bindings: ["["], scope: PLAYER_SCOPE },
        { id: "speedUp", group: "Playback", label: "Speed up", bindings: ["]"], scope: PLAYER_SCOPE },
        { id: "seekBack", group: "Seeking", label: "Seek back", bindings: ["Left"], scope: PLAYER_SCOPE },
        { id: "seekForward", group: "Seeking", label: "Seek forward", bindings: ["Right"], scope: PLAYER_SCOPE },
        { id: "frameBack", group: "Seeking", label: "Frame back when paused, otherwise -30s", bindings: [","], scope: PLAYER_SCOPE },
        { id: "frameForward", group: "Seeking", label: "Frame forward when paused, otherwise +30s", bindings: ["."], scope: PLAYER_SCOPE },
        { id: "seekStart", group: "Seeking", label: "Jump to start", bindings: ["Home"], scope: PLAYER_SCOPE },
        { id: "seekEnd", group: "Seeking", label: "Jump to end", bindings: ["End"], scope: PLAYER_SCOPE },
        { id: "seekPercent", group: "Seeking", label: "Seek to 0-90%", bindings: ["0-9"], scope: PLAYER_SCOPE },
        { id: "mute", group: "Sound", label: "Mute", bindings: ["M"], scope: PLAYER_SCOPE },
        { id: "volumeUp", group: "Sound", label: "Volume up (Shift = larger step)", bindings: ["Up", "Shift+Up"], scope: PLAYER_SCOPE },
        { id: "volumeDown", group: "Sound", label: "Volume down (Shift = larger step)", bindings: ["Down", "Shift+Down"], scope: PLAYER_SCOPE },
        { id: "subtitleDelayDown", group: "Subtitles", label: "Subtitle delay earlier (Shift = finer)", bindings: ["Z", "Shift+Z"], scope: PLAYER_SCOPE },
        { id: "subtitleDelayUp", group: "Subtitles", label: "Subtitle delay later (Shift = finer)", bindings: ["X", "Shift+X"], scope: PLAYER_SCOPE },
        { id: "cycleSubtitle", group: "Subtitles", label: "Cycle subtitles", bindings: ["S", "C"], scope: PLAYER_SCOPE },
        { id: "abLoopA", group: "Loop", label: "Set A point", bindings: ["I"], scope: PLAYER_SCOPE },
        { id: "abLoopB", group: "Loop", label: "Set B point", bindings: ["O"], scope: PLAYER_SCOPE },
        { id: "abLoopClear", group: "Loop", label: "Clear A-B loop", bindings: ["L"], scope: PLAYER_SCOPE },
        { id: "stats", group: "Tools", label: "Playback stats", bindings: ["D"], scope: PLAYER_SCOPE },
        { id: "shortcuts", group: "Help", label: "Show shortcuts", bindings: ["?"], scope: PLAYER_SCOPE }
    ]
}

// Normalize one binding string. Shift+<letter> folds to the letter (Shift+Z == Z); everything
// else uppercases (named keys, symbols, digit range) for case-insensitive matching.
function bindingKey(binding) {
    var b = String(binding == null ? "" : binding).trim()
    if (!b.length) return ""
    var m = b.match(/^shift\+([a-z])$/i)
    if (m) return m[1].toUpperCase()
    return b.toUpperCase()
}

function actionForBinding(binding) {
    var key = bindingKey(binding)
    if (!key.length) return null
    var list = actions()
    for (var i = 0; i < list.length; i++) {
        var a = list[i]
        for (var b = 0; b < a.bindings.length; b++) {
            if (bindingKey(a.bindings[b]) === key)
                return a
        }
    }
    return null
}

// Numeric-keycode -> binding string used by actionForEvent (digits and ? handled specially).
var EVENT_BINDINGS = (function() {
    var m = {}
    m[K.Space] = "Space"; m[K.Escape] = "Esc"
    m[K.Left] = "Left"; m[K.Right] = "Right"; m[K.Up] = "Up"; m[K.Down] = "Down"
    m[K.Home] = "Home"; m[K.End] = "End"
    m[K.Comma] = ","; m[K.Period] = "."
    m[K.BracketLeft] = "["; m[K.BracketRight] = "]"
    m[K.Question] = "?"
    m[K.M] = "M"; m[K.S] = "S"; m[K.C] = "C"; m[K.Z] = "Z"; m[K.X] = "X"
    m[K.I] = "I"; m[K.O] = "O"; m[K.L] = "L"; m[K.D] = "D"
    return m
})()

// Resolve a QML key event ({ key, modifiers }) to an action record, or null. Modifier state does
// not change WHICH action fires (Shift only scales the effect inside PlayerPage.runHotkeyAction);
// the one exception is Shift+/ which yields '?'.
function actionForEvent(event) {
    if (!event) return null
    var key = event.key
    var mods = event.modifiers || 0
    if (key >= K.Zero && key <= K.Nine)
        return actionForBinding("0-9")
    if (key === K.Slash && (mods & SHIFT))
        return actionForBinding("?")
    var binding = EVENT_BINDINGS[key]
    if (binding === undefined)
        return null
    return actionForBinding(binding)
}

// Duplicate ACTIVE bindings across different action ids in the same scope. Intra-action duplicate
// bindings (e.g. Z and Shift+Z on one action) are ignored.
function detectConflicts(records) {
    var seen = {}
    var conflicts = []
    for (var i = 0; i < (records || []).length; i++) {
        var a = records[i]
        var scope = a.scope || PLAYER_SCOPE
        var localSeen = {}
        for (var b = 0; b < a.bindings.length; b++) {
            var nk = bindingKey(a.bindings[b])
            if (!nk.length || localSeen[nk]) continue
            localSeen[nk] = true
            var slot = scope + "::" + nk
            if (seen.hasOwnProperty(slot) && seen[slot] !== a.id)
                conflicts.push({ binding: nk, scope: scope, actions: [seen[slot], a.id] })
            else
                seen[slot] = a.id
        }
    }
    return conflicts
}

// Grouped records for ShortcutsSheet, group order by first appearance.
function groups() {
    var list = actions()
    var order = []
    var byGroup = {}
    for (var i = 0; i < list.length; i++) {
        var a = list[i]
        var g = a.group || "Other"
        if (!byGroup.hasOwnProperty(g)) { byGroup[g] = []; order.push(g) }
        byGroup[g].push({ id: a.id, label: a.label, keys: a.bindings.slice(), note: a.note || "" })
    }
    var out = []
    for (var j = 0; j < order.length; j++)
        out.push({ group: order[j], items: byGroup[order[j]] })
    return out
}
