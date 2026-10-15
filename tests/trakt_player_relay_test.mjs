import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

// Execute the actual QML functions with their native seams replaced by fakes.
// Loading either full player would also start unrelated playback services.
for (const [name, path] of [
  ['Player 1', '../qml/PlayerPage.qml'],
  ['Player 2', '../qml/player2host/Player2Page.qml'],
]) {
  const source = readFileSync(new URL(path, import.meta.url), 'utf8');
  const start = source.indexOf('function sendTraktPlayback(playing)');
  assert.ok(start >= 0);
  let cursor = source.indexOf('{', start), depth = 1;
  const bodyStart = cursor++;
  while (depth && cursor < source.length) {
    if (source[cursor] === '{') depth++;
    if (source[cursor] === '}') depth--;
    cursor++;
  }
  assert.equal(depth, 0);
  const code = `(function(playing) ${source.slice(bodyStart, cursor)})`;

  function fixture(suppression) {
    const events = [];
    const player = {
      traktPlaying: false, mediaId: 'tt0000042', subStreamType: 'movie',
      mediaTitle: 'Fixture', traktVideoId: '', traktTitle: '', traktType: '',
    };
    const context = {
      root: player, page: player,
      mpv: { position: 30, duration: 300 },
      backend: { session: { position: 30, duration: 300 } },
      stremioSyncState: {
        linkedAccount: true, hasTrakt: true,
        sendTraktEvent(action, payload) { events.push({ action, payload }); return true; },
      },
    };
    if (suppression !== undefined)
      context.ProfileTrackers = { suppressLegacyTrackerPlaybackRelay: suppression };
    return { events, player, relay: vm.runInNewContext(code, context) };
  }

  test(`${name}: direct live tracking suppresses playing and paused relays`, () => {
    const { events, player, relay } = fixture(true);
    relay(true);
    player.traktPlaying = true;
    player.traktVideoId = player.mediaId;
    relay(false);
    assert.equal(events.length, 0);
    assert.equal(player.traktPlaying, false);
  });

  for (const state of [false, undefined]) {
    test(`${name}: legacy relays continue when suppression is ${state}`, () => {
      const { events, relay } = fixture(state);
      relay(true);
      relay(false);
      assert.deepEqual(events.map(event => event.action), ['traktPlaying', 'traktPaused']);
      assert.equal(events[0].payload.mediaId, 'tt0000042');
    });
  }
}
