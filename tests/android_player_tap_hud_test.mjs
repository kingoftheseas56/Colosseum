// Regression: on Android a tap on the video shows/hides the HUD and never
// pauses (Hemanth, Nokia T20, 2026-10-22); desktop clicks keep play/pause.
//
// Runs the ACTUAL video MouseArea onClicked body from qml/PlayerPage.qml with
// injected objects. Run: node tests/android_player_tap_hud_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const qml = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "qml", "PlayerPage.qml"), "utf8")
    .replace(/\r\n/g, "\n");
const m = /onPressed: root\.controlsShownAtPress = root\.controlsShown\n\s*onClicked: function\(mouse\) \{([\s\S]*?)\n        \}\n    \}/.exec(qml);
if (!m) throw new Error("video MouseArea onPressed/onClicked boundary not found");
const clicked = new Function("mouse", "root", "PlatformRuntime", "hideTimer", "Qt", "mapToItem", "chrome", m[1]);

let failures = 0;
function check(cond, label) {
    console.log(cond ? "PASS" : "FAIL", label);
    if (!cond) failures += 1;
}
function tap({ android, shownAtPress, paused = false, menuOpen = false, resumeChoiceOpen = false }) {
    const calls = [];
    const root = {
        controlsShown: true, controlsShownAtPress: shownAtPress, anyMenuOpen: menuOpen, paused, resumeChoiceOpen,
        togglePlayPause() { calls.push("toggle"); this.paused = !this.paused; },
        wakeChrome() { calls.push("wake"); this.controlsShown = true; },
        closeMenus() { calls.push("closeMenus"); },
        openContextMenu() { calls.push("context"); },
    };
    if (!shownAtPress) root.controlsShown = false;
    const hideTimer = { stop: () => calls.push("hideTimer.stop") };
    const body = /function handleAndroidVideoTap\(\) \{([\s\S]*?)\n    \}/.exec(qml)?.[1];
    if (!body) throw new Error("Actual Android video-tap owner is missing");
    root.handleAndroidVideoTap = () => new Function("root", "hideTimer", body)(root, hideTimer);
    clicked({ button: 1 }, root, { android }, hideTimer, { RightButton: 2 }, () => ({}), {});
    return { root, calls };
}

for (const paused of [false, true]) {
    const hide = tap({ android: true, shownAtPress: true, paused });
    check(hide.root.controlsShown === false && !hide.calls.includes("toggle") && hide.root.paused === paused,
          `android tap with HUD up hides it, no pause change (paused=${paused})`);
    const show = tap({ android: true, shownAtPress: false, paused });
    check(show.root.controlsShown === true && show.calls.includes("wake") && !show.calls.includes("toggle"),
          `android tap with HUD hidden shows it, no pause change (paused=${paused})`);
}
const menu = tap({ android: true, shownAtPress: true, menuOpen: true });
check(menu.calls.join() === "closeMenus" && menu.root.controlsShown === true,
      "android tap with a menu open only closes the menu");
const desktop = tap({ android: false, shownAtPress: true });
check(desktop.calls.includes("toggle"), "desktop click still toggles play/pause");
const resume = tap({ android: true, shownAtPress: true, resumeChoiceOpen: true });
check(resume.calls.length === 0 && resume.root.controlsShown,
      "outside tap cannot hide an open resume-choice dialog");

console.log(`ANDROID_PLAYER_TAP_HUD ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
