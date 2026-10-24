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

function escapeHtmlAttribute(value) {
  return String(value).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function chapterListIsland(propsJson) {
  return `<astro-island component-url="/_astro/ChapterList.fixture.js" props="${escapeHtmlAttribute(propsJson)}"></astro-island>`;
}

// The legacy anchor route and sample chapter path come from HaruNeko commit
// 88ce1fa5e2e08a00949d2f00619c4bb1e17de29f. Current ChapterList hydration
// comes from HaruNeko commit f00b48e4e2c48252d419e73cb2041baa127a0ca8.
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
assert.doesNotMatch(providerSource, /\beval\s*\(|\b(?:new\s+)?Function\s*\(/,
  'provider never executes upstream scripts or serialized code');

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

// Astro 7.0.4 commit 5978993531ba3f02a4c639d223f810e80a1fa7d3 serializes
// object fields as [type, value]: type 0 is a plain value/object and type 1 is
// an array. Sources: https://github.com/withastro/astro/blob/5978993531ba3f02a4c639d223f810e80a1fa7d3/packages/astro/src/runtime/server/serialize.ts
// and https://github.com/withastro/astro/blob/5978993531ba3f02a4c639d223f810e80a1fa7d3/packages/astro/src/runtime/server/astro-island.ts;
// values are HTML-escaped before placing JSON in the props attribute.
{
  const serializedProps = JSON.stringify({
    chapters: [1, [
      [0, { chapter: [0, 20.5] }],
      [0, { chapter: [0, '19'] }],
      [0, { chapter: [0, 20.5] }]
    ]],
    unrelated: [3, '2026-10-25T00:00:00.000Z'],
    entityText: [0, '&quot; must remain literal after one HTML decode']
  });
  const html = chapterListIsland(serializedProps);
  const ctx = contextFor({ [SERIES_URL]: html });
  const rows = await provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL });

  assert.deepEqual(rows.map(row => row.id), [
    SERIES_PATH + '/leer/19', SERIES_PATH + '/leer/20.5'
  ]);
  assert.deepEqual(rows.map(row => row.number), [19, 20.5]);
  assert.deepEqual(rows.map(row => row.title), ['Capítulo 19', 'Capítulo 20.5']);
  assert(rows.every(row => row.seriesId === SERIES_PATH && row.seriesUrl === SERIES_URL));
  assert.equal(ctx.calls.length, 1);
}

// Unrelated serialized metadata does not consume the chapter budget or make
// ordinary long chapter lists fail. The 1 MiB props, depth 32, and 8,000-row
// limits bound parsing; only the row's chapter tuple is interpreted.
{
  const longChapters = Array.from({ length: 1200 }, (_, index) => [0, {
    chapter: [0, index + 1],
    title: [0, `Chapter label ${index + 1}`],
    date: [3, '2026-10-25T00:00:00.000Z'],
    cover: [0, { src: [0, '/covers/fixture.webp'], width: [0, 320] }],
    tags: [1, [[0, 'fixture'], [0, 'serialized metadata']]],
    author: [0, 'Fixture author'],
    language: [0, 'es'],
    extraA: [0, true],
    extraB: [0, null],
    extraC: [0, index]
  }]);
  const html = chapterListIsland(JSON.stringify({
    chapters: [1, longChapters],
    unrelated: [3, '2026-10-25T00:00:00.000Z']
  }));
  const ctx = contextFor({ [SERIES_URL]: html });
  const rows = await provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL });
  assert.equal(rows.length, 1200);
  assert.deepEqual(rows.slice(0, 3).map(row => row.number), [1, 2, 3]);
  assert.equal(rows[1199].number, 1200);
}

// Once the ChapterList island is present, malformed or unsupported explicit
// props must fail closed instead of falling back to server-rendered anchors.
{
  const fallbackAnchor = `<a href="${CHAPTER_PATH}"><h4>Capítulo 20</h4></a>`;
  const malformedProps = JSON.stringify({ chapters: [2, 'not supported'] });
  const ctx = contextFor({ [SERIES_URL]: chapterListIsland(malformedProps) + fallbackAnchor });
  await assert.rejects(
    () => provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL }),
    /LectorXD Astro chapter props are invalid/
  );
}

// Malformed numeric route segments and foreign URLs cannot escape the
// canonical series path. Legacy anchors remain supported when no island exists.
{
  const foreignUrlProps = JSON.stringify({
    chapters: [1, [[0, { chapter: [0, 'https://evil.example/manhwa/foreign/leer/1'] }]]]
  });
  const ctx = contextFor({ [SERIES_URL]: chapterListIsland(foreignUrlProps) });
  await assert.rejects(
    () => provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL }),
    /chapter entry is invalid/
  );
}

{
  const malformedNestedTuple = JSON.stringify({
    chapters: [1, [[0, { chapter: [2, '21'] }]]]
  });
  const ctx = contextFor({ [SERIES_URL]: chapterListIsland(malformedNestedTuple) });
  await assert.rejects(
    () => provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL }),
    /chapter value has an unsupported Astro type/
  );
}

{
  const legacyWithForeignLinks = [
    `<a href="${SERIES_PATH}/leer/2"><h4>Capítulo 2</h4></a>`,
    '<a href="https://evil.example/manhwa/foreign/leer/1"><h4>Capítulo 1</h4></a>',
    '<a href="/manga/otro-titulo/leer/1"><h4>Capítulo 1</h4></a>'
  ].join('');
  const ctx = contextFor({ [SERIES_URL]: legacyWithForeignLinks });
  const rows = await provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL });
  assert.deepEqual(rows.map(row => row.id), [SERIES_PATH + '/leer/2']);
}

// Hydration JSON nesting and chapter inventories have independent bounds.
{
  let deepValue = [0, 'leaf'];
  for (let i = 0; i < 30; i++) deepValue = [1, [deepValue]];
  const html = chapterListIsland(JSON.stringify({
    chapters: [1, [[0, { chapter: [0, 1] }]]],
    nested: deepValue
  }));
  const ctx = contextFor({ [SERIES_URL]: html });
  await assert.rejects(
    () => provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL }),
    /LectorXD Astro chapter props are invalid/
  );
}

{
  const tooManyChapters = Array.from({ length: 8100 }, (_, i) => [0, { chapter: [0, i] }]);
  const html = chapterListIsland(JSON.stringify({ chapters: [1, tooManyChapters] }));
  const ctx = contextFor({ [SERIES_URL]: html });
  await assert.rejects(
    () => provider.getChapters(ctx, { id: SERIES_PATH, url: SERIES_URL }),
    /LectorXD Astro chapter props are invalid/
  );
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
