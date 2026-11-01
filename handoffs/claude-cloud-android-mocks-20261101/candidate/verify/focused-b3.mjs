// Batch 3 fresh focused checks: Playwright port of Sol's test-search-correction.cjs (12 checks x
// 3 lanes x 2 orientations) plus the exact desktop control-identity checks
// (test-focus-identity.cjs) on ONE integrated candidate revision. tree: baseline|candidate.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, HANDOFF, sha256 } from './harness.mjs';
const tree = process.argv[2] || 'candidate';
const rel = `${tree}/review-batches/03-browsing-libraries/finished-mocks/20-search-browse/search.html`;
const wait = ms => new Promise(r => setTimeout(r, ms));
const { server, origin } = await serve(); const { browser, version } = await launch();
const checks = [], errors = [], blocked = [];
async function open(w, h, preset, lane) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h } }); const page = await ctx.newPage();
  await ctx.route('**/*', r => r.request().url().startsWith(origin) || /^(data|blob|about):/.test(r.request().url()) ? r.continue() : (blocked.push(r.request().url()), r.abort()));
  page.on('pageerror', e => errors.push(e.message));
  await page.goto(`${origin}/${rel}?` + new URLSearchParams({ capture: '1', preset, lane, state: 'empty' }));
  await page.evaluate(() => { window.receivedSearchIntents = []; document.addEventListener('click', e => { const el = e.target.closest('[data-action="item-open"]'); if (el) window.receivedSearchIntents.push(el.dataset.value); }, true); });
  return { ctx, page };
}
for (const [lane, query] of [['Biblio', 'Sherlock'], ['Theatre', 'Empire'], ['Tankoban', 'Arlong']]) for (const [w, h, preset] of [[412, 900, 'portrait'], [900, 412, 'landscape']]) {
  const { ctx, page } = await open(w, h, preset, lane);
  const snap = () => page.$eval('#searchInput', e => ({ value: e.value, focus: document.activeElement === e, start: e.selectionStart, end: e.selectionEnd, activeAction: document.activeElement.dataset.action || null }));
  const replace = async t => { await page.focus('#searchInput'); await page.keyboard.press('Control+A'); await page.keyboard.type(t); };
  const check = async (name, fn) => { try { const d = await fn(); checks.push({ lane, w, h, name, pass: !!d.pass, d }); } catch (e) { checks.push({ lane, w, h, name, pass: false, error: String(e.message).split('\n')[0] }); } };
  await check('pause after first character', async () => { await page.focus('#searchInput'); await page.keyboard.type(query[0]); await wait(350); const d = await snap(); return { pass: d.value === query[0] && d.focus && d.start === 1 && d.end === 1, ...d }; });
  await check('slow remaining characters', async () => { await page.keyboard.type(query.slice(1), { delay: 260 }); await wait(350); const d = await snap(); return { pass: d.value === query && d.focus && d.start === query.length, ...d }; });
  await check('fast replacement', async () => { await replace(query); await wait(350); const d = await snap(); return { pass: d.value === query && d.focus && d.start === query.length, ...d }; });
  await check('Enter dispatches existing top-result intent, field kept (fixture-only focus)', async () => { await page.keyboard.press('Enter'); await wait(350); const d = await snap(), intents = await page.evaluate(() => receivedSearchIntents); return { pass: d.value === query && d.focus && d.start === query.length && intents.length === 1, intents, ...d }; });
  await check('typing continues after Enter; Backspace', async () => { await page.keyboard.type('x'); await wait(350); const a = await snap(); await page.keyboard.press('Backspace'); await wait(350); const d = await snap(); return { pass: a.value === query + 'x' && d.value === query && d.focus && d.start === query.length, ...d }; });
  await check('mid-query caret edit survives debounce', async () => { await page.keyboard.press('Home'); await page.keyboard.press('ArrowRight'); await page.keyboard.press('ArrowRight'); await page.keyboard.type('-'); await wait(350); const i = await snap(); await page.keyboard.press('Backspace'); await wait(350); const d = await snap(); return { pass: i.value === query.slice(0, 2) + '-' + query.slice(2) && i.start === 3 && d.value === query && d.start === 2 && d.focus, ...d }; });
  await check('selection range survives debounce', async () => { await page.keyboard.press('End'); await page.keyboard.type('!'); await page.keyboard.press('Shift+ArrowLeft'); await wait(350); const r = await snap(); await page.keyboard.press('Backspace'); await wait(350); const d = await snap(); return { pass: r.start === query.length && r.end === query.length + 1 && d.value === query && d.focus, ...d }; });
  await check('pending debounce does not steal deliberate result focus', async () => { await page.keyboard.type(' '); await page.click('.top-match [data-action="item-open"]'); await wait(350); const d = await snap(); return { pass: !d.focus && d.activeAction === 'item-open', ...d }; });
  await check('pending debounce keeps deliberate Tab focus', async () => { await page.focus('#searchInput'); await page.keyboard.type(' '); await page.keyboard.press('Tab'); await wait(350); const d = await snap(); return { pass: !d.focus && d.activeAction === 'search-clear', ...d }; });
  await check('Clear cancels pending work and returns focus', async () => { await page.focus('#searchInput'); await page.keyboard.type('pending'); await page.click('[data-action="search-clear"]'); await wait(350); const d = await snap(); return { pass: d.value === '' && d.focus && d.start === 0, ...d }; });
  await check('Backspace to one character then empty', async () => { await replace('AB'); await wait(350); await page.keyboard.press('Backspace'); await wait(350); const one = await snap(); await page.keyboard.press('Backspace'); await wait(350); const d = await snap(); return { pass: one.value === 'A' && one.focus && d.value === '' && d.focus, ...d }; });
  await check('no-match Enter fabricates no action', async () => { await replace('zzzzzz'); await wait(350); const c = await page.evaluate(() => receivedSearchIntents.length); await page.keyboard.press('Enter'); await wait(300); const d = await snap(); return { pass: d.value === 'zzzzzz' && d.focus && (await page.evaluate(() => receivedSearchIntents.length)) === c, ...d }; });
  if (lane === 'Biblio') { await replace(query); await wait(350); fs.mkdirSync(path.join(HANDOFF, 'candidate/evidence/batch3'), { recursive: true }); await page.screenshot({ path: path.join(HANDOFF, `candidate/evidence/batch3/search-${tree}-${w}x${h}.png`) }); }
  await ctx.close();
}
// Exact desktop control identity (REM-F01)
{
  const { ctx, page } = await open(1280, 700, 'desktop', 'Biblio');
  for (const label of ['Minimize', 'Fullscreen', 'Close']) for (const via of ['pointer', 'keyboard']) {
    try {
      await page.focus('#searchInput'); await page.keyboard.type('S');
      const sel = `.sys-controls [aria-label="${label}"]`;
      if (via === 'pointer') await page.click(sel); else await page.focus(sel);
      await wait(400);
      const a = await page.evaluate(() => document.activeElement.getAttribute('aria-label'));
      checks.push({ lane: 'Biblio', w: 1280, h: 700, name: `desktop ${label} ${via} focus identity after debounce`, pass: a === label, d: { active: a } });
    } catch (e) { checks.push({ lane: 'Biblio', w: 1280, h: 700, name: `desktop ${label} ${via}`, pass: false, error: String(e.message).split('\n')[0] }); }
  }
  await ctx.close();
}
await browser.close(); server.close();
const out = { tree, appJsSha256: sha256(path.join(HANDOFF, rel.replace('search.html', 'app.js'))), chromium: version, at: new Date().toISOString(), cases: checks.length, passed: checks.filter(c => c.pass).length, errors, blocked, checks };
fs.mkdirSync(path.join(HANDOFF, 'candidate/evidence/batch3'), { recursive: true });
fs.writeFileSync(path.join(HANDOFF, `candidate/evidence/batch3/focused-${tree}.json`), JSON.stringify(out, null, 1));
console.log(tree, out.passed + '/' + out.cases, 'errors', errors.length, 'blocked', blocked.length);
const fails = {}; for (const c of checks.filter(c => !c.pass)) fails[c.name] = (fails[c.name] || 0) + 1; console.log(JSON.stringify(fails));
