import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// Boundary model: executes the production injected script, not a replacement
// implementation. Native decoding, event ordering and frame retirement are
// independently exercised by feria_lifecycle_probe.
const source = fs.readFileSync(process.env.FERIA_PLAYBACK_SOURCE || new URL('../qml/feria/FeriaPlayback.js', import.meta.url), 'utf8');
function eventTarget(target = {}) {
    const listeners = new Map();
    target.addEventListener = (name, handler) => {
        if (!listeners.has(name)) listeners.set(name, []);
        listeners.get(name).push(handler);
    };
    target.emit = (name, event = {}) => {
        for (const handler of listeners.get(name) || []) handler({target, ...event});
    };
    return target;
}
function root() {
    const r = eventTarget({media: [], frames: [], nodes: []});
    r.querySelectorAll = selector => selector === 'video,audio' ? r.media
        : selector === 'iframe,frame' ? r.frames : selector === '*' ? r.nodes : [];
    return r;
}
function fixture() {
    const document = root(), window = {};
    const scope = vm.createContext({document, window});
    vm.runInContext(source, scope);
    const activity = (suspended, serial, epoch = 1) => vm.runInContext(scope.presentationMedia(suspended, epoch, serial), scope);
    const result = (serial, epoch = 1) => vm.runInContext(scope.presentationMediaResult(epoch, serial), scope);
    function media(doc = document, paused = false) {
        const m = eventTarget({ownerDocument: doc, paused, ended: false, isConnected: true,
            currentSrc: 'https://fixture.test/tone.wav', src: '', tagName: 'AUDIO', playCalls: 0});
        m.getRootNode = () => doc;
        m.pause = function() { if (!this.paused) { this.paused = true; this.emit('pause'); } };
        m.play = function() {
            ++this.playCalls;
            if (this.resume) return this.resume();
            this.paused = false; this.emit('play'); return Promise.resolve();
        };
        doc.media.push(m); return m;
    }
    return {document, window, activity, result, media};
}
const tests = [];
function test(name, body) { tests.push([name, body]); }

test('only owned elements resume, preserving an inactive-space no-op', async () => {
    const f = fixture(), playing = f.media(), manual = f.media(f.document, true);
    f.activity(false, 1); assert.equal(playing.paused, false);
    f.activity(true, 2); assert.equal(playing.paused, true);
    f.activity(false, 3); await Promise.resolve();
    assert.equal(playing.paused, false); assert.equal(manual.paused, true);
    assert.equal(manual.playCalls, 0);
});
test('explicit pause on an already paused element revokes ownership', () => {
    const f = fixture(), m = f.media(); f.activity(true, 1); m.pause(); f.activity(false, 2);
    assert.equal(m.playCalls, 0);
});
test('input revokes ownership even when custom controls cached native pause', () => {
    const f = fixture(), m = f.media(); f.activity(true, 1); f.document.emit('pointerdown'); f.activity(false, 2);
    assert.equal(m.playCalls, 0);
});
test('retired same-origin frame document cannot resume connected old media', () => {
    const f = fixture(), child = root(), m = f.media(child);
    f.document.frames.push({contentDocument: child}); f.activity(true, 1);
    f.document.frames.length = 0; f.activity(false, 2);
    assert.equal(m.playCalls, 0, 'retired frame receipt must never invoke play');
});
test('navigated frame with the same source cannot revive the old document', () => {
    const f = fixture(), child = root(), m = f.media(child), frame = {contentDocument: child};
    f.document.frames.push(frame); f.activity(true, 1);
    frame.contentDocument = root(); f.media(frame.contentDocument, true); f.activity(false, 2);
    assert.equal(m.playCalls, 0, 'replacement frame document invalidates old receipt');
});
test('source reload and element replacement cannot inherit an owned pause', () => {
    const f = fixture(), m = f.media(); f.activity(true, 1);
    m.emit('loadstart'); f.activity(false, 2); assert.equal(m.playCalls, 0);
    m.paused = false; f.activity(true, 3); m.isConnected = false;
    const replacement = f.media(f.document, true); f.activity(false, 4);
    assert.equal(replacement.playCalls, 0); assert.equal(m.playCalls, 0);
});
test('an older operation cannot change the latest suspension', () => {
    const f = fixture(), m = f.media(); f.activity(true, 3); f.activity(false, 2);
    assert.equal(m.playCalls, 0); assert.equal(m.paused, true);
});
test('play rejection is observed as blocked with limited coverage', async () => {
    const f = fixture(), m = f.media(); m.resume = () => Promise.reject(new Error('autoplay denied'));
    f.activity(true, 1); const initial = f.activity(false, 2);
    assert.equal(initial.outcome, 'pending', 'dispatch is not observed resume success');
    await Promise.resolve();
    const result = f.result(2);
    assert.equal(result.outcome, 'blocked'); assert.equal(result.coverage.limited, true);
    assert.equal(result.observations[0].outcome, 'blocked');
});
test('inaccessible frames are explicit and never counted as inspected', () => {
    const f = fixture(); f.document.frames.push({contentDocument: null});
    const result = f.activity(true, 1);
    assert.equal(result.coverage.inaccessibleFrames, 1);
    assert.equal(result.coverage.inspectedRoots, 1);
    assert.equal(result.coverage.limited, true);
});
test('a superseded play Promise cannot overwrite the newer result', async () => {
    const f = fixture(), m = f.media(); let resolve;
    m.resume = () => new Promise(done => { resolve = done; });
    f.activity(true, 1); f.activity(false, 2); f.activity(true, 3); resolve(); await Promise.resolve();
    assert.equal(f.result(3).operationId, '1:3');
    assert.equal(f.result(3).outcome, 'succeeded');
    assert.equal(f.result(2).outcome, 'invalidated');
});
test('user pause while play is pending invalidates its eventual acknowledgement', async () => {
    const f = fixture(), m = f.media(); let resolve;
    m.resume = () => new Promise(done => { resolve = done; });
    f.activity(true, 1); f.activity(false, 2); m.pause(); resolve(); await Promise.resolve();
    assert.equal(f.result(2).outcome, 'invalidated');
});
test('resolved play without observed playback is blocked', async () => {
    const f = fixture(), m = f.media(); m.resume = () => Promise.resolve();
    f.activity(true, 1); f.activity(false, 2); await Promise.resolve();
    assert.equal(f.result(2).outcome, 'blocked');
});
test('a pending resume is invalidated when its frame document changes before settlement', async () => {
    const f = fixture(), child = root(), m = f.media(child), frame = {contentDocument: child};
    f.document.frames.push(frame); let resolve;
    m.resume = () => new Promise(done => { resolve = done; });
    f.activity(true, 1); f.activity(false, 2);
    frame.contentDocument = root(); m.paused = false; resolve(); await Promise.resolve();
    assert.equal(f.result(2).outcome, 'invalidated');
});
test('page input during pending resume revokes the acknowledgement', async () => {
    const f = fixture(), m = f.media(); let resolve;
    m.resume = () => new Promise(done => { resolve = done; });
    f.activity(true, 1); f.activity(false, 2);
    f.document.emit('keydown'); m.paused = false; resolve(); await Promise.resolve();
    assert.equal(f.result(2).outcome, 'invalidated');
});
test('bounded root inspection reports truncation rather than universal coverage', () => {
    const f = fixture(); let current = f.document;
    for (let index = 0; index < 7; ++index) {
        const child = root(); current.frames.push({contentDocument: child}); current = child;
    }
    const result = f.activity(true, 1);
    assert.equal(result.coverage.truncated, true);
    assert.equal(result.coverage.inspectedRoots, 6);
});
test('superseded pending operations release obsolete ledger ownership', () => {
    const f = fixture(), m = f.media(); m.resume = () => new Promise(() => {});
    f.activity(true, 1); f.activity(false, 2);
    assert.equal(f.window.__feriaPresentationMedia.resuming.size, 1);
    m.isConnected = false; f.activity(true, 3);
    assert.equal(f.window.__feriaPresentationMedia.resuming.size, 0,
        'obsolete pending operation must not retain a removed media element');
});
let failures = 0;
for (const [name, body] of tests) {
    if (process.env.FERIA_TEST_FILTER && !name.includes(process.env.FERIA_TEST_FILTER)) continue;
    try { await body(); console.log('PASS', name); }
    catch (error) { ++failures; console.error('FAIL', name, '\n ', error.message); }
}
if (failures) process.exitCode = 1;
