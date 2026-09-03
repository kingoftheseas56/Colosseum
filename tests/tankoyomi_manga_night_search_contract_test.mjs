import fs from 'node:fs';
import vm from 'node:vm';

const path = 'extensions/tankoyomi/languages/pt/manga-night.js';
const src = fs.readFileSync(path, 'utf8');
let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`);
  if (!ok) failures++;
};

check(src.includes('function directSlug'), 'Manga Night has deterministic title slugging');
check(!src.includes("fetchText(`${BASE}/sitemap.xml`)"),
  'Manga Night search does not download the sitemap on the hot path');

const sandbox = { module: { exports: {} }, exports: {}, globalThis: {} };
vm.runInNewContext(src, sandbox, { filename: path });
const provider = sandbox.module.exports;
const rows = provider.searchSeries({}, 'City Hunter');
check(Array.isArray(rows) && rows.length === 1, 'search returns one direct probe row');
check(rows[0].id === 'city-hunter', 'direct probe slug matches title');
check(rows[0].url === 'https://www.manganight.com.br/manga/city-hunter',
  'direct probe targets the Manga Night series URL');

if (failures) process.exit(1);
console.log('\nPASS — Manga Night search avoids the slow sitemap request');