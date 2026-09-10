import fs from 'node:fs';
import vm from 'node:vm';

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

// Manga Night resolves page images through the MangaDex at-home CDN, which
// rejects image requests that carry no Referer. Every page row, including the
// at-home CDN rows, must carry one so the production reader can load them.
{
  const sandbox2 = { module: { exports: {} }, exports: {}, globalThis: {} };
  vm.runInNewContext(source, sandbox2, { filename: path });
  const liveProvider = sandbox2.module.exports;
  const chapterUrl = 'https://www.manganight.com.br/manga/city-hunter/capitulo/623105';
  const readerHtml = [
    '<div class="reader">',
    '<img src="/api/reader/image/mdex/11111111-1111-1111-1111-111111111111/page1.jpg">',
    '<img src="/api/reader/image/mdex/11111111-1111-1111-1111-111111111111/page2.jpg">',
    '</div>'
  ].join('');
  const atHome = {
    baseUrl: 'https://cmdxd98sb0x3yprd.mangadex.network',
    chapter: { hash: 'hash0000', data: ['page1.jpg', 'page2.jpg'], dataSaver: [] }
  };
  const ctx = {
    fetchText: url => url === chapterUrl ? Promise.resolve(readerHtml)
      : Promise.reject(new Error('unexpected fetchText ' + url)),
    fetchJson: url => url === 'https://api.mangadex.org/at-home/server/11111111-1111-1111-1111-111111111111'
      ? Promise.resolve(atHome) : Promise.reject(new Error('unexpected fetchJson ' + url))
  };
  const pages = await liveProvider.getPages(ctx, { id: '623105', seriesId: 'city-hunter', url: chapterUrl });
  check(pages.length === 2, 'at-home pages resolve from the reader payload');
  check(pages.every(p => p.url.startsWith('https://cmdxd98sb0x3yprd.mangadex.network/data/')),
    'at-home page URLs target the CDN data path');
  check(pages.every(p => p.referer === 'https://mangadex.org/'),
    'every at-home page row carries the MangaDex origin referer the CDN accepts');
}

if (failures) process.exit(1);
console.log('\nPASS — Manga Night chapter parsing is bounded for large series pages');
