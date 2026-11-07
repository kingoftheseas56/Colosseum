import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('./public-domain-archive.js', import.meta.url), 'utf8');
const identifier = 'Night_Of_The_Living_Dead_raw_HD_WS';
const filename = 'NightOfTheLivingDeadRawUnrestored.mp4';
const metadata = {
  metadata: { identifier, title: 'Night of the Living Dead - original',
    licenseurl: 'http://creativecommons.org/publicdomain/mark/1.0/' },
  files: [{ name: filename, format: 'h.264 MPEG4', size: '815842023' }],
};
function fixture(body = metadata, secret = '__COLOSSEUM_TMDB_KEY__') {
  const requests = [];
  const context = vm.createContext({
    module: { exports: {} }, TMDB_API_KEY: secret, TMDB_ACCESS_TOKEN: secret,
    process: { env: { TMDB_API_KEY: secret, TMDB_ACCESS_TOKEN: secret } },
    fetch: async url => {
      requests.push(url);
      return { ok: true, json: async () => body };
    },
  });
  vm.runInContext(source, context);
  return { getStreams: context.module.exports.getStreams, requests };
}

test('only the identified public-domain movie invokes a real metadata request', async () => {
  const plugin = fixture();
  assert.equal((await plugin.getStreams('550', 'movie')).length, 0);
  assert.equal((await plugin.getStreams('10331', 'tv')).length, 0);
  assert.equal(plugin.requests.length, 0);
  const streams = await plugin.getStreams(10331, 'movie');
  assert.equal(streams.length, 1);
  assert.equal(streams[0].url, `https://archive.org/download/${identifier}/${filename}`);
  assert.deepEqual(plugin.requests, [`https://archive.org/metadata/${identifier}`]);
});

test('no invented result when live metadata, rights mark, or file is absent', async () => {
  for (const body of [null, {}, { ...metadata, files: [] },
    { ...metadata, metadata: { ...metadata.metadata, licenseurl: '' } },
    { ...metadata, metadata: { ...metadata.metadata, identifier: 'different-item' } },
    { ...metadata, files: [{ ...metadata.files[0], name: '../not-the-approved-file.mp4' }] }]) {
    assert.equal((await fixture(body).getStreams('10331', 'movie')).length, 0);
  }
});

test('rejects real credentials appearing in JS rather than exposing their values', async () => {
  const plugin = fixture(metadata, 'synthetic-test-sentinel');
  await assert.rejects(plugin.getStreams('10331', 'movie'), /Unexpected credential boundary/);
  assert.equal(plugin.requests.length, 0);
});

if (process.argv.includes('--live')) {
  test('live Internet Archive metadata produces the verified original MP4 URL', async () => {
    const response = await fetch(`https://archive.org/metadata/${identifier}`);
    assert.equal(response.status, 200);
    const streams = await fixture(await response.json()).getStreams('10331', 'movie');
    assert.equal(streams.length, 1);
    const media = await fetch(streams[0].url, { headers: { Range: 'bytes=0-1023' } });
    assert.equal(media.status, 206);
    assert.match(media.headers.get('content-type'), /video\/mp4/);
    const bytes = Buffer.from(await media.arrayBuffer());
    assert.equal(bytes.length, 1024);
    assert.equal(bytes.subarray(4, 8).toString(), 'ftyp');
  });
}
