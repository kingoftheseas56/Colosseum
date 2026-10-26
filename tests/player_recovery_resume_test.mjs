import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('../qml/PlayerPage.qml', import.meta.url), 'utf8');

// Run the shipped QML logic; only the asynchronous media/native seams are fakes.
function block(marker) {
  const at = source.indexOf(marker);
  assert.ok(at >= 0, marker);
  const start = source.indexOf('{', at);
  let depth = 0, mode = '', escaped = false;
  for (let i = start; i < source.length; i++) {
    const c = source[i], next = source[i + 1];
    if (mode === '//') { if (c === '\n') mode = ''; continue; }
    if (mode === '/*') { if (c === '*' && next === '/') { mode = ''; i++; } continue; }
    if (mode) {
      if (escaped) escaped = false;
      else if (c === '\\') escaped = true;
      else if (c === mode) mode = '';
      continue;
    }
    if (c === '/' && next === '/') { mode = '//'; i++; continue; }
    if (c === '/' && next === '*') { mode = '/*'; i++; continue; }
    if (c === '"' || c === "'" || c === '`') { mode = c; continue; }
    if (c === '{') depth++;
    if (c === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(`Unclosed ${marker}`);
}

function session({ paused = false, visible = true, direct = false } = {}) {
  const calls = [];
  const noop = () => {};
  let now = 100000;
  const root = {
    currentStreamIndex: direct ? -1 : 0,
    streamCandidates: direct ? [] : [{ infoHash: 'fixture', fileIdx: 0 }],
    streamRetryCount: 0, mediaLocalPath: '', mediaResumeHash: direct ? '' : 'fixture',
    mediaId: 'fixture:1:1', subStreamId: 'fixture:1:1',
    currentPlaybackUrl: 'http://127.0.0.1/fixture', pendingSeekSec: -1,
    resumePromptConsumed: true, resumePromptMinSec: 30, resumeRestartThreshold: 0.8,
    resumeChoiceOpen: false, wakeReconnectPendingSeek: -1,
    starting: false, errored: false, fileReady: true, visible,
    audioHandoffPaused: false, activeDirectHeaders: {},
    recoveryPositionSec: -1, recoveryResumePending: false, recoveryResumePaused: false,
    recoveryLastPosition: 119, recoveryLastMovedAt: now - 2000,
    recoveryUrlStartedAt: now - 60000, recoveryNoVideoSince: now - 21000,
    recoverySawVideo: false, positionAdvanceEpsilonSec: 0.3,
    noVideoGraceSeconds: 20, streamWatchdogSeconds: 75,
    positionStartedFloorSec: 5, positionFrozenSeconds: 18,
    wakeReconnectLastTickAt: now - 40000, wakeReconnectGapSeconds: 30,
    arrivingStreamUrl: '', hydrateGen: 0,
    directStreamUrl: () => '', currentStreamCandidate: () => ({}),
    isStreamDead: () => false, closeMenus: noop, wakeChrome: noop,
    forceActiveFocus: noop, updateMediaSubtitle: noop,
    maybeAutoSelectTracks: noop, maybeAutoSub: noop, syncPowerInhibit: noop,
    detectStubStream: noop, loadSkipSegments: noop, recordProgress: noop,
    syncWatchPartyPlayerObservation: noop, activityDiscontinuity: noop,
    finishStartingIfPlaybackAdvanced: noop,
    clamp: (value, min, max) => Math.max(min, Math.min(max, value)),
    clearAbLoop: noop, cancelSleepTimer: noop, resetSkipSegments: noop,
    resetTrackAutomation: noop, sendTraktPlayback: noop,
    requestAudioPlayback: noop,
  };
  const mpv = {
    position: 120, duration: 3231.958, pause: paused, cacheBufferingState: 100,
    decodedWidth: 0, decodedHeight: 0,
    loadFile: () => { calls.push('load'); mpv.position = 0; },
    loadSource: (url, headers) => { calls.push('load'); mpv.lastHeaders = headers; mpv.position = 0; },
    seekExact: value => { calls.push(['seek', value]); mpv.position = value; },
  };
  const context = vm.createContext({
    root, mpv, visible, Date: { now: () => now }, console: { info: noop, log: noop },
    Stream: { play: () => calls.push('stream-play'), unwatchStats: noop },
    streamWatchdog: { restart: () => calls.push('watchdog-start'), stop: () => calls.push('watchdog-stop') },
    seekSettleGuard: { restart: noop },
  });
  for (const name of [
    'retryCurrentStream', 'playStreamAt', 'resetRecoveryWatch', 'recoveryExcluded',
    'tickRecoveryWatch', 'tickWakeReconnect', 'handlePlaybackFailure', 'handleStreamWatchdog',
    'shouldSkipResumePrompt', 'prepareResumeChoice', 'armPlaybackAfterResumeChoice',
    'startOverFromResumeChoice', 'playUrl', 'resetRecoveryResume', 'prepareRecoveryResume',
    'recoveryPauseIntent', 'clearStreamRouteRestore',
    'seekTo',
    'clearActiveDirectHeaders', 'reloadActiveDirectSource',
  ]) {
    const marker = `function ${name}(`;
    const at = source.indexOf(marker);
    if (at < 0) continue; // New helpers must be exercised through their real callers.
    const signature = source.slice(at, source.indexOf('{', at));
    root[name] = vm.runInContext(`(${signature}${block(marker)})`, context);
  }
  const handler = name => vm.runInContext(`(function() ${block(name + ': {')})`, context);
  return { root, mpv, calls, loaded: handler('onFileLoaded'), positionChanged: handler('onPositionChanged'),
    visibilityChanged: handler('onVisibleChanged'), advanceTime: value => { now += value; },
    reload: () => { mpv.loadFile(); handler('onFileLoaded')(); } };
}

test('same-source retry resumes at the current position without a resume prompt', () => {
  const s = session();
  s.root.retryCurrentStream();
  s.reload();
  assert.equal(s.mpv.position, 120);
  assert.equal(s.root.resumeChoiceOpen, false);
});

test('retry replaces stale pending progress with the live position', () => {
  const s = session();
  s.root.pendingSeekSec = 18.977;
  s.root.retryCurrentStream();
  s.reload();
  assert.equal(s.mpv.position, 120);
});

test('retry retains the last observed position after an error resets mpv to zero', () => {
  const s = session();
  s.positionChanged();
  s.mpv.position = 0;
  s.positionChanged();
  s.root.fileReady = false;
  s.root.handlePlaybackFailure('network');
  s.reload();
  assert.equal(s.mpv.position, 120);
});

test('repeated failure while reopening does not overwrite the recovery target with zero', () => {
  const s = session();
  s.root.retryCurrentStream();
  s.mpv.loadFile();
  s.root.retryCurrentStream();
  s.reload();
  assert.equal(s.mpv.position, 120);
});

test('direct-URL reconnect preserves playback position', () => {
  const s = session({ direct: true });
  s.root.activeDirectHeaders = { Referer: 'https://fixture.invalid/' };
  s.root.handlePlaybackFailure('network');
  s.loaded();
  assert.equal(s.mpv.position, 120);
  assert.equal(s.mpv.lastHeaders.Referer, 'https://fixture.invalid/');
});

test('recovery respects audio ownership handed to another world during reopening', () => {
  const s = session();
  s.root.retryCurrentStream();
  s.root.audioHandoffPaused = true;
  s.mpv.pause = true;
  s.reload();
  assert.equal(s.mpv.pause, true);
  assert.equal(s.root.starting, false);
});

test('retry preserves a deliberate pause', () => {
  const s = session({ paused: true });
  s.root.retryCurrentStream();
  s.reload();
  assert.equal(s.mpv.position, 120);
  assert.equal(s.mpv.pause, true);
  assert.equal(s.root.starting, false);
});

test('an initial buffered load retains the first-open resume choice', () => {
  const s = session();
  Object.assign(s.root, { starting: true, fileReady: false, pendingSeekSec: 120, resumePromptConsumed: false });
  s.mpv.position = 0;
  s.root.retryCurrentStream();
  s.reload();
  assert.equal(s.root.resumeChoiceOpen, true);
  assert.equal(s.mpv.pause, true);
});

test('source switching carries the last position even after a transport reset', () => {
  const s = session();
  s.positionChanged();
  s.mpv.position = 0;
  s.positionChanged();
  s.root.playStreamAt(0, 'switch');
  s.reload();
  assert.equal(s.mpv.position, 120);
});

test('a new item clears a previous recovery target', () => {
  const s = session();
  s.root.retryCurrentStream();
  s.root.playUrl('http://127.0.0.1/new-item', 'New item');
  s.loaded();
  assert.equal(s.mpv.position, 0);
  assert.equal(s.root.recoveryPositionSec, -1);
  assert.equal(s.root.recoveryResumePending, false);
});

test('Start over clears retained recovery progress', () => {
  const s = session();
  s.root.retryCurrentStream();
  s.root.startOverFromResumeChoice();
  assert.equal(s.mpv.position, 0);
  assert.equal(s.root.recoveryPositionSec, 0);
  assert.equal(s.root.recoveryResumePending, false);
});

test('a deliberate seek to zero replaces the remembered recovery position', () => {
  const s = session();
  s.positionChanged();
  s.root.seekTo(0);
  s.root.retryCurrentStream();
  s.reload();
  assert.equal(s.mpv.position, 0);
});

test('hidden zero-dimension player does not invoke recovery or wake reconnect', () => {
  const s = session({ visible: false });
  const failures = [];
  s.root.handlePlaybackFailure = reason => failures.push(reason);
  s.root.tickRecoveryWatch();
  s.root.tickWakeReconnect();
  assert.deepEqual(failures, []);
  assert.equal(s.calls.includes('load'), false);
});

test('hidden startup watchdog does not retry a retained player', () => {
  const s = session({ visible: false });
  s.root.starting = true;
  const failures = [];
  s.root.handlePlaybackFailure = reason => failures.push(reason);
  s.root.handleStreamWatchdog();
  assert.deepEqual(failures, []);
});

test('showing the player gives decoded video a fresh grace period', () => {
  const s = session();
  const failures = [];
  s.root.handlePlaybackFailure = reason => failures.push(reason);
  s.advanceTime(120000);
  s.visibilityChanged();
  s.root.tickRecoveryWatch();
  assert.deepEqual(failures, []);
  s.advanceTime(21000);
  s.root.tickRecoveryWatch();
  assert.deepEqual(failures, ['no video']);
});

test('visible video recovery remains active and ordinary mid-play buffering keeps its position', () => {
  const s = session();
  const failures = [];
  s.root.handlePlaybackFailure = reason => failures.push(reason);
  s.root.tickRecoveryWatch();
  assert.deepEqual(failures, ['no video']);
  failures.length = 0;
  Object.assign(s.mpv, { decodedWidth: 1920, decodedHeight: 1080, cacheBufferingState: 0 });
  s.root.tickRecoveryWatch();
  assert.deepEqual(failures, []);
  assert.equal(s.mpv.position, 120);
});
