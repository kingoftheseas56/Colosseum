import assert from 'node:assert/strict';
import fs from 'node:fs';
import { test } from 'node:test';

const sources = fs.readFileSync(new URL('../qml/SourcesSheet.qml', import.meta.url), 'utf8').replace(/\r\n/g, '\n');
const book = fs.readFileSync(new URL('../qml/BiblioBook.qml', import.meta.url), 'utf8').replace(/\r\n/g, '\n');
const warmBody = /function warmTopRow\(\) \{([\s\S]*?)\n    \}\n    onQualityFilterChanged/.exec(sources)?.[1];
const editionsBody = /function loadEditions\(\) \{([\s\S]*?)\n    \}\n/.exec(book)?.[1];
assert.ok(warmBody && editionsBody, 'load the actual QML function bodies');
const warmTopRow = new Function('sheet', 'Stream', 'Qt', warmBody);
const loadEditions = new Function('detail', 'Stream', 'Qt', 'Abb', 'Audiobooks', 'abCol', editionsBody);

function streamDouble() {
    const calls = [];
    return { calls, warmSource: (...args) => calls.push(['warm', ...args]),
        prefetch: (...args) => calls.push(['download', ...args]) };
}

function sheetFor(mode = 'play') {
    return { mode, warmedHash: '', filteredRows: () => [
        { streamKind: 'Direct', infoHash: 'url:direct' },
        { streamKind: 'Torrent', infoHash: 'torrent-a', fileIdx: 3 },
        { streamKind: 'Torrent', infoHash: 'torrent-b', fileIdx: 4 }
    ] };
}

test('Android source browsing uses best-effort warming, not an explicit download demand', () => {
    const stream = streamDouble(), sheet = sheetFor();
    warmTopRow(sheet, stream, { platform: { os: 'android' } });
    warmTopRow(sheet, stream, { platform: { os: 'android' } });
    assert.deepEqual(stream.calls, [['warm', 'torrent-a', 3]]);
});

test('download source picker performs no warming or download until a source is selected', () => {
    const stream = streamDouble();
    warmTopRow(sheetFor('download'), stream, { platform: { os: 'android' } });
    assert.deepEqual(stream.calls, []);
});

test('desktop source browsing retains its existing prefetch behavior', () => {
    const stream = streamDouble();
    warmTopRow(sheetFor(), stream, { platform: { os: 'windows' } });
    assert.deepEqual(stream.calls, [['download', 'torrent-a', 3]]);
});

for (const os of ['android', 'windows']) {
    test(`${os} audiobook discovery routes its speculative preparation correctly`, () => {
        const stream = streamDouble();
        const detail = { book: { title: 'Book', author: 'Author' }, sourceGeneration: 0,
            pairKey: 'book', loadLibgenEditions() {}, loadTorrents() {}, refreshLocal() {} };
        const abCol = { syncStatus() {} };
        const abb = { resolveAudiobook(_title, _author, done) { done({ rows: [{ slug: 'book' }] }); },
            fetchInfoHash(_slug, done) { done({ infoHash: 'audio-a' }); } };
        loadEditions(detail, stream, { platform: { os } }, abb, { isDownloaded: () => false }, abCol);
        assert.equal(detail.abInfoHash, 'audio-a');
        assert.deepEqual(stream.calls, os === 'android' ? [] : [['download', 'audio-a', 0]]);
    });
}
