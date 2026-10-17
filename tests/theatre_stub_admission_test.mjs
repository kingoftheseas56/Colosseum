import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source = fs.readFileSync(new URL('../qml/PlayerPage.qml', import.meta.url), 'utf8');
function load(name, context) {
    const m = source.match(new RegExp('    function ' + name + '\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));
    assert.ok(m, name + ' production function exists');
    return vm.runInNewContext('(function(' + m[1] + '){' + m[2] + '\n})', context);
}
function fixture() {
    const loads = [], switches = [];
    const mpv = {pause: false, duration: 35, position: 11, loadFile: url => loads.push(url)};
    // Production admission initializes source identity/candidates. Unrelated UI services
    // are inert here; detector and candidate selection below are real production bodies.
    const root = new Proxy({stubDurationThresholdSec: 60, resolveAdjacentContext() { return {}; },
        playStreamAt(index, reason) { switches.push({index, reason}); }},
        {get: (object, key) => key in object ? object[key] : () => {}});
    const context = {root, mpv, streamWatchdog: {stop() {}}};
    for (const name of ['playLocalFile', 'playUrl', 'loadDirectStreamUrl', 'detectStubStream',
        'directStreamUrl', 'currentStreamCandidate', 'streamCandidateKey', 'markStreamDead', 'isStreamDead', 'nextPlayableStreamIndex'])
        root[name] = load(name, context);
    root.playLocalFile({localPath: '/generated/red-35s.mp4', title: 'Synthetic red'});
    root.fileReady = true; root.starting = false;
    return {root, mpv, loads, switches, context};
}
{
    const f = fixture();
    assert.equal(f.root.mediaLocalPath, '/generated/red-35s.mp4');
    assert.equal(f.root.currentStreamIndex, -1); assert.equal(f.root.streamCandidates.length, 0);
    const pause = source.match(/        onPauseChanged: \{([\s\S]*?)\n        \}/);
    assert.ok(pause);
    f.root.audioHandoffPaused = false;
    vm.runInNewContext('(function(){' + pause[1] + '\n})', {...f.context,
        activityTracker: {playbackStateChanged() {}}})();
    assert.equal(f.root.errored, false, 'resuming an admitted short local video is not a fake stream');
    assert.equal(f.root.fileReady, true);
    assert.equal(Object.keys(f.root.deadStreamKeys).length, 0);
    assert.equal(f.switches.length, 0);
}
{
    const f = fixture(); f.root.playUrl('/generated/direct-short.mp4', 'Direct short');
    f.root.fileReady = true; f.root.starting = false;
    assert.equal(f.root.detectStubStream(), false, 'candidate-free direct admission has no stub ladder');
    assert.equal(f.root.errored, false);
}
function streamFixture(candidates) {
    const f = fixture(); f.root.mediaLocalPath = ''; f.root.streamCandidates = candidates;
    f.root.currentStreamIndex = 0; return f;
}
{
    const f = streamFixture([{infoHash: 'short', fileIdx: 0}, {infoHash: 'next', fileIdx: 1}]);
    assert.equal(f.root.detectStubStream(), true);
    assert.equal(f.root.deadStreamKeys['short:0'], 'stub_35s');
    assert.deepEqual(f.switches, [{index: 1, reason: 'switch'}]);
    assert.equal(f.root.detectStubStream(), false, 'same stub is checked once');
}
{
    const f = streamFixture([{infoHash: 'short', fileIdx: 0}]);
    assert.equal(f.root.detectStubStream(), true); assert.equal(f.root.errored, true);
    assert.match(f.root.statusMsg, /No alternate stream available/);
}
for (const state of ['long', 'unknown', 'paused', 'starting', 'not-ready', 'iptv']) {
    const f = streamFixture([{infoHash: 'source', fileIdx: 0}]);
    if (state === 'long') f.mpv.duration = 90;
    if (state === 'unknown') f.mpv.duration = 0;
    if (state === 'paused') f.mpv.pause = true;
    if (state === 'starting') f.root.starting = true;
    if (state === 'not-ready') f.root.fileReady = false;
    if (state === 'iptv') f.root.subStreamId = 'iptv:channel';
    assert.equal(f.root.detectStubStream(), false, state + ' exclusion preserved');
    assert.equal(f.root.errored, false);
}
console.log('PASS production local/direct short admission and resume; streaming stub fallback/exhaustion/dedup and existing exclusions');
for (const candidate of [{}, {infoHash: ''}, {infoHash: '   '}, null]) {
    const f = streamFixture([candidate]);
    assert.equal(f.root.detectStubStream(), false, 'malformed candidate is not a stub source');
    assert.equal(f.root.errored, false);
}
for (const index of [-1, 1, 99, NaN]) {
    const f = streamFixture([{infoHash: 'source', fileIdx: 0}]); f.root.currentStreamIndex = index;
    assert.equal(f.root.detectStubStream(), false, 'invalid candidate index has no active stub source');
}
{
    const f = streamFixture([{infoHash: 'stale', fileIdx: 0}]);
    f.root.mediaLocalPath = '/generated/local.mp4';
    assert.equal(f.root.detectStubStream(), false, 'explicit local transport cannot inherit candidate rejection');
}
console.log('PASS malformed/empty candidates, invalid indices and local transport guard');

for (const candidate of [{url: 'https://example.test/short.mp4'}, {infoHash: 'url:https://example.test/short.mp4'}]) {
    const f = streamFixture([candidate]);
    assert.equal(f.root.detectStubStream(), true, 'active direct URL candidate retains short-source rejection');
    assert.equal(f.root.errored, true);
    assert.match(f.root.statusMsg, /No alternate stream available/);
}
console.log('PASS URL-only and URL-prefixed active candidate short-source rejection');
