// Android background return re-demands retired streams but waits for an explicit
// Play tap. Runs the actual PlayerPage route, onStreamReady, fileLoaded, and
// togglePlayPause bodies with injected objects (no Qt or device required).
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
const readyBody = extract(/function onStreamReady\(url, infoHash, fileIdx\) \{([\s\S]*?)\n        \}\n        function onStreamStats/, "onStreamReady");
const loadedBody = extract(/onFileLoaded:\s*\{([\s\S]*?)\n        \}\n        onPlaybackError:/, "onFileLoaded");
const subtitleBody = extract(/function restoreStreamRouteSubtitle\(\) \{([\s\S]*?)\n    \}\n\n    function tickWakeReconnect/, "restoreStreamRouteSubtitle");
const resetRecoveryResumeBody = extract(/function resetRecoveryResume\(\) \{([\s\S]*?)\n    \}\n/, "resetRecoveryResume");
const resetRecoveryWatchBody = extract(/function resetRecoveryWatch\(\) \{([\s\S]*?)\n    \}\n/, "resetRecoveryWatch");
const captureSelectedSubtitleBody = extract(/function captureSelectedSidecarSubtitle\(\) \{([\s\S]*?)\n    \}\n/, "captureSelectedSidecarSubtitle");
const restoreSelectedSubtitleBody = extract(/function restoreSelectedSidecarSubtitle\(subtitle\) \{([\s\S]*?)\n    \}\n/, "restoreSelectedSidecarSubtitle");
const toggleBody = extract(/function togglePlayPause\(\) \{([\s\S]*?)\n    \}\n/, "togglePlayPause");
const clearBody = /function clearStreamRouteRestore\(\) \{([\s\S]*?)\n    \}\n/.exec(text)?.[1] || null;
const playStreamAtBody = extract(/function playStreamAt\(index, reason\) \{([\s\S]*?)\n    \}\n/, "playStreamAt");
const playTorrentBody = extract(/function playTorrent\(infoHash, fileIdx, title, posterUrl, subType, subId, streamCandidates, playbackContext\) \{([\s\S]*?)\n    \}\n/, "playTorrent");
const playLocalBody = extract(/function playLocalFile\(target\) \{([\s\S]*?)\n    \}\n/, "playLocalFile");
const playRemoteBody = extract(/function playRemoteUrl\(target\) \{([\s\S]*?)\n    \}\n/, "playRemoteUrl");
const minimizeBody = extract(/function handleWindowMinimize\(\) \{([\s\S]*?)\n    \}\n/, "handleWindowMinimize");
const resumeMinimizeBody = extract(/function resumeFromMinimize\(\) \{([\s\S]*?)\n    \}\n/, "resumeFromMinimize");

const restore = new Function("root", "mpv", "streamWatchdog", "Stream", "PlatformRuntime", "console", restoreBody);
const onReady = new Function("root", "mpv", "Stream", "streamWatchdog", "url", "infoHash", "fileIdx", readyBody);
const loaded = new Function("root", "mpv", "streamWatchdog", loadedBody);
const restoreSubtitle = new Function("root", "mpv", subtitleBody);
const resetRecoveryResume = new Function("root", resetRecoveryResumeBody);
const resetRecoveryWatch = new Function("root", resetRecoveryWatchBody);
const captureSelectedSubtitle = new Function("root", "mpv", captureSelectedSubtitleBody);
const restoreSelectedSubtitle = new Function("root", "mpv", "console", "subtitle", restoreSelectedSubtitleBody);
const toggle = new Function("root", "mpv", "WatchPartySync", toggleBody);
const quiet = { info() {} };
const watchdog = { restart() {}, stop() {} };
const active = { applicationState: "active" };
const clearRestore = clearBody ? new Function("root", clearBody) : null;

let failures = 0;
function check(cond, label) {
    if (cond) console.log("PASS", label);
    else { failures += 1; console.log("FAIL", label); }
}

function makePlayback(positionSec, manuallyPaused) {
    const calls = [];
    const sidecar = { external: true, url: "https://subs.example/en.srt", title: "English CC", lang: "en", label: "English CC" };
    const state = {
        streamRouteRestorePending: false,
        streamRouteRestoreLoadPending: false,
        streamRouteRestoreSec: -1,
        streamRouteRestoreIntent: "",
        streamRouteRestoreSubtitle: null,
        streamRouteUrl: "http://127.0.0.1:11450/stream/old",
        streamRouteHash: "7f13e4dfe42445a675cccb6fda1e7549babad2a8",
        streamRouteFileIdx: 3,
        currentPlaybackUrl: "http://127.0.0.1:11450/stream/old",
        starting: false,
        errored: false,
        streamRouteRetired: () => true,
        selectedSubtitleRow: () => sidecar,
        addedOnlineUrls: {},
        subModelRev: 0,
        userTouchedSubs: false,
        autoSubDone: false,
        calls,
    };
    const root = new Proxy(state, {
        get: (target, key) => key in target ? target[key] : (..._args) => calls.push(`root:${String(key)}`),
    });
    if (clearRestore)
        root.clearStreamRouteRestore = () => clearRestore(root);
    root.resetRecoveryResume = () => resetRecoveryResume(root);
    root.resetRecoveryWatch = () => resetRecoveryWatch(root);
    root.captureSelectedSidecarSubtitle = () => captureSelectedSubtitle(root, mpv);
    root.restoreSelectedSidecarSubtitle = subtitle => restoreSelectedSubtitle(root, mpv, quiet, subtitle);
    let paused = manuallyPaused;
    const mpv = {
        position: positionSec,
        duration: 9000,
        subDelay: -0.375,
        seekExact(sec) { calls.push(`seek:${sec}`); },
        loadSource(url) {
            calls.push(`load:${url}`);
            this.position = 0;
            this.subDelay = 0;
            this.pause = false; // Android Media3 starts a fresh generation play-ready.
        },
        addSubtitle(url, title, lang, select) { calls.push(`subtitle:${url}:${title}:${lang}:${select}`); },
    };
    Object.defineProperty(mpv, "pause", {
        get: () => paused,
        set: value => { paused = !!value; calls.push(`pause:${paused}`); },
    });
    root.restoreStreamRouteSubtitle = () => restoreSubtitle(root, mpv);
    const streamCalls = [];
    const stream = {
        play: (hash, fileIdx) => streamCalls.push(["play", hash, fileIdx]),
        watchStats: (hash, fileIdx) => streamCalls.push(["watchStats", hash, fileIdx]),
    };
    return { root, mpv, stream, streamCalls, calls, sidecar };
}

function startReturn(positionSec, manuallyPaused) {
    const s = makePlayback(positionSec, manuallyPaused);
    const oldPosition = s.mpv.position;
    const oldPaused = s.mpv.pause;
    restore(s.root, s.mpv, watchdog, s.stream, active, quiet);
    return { ...s, oldPosition, oldPaused };
}

// Both a playing session and a deliberately paused session return paused. The
// page still re-demands the same torrent/file, position, selected sidecar, and delay.
for (const manuallyPaused of [false, true]) {
    const s = startReturn(5132.649, manuallyPaused);
    const tag = manuallyPaused ? "manual pause" : "previously playing";
    check(s.streamCalls.length === 1 && s.streamCalls[0][0] === "play"
          && s.streamCalls[0][1] === s.root.streamRouteHash && s.streamCalls[0][2] === 3,
          `${tag}: one re-demand for the same route identity`);
    check(s.root.streamRouteRestoreSec === 5132.649, `${tag}: retains the saved position`);
    check(s.root.streamRouteRestoreIntent === "paused" && s.mpv.pause,
          `${tag}: return intent is paused`);
    check(s.root.streamRouteRestoreSubtitle.url === s.sidecar.url
          && s.root.streamRouteRestoreSubtitle.lang === "en"
          && s.root.streamRouteRestoreSubtitle.delay === -0.375,
          `${tag}: snapshots selected subtitle and sync delay`);

    // A late fileLoaded from the retired generation must not consume the return
    // state before Stream publishes the replacement route.
    loaded(s.root, s.mpv, watchdog);
    check(s.root.streamRouteRestoreIntent === "paused" && s.root.streamRouteRestoreSec === 5132.649
          && s.root.streamRouteRestoreSubtitle.url === s.sidecar.url,
          `${tag}: stale fileLoaded leaves restore state untouched`);

    onReady(s.root, s.mpv, s.stream, watchdog, "http://127.0.0.1:11450/stream/new", s.root.streamRouteHash, 3);
    check(s.root.streamRouteRestoreLoadPending === true && s.mpv.pause,
          `${tag}: replacement load is paused before fileLoaded`);
    loaded(s.root, s.mpv, watchdog);
    check(s.mpv.pause === true, `${tag}: fileLoaded does not autoplay`);
    check(s.calls.includes("seek:5132.649"), `${tag}: seeks to the saved position`);
    check(s.mpv.subDelay === -0.375 && s.calls.some(x => x.startsWith(`subtitle:${s.sidecar.url}:`)),
          `${tag}: restores selected sidecar and sync delay`);
    check(s.root.streamRouteRestoreLoadPending === false && s.root.streamRouteRestoreIntent === "",
          `${tag}: consumes return state after replacement fileLoaded`);

    toggle(s.root, s.mpv, undefined);
    check(s.mpv.pause === false, `${tag}: explicit Play resumes after restoration`);
}

// A Play tap during route recovery becomes the intent for the pending load. A stale
// callback may park the retired generation, but cannot erase that user intent.
{
    const s = startReturn(15, false);
    check(s.root.streamRouteRestoreIntent === "paused", "recovery defaults to paused before explicit input");
    toggle(s.root, s.mpv, undefined);
    check(s.root.streamRouteRestoreIntent === "playing" && s.mpv.pause === false,
          "explicit Play during recovery updates the pending playback intent");
    loaded(s.root, s.mpv, watchdog);
    check(s.root.streamRouteRestoreIntent === "playing" && s.root.streamRouteRestoreSec === 15,
          "stale fileLoaded cannot consume an explicit Play intent");
    onReady(s.root, s.mpv, s.stream, watchdog, "http://127.0.0.1:11450/stream/new", s.root.streamRouteHash, 3);
    check(s.mpv.pause === false, "replacement route honors explicit Play before fileLoaded");
    loaded(s.root, s.mpv, watchdog);
    check(s.mpv.pause === false && s.calls.includes("seek:15"),
          "replacement fileLoaded preserves explicit Play and saved position");
}

// Play after the replacement file is ready behaves as a normal explicit toggle.
{
    const s = startReturn(0.5, false);
    onReady(s.root, s.mpv, s.stream, watchdog, "http://127.0.0.1:11450/stream/new", s.root.streamRouteHash, 3);
    loaded(s.root, s.mpv, watchdog);
    check(s.mpv.pause === true, "near-start return is paused without a seek");
    toggle(s.root, s.mpv, undefined);
    check(s.mpv.pause === false, "Play after the paused return resumes the same route");
}

// A fresh source selected while an Android return is pending must clear that old
// route's saved state instead of inheriting its pause, seek, or subtitle.
check(clearBody !== null, "PlayerPage has a dedicated retired-route restore reset");
if (clearBody) {
    const s = startReturn(50, false);
    clearRestore(s.root);
    check(!s.root.streamRouteRestorePending && !s.root.streamRouteRestoreLoadPending
          && s.root.streamRouteRestoreSec === -1 && s.root.streamRouteRestoreIntent === ""
          && s.root.streamRouteRestoreSubtitle === null,
          "fresh source reset clears every retired-route restore field");
    check(/root\.clearStreamRouteRestore\(\)/.test(playStreamAtBody)
          && /root\.clearStreamRouteRestore\(\)/.test(playTorrentBody)
          && /root\.clearStreamRouteRestore\(\)/.test(playLocalBody)
          && /root\.clearStreamRouteRestore\(\)/.test(playRemoteBody),
          "torrent session, stream, local, and direct fresh sources discard retired-route state");

    // playTorrent must clear at session entry even when no stream candidate is
    // available and the later playStreamAt call exits before selecting a stream.
    const fresh = startReturn(5132.649, false);
    fresh.root.normalizeStreamCandidates = () => [];
    fresh.root.findStreamIndex = () => -1;
    fresh.root.resolveAdjacentContext = () => ({});
    fresh.root.fetchSubtitles = () => {};
    fresh.root.playStreamAt = () => {};
    const playTorrent = new Function("root", "infoHash", "fileIdx", "title", "posterUrl",
                                     "subType", "subId", "streamCandidates", "playbackContext", playTorrentBody);
    playTorrent(fresh.root, "fresh-hash", 0, "Another film", "", "movie", "tt1234567", [], {});
    check(!fresh.root.streamRouteRestorePending && !fresh.root.streamRouteRestoreLoadPending
          && fresh.root.streamRouteRestoreSec === -1 && fresh.root.streamRouteRestoreIntent === ""
          && fresh.root.streamRouteRestoreSubtitle === null,
          "new torrent session clears retired-route state even with no playable candidates");
}

// A different route arriving while a return is pending is a fresh selection, not
// the retired route. It must not inherit that route's position, pause intent, or
// selected sidecar.
{
    const s = startReturn(5132.649, false);
    onReady(s.root, s.mpv, s.stream, watchdog,
            "http://127.0.0.1:11450/stream/another", "different-hash", 0);
    check(!s.root.streamRouteRestorePending && !s.root.streamRouteRestoreLoadPending
          && s.root.streamRouteRestoreSec === -1 && s.root.streamRouteRestoreIntent === ""
          && s.root.streamRouteRestoreSubtitle === null,
          "different route clears the retired route's pending restore state");
    loaded(s.root, s.mpv, watchdog);
    check(s.mpv.pause === false && !s.calls.includes("seek:5132.649")
          && !s.calls.some(x => x.startsWith(`subtitle:${s.sidecar.url}:`)),
          "fresh route loads without inheriting retired position or sidecar selection");
}

// Ordinary fresh loads still clear a stale manual pause. Desktop warm minimize
// remains pause-then-resume, and never undoes a manual pause.
{
    const root = { starting: false, errored: false, windowMinimized: true, pipMode: false,
                   fileReady: true, autoPausedInactive: false, wakeChrome() {}, healAudio() {} };
    const mpv = { pause: false };
    const minimize = new Function("root", "mpv", minimizeBody);
    const resume = new Function("root", "mpv", resumeMinimizeBody);
    minimize(root, mpv);
    check(root.autoPausedInactive && mpv.pause, "desktop warm minimize pauses the active stream");
    root.windowMinimized = false;
    minimize(root, mpv);
    check(!root.autoPausedInactive && !mpv.pause, "desktop warm minimize still auto-resumes");
    root.windowMinimized = true;
    mpv.pause = true;
    minimize(root, mpv);
    root.windowMinimized = false;
    resume(root, mpv);
    check(mpv.pause === true, "desktop minimize does not undo a manual pause");
}

// New Stream requests and close must clear a pending return load; Android lifecycle
// handling remains separate from desktop minimization.
check(/root\.clearStreamRouteRestore\(\)[\s\S]*?Stream\.play\(/.test(playStreamAtBody),
      "playStreamAt clears retired-route state before a fresh demand");
check(/target: PlatformRuntime\n\s*enabled: PlatformRuntime\.android\n[\s\S]*?root\.restoreRetiredStreamRoute\(\)/.test(text),
      "retired-route foreground recovery stays Android-only");
check(/root\.clearStreamRouteRestore\(\)[\s\S]*?\/\/ A real close ends the stream's demand/.test(text),
      "close clears retired-route state");

console.log(`ANDROID_STREAM_ROUTE_RESTORE_INTENT ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
