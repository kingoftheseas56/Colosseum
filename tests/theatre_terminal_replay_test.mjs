import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const player = fs.readFileSync(new URL('../qml/PlayerPage.qml', import.meta.url), 'utf8');
const main = fs.readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8');
const policySource = fs.readFileSync(new URL('../qml/DirectSourcePolicy.js', import.meta.url), 'utf8').replace(/^\s*\.pragma library\s*/, '');
function fn(source, name, context) {
    const match = source.match(new RegExp('    function ' + name + '\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));
    assert.ok(match, name + ' production function exists');
    return vm.runInNewContext('(function(' + match[1] + '){' + match[2] + '\n})', context);
}
function handler(name, context, args = '') {
    const match = player.match(new RegExp('        ' + name + ': (?:function\\([^)]*\\) )?\\{([\\s\\S]*?)\\n        \\}'));
    assert.ok(match, name + ' production handler exists');
    return vm.runInNewContext('(function(' + args + '){' + match[1] + '\n})', context);
}
function fixture() {
    const events = [], loads = [];
    const mpv = {pause: false, position: 35, duration: 35,
        loadSource(url, headers = {}) { loads.push({url, headers}); },
        stopPlayback() { events.push('stop'); },
        command(args) { events.push(args[0]); }};
    const root = {starting: false, errored: false, fileReady: true, playbackEnded: false,
        audioHandoffPaused: false, autoPausedInactive: false, currentPlaybackUrl: '/red.mp4',
        activeDirectHeaders: {}, pendingSeekSec: -1, wakeReconnectPendingSeek: -1,
        arrivingStreamUrl: '', adjacentResolveGen: 1, hydrateGen: 1, queue: ['red', 'next'],
        handleSleepEpisodeEnd() { return false; }, startUpNextCountdown() { events.push('up-next'); },
        requestAudioPlayback() { this.audioHandoffPaused = false; events.push('claim'); }};
    for (const name of ['syncPowerInhibit', 'syncWatchPartyPlayerObservation', 'activityNaturalEof',
        'activityDiscontinuity', 'sendTraktPlayback', 'recordProgress', 'wakeChrome', 'healAudio',
        'closeMenus', 'cancelUpNext', 'resetRecoveryWatch', 'activityEndSession', 'switchArrivingToStream'])
        root[name] = () => events.push(name);
    const DirectSourcePolicy = vm.runInNewContext('(function(){' + policySource + '\nreturn {admitProviderUrl, copyHeaders};})()');
    const context = {root, mpv, DirectSourcePolicy, streamWatchdog: {stop() { events.push('watch-stop'); }, restart() { events.push('watch-start'); }}};
    for (const name of ['togglePlayPause', 'resumeFromMinimize', 'suspendForMinimize', 'suspendForAudioHandoff', 'stop', 'loadDirectStreamUrl', 'reloadActiveDirectSource', 'clearActiveDirectHeaders'])
        root[name] = fn(player, name, context);
    if (player.includes('function replayEndedPlayback(')) root.replayEndedPlayback = fn(player, 'replayEndedPlayback', context);
    return {root, mpv, events, loads, context, end: handler('onEndFile', context, 'reason')};
}
function eof(f) { f.end('eof'); f.mpv.position = 0; f.mpv.duration = 0; }
{
    const f = fixture(); eof(f);
    assert.equal(f.mpv.pause, true, 'natural EOF presents Play, not a false buffering/Pause state');
    assert.equal(f.root.fileReady, false);
    assert.equal(f.loads.length, 0, 'EOF alone never replays');
    assert.ok(f.events.includes('up-next'), 'existing episode countdown remains owned by EOF');
    f.root.togglePlayPause();
    assert.equal(f.loads.length, 1, 'visible Play must admit the ended source again');
    assert.equal(f.loads[0].url, '/red.mp4');
    assert.equal(f.root.pendingSeekSec, 0, 'replay starts at zero, not stale progress');
    assert.equal(f.root.starting, true);
    assert.equal(f.root.fileReady, false, 'load request is not decoder readiness');
    assert.ok(f.events.indexOf('claim') < f.events.indexOf('watch-start'));
    assert.deepEqual(f.root.queue, ['red', 'next']);
    assert.ok(f.events.includes('cancelUpNext'), 'explicit replay cancels pending adjacent advance');
    f.root.togglePlayPause(); assert.equal(f.loads.length, 1, 'pending replay is single admission');
}
{
    const f = fixture(); eof(f); f.root.suspendForMinimize();
    assert.equal(f.loads.length, 0, 'Back only parks terminal playback');
    const win = {warmPlayerSessionId: 'red', claimAudio() { f.events.push('shell-claim'); }};
    fn(main, 'activateMovieSession', {win, playerLayer: {item: f.root}})({id: 'red', target: {localPath: '/red.mp4'}});
    assert.equal(f.loads.length, 1, 'actual warm-session dispatcher re-admits terminal source');
    assert.equal(win.warmPlayerSessionId, 'red');
}
{
    const f = fixture(); f.root.togglePlayPause(); assert.equal(f.mpv.pause, true);
    f.root.togglePlayPause(); assert.equal(f.mpv.pause, false); assert.equal(f.loads.length, 0);
    f.root.suspendForMinimize(); f.root.resumeFromMinimize();
    assert.equal(f.mpv.pause, false); assert.equal(f.loads.length, 0, 'live warm resume stays reload-free');
    f.mpv.pause = true; f.root.suspendForMinimize(); f.root.resumeFromMinimize();
    assert.equal(f.mpv.pause, true, 'deliberate pause survives warm return');
}
{
    const f = fixture(); eof(f); f.root.suspendForAudioHandoff();
    assert.equal(f.loads.length, 0); assert.equal(f.mpv.pause, true);
}
console.log('PASS Theatre production EOF, explicit replay, warm activation, pause/minimize and handoff');
{
    const f = fixture(); eof(f); f.root.stop(); f.root.resumeFromMinimize();
    assert.equal(f.loads.length, 0, 'Stop retires replay eligibility');
    assert.ok(f.events.includes('stop'));
}
for (const reason of ['stop', 'error', 'other']) {
    const f = fixture(); f.end(reason);
    assert.equal(f.root.playbackEnded, false, reason + ' does not invent natural completion');
    assert.equal(f.loads.length, 0);
    assert.ok(!f.events.includes('up-next'));
}
{
    const f = fixture(); f.root.arrivingStreamUrl = 'https://example.test/movie';
    f.mpv.position = 4; f.mpv.duration = 35; f.end('eof');
    assert.ok(f.events.includes('switchArrivingToStream'));
    assert.equal(f.root.playbackEnded, false, 'partial frontier EOF remains a handover');
    assert.ok(!f.events.includes('activityNaturalEof'));
}
{
    const f = fixture(); f.root.suspendForAudioHandoff(); eof(f);
    assert.equal(f.root.playbackEnded, true);
    assert.equal(f.mpv.pause, true); assert.equal(f.loads.length, 0);
    assert.ok(!f.events.includes('up-next'), 'late outgoing EOF cannot advance under another owner');
    f.root.resumeFromMinimize(); assert.equal(f.loads.length, 1, 'explicit return may reclaim and replay');
    assert.equal(f.root.audioHandoffPaused, false);
}
{
    const f = fixture(); eof(f);
    const visible = player.match(/    onVisibleChanged: \{([\s\S]*?)\n    \}/);
    assert.ok(visible); f.root.forceActiveFocus = () => {};
    for (const visibleValue of [false, true]) {
        vm.runInNewContext('(function(){' + visible[1] + '\n})', {...f.context, visible: visibleValue})();
        assert.equal(f.loads.length, 0, 'incidental visibility never replays');
    }
    const headers = {Referer: 'https://example.test/'};
    f.root.loadDirectStreamUrl('https://example.test/new', headers);
    headers.Referer = 'https://mutated.example.test/';
    assert.equal(f.root.playbackEnded, false, 'new-source admission supersedes old terminal state');
    eof(f); f.root.togglePlayPause();
    assert.equal(f.loads.at(-1).url, 'https://example.test/new');
    assert.equal(f.loads.at(-1).headers.Referer, 'https://example.test/', 'replay retains active-source headers');
    assert.equal(f.root.loadDirectStreamUrl('/untrusted-provider.mp4', {}), false, 'provider admission remains HTTP(S)-only; local replay above uses the owned source');
    f.root.loadDirectStreamUrl('https://example.test/header-free.mp4', {});
    eof(f); f.root.togglePlayPause();
    assert.equal(f.loads.at(-1).url, 'https://example.test/header-free.mp4');
    // The production policy runs in a separate VM realm; compare map entries.
    assert.deepEqual(Object.entries(f.loads.at(-1).headers), [], 'header-free replacement clears old source headers');
}
{
    const f = fixture(); eof(f); const requests = [];
    f.context.WatchPartySync = {active: true, requestLocalPlayback(...args) { requests.push(args); }};
    f.root.togglePlayPause(); assert.deepEqual(requests, [[true, 0]]);
    assert.equal(f.loads.length, 0, 'room-controlled playback still routes through room authority');
    f.root.resumeFromMinimize();
    assert.equal(f.loads.length, 0, 'warm activation cannot bypass active room authority');
}
{
    const f = fixture(); let advances = 0;
    f.root.goToAdjacentEpisode = direction => { assert.equal(direction, 'next'); advances++; };
    fn(player, 'confirmUpNext', f.context)();
    assert.equal(advances, 1, 'episode confirmation preserves existing adjacent route');
    assert.ok(f.events.includes('cancelUpNext'));
    f.root.handleSleepEpisodeEnd = () => true; eof(f);
    assert.ok(!f.events.includes('up-next'), 'end-of-episode sleep still suppresses advance');
}
console.log('PASS Stop/error, partial-file handover, late handoff EOF, visibility, supersession, headers, room authority, adjacent/sleep controls');
