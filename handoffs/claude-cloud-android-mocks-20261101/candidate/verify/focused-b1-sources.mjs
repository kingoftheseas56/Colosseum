// Fresh re-run of Codex's Batch 1 source-picker interaction checks on the candidate tree
// (pages carried unchanged): provider Escape/focus, outside click, comic weak-match cancel;
// at 412x900 and 900x412.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, HANDOFF } from './harness.mjs';
const tree = process.argv[2] || 'candidate'; const M = `${tree}/review-batches/01-sources-manga/finished-mocks/`;
const { server, origin } = await serve(); const { browser, version } = await launch(); const res = [], errors = [];
for (const [w, h] of [[412, 900], [900, 412]]) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h } }); const p = await ctx.newPage(); p.on('pageerror', e => errors.push(e.message));
  await p.goto(`${origin}/${M}01-theatre-sources/film-sources.html?render=1&w=${w}&h=${h}`); await p.waitForTimeout(300);
  await p.click('#providerTrigger'); await p.keyboard.press('Escape');
  res.push({ w, h, name: 'Provider Escape keeps picker, returns focus', pass: await p.evaluate(() => document.querySelector('#providerMenu').hidden && !document.querySelector('#sourceSheet').hidden && document.activeElement.id === 'providerTrigger') });
  await p.click('#providerTrigger'); const box = await p.$eval('#providerMenu', e => { const r = e.getBoundingClientRect(); return { x: r.left, y: r.bottom }; });
  await p.mouse.click(Math.max(5, box.x - 30 < 5 ? 5 : box.x - 30), Math.min(h - 10, box.y + 40));
  res.push({ w, h, name: 'Provider outside click dismisses without activating a source', pass: await p.evaluate(() => document.querySelector('#providerMenu').hidden && !document.querySelector('#sourceSheet').hidden && !document.querySelector('.chosen')) });
  await p.goto(`${origin}/${M}02-comic-sources/comic-sources.html?capture=1&vw=${w}&vh=${h}`); await p.waitForTimeout(300);
  await p.fill('#queryInput', 'Gotham'); await p.click('.search-button'); await p.click('[data-row="0"]'); await p.keyboard.press('Escape');
  res.push({ w, h, name: 'Weak-match cancel keeps filtered result, query and focus', pass: await p.evaluate(() => document.querySelector('#countText').textContent === '1 result' && document.querySelector('#queryInput').value === 'Gotham' && document.querySelector('#weakLayer').classList.contains('hidden') && document.activeElement.dataset.row === '0') });
  await ctx.close();
}
await browser.close(); server.close();
const out = { tree, chromium: version, at: new Date().toISOString(), cases: res.length, passed: res.filter(r => r.pass).length, errors, results: res };
fs.writeFileSync(path.join(HANDOFF, `candidate/evidence/batch1/sources-interactions-${tree}.json`), JSON.stringify(out, null, 1));
console.log(tree, out.passed + '/' + out.cases, 'errors', errors.length); for (const r of res.filter(r => !r.pass)) console.log(' FAIL', r.w, r.h, r.name);
