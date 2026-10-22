// Regression for seeking/playing after a natural end on Android Media3.
//
// Nokia T20 (task015): re-arming an ended source re-emitted fileLoaded, which
// re-ran PlayerPage's one-shot startup logic. A pendingSeekSec of 0 left by
// "Start over" then sought back to 0 right after the user's seek to 20 s.
// fileLoaded must stay once per load; the re-armed source announces
// fileResumed, whose page handler only undoes onEndFile's state.
//
// Runs the ACTUAL onFileResumed body from qml/PlayerPage.qml with injected
// objects, plus source checks on the item. Run: node tests/android_media3_end_resume_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repo = join(dirname(fileURLToPath(import.meta.url)), "..");
const read = (rel) => readFileSync(join(repo, rel), "utf8").replace(/\r\n/g, "\n");
const qml = read("qml/PlayerPage.qml");
const item = read("native/player/androidmedia3item.cpp");

let failures = 0;
function check(cond, label) {
    console.log(cond ? "PASS" : "FAIL", label);
    if (!cond) failures += 1;
}

const resumed = /function onFileResumed\(\) \{([\s\S]*?)\n            \}\n/.exec(qml);
check(resumed !== null, "PlayerPage has an onFileResumed handler");
check(/target: mpv\n\s*ignoreUnknownSignals: true\n\s*function onFileResumed/.test(qml),
      "handler tolerates players without fileResumed (desktop)");
if (resumed) {
    const calls = [];
    const root = new Proxy({ fileReady: false, pendingSeekSec: 0, streamRouteRestoreIntent: "" }, {
        get: (t, k) => (k in t ? t[k] : () => calls.push(String(k))),
    });
    const mpv = { pause: false, seekExact: () => calls.push("seekExact") };
    new Function("root", "mpv", resumed[1])(root, mpv);
    check(root.fileReady === true, "fileResumed restores the playable state");
    check(calls.includes("cancelUpNext"), "fileResumed cancels an Up Next countdown");
    check(calls.includes("syncPowerInhibit"), "fileResumed re-syncs the screen-awake inhibit");
    check(!calls.includes("seekExact") && mpv.pause === false && root.pendingSeekSec === 0,
          "fileResumed neither seeks nor changes pause (the user's seek/play stands)");
}

const rearm = /bool AndroidMedia3Item::rearmIfEnded\(quint64 generation\)\n\{([\s\S]*?)\n\}/.exec(item);
check(rearm !== null && !/m_fileLoadedEmitted\s*=\s*false/.test(rearm[1]),
      "re-arm does not re-arm fileLoaded");
check(/if \(!m_fileLoadedEmitted\) \{\n\s*m_fileLoadedEmitted = true;\n\s*emit fileLoaded\(\);\n\s*\} else if \(m_resumeAfterEnd\) \{\n\s*m_resumeAfterEnd = false;\n\s*emit fileResumed\(\);/.test(item),
      "READY emits fileLoaded once per load, else fileResumed after a re-arm");
check(/m_fileLoadedEmitted = false;\n\s*m_resumeAfterEnd = false;/.test(item),
      "a new load clears a pending re-arm announcement");

console.log(`ANDROID_MEDIA3_END_RESUME ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
