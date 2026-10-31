// Qualifies Android timeline taps through the real QML press/release handlers.
// Also executes the full-screen Android video tap handler separately so its
// HUD behavior stays distinct from seeking. Run: node tests/android_timeline_tap_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const qml = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "qml", "PlayerPage.qml"), "utf8")
    .replace(/\r\n/g, "\n");

function matchingBrace(text, open) {
    if (text[open] !== "{") throw new Error("expected opening brace");
    let depth = 0;
    let quote = "";
    let lineComment = false;
    let blockComment = false;
    for (let i = open; i < text.length; i += 1) {
        const c = text[i];
        const next = text[i + 1];
        if (lineComment) {
            if (c === "\n") lineComment = false;
            continue;
        }
        if (blockComment) {
            if (c === "*" && next === "/") { blockComment = false; i += 1; }
            continue;
        }
        if (quote) {
            if (c === "\\") { i += 1; continue; }
            if (c === quote) quote = "";
            continue;
        }
        if (c === "/" && next === "/") { lineComment = true; i += 1; continue; }
        if (c === "/" && next === "*") { blockComment = true; i += 1; continue; }
        if (c === "\"" || c === "'") { quote = c; continue; }
        if (c === "{") depth += 1;
        else if (c === "}" && --depth === 0) return i;
    }
    throw new Error("unclosed QML block");
}

function objectBody(source, start) {
    const open = source.indexOf("{", start);
    if (open < 0) throw new Error("QML object body not found");
    const close = matchingBrace(source, open);
    return { open, close, text: source.slice(open + 1, close) };
}

function propertyBody(source, name, optional = false) {
    const match = new RegExp(`\\b${name}\\s*:\\s*\\{`).exec(source);
    if (!match) {
        if (optional) return null;
        throw new Error(`QML ${name} handler not found`);
    }
    const open = match.index + match[0].lastIndexOf("{");
    return source.slice(open + 1, matchingBrace(source, open));
}

function propertyExpression(source, name) {
    const match = new RegExp(`\\b${name}\\s*:\\s*([^\\n]+)`).exec(source);
    if (!match) throw new Error(`QML ${name} expression not found`);
    return match[1].trim();
}

function qmlFunction(name) {
    const match = new RegExp(`\\n    function ${name}\\(([^)]*)\\) \\{`).exec(qml);
    if (!match) throw new Error(`QML function ${name} not found`);
    const open = match.index + match[0].lastIndexOf("{");
    return { args: match[1], body: qml.slice(open + 1, matchingBrace(qml, open)) };
}

const seekAreaId = qml.indexOf("id: seekHover");
if (seekAreaId < 0) throw new Error("seekHover MouseArea not found");
const seekAreaStart = qml.lastIndexOf("MouseArea", seekAreaId);
const seekArea = objectBody(qml, seekAreaStart);
const seekAreaText = seekArea.text;
const seekBarId = qml.indexOf("id: seekBar");
const seekBar = objectBody(qml, qml.lastIndexOf("Item", seekBarId));
const seekRowId = qml.indexOf("id: seekRow");
const seekRow = objectBody(qml, qml.lastIndexOf("Row", seekRowId));

const catcherMarker = qml.indexOf("acceptedButtons: Qt.LeftButton | Qt.RightButton");
if (catcherMarker < 0) throw new Error("full-screen video MouseArea not found");
const catcherStart = qml.lastIndexOf("MouseArea", catcherMarker);
const catcher = objectBody(qml, catcherStart);
const parentClickMatch = /\bonClicked\s*:\s*function\(mouse\)\s*\{/.exec(catcher.text);
if (!parentClickMatch) throw new Error("Android video click handler not found");
const parentClickOpen = parentClickMatch.index + parentClickMatch[0].lastIndexOf("{");
const parentClickBody = catcher.text.slice(parentClickOpen + 1, matchingBrace(catcher.text, parentClickOpen));

function compileBody(args, body, scope) {
    return new Function("scope", args, `with (scope) { ${body}\n }`).bind(null, scope);
}

function makePlayer() {
    const seeks = [];
    const mpv = { duration: 5796, position: 468, seekExact: (sec) => seeks.push(sec) };
    const root = {
        seekPreview: mpv.position,
        seeking: false,
        seekTargetSec: -1,
        clamp: (v, lo, hi) => Math.max(lo, Math.min(hi, v)),
        activityDiscontinuity() {},
        wakeChrome() {},
    };
    const scope = { root, mpv, seeks, seekSettleGuard: { restart() {} }, WatchPartySync: undefined };
    root.previewAt = compileBody(qmlFunction("previewAt").args, qmlFunction("previewAt").body, scope);
    root.seekTo = compileBody(qmlFunction("seekTo").args, qmlFunction("seekTo").body, scope);
    root.requestUserSeekTo = compileBody(qmlFunction("requestUserSeekTo").args,
                                         qmlFunction("requestUserSeekTo").body, scope);
    return { root, mpv, scope, seeks };
}

function makeSeekArea(player, mouseX, width) {
    const scope = { ...player.scope, mouseX, width, seekBar: { hovered: false } };
    return {
        onPressed: compileBody("", propertyBody(seekAreaText, "onPressed"), scope),
        onReleased: compileBody("", propertyBody(seekAreaText, "onReleased"), scope),
    };
}

let failures = 0;
function check(cond, label) {
    console.log(cond ? "PASS" : "FAIL", label);
    if (!cond) failures += 1;
}

const player = makePlayer();
const seek = makeSeekArea(player, 446, 892); // midpoint coordinates from the Nokia T20 UI tree
seek.onPressed();
check(player.seeks.length === 0 && player.root.seeking,
      "timeline press previews without issuing a premature seek");
seek.onReleased();
check(player.seeks.length === 1 && Math.abs(player.seeks[0] - 2898) <= 1,
      "Android midpoint tap seeks through the real previewAt/requestUserSeekTo/seekTo bodies");
check(!player.root.seeking && player.root.seekTargetSec === player.seeks[0],
      "release ends seeking and records the settled target");

const desktopPlayer = makePlayer();
const desktopSeek = makeSeekArea(desktopPlayer, 600, 892);
desktopSeek.onPressed();
desktopSeek.onReleased();
check(desktopPlayer.seeks.length === 1 && Math.abs(desktopPlayer.seeks[0] - (5796 * 600 / 892)) < 0.01,
      "desktop timeline tap retains the same single release seek");

const chromeId = qml.indexOf("id: chrome");
const chromeStart = qml.lastIndexOf("Rectangle", chromeId);
const chrome = objectBody(qml, chromeStart);
const chromeText = chrome.text;
const chromeZ = /\bz:\s*(\d+)/.exec(chromeText);
check(catcherStart < chromeId && chromeId < seekAreaId && chromeZ && Number(chromeZ[1]) === 99999,
      "seekHover is in the high-z chrome above the full-screen video tap catcher");
check(/anchors\.fill:\s*parent/.test(seekAreaText)
      && /enabled:\s*mpv\.duration\s*>\s*0/.test(seekAreaText)
      && /height:\s*parent\.height/.test(seekBar.text)
      && /height:\s*42\b/.test(seekRow.text),
      "touch target fills the 42-pixel seek bar while playback duration is available");
check(!/\bpropagateComposedEvents\s*:\s*true/.test(seekAreaText),
      "seekHover keeps MouseArea's default composed-click handling, avoiding parent video-tap propagation");

const parentState = {
    root: {
        anyMenuOpen: false,
        controlsShownAtPress: true,
        controlsShown: true,
        hiddenByVideoTap: 0,
        wakeChrome() { this.controlsShown = true; },
        closeMenus() {},
        openContextMenu() {},
        togglePlayPause() {},
    },
    Qt: { RightButton: 2, LeftButton: 1 },
    PlatformRuntime: { android: true },
    hideTimer: { stops: 0, stop() { this.stops += 1; } },
};
// The click body hands Android taps to root.handleAndroidVideoTap(); compile the real one.
const tapFn = qml.indexOf("function handleAndroidVideoTap()");
const tapOpen = qml.indexOf("{", tapFn);
const tapBody = qml.slice(tapOpen + 1, matchingBrace(qml, tapOpen));
function attachVideoTap(state) {
    state.root.handleAndroidVideoTap = new Function("scope", `with (scope) { ${tapBody}
 }`).bind(null, state);
}
attachVideoTap(parentState);
const videoPress = new Function("scope", `with (scope) { ${propertyExpression(catcher.text, "onPressed")}\n }`).bind(null, parentState);
const videoClick = compileBody("mouse", parentClickBody, parentState);
videoPress();
videoClick({ button: parentState.Qt.LeftButton });
check(!parentState.root.controlsShown && parentState.hideTimer.stops === 1,
      "a separate Android video tap still hides the HUD through the real parent handlers");

const hiddenState = {
    ...parentState,
    root: { ...parentState.root, controlsShown: false },
    hideTimer: { stops: 0, stop() { this.stops += 1; } },
};
attachVideoTap(hiddenState);
new Function("scope", `with (scope) { ${propertyExpression(catcher.text, "onPressed")}\n }`).bind(null, hiddenState)();
compileBody("mouse", parentClickBody, hiddenState)({ button: hiddenState.Qt.LeftButton });
check(hiddenState.root.controlsShown && hiddenState.hideTimer.stops === 0,
      "a separate Android video tap reveals the HUD when it was hidden at press time");

if (failures) { console.error(`${failures} failure(s)`); process.exit(1); }