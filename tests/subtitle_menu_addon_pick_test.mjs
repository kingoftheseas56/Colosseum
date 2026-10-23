// Regression: picking a not-yet-loaded add-on subtitle row ("ext:...") showed
// "Couldn't switch subtitle" even though the subtitle loaded, because the player
// adds it under a new track id that never equals "ext:..." (Nokia T20, 2026-10-23).
//
// Runs the ACTUAL pickTrack / resolvePending / failPending / clearPending bodies
// from qml/SubtitleMenu.qml. Run: node tests/subtitle_menu_addon_pick_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const qml = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "qml", "SubtitleMenu.qml"), "utf8")
    .replace(/\r\n/g, "\n");
function body(name) {
    const m = new RegExp(`\\n    function ${name}\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}\\n`).exec(qml);
    if (!m) throw new Error(`function ${name} not found`);
    return { args: m[1], text: m[2] };
}

function makeMenu(selectedId) {
    const menu = {
        selectedId, selectionError: "", pendingId: "", pendingOff: false, pendingOnline: false,
        pendingBaseId: "", panelOpen: true, picked: [],
        get pending() { return this.pendingId.length > 0 || this.pendingOff || this.pendingOnline; },
        trackPicked(id) { this.picked.push(id); },
        pendingTimer: { restart() {}, stop() {} },
    };
    for (const name of ["pickTrack", "resolvePending", "failPending", "clearPending"]) {
        const f = body(name);
        // sloppy-mode `with` resolves the QML component scope's bare names on the fake menu
        menu[name] = new Function("menu", f.args, `with (menu) {${f.text}\n}`).bind(null, menu);
    }
    return menu;
}

let failures = 0;
function check(cond, label) {
    console.log(cond ? "PASS" : "FAIL", label);
    if (!cond) failures += 1;
}

const addon = makeMenu("");
addon.pickTrack("ext:OpenSubtitles#3614689");
check(addon.picked[0] === "ext:OpenSubtitles#3614689", "add-on pick is still emitted with its row id");
addon.selectedId = "x:3";               // the player loaded it under its own id
addon.resolvePending();
check(!addon.pending && !addon.panelOpen && addon.selectionError === "",
      "add-on pick confirms when a new track becomes selected");

const swap = makeMenu("x:1");
swap.pickTrack("ext:OpenSubtitles#4256041");
swap.resolvePending();
check(swap.pending && swap.panelOpen, "add-on pick does not confirm on the previously selected track");
swap.failPending();
check(swap.selectionError.length > 0, "add-on pick that never loads still reports the error");

const embedded = makeMenu("");
embedded.pickTrack("s:2:0");
embedded.selectedId = "s:2:0";
embedded.resolvePending();
check(!embedded.pending && !embedded.panelOpen, "embedded pick still confirms by matching its id");

if (failures) { console.error(`${failures} failure(s)`); process.exit(1); }
