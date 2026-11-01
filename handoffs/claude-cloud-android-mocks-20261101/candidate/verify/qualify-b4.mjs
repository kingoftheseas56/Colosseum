// Batch 4 independent qualification on the candidate (no Pro final report or Sol review exists).
// 13 pages x 7 viewport presets + 2 stress cases (354x792 and 792x354 at 1.3 text, 24px insets, long content),
// using each page family's own preset convention (from Pro's qa-core.cjs). Checks: script/resource
// errors, frame sized to the viewport, controls inside the frame, broken images, Android window-action
// reachability, generic "Choose an option" replacement sheets, keyboard Tab traversal staying in the app
// and landing on visible controls. Then Pro's nine regression probes (B4-001..006) re-run fresh.
import fs from 'node:fs'; import path from 'node:path';
import { serve, launch, HANDOFF, sha256 } from './harness.mjs';
const tree = process.argv[2] || 'candidate';
const FM = `${tree}/review-batches/04-accounts-configuration/finished-mocks/`;
const tasks = { 'profile': '10-profile', 'account-welcome': '11-account-entry', 'account-flyout': '11-account-entry', 'account-centre': '11-account-entry', 'account-profile': '12-account-forms', 'account-security': '12-account-forms', 'account-recovery': '12-account-forms', 'account-privacy': '12-account-forms', 'account-devices': '12-account-forms', 'account-your-colosseum': '12-account-forms', 'stremio-sync': '13-stremio-sync', 'extension-setup': '14-extension-setup', 'tankoyomi-configuration': '14-extension-setup' };
const dims = { portrait: [412, 900], landscape: [900, 412], small: [354, 792], short: [792, 354], desktop: [1280, 700], tablet: [800, 1280], tabletLandscape: [1280, 800] };
const presets = { '11-account-entry': { portrait: 'portrait', landscape: 'landscape', small: 'narrow', short: 'narrow-landscape', desktop: 'desktop', tablet: 'tablet', tabletLandscape: 'tablet-landscape' },
  '12-account-forms': { portrait: 'phone412', landscape: 'landscape900', small: 'phone354', short: 'landscape792', desktop: 'desktop', tablet: 'tablet800', tabletLandscape: 'tablet1280' },
  '13-stremio-sync': { portrait: 'portrait', landscape: 'landscape', small: 'smallPortrait', short: 'smallLandscape', desktop: 'desktop', tablet: 'tabletPortrait', tabletLandscape: 'tabletLandscape' },
  '14-extension-setup': { portrait: 'portrait', landscape: 'landscape', small: 'small', short: 'smallLandscape', desktop: 'desktop', tablet: 'tablet', tabletLandscape: 'tabletLandscape' } };
const roots = { '10-profile': '#device', '11-account-entry': '#mock,#app-frame', '12-account-forms': '#app-frame', '13-stremio-sync': '#frame', '14-extension-setup': '#viewport' };
function queryFor(task, kind, extra = {}) {
  const [w, h] = dims[kind], q = new URLSearchParams({ capture: '1' });
  if (task === '10-profile') { q.set('w', w); q.set('h', h); }
  if (task === '11-account-entry') q.set('v', presets[task][kind]);
  if (task === '12-account-forms' || task === '14-extension-setup') q.set('preset', presets[task][kind]);
  if (task === '13-stremio-sync') q.set('capture', presets[task][kind]);
  for (const [k, v] of Object.entries(extra)) q.set(k, v);
  return q.toString();
}
function stressFor(task) {
  if (task === '10-profile' || task === '11-account-entry') return { top: 24, bottom: 24, scale: 1.3, ...(task === '11-account-entry' ? { stress: 1 } : {}) };
  if (task === '12-account-forms') return { top: 24, bottom: 24, font: 1.3, state: 'long' };
  if (task === '13-stremio-sync') return { inset: 24, font: 1.3, long: 1 };
  return { insetTop: 24, insetBottom: 24, scale: 1.3 };
}
const { server, origin } = await serve(); const { browser, version } = await launch();
const external = [];
async function open(name, kind, extra = {}) {
  const task = tasks[name], [w, h] = dims[kind];
  const ctx = await browser.newContext({ viewport: { width: w, height: h } }); const page = await ctx.newPage(); const log = { errors: [], failed: [] };
  await ctx.route('**/*', r => r.request().url().startsWith(origin) || r.request().url().startsWith('data:') ? r.continue() : (external.push(r.request().url()), r.abort()));
  page.on('pageerror', e => log.errors.push(e.message));
  page.on('response', r => { if (r.status() >= 400 && !/favicon|local-fonts\/seg/.test(r.url())) log.failed.push(r.status() + ' ' + r.url().replace(origin, '')); });
  await page.goto(`${origin}/${FM}${task}/${name}.html?${queryFor(task, kind, extra)}`);
  await page.evaluate(() => Promise.race([document.fonts.ready, new Promise(r => setTimeout(r, 3000))])); await page.waitForTimeout(250);
  return { ctx, page, log, task, w, h };
}
const cases = [];
const KINDS = [...Object.keys(dims).map(k => [k, false]), ['small', true], ['short', true]];
for (const name of Object.keys(tasks)) for (const [kind, stress] of KINDS) {
  const task = tasks[name]; const { ctx, page, log, w, h } = await open(name, kind, stress ? stressFor(task) : {});
  const android = kind !== 'desktop';
  const m = await page.evaluate(([sel, android]) => {
    const root = document.querySelector(sel), r = root.getBoundingClientRect();
    const vis = e => { const s = getComputedStyle(e), b = e.getBoundingClientRect(); return b.width > 0 && b.height > 0 && s.visibility !== 'hidden' && s.display !== 'none' && !e.closest('[hidden]'); };
    const controls = [...root.querySelectorAll('button,input,select,a,[tabindex]')].filter(vis);
    const outside = controls.filter(e => { const b = e.getBoundingClientRect(); let p = e.parentElement, clipped = false; while (p && p !== root) { const s = getComputedStyle(p); if (/(auto|scroll|hidden)/.test(s.overflowX)) { clipped = true; break; } p = p.parentElement; } return !clipped && (b.left < r.left - .5 || b.right > r.right + .5); }).map(e => (e.getAttribute('aria-label') || e.textContent || e.tagName).trim().slice(0, 40));
    const rx = /^(minimi[sz]e( window)?|fullscreen|toggle fullscreen|exit fullscreen|enter fullscreen|close( window)?|quit( colosseum)?…?|window and app)$/i;
    // A bare "Close" counts only when it is a window control (panel/dialog Close buttons are not window actions).
    const isWin = e => { const n = (e.getAttribute('aria-label') || e.getAttribute('title') || e.textContent || '').trim(); if (!rx.test(n)) return false; if (!/^close/i.test(n)) return true; return /window|desktop-sys|sys-control/i.test(e.className + ' ' + (e.dataset.act || '') + ' ' + (e.dataset.action || '') + ' ' + (e.parentElement?.className || '')); };
    const win = android ? controls.filter(isWin).map(e => e.outerHTML.slice(0, 80)) : [];
    const desktopWin = android ? null : controls.filter(isWin).length;
    return { root: { w: Math.round(r.width), h: Math.round(r.height) }, controls: controls.length, desktopWindowActions: desktopWin, controlsOutsideFrame: outside, windowActionsVisible: win,
      chooseAnOption: /choose an option/i.test(root.innerText), broken: [...document.images].filter(i => i.complete && i.currentSrc && !i.naturalWidth).map(i => i.getAttribute('src')),
      docOverflow: document.documentElement.scrollWidth > innerWidth + 1 };
  }, [roots[task], android]);
  // Keyboard: Tab 14 times from the document start; focus must stay inside the app root on visible controls.
  const tabs = await (async () => { const seen = []; await page.evaluate(() => document.activeElement?.blur()); for (let i = 0; i < 14; i++) { await page.keyboard.press('Tab'); seen.push(await page.evaluate(sel => { const a = document.activeElement, root = document.querySelector(sel); const b = a.getBoundingClientRect(); return { inRoot: root.contains(a), body: a === document.body, visible: b.width > 0 && b.height > 0, label: (a.getAttribute('aria-label') || a.id || a.textContent || a.tagName).trim().slice(0, 30) }; }, roots[task])); } return seen; })();
  const inRootTabs = tabs.filter(t => t.inRoot);
  const c = { page: name, kind, stress, w, h, ...m, tabStops: tabs.length, tabStopsInApp: inRootTabs.length, tabStopsInvisible: inRootTabs.filter(t => !t.visible).map(t => t.label), errors: log.errors, failed: log.failed };
  c.checks = { noErrors: !log.errors.length && !log.failed.length, frameMatchesViewport: Math.abs(m.root.w - w) <= 2 && Math.abs(m.root.h - h) <= 2, noControlsOutsideFrame: !m.controlsOutsideFrame.length,
    noBrokenImages: !m.broken.length, noDocOverflow: !m.docOverflow, noAndroidWindowActions: !m.windowActionsVisible.length, noGenericChooseSheet: !m.chooseAnOption,
    keyboardReachesApp: inRootTabs.length > 0, keyboardStopsVisible: !c.tabStopsInvisible.length };
  // Desktop keeps the approved window controls where the original page has them.
  if (kind === 'desktop' && /^account-(flyout|centre|profile|security|recovery|privacy|devices|your-colosseum)$/.test(name)) c.checks.desktopWindowActionsRetained = m.desktopWindowActions === 3;
  c.pass = Object.values(c.checks).every(Boolean);
  if (['portrait', 'landscape'].includes(kind) && !stress || stress && kind === 'short') { const dir = path.join(HANDOFF, 'candidate/evidence/batch4/captures'); fs.mkdirSync(dir, { recursive: true }); const f = path.join(dir, `${name}-${kind}${stress ? '-stress' : ''}.png`); await page.screenshot({ path: f }); c.capture = { file: path.relative(path.join(HANDOFF, 'candidate/evidence/batch4'), f), sha256: sha256(f) }; }
  cases.push(c); await ctx.close();
}
// Pro's regression probes, re-run fresh against this tree.
const probes = [];
const active = p => p.evaluate(() => ({ action: document.activeElement.dataset.action || document.activeElement.dataset.act, id: document.activeElement.id, inFlyout: !!document.activeElement.closest('.flyout-card') }));
async function probe(id, name, kind, extra, fn) { const { ctx, page, log } = await open(name, kind, extra); let r; try { r = await fn(page); } catch (e) { r = { pass: false, error: String(e.message).split('\n')[0] }; } probes.push({ id, page: name, kind, ...r, errors: log.errors }); await ctx.close(); }
await probe('F01-flyout-initial-focus', 'account-flyout', 'portrait', {}, async p => { const a = await active(p); return { pass: a.inFlyout && a.action === 'open-section', observed: a }; });
await probe('F02-flyout-tab-boundary', 'account-flyout', 'landscape', {}, async p => { await p.focus('.flyout-card .session-btn'); await p.keyboard.press('Tab'); const a = await active(p); return { pass: a.inFlyout, observed: a }; });
await probe('F03-flyout-escape-return', 'account-flyout', 'portrait', { open: 0 }, async p => { await p.evaluate(() => document.querySelector('[data-action="toggle-flyout"]').scrollIntoView()); await p.click('[data-action="toggle-flyout"]'); await p.keyboard.press('Escape'); const a = await active(p); return { pass: a.action === 'toggle-flyout' && !(await p.$('.flyout-card')), observed: a }; });
await probe('F04-account-route-context', 'account-profile', 'short', { top: 24, bottom: 24, font: 1.3 }, async p => { await Promise.all([p.waitForNavigation({ waitUntil: 'load' }), p.click('.compact-nav [data-page="security"]')]); const o = await p.evaluate(() => ({ query: location.search, width: document.querySelector('#app-frame').offsetWidth, height: document.querySelector('#app-frame').offsetHeight })); return { pass: o.width === 792 && o.height === 354 && o.query.includes('font=1.3'), observed: o }; });
await probe('F05-password-draft-rerender', 'account-security', 'portrait', { state: 'password' }, async p => { await p.type('#security-current', 'LocalDemoCurrent'); await p.type('#security-new', 'LocalDemoNext'); await p.type('#security-confirm', 'LocalDemoNext'); await p.click('[data-act="switch"]'); const o = await p.evaluate(() => ({ current: document.querySelector('#security-current').value.length, n: document.querySelector('#security-new').value.length, saveDisabled: document.querySelector('[data-act="security-password-save"]').disabled, active: document.activeElement.dataset.act })); return { pass: o.current === 16 && o.n === 13 && !o.saveDisabled && o.active === 'switch', observed: o }; });
await probe('F06-avatar-focus', 'account-profile', 'landscape', {}, async p => { await p.click('[data-avatar="book"]'); const a = await active(p); return { pass: a.action === 'avatar', observed: a }; });
await probe('F07-reset-preserves-enabled', 'tankoyomi-configuration', 'portrait', {}, async p => { await p.click('[data-toggle="zonatmo"]'); await p.click('[data-move="zonatmo"][data-dir="1"]'); await p.click('#reset-order'); const o = await p.evaluate(() => ({ first: document.querySelector('[data-provider]').dataset.provider, enabled: document.querySelector('[data-toggle="zonatmo"]').getAttribute('aria-checked') })); return { pass: o.first === 'zonatmo' && o.enabled === 'false', observed: o }; });
await probe('F08-language-select-focus', 'tankoyomi-configuration', 'landscape', { menu: 'open' }, async p => { await p.click('[data-default="en"]'); const o = await p.evaluate(() => ({ active: document.activeElement.id, open: !document.querySelector('#default-options').hidden, def: document.querySelector('#default-name').textContent })); return { pass: o.active === 'default-selector' && !o.open && o.def === 'English', observed: o }; });
await probe('F09-month-layout-width', 'account-your-colosseum', 'landscape', {}, async p => { const o = await p.evaluate(() => ({ columns: getComputedStyle(document.querySelector('.portrait-grid')).gridTemplateColumns, metricColumns: getComputedStyle(document.querySelector('.metrics-grid')).gridTemplateColumns })); return { pass: o.columns.split(' ').length === 1 && o.metricColumns.split(' ').length === 4, observed: o }; });
// Local fixtures only: the pages must never attempt a network request.
await browser.close(); server.close();
const files = fs.readdirSync(path.join(HANDOFF, FM), { recursive: true }).filter(f => /\.(js|css|html)$/.test(f)).sort();
const out = { tree, tool: 'candidate/verify/qualify-b4.mjs', chromium: version, at: new Date().toISOString(), externalRequests: external.length,
  fileHashes: Object.fromEntries(files.map(f => [f, sha256(path.join(HANDOFF, FM, f))])),
  cases: cases.length, passed: cases.filter(c => c.pass).length, probes: probes.length, probesPassed: probes.filter(p => p.pass).length, results: cases, probeResults: probes };
fs.mkdirSync(path.join(HANDOFF, 'candidate/evidence/batch4'), { recursive: true });
fs.writeFileSync(path.join(HANDOFF, `candidate/evidence/batch4/qualification-${tree}.json`), JSON.stringify(out, null, 1));
console.log(tree, 'cases', out.passed + '/' + out.cases, 'probes', out.probesPassed + '/' + out.probes, 'external', external.length);
const agg = {}; for (const c of cases.filter(c => !c.pass)) for (const [k, v] of Object.entries(c.checks)) if (!v) (agg[k] ||= []).push(c.page + ':' + c.kind + (c.stress ? '*' : ''));
for (const [k, v] of Object.entries(agg)) console.log(' FAIL', k, v.join(' '));
for (const p of probes.filter(p => !p.pass)) console.log(' PROBE FAIL', p.id, JSON.stringify(p.observed || p.error));
