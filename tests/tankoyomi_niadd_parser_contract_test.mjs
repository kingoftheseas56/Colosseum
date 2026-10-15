import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

// Run from the repository root, matching the other tankoyomi Node contracts.
const require = createRequire(import.meta.url);
const provider = require(path.resolve('extensions/tankoyomi/languages/es/niadd.js'));
const fixture = fs.readFileSync('tests/fixtures/tankoyomi/niadd-one-piece-chapters.html', 'utf8');

const CANONICAL = 'https://es.niadd.com/manga/One_Piece.html';
const LEGACY = 'https://es.niadd.com/manga/One_Piece/chapters.html';
const EMPTY = '<html><body>capitulos no disponibles</body></html>';

let failures = 0;
const check = (ok, msg) => { console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`); if (!ok) failures++; };

function makeCtx(pages) {
  const calls = [];
  return {
    calls,
    fetchText(url) {
      calls.push(String(url));
      if (!(url in pages)) return Promise.reject(new Error('unexpected fetch ' + url));
      return Promise.resolve(pages[url]);
    }
  };
}

// The observed 2026-09-07 failure: search resolves the canonical series page,
// the adapter rewrites it to the historical /chapters.html form before parsing,
// and the chapter list comes back empty. The canonical page itself has the rows.
{
  const ctx = makeCtx({ [CANONICAL]: fixture, [LEGACY]: EMPTY });
  const series = { id: 'One_Piece', title: 'One Piece', url: CANONICAL, source: 'niadd', language: 'es' };
  const rows = await provider.getChapters(ctx, series);
  check(rows.length === 5, `canonical series page yields the chapter rows (got ${rows.length})`);
  check(ctx.calls.length === 1 && ctx.calls[0] === CANONICAL,
    'the canonical series URL is fetched first, without the /chapters.html rewrite');
  const latest = rows.find(r => r.number === 1192);
  check(!!latest && latest.id === '3531344', 'chapter 1192 carries provider id 3531344');
  check(rows.every(r => r.source === 'niadd' && r.language === 'es'), 'rows keep the Spanish source/language fields');
  check(rows.every((r, i) => i === 0 || rows[i - 1].number <= r.number), 'rows sort in ascending numeric order');
  check(!rows.some(r => r.url.includes('/manga/')), 'non-chapter series links stay out of the rows');
  const relative = rows.find(r => r.number === 1189);
  const protocolRelative = rows.find(r => r.number === 1188);
  check(!!relative && relative.url === 'https://es.niadd.com/chapter/One_Piece_Capitulo_1189/3511189/',
    'relative chapter hrefs normalize to absolute es.niadd.com URLs');
  check(!!protocolRelative && protocolRelative.url === 'https://es.niadd.com/chapter/One_Piece_Capitulo_1188/3511188/',
    'protocol-relative chapter hrefs normalize to absolute https URLs');
  check(rows.filter(r => r.number === 1190).length === 1, 'duplicate chapter hrefs collapse to one row');
}

// Bounded compatibility fallback: the historical form is tried exactly once,
// and only when the canonical page yields zero rows.
{
  const ctx = makeCtx({ [CANONICAL]: EMPTY, [LEGACY]: fixture });
  const rows = await provider.getChapters(ctx, { url: CANONICAL });
  check(rows.length === 5 && ctx.calls.length === 2 && ctx.calls[1] === LEGACY,
    'a zero-row canonical page falls back to /chapters.html exactly once');
}

// Honest empty result with no unbounded retrying.
{
  const ctx = makeCtx({ [CANONICAL]: EMPTY, [LEGACY]: EMPTY });
  const rows = await provider.getChapters(ctx, { url: CANONICAL });
  check(rows.length === 0 && ctx.calls.length === 2,
    'both pages empty returns zero rows without further fetches');
}

if (failures) process.exit(1);
console.log('\nPASS — NiAdd parses the canonical series page with a bounded compatibility fallback');
