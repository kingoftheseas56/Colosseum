import test from 'node:test';
import assert from 'node:assert/strict';
import { createWorker } from '../src/worker.mjs';
import { manifest as productionManifest, providers as productionProviders } from '../src/index.js';
import lectorxd from '../../../extensions/tankoyomi/languages/es/lectorxd.js';

const seriesPath = '/manhwa/streaming-de-la-guia-oculta-del-retornado';
const seriesUrl = 'https://lectorxd.com' + seriesPath;
const parkedLectorManifest = {
  id: 'colosseum.well.tankoyomi',
  version: 'parked-lectorxd-fixture',
  defaultLanguage: 'en',
  languages: [{
    code: 'es',
    providers: [{ id: 'lectorxd', enabled: false, allowedHosts: ['lectorxd.com'] }]
  }]
};
const parkedLectorProviders = { 'es/lectorxd': lectorxd };

function post(worker, provider, method, value) {
  return worker.fetch(new Request(`https://worker.example/v1/es/${provider}/${method}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(value)
  }));
}

function parkedLectorWorker(fetchImpl) {
  return createWorker({ manifest: parkedLectorManifest, providers: parkedLectorProviders, fetchImpl });
}

test('injected parked LectorXD hydration reaches the JSON API through the scoped upstream transport', async () => {
  // Synthetic serialized Astro props based on the pinned upstream contract;
  // this test does not establish live source availability.
  const props = JSON.stringify({ chapters: [1, [
    [0, { chapter: [0, 20.5], createdAt: [3, '2026-10-25T00:00:00Z'] }],
    [0, { chapter: [0, '19'] }]
  ]] }).replace(/"/g, '&quot;');
  const calls = [];
  const worker = parkedLectorWorker(async (url, init) => {
    calls.push({ url: String(url), init });
    return new Response(`<astro-island component-url="/_astro/ChapterList.fixture.js" props="${props}"></astro-island>`);
  });
  const response = await post(worker, 'lectorxd', 'getChapters', { series: { url: seriesUrl } });
  assert.equal(response.status, 200);
  const payload = await response.json();
  assert.equal(payload.contractVersion, 1);
  assert.deepEqual(payload.result.map(row => [row.number, row.url]), [
    [19, seriesUrl + '/leer/19'], [20.5, seriesUrl + '/leer/20.5']
  ]);
  assert(payload.result.every(row => row.source === 'lectorxd' && row.language === 'es'));
  assert.equal(calls.length, 1);
  assert.equal(calls[0].url, seriesUrl);
  assert.equal(calls[0].init.redirect, 'manual');
  assert.equal(new Headers(calls[0].init.headers).get('referer'), 'https://lectorxd.com/');
});

test('injected parked LectorXD rejects malformed hydration without accepting legacy fallback rows', async () => {
  const worker = parkedLectorWorker(async () => new Response(
    '<astro-island component-url="/_astro/ChapterList.fixture.js" props="{}"></astro-island>' +
    `<a href="${seriesPath}/leer/1"><h4>Capítulo 1</h4></a>`
  ));
  const response = await post(worker, 'lectorxd', 'getChapters', { series: { url: seriesUrl } });
  assert.equal(response.status, 502);
  const payload = await response.json();
  assert.equal(payload.contractVersion, 1);
  assert.match(payload.error, /Astro chapter props are invalid/);
  assert.equal(payload.result, undefined);
});

test('production MangaSin stays disabled and upstream denials are explicit API failures', async () => {
  const entries = productionManifest.languages.find(language => language.code === 'es').providers;
  assert.equal(entries.find(entry => entry.id === 'mangasin')?.enabled, false);
  const calls = [];
  const worker = createWorker({ manifest: productionManifest, providers: productionProviders, fetchImpl: async url => {
    calls.push(String(url));
    return new Response('Blocked', { status: 403 });
  } });
  const response = await post(worker, 'mangasin', 'searchSeries', { query: 'One Piece' });
  assert.equal(response.status, 502);
  assert.deepEqual(await response.json(), { contractVersion: 1, error: 'Upstream returned HTTP 403' });
  assert.deepEqual(calls, ['https://m440.in/search?q=One%20Piece']);
});

test('injected parked LectorXD title search makes no speculative upstream requests', async () => {
  let calls = 0;
  const worker = parkedLectorWorker(async () => {
    calls++;
    throw new Error('Unexpected upstream request');
  });
  const response = await post(worker, 'lectorxd', 'searchSeries', { query: 'One Piece' });
  assert.equal(response.status, 502);
  assert.match((await response.json()).error, /title search is unsupported/);
  assert.equal(calls, 0);
});
