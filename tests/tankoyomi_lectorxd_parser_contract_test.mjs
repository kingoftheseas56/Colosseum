import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const providerPath = path.resolve('extensions/tankoyomi/languages/es/lectorxd.js');
const providerSource = fs.readFileSync(providerPath, 'utf8');
const provider = require(providerPath);

const BASE = 'https://lectorxd.com';
const SERIES_PATH = '/manhwa/streaming-de-la-guia-oculta-del-retornado';
const SERIES_URL = BASE + SERIES_PATH;
const CHAPTER_PATH = SERIES_PATH + '/leer/20';
const CHAPTER_URL = BASE + CHAPTER_PATH;

function contextFor(responses) {
  const calls = [];
  return {
    calls,
    fetchText(url, options) {
      calls.push({ url: String(url), options: options || {} });
      if (!(url in responses)) return Promise.reject(new Error('unexpected fetch ' + url));
      return Promise.resolve(responses[url]);
    }
  };
}

// The route shape, selectors, and sample chapter path come from the pinned
// HaruNeko LectorXD adapter at 88ce1fa5e2e08a00949d2f00619c4bb1e17de29f.
// Image paths below are deterministic parser fixtures, not captured live URLs.
const seriesHtml = [
  '<div class="grid">',
  `  <div class="bg-white"><div><a href="${CHAPTER_PATH}"><div><h4>Capítulo 20</h4></div></a></div></div>`,
  `  <div class="bg-white"><div><a href="${SERIES_PATH}/leer/19"><div><h4>Capítulo 19</h4></div></a></div></div>`,
  `  <div class="bg-white"><div><a href="${CHAPTER_PATH}"><div><h4>Capítulo 20</h4></div></a></div></div>`,
  '  <div class="bg-white"><a href="/manga/otro-titulo/leer/1"><h4>Capítulo 1</h4></a></div>',
  '</div>'
].join('');

const pageHtml = [
  '<main id="reader-content">',
  '<img class="page-image" src="/images/placeholder.webp" data-src="https://s1.cdnlxd.xyz/fixture/chapter-20/page-01.webp">',
  '<img class="page-image" src="https://s2.cdnlxd.xyz/fixture/chapter-20/page-02.webp">',
  '<img class="page-image" data-src="https://s1.cdnlxd.xyz/fixture/chapter-20/page-01.webp">',
  '</main>',
  '<img class="cover-image" src="https://s1.cdnlxd.xyz/fixture/cover.webp">'
].join('');

assert.equal(provider.id, 'lectorxd');
assert.equal(provider.language, 'es');
assert.equal(provider.baseUrl, BASE);
assert.equal(typeof provider.searchSeries, 'function');
assert.equal(typeof provider.getChapters, 'function');
assert.equal(typeof provider.getPages, 'function');
assert.doesNotMatch(providerSource, /^\s*(?:import|export)\s/m, 'provider stays script-runtime compatible');
assert.doesNotMatch(providerSource, /\basync\s+function\b|\bawait\b|\bfetch\s*\(/,
  'provider uses the scoped Promise network context');
assert.doesNotMatch(providerSource, /new\s+URL\s*\(/,
  'provider does not depend on the browser-only URL constructor');

// HaruNeko's pinned adapter defines catalog pagination but no title-search
// route. The provider must surface that limitation instead of returning a
// guessed URL or fabricated result.
{
  const ctx = contextFor({});
  await assert.rejects(
    () => provider.searchSeries(ctx, 'Streaming de la guia oculta del retornado'),
    /LectorXD title search is unsupported/
  );
  assert.equal(ctx.calls.length, 0, 'unsupported search does not make a guessed request');
}

{
  const ctx = contextFor({ [SERIES_URL]: seriesHtml });
  const rows = await provider.getChapters(ctx, {
    id: SERIES_PATH,
    title: 'Streaming de la guia oculta del retornado',
    url: SERIES_URL,
    source: 'lectorxd',
    language: 'es'
  });

  assert.deepEqual(rows.map(row => row.id), [SERIES_PATH + '/leer/19', CHAPTER_PATH]);
  assert.deepEqual(rows.map(row => row.number), [19, 20]);
  assert.deepEqual(rows.map(row => row.url), [BASE + SERIES_PATH + '/leer/19', CHAPTER_URL]);
  assert(rows.every(row => row.seriesId === SERIES_PATH && row.seriesUrl === SERIES_URL));
  assert(rows.every(row => row.source === 'lectorxd' && row.language === 'es'));
  assert.equal(ctx.calls.length, 1, 'chapter parsing fetches one canonical series page');
  assert.equal(ctx.calls[0].url, SERIES_URL);
  assert.equal(ctx.calls[0].options.headers.Referer, BASE + '/');
}

{
  const ctx = contextFor({ [CHAPTER_URL]: pageHtml });
  const pages = await provider.getPages(ctx, {
    id: CHAPTER_PATH,
    seriesId: SERIES_PATH,
    seriesUrl: SERIES_URL,
    url: CHAPTER_URL,
    source: 'lectorxd',
    language: 'es'
  });

  assert.deepEqual(pages.map(page => page.index), [0, 1]);
  assert.deepEqual(pages.map(page => page.url), [
    'https://s1.cdnlxd.xyz/fixture/chapter-20/page-01.webp',
    'https://s2.cdnlxd.xyz/fixture/chapter-20/page-02.webp'
  ]);
  assert(pages.every(page => page.referer === CHAPTER_URL));
  assert.equal(ctx.calls.length, 1, 'page parsing fetches one reader page');
  assert.equal(ctx.calls[0].url, CHAPTER_URL);
  assert.equal(ctx.calls[0].options.headers.Referer, SERIES_URL);
}

console.log('PASS — LectorXD parser preserves route IDs, chapter order, and page URLs; title search stays explicitly unsupported');
