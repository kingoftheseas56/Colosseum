// Execute the actual retired-route, stream-ready, and file-loaded QML bodies. A
// Media3 reload drops sidecars; only a same-route restore may re-add the selected one.
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
const qml = readFileSync(new URL('../qml/PlayerPage.qml', import.meta.url), 'utf8').replace(/\r\n/g, '\n');
function body(name) {
    const m = new RegExp(`function ${name}\\([^)]*\\) \\{([\\s\\S]*?)\\n    \\}\\n`).exec(qml);
    return m ? m[1] : '';
}
function installRecoveryHelpers(root, mpv, logger) {
    const resetRecoveryResume = new Function('root', body('resetRecoveryResume'));
    const resetRecoveryWatch = new Function('root', body('resetRecoveryWatch'));
    const captureSelectedSidecarSubtitle = new Function('root', 'mpv', body('captureSelectedSidecarSubtitle'));
    const restoreSelectedSidecarSubtitle = new Function('root', 'mpv', 'console', 'subtitle', body('restoreSelectedSidecarSubtitle'));
    root.resetRecoveryResume = () => resetRecoveryResume(root);
    root.resetRecoveryWatch = () => resetRecoveryWatch(root);
    root.captureSelectedSidecarSubtitle = () => captureSelectedSidecarSubtitle(root, mpv);
    root.restoreSelectedSidecarSubtitle = subtitle => restoreSelectedSidecarSubtitle(root, mpv, logger, subtitle);
}
function run(selected, delay = 0.4, duringReconnect = null) {
    const additions = [], demands = [];
    const mpv = { position: 123, pause: false, subDelay: delay, subtitleTrack: selected?.id || '',
        addSubtitle: (...args) => additions.push(args), seekExact() {},
        loadSource() { this.position = 0; this.pause = false; this.subDelay = 0; this.subtitleTrack = ''; } };
    const values = { streamRouteRestorePending: false, streamRouteRestoreLoadPending: false, streamRouteRestoreSec: -1,
        streamRouteRestoreIntent: '', streamRouteRestoreSubtitle: null,
        streamRouteHash: 'abc', streamRouteFileIdx: 0, streamRouteRetired: () => true,
        selectedSubtitleRow: () => selected, addedOnlineUrls: { old: true }, subModelRev: 0,
        pendingSeekSec: -1, userTouchedSubs: false, currentShowKey: () => 'video:tt0063350' };
    const root = new Proxy(values, { get: (o, k) => k in o ? o[k] : () => {} });
    const consoleStub = { info() {} };
    installRecoveryHelpers(root, mpv, consoleStub);
    const watchdog = { restart() {}, stop() {} };
    const clearRestore = new Function('root', body('clearStreamRouteRestore'));
    root.clearStreamRouteRestore = () => clearRestore(root);
    root.restoreStreamRouteSubtitle = new Function('root', 'mpv', body('restoreStreamRouteSubtitle')).bind(null, root, mpv);
    const stream = { play: (...a) => demands.push(a), watchStats() {} };
    const platform = { applicationState: 'active' };
    new Function('root', 'mpv', 'streamWatchdog', 'Stream', 'PlatformRuntime', 'console', body('restoreRetiredStreamRoute'))(
        root, mpv, watchdog, stream, platform, consoleStub);
    if (duringReconnect)
        duringReconnect(root, mpv);
    // Duplicate activation while waiting must preserve the first snapshot.
    new Function('root', 'mpv', 'streamWatchdog', 'Stream', 'PlatformRuntime', 'console', body('restoreRetiredStreamRoute'))(
        root, mpv, watchdog, stream, platform, consoleStub);
    const ready = /function onStreamReady\(url, infoHash, fileIdx\) \{([\s\S]*?)\n        \}\n        function onStreamStats/.exec(qml)[1];
    new Function('root', 'mpv', 'Stream', 'streamWatchdog', 'url', 'infoHash', 'fileIdx', ready)(
        root, mpv, stream, watchdog, 'http://127.0.0.1:11450/stream/new', 'abc', 0);
    const loaded = /onFileLoaded:\s*\{([\s\S]*?)\n        \}\n        onPlaybackError:/.exec(qml)[1];
    new Function('root', 'mpv', 'streamWatchdog', loaded)(root, mpv, watchdog);
    return { root, mpv, additions, demands };
}
const sidecar = { id: 'x:7', external: true, url: 'https://example.test/english.srt', title: 'English', lang: 'eng' };
const r = run(sidecar);
assert.deepEqual(r.additions, [[sidecar.url, sidecar.title, sidecar.lang, true]], 'same-route reload re-adds the exact selected sidecar once');
assert.equal(r.mpv.subDelay, 0.4, 'same-route reload preserves sync');
assert.equal(r.root.streamRouteRestoreSubtitle, null, 'restore snapshot consumed');
assert.deepEqual(r.root.addedOnlineUrls, { [sidecar.url]: true }, 'cleared Media3 sidecars become available again');
assert.equal(r.demands.length, 1, 'duplicate activation does not duplicate demand');
assert.equal(run(null).additions.length, 0, 'Off stays off');
assert.equal(run({ id: 's:1', external: false }).additions.length, 0, 'embedded track is not re-added');
assert.equal(run(sidecar, 0.4, (root, mpv) => {
    root.saveTrackPreference = () => {};
    new Function('root', 'mpv', body('turnSubtitlesOff'))(root, mpv);
}).additions.length, 0, 'Off chosen during reconnect cancels the old sidecar snapshot');
for (const [name, delta, expected] of [['adjustSubtitleDelay', 0.1, 0.5], ['resetSubtitleDelay', 0, 0]]) {
    const changed = run(sidecar, 0.4, (root, mpv) => {
        root.supportsPlayerCapability = () => true;
        root.round2 = value => Math.round(value * 100) / 100;
        root.saveTrackPreference = () => {};
        new Function('root', 'mpv', 'delta', body(name))(root, mpv, delta);
    });
    assert.equal(changed.mpv.subDelay, expected, `${name} during reconnect wins over the captured sync`);
}
// Reset happens on genuinely new media and replacement sources, without retaining
// adapter sidecars across loadSource calls.
for (const name of ['resetTrackAutomation', 'playStreamAt', 'stop']) {
    const values = { streamRouteRestoreSubtitle: sidecar, streamRouteRestorePending: true,
        streamRouteRestoreLoadPending: true, streamRouteRestoreSec: 123,
        streamRouteRestoreIntent: 'paused', recoveryResumePending: false, streamCandidates: [{}],
        isStreamDead: () => false, directStreamUrl: () => '', currentStreamIndex: 0 };
    const root = new Proxy(values, { get: (o, k) => k in o ? o[k] : () => {} });
    installRecoveryHelpers(root, { position: 0, subDelay: 0, addSubtitle() {} }, { info() {} });
    root.clearStreamRouteRestore = () => new Function('root', body('clearStreamRouteRestore'))(root);
    if (name === 'playStreamAt') {
        new Function('root','mpv','streamWatchdog','Stream','console','index','reason', body(name))(
            root, { position: 0 }, { restart() {} }, { play() {} }, { info() {} }, 0, 'switch');
    } else {
        new Function('root','mpv','Stream', body(name))(root, { stopPlayback() {} }, { stopDemand() {} });
    }
    assert.equal(root.streamRouteRestoreSubtitle, null, `${name} discards stale subtitle restore`);
}
console.log('ANDROID_SUBTITLE_RESTORE PASS');
