// Batch 1 fresh focused checks on the candidate: rebuilt manga series/chapters as the main mock with
// the real TitlePageBar, Android phone/tablet omission of desktop window actions, desktop menu
// behaviour, wordmark fit, popups/focus restoration, and the cover-flow allocation geometry (A fit /
// B scroll) under the chosen header. Captures before/after images.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, HANDOFF, sha256 } from './harness.mjs';
const OUT = path.join(HANDOFF, 'candidate/evidence/batch1'); fs.mkdirSync(path.join(OUT, 'captures'), { recursive: true });
const M = 'candidate/review-batches/01-sources-manga/finished-mocks/';
const VP = [['phone-portrait', 412, 900, 1, 0], ['phone-landscape', 900, 412, 1, 0], ['small-portrait-1.3-insets', 354, 792, 1.3, 24], ['small-landscape-1.3-insets', 792, 354, 1.3, 24],
  ['tablet-portrait', 800, 1280, 1, 0], ['tablet-landscape', 1280, 800, 1, 0], ['desktop', 1280, 700, 1, 0], ['desktop-1.3', 1280, 700, 1.3, 0]];
const WIN = /^(minimi[sz]e|fullscreen|exit fullscreen|close|quit( colosseum)?…?|window and app)$/i;
const { server, origin } = await serve(); const { browser, version } = await launch();
const cases = [], external = [];
async function open(rel, w, h, q = {}) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h } }); const page = await ctx.newPage(); const log = { errors: [], failed: [] };
  await ctx.route('**/*', r => r.request().url().startsWith(origin) || r.request().url().startsWith('data:') ? r.continue() : (external.push(r.request().url()), r.abort()));
  page.on('pageerror', e => log.errors.push(e.message));
  page.on('response', r => { if (r.status() >= 400 && !/favicon|local-fonts\/seg/.test(r.url())) log.failed.push(r.status() + ' ' + r.url().replace(origin, '')); });
  await page.goto(origin + '/' + rel + '?' + new URLSearchParams({ capture: '1', ...q })); await page.evaluate(() => document.fonts.ready); await page.waitForTimeout(350);
  return { ctx, page, log };
}
const rect = (page, sel) => page.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { l: r.left, t: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height }; }, sel);
const overlap = (a, b) => a && b && a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
const shot = async (page, name) => { const p = path.join(OUT, 'captures', name + '.png'); await page.screenshot({ path: p }); return { file: 'captures/' + name + '.png', sha256: sha256(p) }; };
async function common(page, android) {
  return page.evaluate(([android, src]) => {
    const WIN = new RegExp(src, 'i'); const names = [...document.querySelectorAll('#app button,#app a,#app [tabindex]')].map(e => (e.getAttribute('aria-label') || e.textContent || '').trim());
    const imgs = [...document.querySelectorAll('#app img')].filter(i => i.complete && !i.naturalWidth).map(i => i.getAttribute('src'));
    return { windowActionNamesInDom: names.filter(n => WIN.test(n)), back: document.querySelector('#tbBack')?.textContent.trim() || null, search: !!document.querySelector('#tbSearch'),
      menuButton: !!document.querySelector('#tbMenuButton'), brokenImages: imgs, docOverflow: document.documentElement.scrollWidth > innerWidth + 1,
      appOverflowX: (() => { const a = document.getElementById('appScroll'); return a.scrollWidth > a.clientWidth + 1; })() };
  }, [android, WIN.source]);
}
for (const pageName of ['manga-series', 'manga-chapters']) for (const [vp, w, h, scale, inset] of VP) {
  const android = !vp.startsWith('desktop');
  const { ctx, page, log } = await open(M + '04-manga-series/' + pageName + '.html', w, h, { scale, top: inset, bottom: inset });
  const c = { page: pageName, vp, w, h, scale, inset, android, checks: {} }; const k = c.checks;
  try {
    const d = await common(page, android); c.dom = d;
    k.routeIsRebuilt = await page.$eval('#app', a => a.dataset.route === 'rebuilt');
    k.backNamesDestination = d.back === 'Tankoban'; k.searchPresent = d.search;
    k.noBrokenImages = !d.brokenImages.length; k.noHorizontalOverflow = !d.docOverflow && !d.appOverflowX;
    const back = await rect(page, '#tbBack'), right = await rect(page, '#tbRight'), mark = await rect(page, '#tbWordmark'), bar = await rect(page, '#titleBar'), poster = await rect(page, '.poster-frame');
    k.wordmarkClearOfControls = !overlap(mark, back) && !overlap(mark, right) && mark.l >= 0 && mark.r <= w;
    k.mastheadStartsBelowBar = poster.t >= bar.b - 1;
    c.wordmarkWrapped = await page.$eval('#titleBar', b => b.classList.contains('tb-wrap'));
    if (android) {
      k.noWindowActionsInDom = d.windowActionNamesInDom.length === 0 && !d.menuButton;
      await page.focus('#tbBack'); const tabs = [];
      for (let i = 0; i < 5; i++) { tabs.push(await page.evaluate(() => (document.activeElement.getAttribute('aria-label') || document.activeElement.textContent || '').trim().slice(0, 30))); await page.keyboard.press('Tab'); }
      c.tabs = tabs; k.tabPathHasNoWindowActions = !tabs.some(t => WIN.test(t)); k.tabReachesSearch = tabs.includes('Search');
    } else {
      k.menuButtonPresent = d.menuButton;
      await page.focus('#tbMenuButton'); await page.keyboard.press('Enter'); await page.waitForTimeout(80);
      const labels = await page.$$eval('#tbMenu button', es => es.map(e => e.textContent.trim()));
      k.menuKeepsActions = ['Minimize', 'Exit fullscreen', 'Quit Colosseum…'].every(l => labels.includes(l));
      k.menuFocusesMinimize = await page.evaluate(() => document.activeElement.dataset.tb === 'minimize');
      k.menuInsideViewport = (await rect(page, '#tbMenu')).r <= w;
      if (pageName === 'manga-series' && vp === 'desktop') c.captures = [await shot(page, 'after-series-desktop-menu-open')];
      await page.keyboard.press('Escape'); await page.waitForTimeout(80);
      k.escapeClosesAndRestoresFocus = !(await page.$('#tbMenu')) && await page.evaluate(() => document.activeElement.id === 'tbMenuButton');
      await page.click('#tbMenuButton'); await page.click('[data-tb="arm-quit"]'); await page.waitForTimeout(80);
      k.quitConfirmationFocusesKeepWatching = await page.evaluate(() => document.activeElement.dataset.tb === 'keep') && !!(await page.$('[data-tb="quit"]'));
      await page.click('[data-tb="keep"]'); await page.waitForTimeout(80);
      k.keepWatchingRestoresFocus = await page.evaluate(() => document.activeElement.id === 'tbMenuButton');
      await page.click('#tbMenuButton'); await page.click('[data-tb="minimize"]');
      k.minimizeClosesMenuAsLocalIntent = !(await page.$('#tbMenu'));
      await page.click('#tbMenuButton'); await page.setViewportSize({ width: 412, height: 900 }); await page.waitForTimeout(300);
      k.openMenuClearedOnResizeToPhone = !(await page.$('#tbMenu')) && !(await page.$('#tbMenuButton'));
      await page.setViewportSize({ width: w, height: h }); await page.waitForTimeout(250);
      k.menuButtonReturnsOnDesktop = !!(await page.$('#tbMenuButton'));
    }
    if (pageName === 'manga-series') {
      await page.click('#synopsisMore'); k.synopsisMoreExpands = await page.$eval('#synopsisWrap', e => e.classList.contains('expanded')); await page.click('#synopsisMore');
      const g = await page.evaluate(() => window.__flowGeometry()); c.flow = g;
      k.coverFloor190 = g.bookHeight >= 190; k.focusScale110 = Math.abs(g.focusedScale - 1.10) < 0.01;
      k.neighbourFalloff = g.neighbourScales.length >= 5 && g.neighbourScales.every(([n, s]) => s === [1.1, 1, 0.95, 0.9, 0.86][Math.min(4, Math.abs(n - 3))]);
      const centred = await page.evaluate(() => { const f = document.getElementById('volumeFlow').getBoundingClientRect(), a = document.querySelector('.volume-card.active').getBoundingClientRect(); return Math.abs((a.left + a.right) / 2 - (f.left + f.right) / 2); });
      k.focusedCentred = centred < 3;
      await page.focus('#volumeFlow'); await page.keyboard.press('ArrowRight'); await page.waitForTimeout(250);
      k.keyboardMovesFocus = await page.evaluate(() => document.querySelector('.volume-card.active').dataset.vol === '4');
    } else {
      await page.click('#languageSelector'); await page.waitForTimeout(80);
      const pop = await rect(page, '#menuPopup'); k.languagePopupInsideApp = pop && pop.l >= 0 && pop.r <= w && pop.t >= 0 && pop.b <= h;
      await page.keyboard.press('Escape'); k.languageEscapeRestoresFocus = await page.evaluate(() => document.activeElement.id === 'languageSelector');
      await page.click('#pageSelector'); await page.waitForTimeout(80); await page.keyboard.press('Escape');
      k.pageEscapeRestoresFocus = await page.evaluate(() => document.activeElement.id === 'pageSelector');
      await page.fill('#goTo', '57'); await page.focus('#goTo'); await page.keyboard.press('Enter'); await page.waitForTimeout(100);
      k.goToChapterLands = await page.evaluate(() => document.activeElement.dataset.chapter === '57' && /51–60/.test(document.getElementById('chapterRange').textContent));
    }
    if (['phone-portrait', 'phone-landscape', 'desktop', 'small-landscape-1.3-insets', 'tablet-portrait'].includes(vp)) { await page.evaluate(() => { const a = document.getElementById('appScroll'); a.scrollTop = 0; }); (c.captures ||= []).push(await shot(page, `after-${pageName}-${vp}`)); }
  } catch (e) { log.errors.push('harness: ' + String(e.message).split('\n')[0]); }
  c.errors = log.errors; c.failed = log.failed; c.pass = !log.errors.length && !log.failed.length && Object.values(k).every(Boolean);
  cases.push(c); await ctx.close();
}
// Older composition stays available as comparison; Android must not expose window invokers there either.
for (const [vp, w, h] of [['phone-portrait', 412, 900], ['tablet-landscape', 1280, 800], ['desktop', 1280, 700]]) {
  const { ctx, page, log } = await open(M + '04-manga-series/manga-series.html', w, h, { route: 'reading-room' });
  const n = await page.$$eval('#chromeWindow button', b => b.length);
  const c = { page: 'manga-series?route=reading-room', vp, w, h, checks: vp === 'desktop' ? { desktopWindowButtonsRetained: n === 3 } : { noWindowButtonsInDom: n === 0 } };
  if (vp !== 'tablet-landscape') c.captures = [await shot(page, `after-reading-room-comparison-${vp}`)];
  c.errors = log.errors; c.failed = log.failed; c.pass = !log.errors.length && Object.values(c.checks).every(Boolean); cases.push(c); await ctx.close();
}
// Cover-flow allocation A (fit) vs B (scroll) under the chosen header.
const flow = [];
for (const [file, mode] of [['volume-flow-original-fit.html', 'A-fit'], ['volume-flow-minimal-alternative.html', 'B-scroll']]) for (const [vp, w, h, scale, inset] of [...VP.slice(0, 4), ['small-portrait', 354, 792, 1, 0], ['small-landscape', 792, 354, 1, 0], VP[4], VP[5], VP[6]]) {
  const { ctx, page, log } = await open(M + '05-volume-flow/' + file, w, h, { scale, top: inset, bottom: inset });
  const g = await page.evaluate(() => window.__flowGeometry());
  const d = await common(page, !vp.startsWith('desktop'));
  const r = { mode, vp, w, h, scale, inset, ...g, errors: log.errors, failed: log.failed, noHorizontalOverflow: !d.docOverflow && !d.appOverflowX };
  r.viable = g.bookHeight >= 190 && g.focusedCardFullyInsideFlow && (mode === 'B-scroll' || g.focusedCardFullyOnFirstScreen);
  if (['phone-portrait', 'phone-landscape', 'small-landscape', 'desktop'].includes(vp)) r.capture = await shot(page, `flow-${mode}-${vp}`);
  // Below the pinned TitlePageBar: does focused cover + caption + action row fit at once?
  r.pinnedBarBottom = await page.evaluate(() => Math.round(document.getElementById('titleBar').getBoundingClientRect().bottom - document.getElementById('app').getBoundingClientRect().top));
  r.libraryNeedsHeight = await page.evaluate(() => { const st = document.querySelector('.volume-stage').getBoundingClientRect(), a = document.querySelector('.volume-action').getBoundingClientRect(); return Math.round(a.bottom - st.top); });
  r.libraryFitsBelowPinnedBar = r.libraryNeedsHeight <= r.appHeight - r.pinnedBarBottom;
  if (mode === 'B-scroll' && ['phone-portrait', 'phone-landscape', 'small-landscape'].includes(vp)) { await page.evaluate(() => { const sc = document.getElementById('appScroll'), st = document.querySelector('.volume-stage').getBoundingClientRect(), bar = document.getElementById('titleBar').getBoundingClientRect(); sc.scrollTop += st.top - bar.bottom; }); await page.waitForTimeout(200); r.captureScrolled = await shot(page, `flow-${mode}-${vp}-scrolled-to-flow`); }
  flow.push(r); await ctx.close();
}
await browser.close(); server.close();
const out = { tool: 'candidate/verify/focused-b1.mjs', chromium: version, at: new Date().toISOString(),
  candidate: { appJs: sha256(path.join(HANDOFF, M, '04-manga-series/app.js')), styleCss: sha256(path.join(HANDOFF, M, '04-manga-series/style.css')) },
  cases: cases.length, passed: cases.filter(c => c.pass).length, external, results: cases, flowAllocation: flow };
fs.writeFileSync(path.join(OUT, 'focused-candidate.json'), JSON.stringify(out, null, 1));
console.log('cases', out.passed + '/' + out.cases, 'external', external.length);
for (const c of cases.filter(c => !c.pass)) console.log(' FAIL', c.page, c.vp, JSON.stringify(Object.fromEntries(Object.entries(c.checks).filter(([, v]) => !v))), c.errors.join('|').slice(0, 200), c.failed.slice(0, 2));
for (const f of flow) console.log(f.mode.padEnd(8), f.vp.padEnd(28), 'mast', f.mastheadBottom, 'flowVP', f.flowViewportHeight, 'raw', f.unclampedBookHeight, 'book', f.bookHeight, 'drawn', f.focusedDrawnCoverHeight, 'scale', f.focusedScale, 'inFlow', f.focusedCardFullyInsideFlow, '1stScreen', f.focusedCardFullyOnFirstScreen, 'scroll', f.scrollToSeeFocusedAndActions, 'viable', f.viable, 'belowBar', f.libraryNeedsHeight + '/' + (f.appHeight - f.pinnedBarBottom), f.errors.length ? f.errors : '');
