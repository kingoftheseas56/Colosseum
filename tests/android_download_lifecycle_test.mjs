// Execute the production resolver bodies: old asynchronous source results must
// not restart a download after its Android foreground generation was retired.
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
const qml = readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8').replace(/\r\n/g, '\n');
function body(name, indent = 4) {
    const pad = ' '.repeat(indent);
    const match = new RegExp(`function ${name}\\([^)]*\\) \\{([\\s\\S]*?)\\n${pad}\\}`).exec(qml);
    assert.ok(match, `production function ${name} exists`);
    return match[1];
}
function fixture() {
    const calls = [], callbacks = [];
    const win = { pendingFeeds: {}, pendingResolves: [], downloadResolveEpoch: 0 };
    const Download = {
        jobs: () => [],
        failJob: (...args) => calls.push(['fail', ...args]),
        feedSource: (...args) => calls.push(['direct', ...args]),
        feedUrl: (...args) => calls.push(['old-feed', ...args]),
        feedTorrentUrl: (...args) => calls.push(['torrent-feed', ...args]),
    };
    const Stream = { prefetch: (...args) => calls.push(['prefetch', ...args]) };
    const AddonClient = { streamExtensions: () => [], torrentioEnabled: () => true };
    const Torrentio = { loadStreams: (_type, _id, cb) => callbacks.push(cb) };
    const globals = [win, Download, Stream, AddonClient, Torrentio, { installed: () => [] }];
    const names = ['win', 'Download', 'Stream', 'AddonClient', 'Torrentio', 'Extensions'];
    const pinned = new Function('Download', 'id', body('pinnedPickFor')).bind(null, Download);
    const resolve = new Function(...names, 'pinnedPickFor', 'id', 'streamId', 'mediaType', body('resolveDownloadJob'));
    return { win, calls, callbacks, Download,
        resolve: () => resolve(...globals, pinned, 'movie', 'tt0063350', 'movie') };
}
const hash = '11ea02584fa6351956f35671962ab46354d99060';
for (const rows of [[{ infoHash: hash, fileIdx: 0 }], []]) {
    const f = fixture();
    f.resolve();
    f.win.downloadResolveEpoch++;
    f.callbacks[0](rows);
    assert.deepEqual(f.calls, [], 'retired source callback cannot prefetch or fail the paused/restarted job');
}
const f = fixture();
f.resolve();
f.callbacks[0]([{ infoHash: hash, fileIdx: 3 }]);
assert.deepEqual(f.calls, [['prefetch', hash, 3]], 'current source resolution still demands its exact file');
new Function('win', 'Download', 'url', 'infoHash', 'fileIdx', body('onFetchReady', 8))(
    f.win, f.Download, 'http://127.0.0.1:9001/fresh', hash, 3);
assert.deepEqual(f.calls[1], ['torrent-feed', 'movie', 'http://127.0.0.1:9001/fresh', hash, 3],
    'fresh route hands the resolved torrent identity to the partial-file owner');
f.win.pendingResolves = [{ id: 'movie' }];
f.win.pendingFeeds = { old: 'movie' };
const oldEpoch = f.win.downloadResolveEpoch;
new Function('win', body('onResolutionReset', 8))(f.win);
assert.equal(f.win.downloadResolveEpoch, oldEpoch + 1);
assert.deepEqual(f.win.pendingFeeds, {});
assert.deepEqual(f.win.pendingResolves, []);
console.log('ANDROID_DOWNLOAD_LIFECYCLE PASS');
