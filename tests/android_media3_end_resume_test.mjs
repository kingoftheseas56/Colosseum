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

function balancedBlock(source, openingBrace) {
    let depth = 0;
    let mode = "";
    let escaped = false;
    for (let i = openingBrace; i < source.length; i++) {
        const c = source[i];
        const next = source[i + 1];
        if (mode === "//") {
            if (c === "\n") mode = "";
            continue;
        }
        if (mode === "/*") {
            if (c === "*" && next === "/") { mode = ""; i++; }
            continue;
        }
        if (mode) {
            if (escaped) escaped = false;
            else if (c === "\\") escaped = true;
            else if (c === mode) mode = "";
            continue;
        }
        if (c === "/" && next === "/") { mode = "//"; i++; continue; }
        if (c === "/" && next === "*") { mode = "/*"; i++; continue; }
        if (c === "\"" || c === "'" || c === "`") { mode = c; continue; }
        if (c === "{") depth++;
        if (c === "}" && --depth === 0) return source.slice(openingBrace, i + 1);
    }
    throw new Error("unclosed production block");
}

function productionFunctionBody(name) {
    const marker = `function ${name}(`;
    const at = qml.indexOf(marker);
    if (at < 0) throw new Error(`production function not found: ${name}`);
    const openingBrace = qml.indexOf("{", at + marker.length);
    if (openingBrace < 0) throw new Error(`function body not found: ${name}`);
    return balancedBlock(qml, openingBrace);
}

const resumed = /function onFileResumed\(\) \{([\s\S]*?)\n            \}\n/.exec(qml);
check(resumed !== null, "PlayerPage has an onFileResumed handler");
check(/target: mpv\n\s*ignoreUnknownSignals: true\n\s*function onFileResumed/.test(qml),
      "handler tolerates players without fileResumed (desktop)");
if (resumed) {
    const calls = [];
    const root = new Proxy({ fileReady: false, playbackEnded: true,
        pendingSeekSec: 0, streamRouteRestoreIntent: "" }, {
        get: (t, k) => (k in t ? t[k] : () => calls.push(String(k))),
    });
    const mpv = { pause: false, seekExact: () => calls.push("seekExact") };
    new Function("root", "mpv", resumed[1])(root, mpv);
    check(root.fileReady === true, "fileResumed restores the playable state");
    check(calls.includes("cancelUpNext"), "fileResumed cancels an Up Next countdown");
    check(calls.includes("syncPowerInhibit"), "fileResumed re-syncs the screen-awake inhibit");
    check(root.playbackEnded === false, "fileResumed clears the natural-end marker");
    check(!calls.includes("prepareResumeChoice") && !calls.includes("loadSource"),
          "fileResumed does not re-enter one-shot startup or reload the source");
    // T20: replay at 0 s tripped "source did not start" on stale recovery clocks.
    check(calls.indexOf("resetRecoveryWatch") >= 0 && calls.indexOf("resetRecoveryWatch") < 2,
          "fileResumed restarts the recovery watch before re-entering the playable state");
    check(!calls.includes("seekExact") && mpv.pause === false && root.pendingSeekSec === 0,
          "fileResumed neither seeks nor changes pause (the user's seek/play stands)");
}

function endedPlaybackHarness(os, phoneShellArg = false) {
    const calls = [];
    const root = {
        playbackEnded: true,
        starting: false,
        errored: false,
        fileReady: false,
        phoneShellArg,
        androidHost: os === "android" || phoneShellArg,
        pendingSeekSec: 41,
        wakeReconnectPendingSeek: -1,
        resumeChoiceOpen: false,
        resumeChoiceSec: -1,
        resumePromptConsumed: false,
        autoPausedInactive: false,
        statusMsg: "Playback finished",
        activeDirectHeaders: { Authorization: "test" },
        requestAudioPlayback: () => calls.push("requestAudioPlayback"),
        cancelUpNext: () => calls.push("cancelUpNext"),
        resetRecoveryWatch: () => calls.push("resetRecoveryWatch"),
        wakeChrome: () => calls.push("wakeChrome"),
    };
    const url = "https://media.example/episode.m3u8";
    let currentUrlReads = 0;
    Object.defineProperty(root, "currentPlaybackUrl", {
        get() { currentUrlReads++; return url; },
    });
    const mpv = {
        pause: true,
        position: 0,
        loadSource: (source, headers) => calls.push(["loadSource", source, headers]),
        seekExact: sec => calls.push(["seekExact", sec]),
    };
    const Qt = { platform: { os } };
    const streamWatchdog = { restart: () => calls.push("streamWatchdog.restart") };
    const reloadActiveDirectSource = new Function("root", "mpv",
        `return (function reloadActiveDirectSource() ${productionFunctionBody("reloadActiveDirectSource")});`)(root, mpv);
    root.reloadActiveDirectSource = () => {
        calls.push("reloadActiveDirectSource");
        return reloadActiveDirectSource();
    };
    root.replayEndedPlayback = new Function("root", "mpv", "streamWatchdog", "Qt", "WatchPartySync",
        `return (function replayEndedPlayback() ${productionFunctionBody("replayEndedPlayback")});`)(
            root, mpv, streamWatchdog, Qt, undefined);
    const togglePlayPause = new Function("root", "mpv", "Qt", "WatchPartySync",
        `return (function togglePlayPause() ${productionFunctionBody("togglePlayPause")});`)(
            root, mpv, Qt, undefined);
    return { root, mpv, calls, togglePlayPause, currentUrlReads: () => currentUrlReads, url };
}

for (const [os, phoneShellArg, label] of [
    ["android", false, "Android"],
    ["windows", false, "desktop"],
    ["windows", true, "desktop phone-shell"],
]) {
    const playback = endedPlaybackHarness(os, phoneShellArg);
    playback.togglePlayPause();
    if (os === "android") {
        check(playback.mpv.pause === false,
              "Android explicit Play after EOF uses native pause=false to re-arm the ended source");
        check(!playback.calls.includes("reloadActiveDirectSource")
              && !playback.calls.some(call => Array.isArray(call) && call[0] === "loadSource"),
              "Android EOF Play does not reload or re-admit currentPlaybackUrl");
        check(!playback.calls.includes("streamWatchdog.restart"),
              "Android EOF Play leaves startup/reload monitoring to fileResumed");
    } else {
        check(playback.calls.includes("reloadActiveDirectSource")
              && playback.calls.some(call => Array.isArray(call) && call[0] === "loadSource"
                  && call[1] === playback.url),
              `${label} EOF Play retains upstream source reload`);
        check(playback.calls.includes("streamWatchdog.restart"),
              `${label} EOF replay retains its existing startup watchdog`);
    }
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
