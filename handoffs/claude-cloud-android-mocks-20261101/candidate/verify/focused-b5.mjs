// Batch 5 fresh focused checks: Playwright port of Sol's test-remediation.mjs (26 scenarios x
// 354x792 / 792x354 / 1280x700), extended with 412x900 / 900x412. tree: baseline|candidate.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, HANDOFF, sha256 } from './harness.mjs';
const tree = process.argv[2] || 'candidate';
const { server, origin } = await serve(); const { browser, version } = await launch();
const checks = [], errors = [], external = [];
const assert = (v, m) => { if (!v) throw Error(m); };
for (const [context, w, h] of [['phone-portrait', 354, 792], ['phone-landscape', 792, 354], ['phone-portrait-412', 412, 900], ['phone-landscape-900', 900, 412], ['desktop', 1280, 700]]) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h } }); const p = await ctx.newPage();
  await ctx.route('**/*', r => r.request().url().startsWith(origin) || r.request().url().startsWith('data:') ? r.continue() : (external.push(r.request().url()), r.abort()));
  p.on('pageerror', e => errors.push(e.message));
  p.on('response', r => { if (r.status() >= 400 && !/favicon|local-fonts\/seg|assets\/kde\//.test(r.url())) errors.push(r.status() + ' ' + r.url()); });
  const tap = async s => { await p.$eval(s, e => e.scrollIntoView({ block: 'center' })); await p.click(s); };
  const active = () => p.evaluate(() => { const a = document.activeElement; return { action: a.dataset.action, key: a.dataset.key, item: a.dataset.id, choice: a.dataset.choice, classes: String(a.className), dialog: !!a.closest('.dialog'), dossier: !!a.closest('.dossier') }; });
  const check = async (name, fn) => { try { checks.push({ context, name, pass: true, detail: await fn() }); } catch (e) { checks.push({ context, name, pass: false, error: String(e.message).split('\n')[0], active: await active().catch(() => null) }); } };
  const go = async (capture = 1) => { await p.goto(`${origin}/${tree}/review-batches/05-tracker-utilities/finished-mocks/tracker-connections.html?` + new URLSearchParams({ capture, w, h, platform: context === 'desktop' ? 'desktop' : 'android' })); await p.evaluate(() => document.fonts.ready); };
  for (const [key, card] of [['simkl', 'connected-card'], ['mal', 'connected-card'], ['trakt', 'catalogue-card'], ['simkl', 'catalogue-card']]) for (const close of ['Escape', 'explicit'])
    await check(`${card} ${key} ${close}`, async () => { await go(); await tap(`.${card}[data-key="${key}"]`); if (close === 'Escape') await p.keyboard.press('Escape'); else await tap('[data-action="close-dossier"]'); const a = await active(); assert(a.action === 'open-provider' && a.key === key && a.classes.split(' ').includes(card), 'Wrong exact opener'); assert(!(await p.$('.dossier')), 'Dossier remains'); return a; });
  for (const close of ['Escape', 'explicit', 'veil'])
    await check(`Preferences ${close}`, async () => { await go(); await tap('[data-action="preferences"]'); if (close === 'Escape') await p.keyboard.press('Escape'); else if (close === 'explicit') await tap('[data-action="close-dossier"]'); else await p.mouse.click(2, 2); assert((await active()).action === 'preferences', 'Preferences opener lost'); assert(!(await p.$('.dossier')), 'Panel remains'); });
  for (const action of ['review-import', 'review-export', 'disconnect', 'remove-data']) for (const close of ['Escape', 'Cancel/close'])
    await check(`nested ${action} ${close}`, async () => { await go(); await tap('.connected-card[data-key="simkl"]'); await tap(`[data-action="${action}"]`); if (close === 'Escape') await p.keyboard.press('Escape'); else { const b = await p.$$('.dialog [data-action="dialog-close"]'); await b.at(-1).click(); } const a = await active(); assert(a.action === action && a.dossier && !a.dialog, 'Nested invoker lost'); assert((await p.$('.dossier')) && !(await p.$('.dialog')), 'Wrong modal boundary'); return a; });
  for (const close of ['Escape', 'explicit'])
    await check(`title match ${close} returns Find match`, async () => { await go(); await tap('.connected-card[data-key="simkl"]'); await tap('[data-action="review-import"]'); await tap('[data-choice="Find match"]'); if (close === 'Escape') await p.keyboard.press('Escape'); else await tap('.dialog [data-action="dialog-close"]'); const a = await active(); assert(await p.$('.dialog-import'), 'Import dismissed'); assert(a.action === 'import-choice' && a.item === 'aot' && a.choice === 'Find match', 'Wrong match invoker'); await p.keyboard.press('Escape'); assert((await active()).action === 'review-import', 'Import close lost dossier invoker'); return a; });
  await check('title match selection focuses separate decision', async () => { await go(); await tap('.connected-card[data-key="simkl"]'); await tap('[data-action="review-import"]'); await tap('[data-choice="Find match"]'); await tap('[data-action="match-choose"]'); const a = await active(); assert(a.action === 'import-choice' && a.item === 'aot' && a.choice === 'Use tracker progress', 'Wrong post-match focus'); return a; });
  for (const close of ['Escape', 'Cancel'])
    await check(`bulk ${close} returns review control`, async () => { await go(); await tap('.connected-card[data-key="simkl"]'); await tap('[data-action="review-import"]'); await tap('[data-action="select-import"][data-id="one-piece"]'); await tap('[data-action="select-import"][data-id="naruto"]'); await tap('[data-action="bulk-review"]'); if (close === 'Escape') await p.keyboard.press('Escape'); else await tap('[data-action="back-to-import"]'); const a = await active(); assert(await p.$('.dialog-import'), 'Import dismissed'); assert(a.action === 'bulk-review' && a.dialog, 'Bulk opener lost'); return a; });
  await check('removed opener falls back to Preferences', async () => { await go(); await tap('.connected-card[data-key="simkl"]'); await tap('[data-action="disconnect"]'); await tap('[data-action="no-disconnect"]'); assert((await active()).dossier, 'Confirmation escaped dossier'); await p.keyboard.press('Escape'); assert((await active()).action === 'preferences', 'Unavailable opener fallback is wrong'); });
  await check('deliberate external review navigation retains focus', async () => { await go(0); await p.focus('#provider'); await p.selectOption('#provider', 'trakt'); assert(await p.$eval('#provider', e => e === document.activeElement), 'External navigation focus stolen'); });
  await ctx.close();
}
await browser.close(); server.close();
const out = { tree, trackerJsSha256: sha256(path.join(HANDOFF, tree, 'review-batches/05-tracker-utilities/finished-mocks/tracker.js')), chromium: version, at: new Date().toISOString(), cases: checks.length, passed: checks.filter(c => c.pass).length, errors, external, checks };
fs.mkdirSync(path.join(HANDOFF, 'candidate/evidence/batch5'), { recursive: true });
fs.writeFileSync(path.join(HANDOFF, `candidate/evidence/batch5/focused-${tree}.json`), JSON.stringify(out, null, 1));
console.log(tree, out.passed + '/' + out.cases, 'errors', errors.length, 'external', external.length);
const f = {}; for (const c of checks.filter(c => !c.pass)) f[c.name] = (f[c.name] || 0) + 1; console.log(JSON.stringify(f));
