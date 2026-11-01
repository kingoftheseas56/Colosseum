// Batch 2 fresh focused checks on the integrated candidate (port of Sol's verify.mjs to
// Playwright, extended to 412x900/900x412). tree arg: baseline|candidate.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, HANDOFF } from './harness.mjs';
const tree = process.argv[2] || 'candidate';
const WIN = ['system-menu', 'minimize', 'fullscreen', 'quit', 'arm-quit', 'keep-watching'];
const HOSTS = [['phone-portrait', 354, 792], ['phone-landscape', 792, 354], ['phone-portrait-412', 412, 900], ['phone-landscape-900', 900, 412], ['tablet', 1280, 800], ['tablet-portrait', 800, 1280], ['desktop', 1280, 700]];
const { server, origin } = await serve(); const { browser, version } = await launch();
const results = [], blocked = [];
for (const kind of ['film', 'series']) for (const renderer of ['legacy', 'rebuilt']) for (const [host, w, h] of HOSTS) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h } }); const page = await ctx.newPage();
  const errors = [];
  await ctx.route('**/*', r => r.request().url().startsWith(origin) || r.request().url().startsWith('data:') ? r.continue() : (blocked.push(r.request().url()), r.abort()));
  page.on('pageerror', e => errors.push(e.message));
  page.on('response', r => { if (r.status() >= 400 && !/favicon|local-fonts\/seg/.test(r.url())) errors.push(r.status() + ' ' + r.url()); });
  const res = { kind, renderer, host, w, h, checks: {} };
  try {
    await page.goto(`${origin}/${tree}/review-batches/02-detail-pages/finished-mocks/17-theatre-details/${kind}-detail.html?capture=1&renderer=${renderer}&scale=1.3&top=24&bottom=24`);
    await page.evaluate(() => document.fonts.ready); await page.waitForTimeout(200);
    const dom = await page.$eval('#app', (app, WIN) => ({ cls: app.className,
      win: [...app.querySelectorAll('[data-action]')].filter(e => WIN.includes(e.dataset.action)).map(e => e.dataset.action),
      back: !!app.querySelector('[data-action="back"]'), search: !!app.querySelector('[data-action="search"]') }), WIN);
    res.dom = dom;
    if (host !== 'desktop') {
      res.checks.noWindowActionsInDom = dom.win.length === 0;
      res.checks.backPresent = dom.back; if (renderer === 'rebuilt') res.checks.searchPresent = dom.search;
      await page.focus('#chrome [data-action="back"]'); const tabs = [];
      for (let i = 0; i < 6; i++) { tabs.push(await page.evaluate(() => document.activeElement?.dataset?.action || document.activeElement?.tagName)); await page.keyboard.press('Tab'); }
      res.tabs = tabs; res.checks.tabTraversalAvoidsWindowActions = !tabs.some(t => WIN.includes(t));
      res.checks.noHorizontalOverflow = await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1);
    } else if (renderer === 'rebuilt') {
      await page.focus('[data-action="system-menu"]'); await page.keyboard.press('Enter');
      const labels = await page.$$eval('.system-menu button', es => es.map(e => e.innerText.trim()));
      res.labels = labels; res.checks.menuKeepsActions = ['Minimize', 'Exit fullscreen', 'Quit Colosseum…'].every(l => labels.includes(l));
      await page.click('[data-action="minimize"]'); await page.click('[data-action="system-menu"]'); await page.click('[data-action="fullscreen"]');
      await page.click('[data-action="system-menu"]'); await page.click('[data-action="arm-quit"]');
      res.checks.quitConfirmation = !!(await page.$('[data-action="quit"]')); await page.click('[data-action="keep-watching"]');
      await page.click('[data-action="system-menu"]'); await page.setViewportSize({ width: 354, height: 792 }); await page.waitForTimeout(200);
      const remaining = await page.$$eval('#chrome [data-action]', es => es.map(e => e.dataset.action));
      res.checks.openMenuClearedOnAndroidResize = !remaining.some(a => WIN.includes(a));
    } else {
      for (const a of ['minimize', 'fullscreen', 'quit']) await page.click(`[data-action="${a}"]`);
      res.checks.desktopLegacyWindowActionsRetained = ['minimize', 'fullscreen', 'quit'].every(a => dom.win.includes(a));
    }
  } catch (e) { errors.push(String(e.message).split('\n')[0]); }
  res.errors = errors; res.pass = !errors.length && Object.values(res.checks).every(Boolean);
  results.push(res); await ctx.close();
}
await browser.close(); server.close();
const out = { tree, chromium: version, at: new Date().toISOString(), cases: results.length, passed: results.filter(r => r.pass).length, blocked, results };
fs.mkdirSync(path.join(HANDOFF, 'candidate/evidence/batch2'), { recursive: true });
fs.writeFileSync(path.join(HANDOFF, `candidate/evidence/batch2/focused-${tree}.json`), JSON.stringify(out, null, 1));
console.log(tree, out.passed + '/' + out.cases, 'blocked', blocked.length);
for (const r of results.filter(r => !r.pass)) console.log(' FAIL', r.kind, r.renderer, r.host, JSON.stringify(r.checks), r.errors.join('|').slice(0, 200));
