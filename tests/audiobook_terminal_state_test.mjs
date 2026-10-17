import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
// Execute production QML function bodies with a deterministic mpv adapter. This is
// a session-state regression, not a claim of native decode or visible UI coverage.
const source = fs.readFileSync(new URL('../qml/AudiobookSession.qml', import.meta.url), 'utf8');
function body(name, indent = 4) {
    const pad = ' '.repeat(indent);
    const m = source.match(new RegExp(pad + 'function ' + name + '\\(([^)]*)\\) \\{([\\s\\S]*?)\\n' + pad + '\\}'));
    assert.ok(m, name);
    return '(function(' + m[1] + '){' + m[2] + '\n})';
}
let loads = [], requests = 0, completions = 0, begins = 0, records = [];
const mpv = { pause: false, position: 40, duration: 40, speed: 1,
    loadFile(file) { loads.push(file); this.position = 0; this.duration = 0; },
    seekExact(t) { this.position = t; } };
const session = { ready: true, completed: false, audioHandoffPaused: false,
    activePairKey: 'synthetic|author', book: {title: 'Synthetic'}, files: ['one.wav'],
    currentIndex: 0, pendingResumeSec: -1, chapterModel: [],
    activityNaturalEof() { completions++; }, activityBeginIfNeeded() { begins++; },
    activityDiscontinuity() {}, playbackRequested() { requests++; } };
Object.defineProperty(session, 'multiFile', {get() {return this.files.length > 1;}});
const ctx = {session, mpv, Progress: {record(r) {records.push(r);}}};
for (const name of ['requestAudioPlayback','suspendForAudioHandoff','playIndex','playIndexKeepState','goToChapterKeepState','seekTo','togglePlay','play','overallProgress','recordProgress'])
    session[name] = vm.runInNewContext(body(name), ctx);
const eof = vm.runInNewContext(body('onEndFile', 8), ctx);
const loaded = vm.runInNewContext(body('onFileLoaded', 8), ctx);
const pauseChanged = vm.runInNewContext(body('onPauseChanged', 8), ctx);
const openFor = vm.runInNewContext(body('openFor'), ctx);
eof('eof');
assert.equal(mpv.pause, true, 'final EOF must stop claiming to play');
assert.equal(session.completed, true, 'final EOF has an explicit terminal state');
assert.equal(session.ready, true, 'selected book remains available to reader transport');
assert.equal(completions, 1);
mpv.position = 0; mpv.duration = 0; // real mpv unloads the finished file
openFor(session.activePairKey, session.book, false);
assert.equal(loads.length, 0, 'returning to the completed book must not autoplay');
mpv.pause = false; pauseChanged();
assert.equal(mpv.pause, true, 'late unpause notification cannot revive completed playback');
session.recordProgress();
assert.equal(records.at(-1).progress, 1, 'idle heartbeat must not erase completion');
assert.equal(records.at(-1).resume.position, 0, 'completed book explicitly replays from start');
assert.equal(records.at(-1).resume.fileIndex, 0);
eof('eof'); assert.equal(completions, 1, 'duplicate final EOF cannot complete again');
session.suspendForAudioHandoff();
assert.equal(requests, 0, 'completion and Vinyl handoff do not request audio');
session.togglePlay();
assert.deepEqual(loads, ['one.wav'], 'explicit replay reloads unloaded mpv');
assert.equal(session.completed, false); assert.equal(mpv.pause, false);
assert.equal(requests, 1); assert.equal(begins, 1);
// Multi-file chapters advance normally; only final EOF completes the book.
session.files = ['one.wav','two.wav']; session.currentIndex = 0;
eof('eof'); assert.equal(session.currentIndex, 1); assert.equal(session.completed, false);
eof('eof'); assert.equal(session.completed, true); assert.equal(mpv.pause, true);
session.play(); assert.equal(loads.at(-1), 'one.wav'); assert.equal(session.currentIndex, 0);
// A queued EOF after another world claims audio cannot advance or complete.
session.suspendForAudioHandoff(); const count = loads.length, done = completions;
eof('eof'); assert.equal(loads.length, count); assert.equal(completions, done);
mpv.pause = false; loaded();
assert.equal(mpv.pause, true, 'late fileLoaded remains paused after another world claims audio');
session.audioHandoffPaused = false; session.completed = false;
eof('stop'); eof('error');
assert.equal(completions, done, 'non-EOF events do not mark completion');
// Read-along explicitly seeks before play: keep its selected-file target at EOF.
for (const files of [['one.wav'], ['one.wav', 'two.wav']]) {
    session.files = files; session.currentIndex = files.length - 1;
    session.completed = false; mpv.pause = false;
    eof('eof');
    const beforeSeek = loads.length;
    session.seekTo(7);
    assert.equal(loads.length, beforeSeek + 1);
    assert.equal(loads.at(-1), files.at(-1));
    assert.equal(mpv.pause, true, 'seek after EOF loads paused until explicit play');
    assert.equal(session.pendingResumeSec, 7);
    session.play();
    assert.equal(loads.length, beforeSeek + 1, 'play preserves pending terminal seek');
    loaded();
    assert.equal(mpv.position, 7);
    assert.equal(session.pendingResumeSec, -1);
    assert.equal(mpv.pause, false);
}
// Exact ReaderShell embedded-chapter sequence after EOF: chapter selection parks
// its start, then the passage seek must replace it before the async load finishes.
session.files = ['embedded.m4b']; session.currentIndex = 0;
session.chapterModel = [{kind: 'seek', index: 0, time: 0}, {kind: 'seek', index: 1, time: 12}];
session.completed = false; session.audioHandoffPaused = false; mpv.pause = false;
eof('eof');
session.goToChapterKeepState(1);
assert.equal(session.pendingResumeSec, 12);
session.seekTo(17);
session.play();
loaded();
assert.equal(mpv.position, 17, 'passage target must replace parked chapter start after EOF');
// Zero is also a real parked target, and must not lose a later passage request.
session.completed = false; mpv.pause = false; eof('eof');
session.goToChapterKeepState(0);
assert.equal(session.pendingResumeSec, 0);
session.seekTo(9); session.play(); loaded();
assert.equal(mpv.position, 9);
console.log('PASS production audiobook terminal bodies: final EOF, heartbeat, replay, chapters, handoff');
