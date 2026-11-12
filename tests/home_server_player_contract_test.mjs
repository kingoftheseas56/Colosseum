import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('../qml/PlayerPage.qml', import.meta.url), 'utf8');

// Execute the shipped QML functions, with only native/media services replaced.
function extractedFunction(name) {
  const marker = `function ${name}(`;
  const start = source.indexOf(marker);
  assert.ok(start >= 0, marker);
  const open = source.indexOf('{', start);
  let depth = 0, mode = '', escaped = false;
  for (let i = open; i < source.length; i++) {
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
  throw new Error(`Unclosed ${name}`);
}

const key = 'home-server:server-uuid:ItemCase:VersionCase';
const home = () => ({ infoHash: key, homeServer: true, canonicalId: 'tt1234567:2:3',
  connectionId: 'server-uuid', itemId: 'ItemCase', versionId: 'VersionCase',
  sourceName: 'Your server · Living room', addonId: 'home-server:server-uuid',
  addonName: 'Your server · Living room', streamKind: 'Home server', fileIdx: 0 });
function session(os = 'android') {
  const native = [], plain = [], torrents = [], policyInputs = [], cancellations = [], events = [], progress = [];
  const noop = () => {};
  let restarts = 0, stops = 0;
  const service = {
    profileGeneration: 1, accepts: true,
    loadPlayback(backend, sourceKey, resumeMs, forceTranscode) {
      events.push('native-load');
      native.push({ backend, sourceKey, resumeMs, forceTranscode });
      return this.accepts;
    },
    cancelPlayback(backend) { cancellations.push(backend); events.push('native-cancel'); },
  };
  const root = {
    homeServerSourceService: service, homePlaybackActive: false,
    homePlaybackTranscoded: false, homeTranscodeTried: false,
    homePlaybackKey: '', homePlaybackProfileGeneration: -1,
    homePlaybackService: null,
    currentPlaybackUrl: '', activeDirectHeaders: { Authorization: 'old-direct-header' },
    currentStreamIndex: 0, streamCandidates: [home()], pendingSeekSec: 56.75,
    mediaResumeHash: '', mediaResumeFileIdx: 0, streamRetryCount: 0,
    mediaLocalPath: '', mediaId: '', mediaTitle: '', mediaArt: '', mediaYear: '',
    starting: true, errored: false, fileReady: false, visible: true,
    streamDemandActive: true, streamDemandEpoch: 1, playbackEnded: false,
    recoveryResumePending: false, recoveryPositionSec: -1,
    wakeReconnectLastTickAt: 1, wakeReconnectGapSeconds: 30,
    subStreamId: '', subStreamType: '', hydrateGen: 0,
    isStreamDead: () => false, updateMediaSubtitle: noop, clearStreamRouteRestore: noop,
    resetRecoveryWatch: noop, closeMenus: noop, wakeChrome: noop, forceActiveFocus: noop,
    recordProgress: noop, clearAbLoop: noop, cancelSleepTimer: noop,
    resetSkipSegments: noop, resetTrackAutomation: noop, cancelUpNext: noop,
    fetchSubtitles: noop, maybeHydrateContext: noop, resolveAdjacentContext: () => ({}),
    requestAudioPlayback: noop, recoveryExcluded: () => false,
    sendTraktPlayback: noop, activityEndSession: noop,
    streamRouteRetired: () => false, recoveryPauseIntent: () => false,
    captureSelectedSidecarSubtitle: () => null, resetRecoveryResume: noop,
    activityCompletionEventId: '', activitySessionId: '',
    parseSubtitleMeta: () => ({ type: 'movie' }), fmtTime: () => 'remaining',
    clamp: (value, min, max) => Math.max(min, Math.min(max, value)),
  };
  const mpv = {
    position: 120, duration: 3600, pause: false,
    loadSource(url, headers) { assert.ok(!url.startsWith('home-server:')); events.push('plain-load'); plain.push({ url, headers }); },
    stopPlayback() { events.push('backend-stop'); },
  };
  const context = vm.createContext({ root, mpv, qsTr: value => value,
    Qt: { platform: { os } }, Date: { now: () => 100000 },
    Stream: {
      stopDemand: () => stops++, play(hash) { assert.ok(!hash.startsWith('home-server:')); events.push('torrent-play'); torrents.push(hash); },
    },
    DirectSourcePolicy: {
      admitProviderUrl(url) { policyInputs.push(url); assert.ok(!url.startsWith('home-server:')); return /^https?:/.test(url) ? url : ''; },
      copyHeaders: headers => ({ ...headers }),
    },
    streamWatchdog: { restart: () => restarts++, stop: noop },
    Progress: { record: entry => progress.push(entry), recordSilent: entry => progress.push(entry), get: () => ({}) },
    EpisodeBrowser: { seriesRootId: id => id.split(':')[0] },
  });
  for (const name of ['normalizeStreamCandidates', 'directStreamUrl', 'findStreamIndex', 'streamCandidateKey',
    'currentStreamCandidate', 'clearActiveDirectHeaders', 'loadHomeServerCandidate', 'failHomeServerPlayback', 'invalidateHomeServerRoute',
    'homePlaybackContextCurrent', 'homeServerPlaybackResolved', 'homeServerPlaybackFailed',
    'playStreamAt', 'playTorrent', 'retryCurrentStream', 'reloadActiveDirectSource', 'loadDirectStreamUrl',
    'handlePlaybackFailure', 'prepareRecoveryResume', 'replayEndedPlayback', 'tickWakeReconnect', 'stop']) {
    root[name] = vm.runInContext(`(${extractedFunction(name)})`, context);
  }
  const recordProgress = vm.runInContext(`(${extractedFunction('recordProgress')})`, context);
  return { root, mpv, service, native, plain, torrents, policyInputs, cancellations, events, progress, recordProgress,
    get restarts() { return restarts; }, get stops() { return stops; } };
}

test('normalization preserves opaque identity and excludes URL/headers from home candidates', () => {
  const s = session();
  const row = { ...home(), url: 'https://must-not-be-used.example.org', headers: { Authorization: 'must-not-survive' } };
  const [normalized] = s.root.normalizeStreamCandidates(key, 0, 'Movie', [row]);
  for (const field of ['infoHash', 'canonicalId', 'connectionId', 'itemId', 'versionId', 'addonId'])
    assert.equal(normalized[field], row[field]);
  assert.equal(normalized.homeServer, true);
  assert.equal(normalized.url, '');
  assert.equal(Object.keys(normalized.headers).length, 0);
  assert.equal(s.root.directStreamUrl(row), '');
  assert.equal(s.policyInputs.length, 0);
  const [restored] = s.root.normalizeStreamCandidates(key, 0, 'Movie', []);
  assert.equal(restored.homeServer, true);
  assert.equal(restored.streamKind, 'Home server');
  s.root.streamCandidates = [{ ...home(), infoHash: key.toLowerCase() }, home()];
  assert.equal(s.root.findStreamIndex(key, 0), 1);
  assert.notEqual(s.root.streamCandidateKey(s.root.streamCandidates[0]), s.root.streamCandidateKey(home()));
});

test('selected home source uses native backend handoff with opaque progress key and resume', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  assert.equal(s.native.length, 1);
  assert.equal(s.native[0].backend, s.mpv);
  assert.equal(s.native[0].sourceKey, key);
  assert.equal(s.native[0].resumeMs, 56750);
  assert.equal(s.native[0].forceTranscode, false);
  assert.equal(s.root.currentPlaybackUrl, key);
  assert.equal(s.root.mediaResumeHash, key);
  assert.equal(s.root.streamDemandActive, false);
  assert.equal(s.stops, 1);
  assert.equal(Object.keys(s.root.activeDirectHeaders).length, 0);
  assert.equal(s.plain.length + s.torrents.length + s.policyInputs.length, 0);
});

test('playTorrent selects canonical home identity before progress/resume setup', () => {
  const s = session();
  s.root.playTorrent(key, 0, 'Episode', '', 'series', 'tmdb:tv:456:2:3', [home()], { resumePosition: 70 });
  assert.equal(s.root.mediaId, 'tt1234567:2:3');
  assert.equal(s.native[0].sourceKey, key);
  assert.equal(s.native[0].resumeMs, 70000);
});

test('callbacks reject stale source, closed session and profile generations', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  const baseline = s.restarts;
  s.root.homeServerPlaybackResolved('home-server:other:item:version', true);
  assert.equal(s.restarts, baseline);
  s.service.profileGeneration++;
  s.root.homeServerPlaybackResolved(key, true);
  s.root.homeServerPlaybackFailed(key, 'stale failure');
  assert.equal(s.root.errored, false);
  assert.equal(s.restarts, baseline);
  s.service.profileGeneration--;
  s.root.homeServerPlaybackResolved(key, true);
  assert.equal(s.root.homePlaybackTranscoded, true);
  assert.equal(s.root.currentPlaybackUrl, key);
  assert.equal(s.restarts, baseline + 1);
  s.root.homePlaybackActive = false;
  s.root.homeServerPlaybackFailed(key, 'closed');
  assert.equal(s.root.errored, false);
});

test('one forced transcode preserves live position, second failure is honest', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  s.root.starting = false;
  s.root.homeServerPlaybackResolved(key, false);
  s.root.handlePlaybackFailure('decode');
  assert.equal(s.native.length, 2);
  assert.equal(s.native[1].forceTranscode, true);
  assert.equal(s.native[1].resumeMs, 120000);
  assert.equal(s.root.homeTranscodeTried, true);
  s.root.homeServerPlaybackResolved(key, true);
  s.root.handlePlaybackFailure('decode');
  assert.equal(s.native.length, 2);
  assert.equal(s.root.errored, true);
  assert.equal(s.root.starting, false);
  assert.equal(s.root.homePlaybackActive, false);
});

test('already transcoded playback fails without repeated negotiation', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  s.root.homeServerPlaybackResolved(key, true);
  s.root.handlePlaybackFailure('network');
  assert.equal(s.native.length, 1);
  assert.equal(s.root.errored, true);
});

test('retry, wake reconnect and Android EOF replay keep native path', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  s.root.retryCurrentStream();
  assert.equal(s.native.length, 2);
  s.root.starting = false;
  s.root.tickWakeReconnect();
  assert.equal(s.native.length, 3);
  assert.equal(s.native[2].resumeMs, 120000);
  s.root.starting = false;
  s.root.playbackEnded = true;
  s.root.homeTranscodeTried = true;
  s.root.homePlaybackTranscoded = true;
  assert.equal(s.root.replayEndedPlayback(), true);
  assert.equal(s.native.length, 4);
  assert.equal(s.native[3].resumeMs, 0);
  assert.equal(s.native[3].forceTranscode, false);
  assert.equal(s.plain.length + s.torrents.length + s.policyInputs.length, 0);
});

test('native rejection and async failure use existing error state', () => {
  const s = session();
  s.service.accepts = false;
  s.root.playStreamAt(0, 'start');
  assert.equal(s.root.errored, true);
  assert.equal(s.root.starting, false);
  s.service.accepts = true;
  s.root.playStreamAt(0, 'start');
  s.root.homeServerPlaybackFailed(key, 'Server unavailable');
  assert.equal(s.root.statusMsg, 'Server unavailable');
  assert.equal(s.root.errored, true);
  assert.equal(s.native.length, 2);
});

test('switching from home retains ordinary direct and torrent dispatch', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  const direct = { infoHash: 'url:https://video.example.org/movie.m3u8', streamKind: 'Direct', headers: { Referer: 'fixture' } };
  s.root.streamCandidates = [home(), direct, { infoHash: 'ABCDEF', fileIdx: 0 }];
  s.root.playStreamAt(1, 'switch');
  assert.equal(s.cancellations.length, 1);
  assert.equal(s.cancellations[0], s.mpv);
  assert.ok(s.events.indexOf('native-cancel') < s.events.indexOf('plain-load'));
  assert.equal(s.root.homePlaybackActive, false);
  assert.equal(s.plain[0].url, 'https://video.example.org/movie.m3u8');
  s.root.homeServerPlaybackResolved(key, true);
  assert.equal(s.root.homePlaybackTranscoded, false);
  s.root.playStreamAt(2, 'switch');
  assert.deepEqual(s.torrents, ['ABCDEF']);
  assert.equal(s.cancellations.length, 1);
});

test('new home load cancels prior native generation before handoff; ordinary sessions do not cancel', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  s.root.retryCurrentStream();
  assert.deepEqual(s.events, ['native-load', 'native-cancel', 'native-load']);
  s.root.invalidateHomeServerRoute();
  s.root.invalidateHomeServerRoute();
  assert.equal(s.cancellations.length, 2);
});

test('home to torrent and explicit stop invalidate native generation before ordinary backend work', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  s.root.streamCandidates.push({ infoHash: 'ABCDEF', fileIdx: 0 });
  s.root.playStreamAt(1, 'switch');
  assert.deepEqual(s.events, ['native-load', 'native-cancel', 'torrent-play']);
  s.root.playStreamAt(0, 'switch');
  s.root.stop();
  assert.deepEqual(s.events.slice(-3), ['native-load', 'native-cancel', 'backend-stop']);
  assert.equal(s.root.homePlaybackActive, false);
});

test('old-profile home EOF/stop cannot write into newly bound Progress; ordinary sources retain writes', () => {
  const s = session();
  s.root.playStreamAt(0, 'start');
  s.root.mediaId = 'tt1234567';
  s.recordProgress(false);
  assert.equal(s.progress.length, 1);
  assert.equal(s.progress[0].resume.infoHash, key);
  s.service.profileGeneration++;
  s.recordProgress(false);
  s.recordProgress(true);
  assert.equal(s.progress.length, 1);
  s.root.invalidateHomeServerRoute();
  s.recordProgress(false);
  assert.equal(s.progress.length, 1);
  s.root.currentPlaybackUrl = 'https://video.example.org/movie.m3u8';
  s.root.mediaResumeHash = 'url:https://video.example.org/movie.m3u8';
  s.recordProgress(false);
  assert.equal(s.progress.length, 2);
});
