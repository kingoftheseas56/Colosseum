import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

function load(name) {
    const scope = vm.createContext({});
    vm.runInContext(fs.readFileSync(new URL('../qml/feria/' + name, import.meta.url), 'utf8'), scope);
    return scope;
}
const Playback = load('FeriaPlayback.js');
const Resume = load('FeriaResume.js');
const generic = 'function(){return {outcome:"not-applicable"}}';

function fixture(ready = false) {
    const events = {};
    const media = {
        tagName:'VIDEO', duration:ready ? 1000 : NaN, currentTime:ready ? 20 : 0,
        paused:false, readyState:ready ? 4 : 0, playbackRate:1,
        currentSrc:ready ? 'first.mp4' : '', isConnected:true,
        addEventListener(name, handler) { (events[name] ??= []).push(handler); }
    };
    const page = vm.createContext({URL, window:{}, navigator:{},
        location:{href:'https://provider.test/watch/1'}, document:{
            title:'Track', querySelector:()=>null,
            querySelectorAll:selector=>selector === 'video,audio' ? [media] : []
        }});
    return {
        media, page,
        fire(name) { for (const handler of events[name] || []) handler(); },
        poll(token = 1) {
            return vm.runInContext(Playback.mediaObservation('watch', 'youtube', generic, token), page);
        },
        seek(sample, token = 1) {
            return vm.runInContext(Playback.seekMedia(600, sample, 'watch', 'youtube', generic, token), page);
        }
    };
}

let failed = 0;
let passed = 0;
function test(name, run) {
    try { run(); ++passed; console.log('PASS ' + name); }
    catch (error) { ++failed; console.error('FAIL ' + name + ': ' + error.message); }
}

for (const startEvents of [['loadstart'], ['emptied', 'loadstart']]) {
    test('first hydration preserves resume through ' + startEvents.join(' -> '), () => {
        const f = fixture();
        const key = Playback.mediaKey('youtube', f.page.location.href);
        const state = Resume.begin(1, key, 600, 0);
        const first = f.poll();
        Resume.tick(state, first.sample, key, 100);
        for (const event of startEvents) f.fire(event);
        f.media.currentSrc = 'first.mp4';
        f.media.duration = 1000;
        f.media.readyState = 4;
        f.media.currentTime = 20;
        f.fire('loadedmetadata');
        const ready = f.poll();
        assert.equal(ready.sample.mediaGeneration, first.sample.mediaGeneration);
        assert.equal(Resume.tick(state, ready.sample, key, 2100).seek, 600);
        assert.equal(state.target, 600);
        assert.equal(f.seek(ready.sample), true);
        assert.equal(f.media.currentTime, 600);
    });
}

test('established same-source reload cancels old target', () => {
    const f = fixture(true);
    const key = Playback.mediaKey('youtube', f.page.location.href);
    const state = Resume.begin(1, key, 600, 0);
    const first = f.poll();
    Resume.tick(state, first.sample, key, 100);
    for (const event of ['emptied', 'loadstart', 'loadedmetadata']) f.fire(event);
    const replacement = f.poll();
    assert.notEqual(replacement.sample.mediaGeneration, first.sample.mediaGeneration);
    assert.equal(Resume.tick(state, replacement.sample, key, 2100).seek, null);
    assert.equal(state.target, 0);
    assert.equal(f.seek(first.sample), false);
});

test('metadata-ready same-source reload is a replacement, not first hydration', () => {
    const f = fixture(true);
    f.media.readyState = 1;
    const first = f.poll();
    for (const event of ['emptied', 'loadstart', 'loadedmetadata']) f.fire(event);
    assert.notEqual(f.poll().sample.mediaGeneration, first.sample.mediaGeneration);
});

test('obsolete probe cannot lower cancellation token or authorize old seek', () => {
    const f = fixture(true);
    const old = f.poll();
    vm.runInContext('window.__feriaRequestToken=2', f.page);
    const stale = f.poll(1);
    assert.equal(f.page.window.__feriaRequestToken, 2);
    assert.equal(stale.outcome, 'hold');
    assert.equal(f.seek(old.sample), false);
    assert.equal(f.media.currentTime, 20);
    assert.equal(f.poll(2).outcome, 'observation');
});

test('older invalidation cannot lower a newer page fence', () => {
    const f = fixture(true);
    f.poll(1);
    vm.runInContext(Playback.invalidateMediaRequest(3), f.page);
    vm.runInContext(Playback.invalidateMediaRequest(2), f.page);
    assert.equal(f.page.window.__feriaRequestToken, 3);
    assert.equal(f.poll(2).outcome, 'hold');
    assert.equal(f.poll(3).outcome, 'observation');
});

test('a new document initializes its own fence', () => {
    const f = fixture(true);
    vm.runInContext(Playback.invalidateMediaRequest(5), f.page);
    assert.equal(f.page.window.__feriaRequestToken, 5);
    assert.equal(f.poll(5).outcome, 'observation');
});

console.log('FERIA_RESUME_REPAIRS_PASS ' + passed + ', FAIL ' + failed);
process.exitCode = failed ? 1 : 0;
