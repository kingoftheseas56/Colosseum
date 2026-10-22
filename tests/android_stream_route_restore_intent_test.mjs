// Behavioral regression for the Android retired-stream-route reopen.
//
// Runs the ACTUAL restoreRetiredStreamRoute() and onFileLoaded bodies from
// qml/PlayerPage.qml with injected root/player/platform objects (no Qt, no
// device). A background quiesce retires the Stream route; on return the page
// re-demands it and the fresh Media3 load autoplays, so the reopen must restore
// the user's playback intent whether or not a position seek is needed.
//
// Run: node tests/android_stream_route_restore_intent_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const qmlPath = join(dirname(fileURLToPath(import.meta.url)), "..", "qml", "PlayerPage.qml");
const text = readFileSync(qmlPath, "utf8").replace(/\r\n/g, "\n");

function extract(re, what) {
    const m = re.exec(text);
    if (!m) throw new Error(`PlayerPage.qml: ${what} boundary not found`);
    return m[1];
}
const restoreBody = extract(/function restoreRetiredStreamRoute\(\) \{([\s\S]*?)\n    \}\n/, "restoreRetiredStreamRoute");
const loadedBody = extract(/onFileLoaded:\s*\{([\s\S]*?)\n        \}\n        onPlaybackError:/, "onFileLoaded");
const playStreamAtBody = extract(/function playStreamAt\(index, reason\) \{([\s\S]*?)\n    \}\n/, "playStreamAt");
const restore = new Function("root", "mpv", "streamWatchdog", "Stream", "PlatformRuntime", "console", restoreBody);
const loaded = new Function("root", "mpv", "streamWatchdog", loadedBody);

let failures = 0;
function check(cond, label) {
    if (cond) {
        console.log("PASS", label);
    } else {
        failures += 1;
        console.log("FAIL", label);
    }
}

function makeRoot(extra = {}) {
    const values = {
        streamRouteRestorePending: false, streamRouteRestoreSec: -1, streamRouteRestoreIntent: "",
        wakeReconnectPendingSeek: -1, pendingSeekSec: -1, starting: true, statusMsg: "",
        streamRouteHash: "7f13e4dfe42445a675cccb6fda1e7549babad2a8", streamRouteFileIdx: 0,
        streamRouteRetired: () => true, resetRecoveryWatch() {}, ...extra,
    };
    // Unlisted page helpers called by onFileLoaded are no-ops here.
    return new Proxy(values, { get: (t, k) => (k in t ? t[k] : () => {}) });
}
const quiet = { info() {} };
const watchdog = { restart() {}, stop() {} };
const active = { applicationState: "active" };

// Background return: record intent at the re-demand, then the fresh load.
function reopen(positionSec, paused) {
    const root = makeRoot();
    const seeks = [];
    const plays = [];
    const before = { position: positionSec, pause: paused };
    restore(root, before, watchdog, { play: (h, i) => plays.push([h, i]) }, active, quiet);
    // AndroidMedia3Item::loadSource: a fresh load starts playing at 0.
    const after = { position: 0, duration: 60, pause: false, seekExact: (s) => seeks.push(s) };
    loaded(root, after, watchdog);
    return { root, after, seeks, plays };
}

for (const paused of [true, false]) {
    for (const sec of [0, 0.5, 1, 15]) {
        const r = reopen(sec, paused);
        const tag = `${paused ? "paused" : "playing"} restore at ${sec}s`;
        check(r.plays.length === 1, `${tag}: one re-demand`);
        check(r.after.pause === paused, `${tag}: playback intent restored`);
        check(sec > 1 ? r.seeks.length === 1 && r.seeks[0] === sec : r.seeks.length === 0,
              `${tag}: ${sec > 1 ? "seeks to saved position" : "no seek"}`);
        check(r.root.streamRouteRestoreIntent === "" && r.root.streamRouteRestoreSec === -1,
              `${tag}: restore state consumed`);
    }
}

{
    // An ordinary fresh load with no restore pending keeps autoplay, even if a
    // stale pause carried over from a previous deliberate pause.
    const root = makeRoot();
    const mpv = { position: 0, duration: 60, pause: true, seekExact() {} };
    loaded(root, mpv, watchdog);
    check(mpv.pause === false, "fresh load without restore: autoplays");
}
{
    // A second load after a consumed restore is ordinary again.
    const r = reopen(0.5, true);
    const mpv = { position: 0, duration: 60, pause: true, seekExact() {} };
    loaded(r.root, mpv, watchdog);
    check(mpv.pause === false, "load after a consumed paused restore: no stale pause");
}
{
    // Not active (background): no re-demand and no intent recorded.
    const root = makeRoot();
    const plays = [];
    const handled = restore(root, { position: 0.5, pause: true }, watchdog,
                            { play: () => plays.push(1) }, { applicationState: "suspended" }, quiet);
    check(handled === false && plays.length === 0 && root.streamRouteRestoreIntent === "",
          "background: no re-demand, no intent");
}
// A fresh stream start and a real close both discard a pending restore intent.
check(/root\.streamRouteRestoreIntent = ""[\s\S]*Stream\.play\(/.test(playStreamAtBody),
      "playStreamAt clears a pending restore intent before demanding");
check(/root\.streamRouteRestoreSec = -1\n\s*root\.streamRouteRestoreIntent = ""\n\s*\/\/ A real close/.test(text),
      "close clears the restore intent");

console.log(`ANDROID_STREAM_ROUTE_RESTORE_INTENT ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
