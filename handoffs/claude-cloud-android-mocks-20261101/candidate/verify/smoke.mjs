// Fresh all-pages smoke: 53 required pages + 3 alternate routes, each at the 7 brief viewports
// (412x900, 900x412, 354x792, 792x354, 1280x700, 800x1280, 1280x800) plus 354x792 and 792x354 at
// 1.3 text with 24px top/bottom insets, using each family's own preset convention (harness.familyQuery).
// Records script errors, failed local requests (inherited omitted Segoe fonts counted separately),
// external requests, broken images, document overflow, whether the frame really rendered at the
// requested size, and reachable desktop window actions on Android-classified viewports.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, openPage, familyQuery, frameFacts, SIZES, HANDOFF } from './harness.mjs';
const tree = process.argv[2] || 'candidate';
const only = process.argv[3] ? new RegExp(process.argv[3]) : null;
const pages = JSON.parse(fs.readFileSync(path.join(HANDOFF, 'candidate/verify/pages.json')));
const alt = [{ batch: '02-detail-pages', family: '17-theatre-details', page: 'film-detail', variant: 'renderer=rebuilt', path: 'review-batches/02-detail-pages/finished-mocks/17-theatre-details/film-detail.html', q: { renderer: 'rebuilt' } },
  { batch: '02-detail-pages', family: '17-theatre-details', page: 'series-detail', variant: 'renderer=rebuilt', path: 'review-batches/02-detail-pages/finished-mocks/17-theatre-details/series-detail.html', q: { renderer: 'rebuilt' } },
  { batch: '01-sources-manga', family: '04-manga-series', page: 'manga-series', variant: tree === 'candidate' ? 'route=reading-room' : 'route=rebuilt', path: 'review-batches/01-sources-manga/finished-mocks/04-manga-series/manga-series.html', q: tree === 'candidate' ? { route: 'reading-room' } : { route: 'rebuilt' } }];
const STRESS = { scale: 1.3, font: 1.3, fontScale: 1.3, top: 24, bottom: 24, insetTop: 24, insetBottom: 24, inset: 24 };
const CASES = [...Object.keys(SIZES).map(k => [k, false]), ['SP', true], ['SL', true]];
const { server, origin } = await serve(); const { browser, version } = await launch();
const results = [];
for (const pg of [...pages, ...alt].filter(p => !only || only.test(p.path))) for (const [key, stress] of CASES) {
  const [w, h] = SIZES[key], android = key !== 'D';
  const { ctx, page, log } = await openPage(browser, origin, tree + '/' + pg.path, { w, h, query: familyQuery(pg.family, key, { ...(pg.q || {}), ...(stress ? STRESS : {}) }) });
  const f = await frameFacts(page, w, h);
  const r = await page.evaluate(([android]) => {
    const rx = /^(minimi[sz]e( window)?|fullscreen|toggle fullscreen|exit fullscreen|enter fullscreen|close( window| colosseum)?|quit( colosseum)?…?|window and app)$/i;
    const lab = '.lab,.studio-controls,#controls,.labfields,.review-controls,.controls-panel,[data-lab],.mock-lab,.workbench,aside.studio,.studio,.reviewer,.review-panel,.mock-controls,#lab,#review,#reviewer';
    const isWin = e => { const n = (e.getAttribute('aria-label') || e.getAttribute('title') || e.textContent || '').trim(); if (!rx.test(n)) return false; if (!/^close/i.test(n)) return true; return /window|desktop-sys|sys-control/i.test(e.className + ' ' + (e.dataset.act || '') + ' ' + (e.dataset.action || '') + ' ' + (e.parentElement?.className || '')); };
    const reach = []; if (android) for (const e of document.querySelectorAll('button,[role=button],a,[tabindex]')) { if (e.closest(lab) || !isWin(e)) continue; const b = e.getBoundingClientRect(), s = getComputedStyle(e); if ((b.width > 0 && b.height > 0 && s.visibility !== 'hidden' && s.display !== 'none') || e.tabIndex >= 0 && e.getClientRects().length) reach.push((e.getAttribute('aria-label') || e.textContent).trim().slice(0, 24)); }
    return { reach, broken: [...document.images].filter(i => i.complete && i.currentSrc && !i.naturalWidth).map(i => i.getAttribute('src')), overflow: document.documentElement.scrollWidth > innerWidth + 1 };
  }, [android]);
  results.push({ batch: pg.batch, page: pg.page, variant: pg.variant || '', size: key + (stress ? '-1.3x-24px' : ''), w, h, android, errors: log.errors, failed: log.failed, omittedSegoe: log.intentional.length,
    external: log.external, brokenImages: r.broken, horizontalOverflow: r.overflow, androidWindowActionsReachable: r.reach, ...f,
    frameAtRequestedSize: f.elementAtRequestedSize || f.contentSpan >= 0.55 });
  await ctx.close();
}
await browser.close(); server.close();
const bad = k => results.filter(k).length;
const summary = { tree, chromium: version, at: new Date().toISOString(), routes: new Set(results.map(r => r.page + r.variant)).size, cases: results.length,
  scriptErrors: bad(r => r.errors.length), failedLocalRequests: bad(r => r.failed.length), externalRequests: results.reduce((a, r) => a + r.external.length, 0),
  brokenImages: bad(r => r.brokenImages.length), horizontalOverflow: bad(r => r.horizontalOverflow), frameNotAtRequestedSize: bad(r => !r.frameAtRequestedSize),
  androidWindowActionsReachable: bad(r => r.androidWindowActionsReachable.length) };
const out = path.join(HANDOFF, `candidate/evidence/smoke-${tree}.json`);
if (!only) fs.writeFileSync(out, JSON.stringify({ summary, results }, null, 1));
console.log(JSON.stringify(summary));
for (const r of results) { const p = []; if (r.errors.length) p.push('ERR ' + r.errors[0].slice(0, 80)); if (r.failed.length) p.push('FAIL ' + r.failed[0]); if (r.brokenImages.length) p.push('BROKEN ' + r.brokenImages.length); if (r.horizontalOverflow) p.push('OVERFLOW'); if (!r.frameAtRequestedSize) p.push('FRAME span=' + r.contentSpan); if (r.androidWindowActionsReachable.length) p.push('WIN ' + r.androidWindowActionsReachable.join(',')); if (p.length) console.log(' ', r.batch.slice(0, 2), r.page, r.variant, r.size, p.join(' | ')); }
