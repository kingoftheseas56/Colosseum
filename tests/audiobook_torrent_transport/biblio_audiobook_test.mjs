import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const source = fs.readFileSync(new URL('../../qml/BiblioBook.qml', import.meta.url), 'utf8');
function method(name) {
  const match = source.match(new RegExp(String.raw`^([ \t]*)function ${name}\(([^)]*)\) \{\r?\n([\s\S]*?)^\1\}`, 'm'));
  assert.ok(match, name);
  return '(function(' + match[2] + '){' + match[3] + '\n})';
}

let prefetches = 0;
const calls = [];
const abCol = { abState: 'idle', abActiveSlug: '', syncStatus() {} };
const detail = {
  book: { title: 'Book', author: 'Author', pairKey: 'book' },
  pairKey: 'book', audiobookGeneration: 0, audiobookBayEnabled: true,
  audiobooksRef: { isDownloaded: () => false },
  streamRef: { prefetch: () => prefetches++ },
  loadLibgenEditions: () => calls.push('LibGen editions'),
  loadTorrents: () => calls.push('ebook torrents'),
  refreshLocal: () => calls.push('local ebook')
};
const context = {
  detail, abCol, Qt: { platform: { os: 'windows' } },
  Abb: {
    resolveAudiobook: (_title, _author, done) => done({ rows: [{ slug: 'top' }] }),
    fetchInfoHash: (_slug, done) => done({ infoHash: 'audiobook-hash' })
  }
};
for (const name of ['audiobookBusy', 'loadAudiobookSources']) {
  if (source.includes('function ' + name + '('))
    detail[name] = vm.runInNewContext(method(name), context);
}
const openEbook = vm.runInNewContext(method('loadEditions'), context);

for (const platform of ['windows', 'android']) {
  context.Qt.platform.os = platform;
  calls.length = 0;
  openEbook();
  assert.equal(prefetches, 0, 'opening an ebook must not prefetch an audiobook on ' + platform);
  assert.deepEqual(calls, ['LibGen editions', 'ebook torrents', 'local ebook']);
}
console.log('PASS: the actual ebook page-open function still loads ebook editions and local copies without starting an audiobook torrent.');
