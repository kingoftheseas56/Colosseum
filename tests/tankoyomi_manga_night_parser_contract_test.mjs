import fs from 'node:fs';

const path = 'extensions/tankoyomi/languages/pt/manga-night.js';
const source = fs.readFileSync(path, 'utf8');
const chaptersSource = source.slice(source.indexOf('function getChapters'), source.indexOf('function getPages'));
let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`);
  if (!ok) failures++;
};

check(chaptersSource.includes('const hrefRe = /href='),
  'Manga Night scans chapter hrefs directly');
check(chaptersSource.includes("body.indexOf('</a>', m.index)"),
  'chapter parsing bounds work to the current anchor');
check(chaptersSource.includes("segment.indexOf('<p')"),
  'chapter label parsing uses the local anchor segment');
check(!chaptersSource.includes('([\\s\\S]*?)<\\/a>'),
  'chapter parser avoids the expensive whole-anchor capture regex');

if (failures) process.exit(1);
console.log('\nPASS — Manga Night chapter parsing is bounded for large series pages');
