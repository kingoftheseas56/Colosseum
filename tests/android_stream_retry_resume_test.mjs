import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import vm from 'node:vm';

// An alternate source permits the same fixture to verify archived baselines.
const sourcePath = process.env.COLOSSEUM_PLAYER_TEST_SOURCE
  || fileURLToPath(new URL('../qml/PlayerPage.qml', import.meta.url));
const source = readFileSync(sourcePath, 'utf8');

function block(marker) {
  const at = source.indexOf(marker);
  assert.ok(at >= 0, `production source marker not found: ${marker}`);
  const start = source.indexOf('{', at);
  assert.ok(start >= 0, `production body not found: ${marker}`);
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
  throw new Error(`unclosed production body: ${marker}`);
}

function installProductionLogic(session) {
  const { context, root } = session;
  for (const name of [
    'retryCurrentStream', 'handlePlaybackFailure', 'handlePlaybackIssue', 'playUrl',
    'playStreamAt', 'pickAnotherStream',
    'resetRecoveryResume', 'prepareRecoveryResume', 'recoveryPauseIntent',
    'captureSelectedSidecarSubtitle', 'restoreSelectedSidecarSubtitle',
    'clearStreamRouteRestore', 'restoreStreamRouteSubtitle',
    'turnSubtitlesOff', 'adjustSubtitleDelay', 'resetSubtitleDelay',
  ]) {
    const marker = `function ${name}(`;
    const at = source.indexOf(marker);
    if (at < 0) continue; // An absent optional entry remains covered through real callers below.
    const signature = source.slice(at, source.indexOf('{', at));
    root[name] = vm.runInContext(`(${signature}${block(marker)})`, context);
  }
  const handler = marker => vm.runInContext(`(function() ${block(marker)})`, context);
  root.fileLoaded = handler('onFileLoaded: {');
  root.positionChanged = handler('onPositionChanged: {');
  const errorHandler = block('onPlaybackError: function(code, message)');
  root.playbackError = vm.runInContext(`(function(code, message) ${errorHandler})`, context);
}

function makeSession({ preErrorPaused = false, platform = 'android' } = {}) {
  const calls = [];
  let now = 1_000_000;
  const root = {
    currentStreamIndex: 0,
    streamCandidates: [{ infoHash: 'fixture-hash', fileIdx: 2 }],
    streamRetryCount: 0,
    mediaResumeHash: 'fixture-hash',
    mediaResumeFileIdx: 2,
    mediaId: 'tt0000001',
    mediaLocalPath: '',
    arrivingStreamUrl: '',
    currentPlaybackUrl: 'http://127.0.0.1:11450/fixture',
    pendingSeekSec: -1,
    explicitResumePosition: false,
    starting: false,
    errored: false,
    fileReady: true,
    audioHandoffPaused: false,
    statusMsg: '',
    resumeChoiceOpen: false,
    resumeChoiceSec: -1,
    resumePromptConsumed: true,
    resumePromptMinSec: 30,
    resumeRestartThreshold: 0.8,
    streamRouteRestorePending: false,
    streamRouteRestoreLoadPending: false,
    streamRouteRestoreSec: -1,
    streamRouteRestoreIntent: '',
    streamRouteRestoreSubtitle: null,
    wakeReconnectPendingSeek: -1,
    lastPlaybackErrorCode: '',
    lastPlaybackErrorMessage: '',
    recoveryPositionSec: -1,
    recoveryResumePending: false,
    recoveryResumePaused: false,
    recoveryLastPosition: 0,
    recoveryLastMovedAt: now,
    recoveryUrlStartedAt: now,
    recoveryNoVideoSince: 0,
    recoverySawVideo: false,
    recoveryPausedIntent: null,
    recoverySubtitle: null,
    hydrateGen: 0,
    playbackQueue: [],
    playbackQueueIndex: -1,
    playbackQueueOrderingMode: '',
    mediaTitle: 'Fixture title',
    mediaYear: '',
    mediaArt: '',
    mediaTransport: 'Torrent stream',
    activeDirectHeaders: {},
    directStreamUrl: () => '',
    isStreamDead: () => false,
    nextPlayableStreamIndex: () => 1,
    markStreamDead(index) { calls.push(['markStreamDead', index]); },
    currentStreamCandidate: () => ({ infoHash: 'fixture-hash', fileIdx: 2 }),
    resetRecoveryWatch() { this.recoveryLastPosition = 0; this.recoveryLastMovedAt = now; },
    recordProgress() { calls.push(['recordProgress', mpv.position]); },
    wakeChrome() {},
    closeMenus() {},
    forceActiveFocus() {},
    updateMediaSubtitle() {},
    clearAbLoop() {}, cancelSleepTimer() {}, resetSkipSegments() {}, resetTrackAutomation() {},
    clearActiveDirectHeaders() {},
    maybeAutoSelectTracks() {}, maybeAutoSub() {}, syncPowerInhibit() {},
    detectStubStream() {}, loadSkipSegments() {}, syncWatchPartyPlayerObservation() {},
    activityDiscontinuity() {}, restoreStreamRouteSubtitle() {},
    prepareResumeChoice() { return false; },
    finishStartingIfPlaybackAdvanced() {},
    selectedSubtitleRow() {
      return ['selected-sidecar-id', 'restored-sidecar-id'].includes(mpv.subtitleTrack)
        ? this.fixtureSubtitleRow : null;
    },
    supportsPlayerCapability: () => true,
    round2: value => Math.round(value * 100) / 100,
    saveTrackPreference(patch) { calls.push(['saveTrackPreference', { ...patch }]); },
    fixtureSubtitleRow: {
      id: 'selected-sidecar-id', external: true,
      url: 'https://fixture.invalid/subtitles/en.srt', title: 'Fixture English', lang: 'en',
    },
  };
  const mpv = {
    position: 4015.357,
    duration: 7200,
    pause: false,
    subDelay: 0,
    subtitleTrack: 'selected-sidecar-id',
    errorPending: false,
    priorErrorPause: preErrorPaused,
    statCalls: [],
    seekCalls: [],
    loadSource(url) { calls.push(['loadSource', url]); this.position = 0; },
    seekExact(value) { calls.push(['seekExact', value]); this.seekCalls.push(value); this.position = value; },
    addSubtitle(url, title, lang, select) {
      calls.push(['addSubtitle', url, title, lang, select]);
      this.subtitleTrack = select ? 'restored-sidecar-id' : this.subtitleTrack;
    },
    playbackStat(name) {
      this.statCalls.push(name);
      if (name === 'recovery-paused') return this.errorPending ? this.priorErrorPause : null;
      return null;
    },
  };
  root.mediaLoadingLine = '';
  const streamWatchdog = { restart() { calls.push(['watchdog', 'restart']); }, stop() { calls.push(['watchdog', 'stop']); } };
  const Stream = {
    play(hash, fileIdx) {
      calls.push(['Stream.play', hash, fileIdx, mpv.position]);
      // Model the Android engine's same-source reload clearing time-pos.
      mpv.position = 0;
      mpv.subtitleTrack = '';
      mpv.subDelay = 0;
    },
  };
  const context = vm.createContext({
    root, mpv, Stream, streamWatchdog,
    Qt: { platform: { os: platform }, callLater(fn) { fn(); } },
    Date: { now: () => now },
    console: { info() {}, log() {}, warn() {}, error() {} },
  });
  const session = { root, mpv, calls, context, advanceTime: ms => { now += ms; } };
  installProductionLogic(session);
  return session;
}

function deliverAndroidNetworkError(s, preErrorPaused) {
  // Media3 marks the item paused before it emits onPlaybackError. The native stat
  // seam reports the pause state from immediately before that error transition.
  s.mpv.priorErrorPause = preErrorPaused;
  s.mpv.errorPending = true;
  s.mpv.pause = true;
  s.root.playbackError('network_error', 'Playback failed');
}

function finishReload(s) {
  s.mpv.errorPending = false;
  s.mpv.position = 0;
  s.root.fileLoaded();
}

const position = 4015.357;

test('Android same-source retry seeks back to the captured position after reload clears time-pos', () => {
  const s = makeSession({ preErrorPaused: false });
  s.mpv.position = position;
  deliverAndroidNetworkError(s, false);
  assert.equal(s.calls.filter(call => call[0] === 'Stream.play').length, 1,
    'network error re-demands the same torrent and file');
  assert.equal(s.calls.find(call => call[0] === 'Stream.play')[3], position,
    'retry captures position before the engine reload');
  finishReload(s);
  assert.ok(s.mpv.seekCalls.some(value => Math.abs(value - position) < 0.001),
    `reloaded same source should seek to ${position}; got ${JSON.stringify(s.mpv.seekCalls)}`);
  assert.equal(s.root.resumeChoiceOpen, false, 'recovery is silent and does not show first-open resume UI');
});

test('Android error-induced pause preserves the pre-error playing intent', () => {
  const s = makeSession({ preErrorPaused: false });
  s.mpv.position = position;
  deliverAndroidNetworkError(s, false);
  finishReload(s);
  assert.ok(s.mpv.statCalls.includes('recovery-paused'),
    'Android recovery reads the native pause state captured before markError paused Media3');
  assert.equal(s.mpv.pause, false, 'a session playing before the error resumes after recovery');
});

test('a genuine manual pause remains paused across same-source retry', () => {
  const s = makeSession({ preErrorPaused: true });
  s.mpv.position = position;
  deliverAndroidNetworkError(s, true);
  finishReload(s);
  assert.equal(s.mpv.pause, true, 'retry must not resume a deliberately paused session');
  assert.ok(s.mpv.seekCalls.some(value => Math.abs(value - position) < 0.001),
    'manual pause still restores the same source position');
  assert.ok(s.mpv.statCalls.includes('recovery-paused'),
    'recovery distinguishes the user pause from the pause introduced by the error');
});

test('a live position observed before Android resets time-pos is still used for recovery', () => {
  const s = makeSession({ preErrorPaused: false });
  s.mpv.position = position;
  s.root.positionChanged(); // Actual production onPositionChanged handler.
  s.mpv.position = 0; // Native error has already reset time-pos before QML receives it.
  deliverAndroidNetworkError(s, false);
  finishReload(s);
  assert.ok(s.mpv.seekCalls.some(value => Math.abs(value - position) < 0.001),
    `the last live position ${position} should survive an already-zero error position; got ${JSON.stringify(s.mpv.seekCalls)}`);
});

test('same-item Android reload restores the selected external subtitle and sync delay', () => {
  const s = makeSession({ preErrorPaused: false });
  s.mpv.position = position;
  s.mpv.subDelay = 0.375;
  deliverAndroidNetworkError(s, false);
  finishReload(s);
  const restored = s.calls.find(call => call[0] === 'addSubtitle');
  assert.deepEqual(restored, [
    'addSubtitle', 'https://fixture.invalid/subtitles/en.srt', 'Fixture English', 'en', true,
  ], 'same-source reload re-adds the selected external sidecar');
  assert.equal(s.mpv.subDelay, 0.375, 'same-source reload restores the subtitle sync delay');
});

test('turning subtitles Off during retry does not restore the retired sidecar', () => {
  const s = makeSession();
  deliverAndroidNetworkError(s, false);
  s.root.turnSubtitlesOff();
  finishReload(s);
  assert.equal(s.calls.filter(call => call[0] === 'addSubtitle').length, 0,
    'the latest explicit Off choice invalidates the pending sidecar');
  assert.equal(s.mpv.subtitleTrack, '');
});

test('subtitle sync edits during retry update the sidecar restored on load', () => {
  const s = makeSession();
  s.mpv.subDelay = 0.375;
  deliverAndroidNetworkError(s, false);
  s.root.adjustSubtitleDelay(0.5);
  finishReload(s);
  assert.equal(s.mpv.subDelay, 0.5,
    'the latest sync edit replaces the pre-retry delay (native reload reset it to zero)');
});

test('resetting subtitle sync during retry clears the saved delay', () => {
  const s = makeSession();
  s.mpv.subDelay = 0.375;
  deliverAndroidNetworkError(s, false);
  s.root.resetSubtitleDelay();
  finishReload(s);
  assert.equal(s.mpv.subDelay, 0, 'Reset applies to the pending sidecar too');
});

test('a second failure switches candidate while preserving the selected sidecar and delay', () => {
  const s = makeSession();
  s.root.streamCandidates.push({ infoHash: 'alternate-fixture-hash', fileIdx: 3 });
  s.mpv.subDelay = 0.375;
  deliverAndroidNetworkError(s, false);
  finishReload(s);
  // Model observed playback advancing after the first recovery settles.
  s.root.starting = false;
  s.mpv.position = position + 5;
  deliverAndroidNetworkError(s, false);
  assert.equal(s.root.currentStreamIndex, 1, 'the actual failure handler advances the candidate ladder');
  finishReload(s);
  assert.equal(s.calls.filter(call => call[0] === 'addSubtitle').length, 2,
    'both the same-source retry and alternate load restore the selected sidecar');
  assert.equal(s.mpv.subDelay, 0.375);
  assert.equal(s.mpv.position, position + 5);
});

test('candidate failover retains an explicit subtitle Off made while retry is pending', () => {
  const s = makeSession();
  s.root.streamCandidates.push({ infoHash: 'alternate-fixture-hash', fileIdx: 3 });
  deliverAndroidNetworkError(s, false);
  s.root.turnSubtitlesOff();
  deliverAndroidNetworkError(s, false);
  assert.equal(s.root.currentStreamIndex, 1);
  finishReload(s);
  assert.equal(s.calls.filter(call => call[0] === 'addSubtitle').length, 0);
  assert.equal(s.mpv.subtitleTrack, '');
});

test('a fresh direct source does not inherit a previous stream recovery position', () => {
  const s = makeSession({ preErrorPaused: false });
  s.mpv.position = position;
  deliverAndroidNetworkError(s, false);
  // Start a different source before the retry load finishes; the old recovery
  // target must be invalidated by the actual fresh-source entry point.
  s.root.playUrl('file:///fixture/new-source.mp4', 'New source');
  s.root.fileLoaded();
  assert.equal(s.root.mediaId, '', 'identity-less direct source clears the prior media identity');
  assert.equal(s.mpv.position, 0, 'new source remains at its own start');
  assert.ok(!s.mpv.seekCalls.some(value => Math.abs(value - position) < 0.001),
    'new source must not seek to the retired stream recovery position');
});

test('Android D2 route return keeps its paused or explicit-Play intent ahead of stream recovery', () => {
  const observations = [];
  for (const intent of ['paused', 'playing']) {
    const s = makeSession({ preErrorPaused: false });
    s.root.streamRouteRestoreLoadPending = true;
    s.root.streamRouteRestoreSec = position;
    s.root.streamRouteRestoreIntent = intent;
    s.root.recoveryResumePending = true;
    s.root.recoveryPositionSec = 777;
    s.root.recoveryResumePaused = true;
    s.root.pendingSeekSec = 777;
    s.mpv.pause = true;
    s.mpv.position = 0;
    s.root.fileLoaded();
    observations.push({
      intent,
      position: s.mpv.position,
      paused: s.mpv.pause,
      routePending: s.root.streamRouteRestoreLoadPending,
      genericRecoveryPending: s.root.recoveryResumePending,
      pendingSeekSec: s.root.pendingSeekSec,
    });
  }
  assert.deepEqual(observations, [
    { intent: 'paused', position, paused: true, routePending: false, genericRecoveryPending: false, pendingSeekSec: -1 },
    { intent: 'playing', position, paused: false, routePending: false, genericRecoveryPending: false, pendingSeekSec: -1 },
  ], 'both D2 intents restore their own position/pause state and discard generic retry state');
});
