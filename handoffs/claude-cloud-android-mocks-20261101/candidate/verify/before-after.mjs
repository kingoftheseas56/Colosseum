// Before (frozen baseline) / after (candidate) captures for each corrected defect, same URL and viewport.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, openPage, familyQuery, SIZES, HANDOFF, sha256 } from './harness.mjs';
const P = [
  ['b1-manga-series-portrait', 'review-batches/01-sources-manga/finished-mocks/04-manga-series/manga-series.html', '04-manga-series', 'P', {}, 'Batch 1 main series mock: baseline default (older reading room) vs candidate default (rebuilt + TitlePageBar)'],
  ['b1-manga-series-landscape', 'review-batches/01-sources-manga/finished-mocks/04-manga-series/manga-series.html', '04-manga-series', 'L', {}, 'Same, phone landscape'],
  ['b1-manga-chapters-portrait', 'review-batches/01-sources-manga/finished-mocks/04-manga-series/manga-chapters.html', '04-manga-series', 'P', {}, 'Batch 1 chapters: baseline default vs candidate rebuilt'],
  ['b1-manga-series-desktop', 'review-batches/01-sources-manga/finished-mocks/04-manga-series/manga-series.html', '04-manga-series', 'D', {}, 'Desktop: window controls retained (baseline icons; candidate one system menu)'],
  ['b1-rebuilt-preview-vs-final-portrait', 'review-batches/01-sources-manga/finished-mocks/04-manga-series/manga-series.html', '04-manga-series', 'P', { route: 'rebuilt' }, 'Hemanth-viewed rebuilt preview (older chrome) vs final rebuilt with TitlePageBar'],
  ['b2-film-rebuilt-phone', 'review-batches/02-detail-pages/finished-mocks/17-theatre-details/film-detail.html', '17-theatre-details', 'SP', { renderer: 'rebuilt' }, 'Batch 2 Theatre rebuilt on Android phone: Window menu invoker removed (Sol SOL-B2-001)'],
  ['b3-genre-index-tablet', 'review-batches/03-browsing-libraries/finished-mocks/20-search-browse/genre-index.html', '20-search-browse', 'TL', {}, 'Batch 3 genre index on Android tablet: window controls removed'],
  ['b4-account-profile-tablet', 'review-batches/04-accounts-configuration/finished-mocks/12-account-forms/account-profile.html', '12-account-forms', 'TL', {}, 'Batch 4 account profile on Android tablet: window controls removed'],
  ['b4-account-flyout-tablet', 'review-batches/04-accounts-configuration/finished-mocks/11-account-entry/account-flyout.html', '11-account-entry', 'TL', {}, 'Batch 4 flyout TopBar underlay on Android tablet: window controls removed'],
  ['b5-wallpaper-search-portrait', 'review-batches/05-tracker-utilities/finished-mocks/wallpaper-search.html', '05-tracker-utilities', 'P', {}, 'Batch 5 wallpaper search: omitted KDE art restored byte-exact (requires verify/fetch-kde-wallpapers.py)'],
];
const dir = path.join(HANDOFF, 'candidate/evidence/before-after'); fs.mkdirSync(dir, { recursive: true });
const { server, origin } = await serve(); const { browser, version } = await launch(); const rows = [];
for (const [id, rel, fam, key, q, note] of P) { const row = { id, page: rel, size: key, viewport: SIZES[key], note };
  for (const tree of ['baseline', 'candidate']) { const [w, h] = SIZES[key]; const { ctx, page } = await openPage(browser, origin, tree + '/' + rel, { w, h, query: familyQuery(fam, key, id.includes('preview') && tree === 'candidate' ? {} : q) }); await page.waitForTimeout(300);
    const f = path.join(dir, `${id}-${tree === 'baseline' ? 'before' : 'after'}.png`); await page.screenshot({ path: f }); row[tree === 'baseline' ? 'before' : 'after'] = { file: path.basename(f), sha256: sha256(f) }; await ctx.close(); }
  rows.push(row); }
await browser.close(); server.close();
fs.writeFileSync(path.join(dir, 'pairs.json'), JSON.stringify({ chromium: version, at: new Date().toISOString(), pairs: rows }, null, 1));
console.log(rows.length, 'pairs');
