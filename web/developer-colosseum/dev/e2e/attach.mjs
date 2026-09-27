// node attach.mjs [port] ['<route json>'] [label]
// Drives the web layer INSIDE the running Colosseum app over the Chrome DevTools Protocol.
// Start Colosseum with both variables set, e.g. in PowerShell:
//   $env:COLOSSEUM_WEBUI='1'; $env:QTWEBENGINE_REMOTE_DEBUGGING='9222'; .\native\build-msvc\colosseum.exe
// Then: node attach.mjs 9222 '{"name":"world","world":"Theatre","tab":"discover"}' live-theatre
// It navigates the router (never reloads the app), screenshots, and runs the same keyboard walk on real data.
//
// Raw CDP, not Playwright's connectOverCDP: that path calls Browser.setDownloadBehavior on
// connect, which Qt WebEngine does not implement (no browser-context management) and fails on.
// This client talks to the page target's own websocket — Runtime.evaluate, Input.dispatchKeyEvent,
// Page.captureScreenshot — using only Node built-ins (fetch + the global WebSocket), so the same
// checks run against the live app with no Playwright session at all.
import fs from 'node:fs';
import path from 'node:path';
import { OUT } from './lib.mjs';           // only the output-dir constant; no Playwright involved

const port = process.argv[2] || '9222';
const route = process.argv[3] ? JSON.parse(process.argv[3]) : null;
const label = process.argv[4] || 'live';
const probeFile = process.argv[5] || '';
fs.mkdirSync(OUT, { recursive: true });
const wait = ms => new Promise(r => setTimeout(r, ms));

// ---- minimal CDP session over the page target's debugging websocket ----
class Cdp {
  constructor(ws) {
    this.ws = ws;
    this.seq = 0;
    this.pending = new Map();
    this.timers = new Map();
    this.listeners = [];
    ws.addEventListener('message', ev => {
      const m = JSON.parse(ev.data);
      if (m.id != null && this.pending.has(m.id)) {
        const { resolve, reject } = this.pending.get(m.id);
        this.pending.delete(m.id);
        clearTimeout(this.timers.get(m.id));
        this.timers.delete(m.id);
        m.error ? reject(new Error(`${m.error.message || 'CDP error'}`)) : resolve(m.result);
      } else if (m.method) {
        for (const fn of this.listeners) fn(m);
      }
    });
    ws.addEventListener('close', () => {
      for (const id of [...this.pending.keys()]) {
        clearTimeout(this.timers.get(id));
        this.timers.delete(id);
        this.pending.get(id).reject(new Error('CDP socket closed'));
        this.pending.delete(id);
      }
    });
  }
  static async connect(url) {
    const ws = new WebSocket(url);
    await new Promise((resolve, reject) => {
      ws.addEventListener('open', resolve, { once: true });
      ws.addEventListener('error', () => reject(new Error(`cannot open ${url}`)), { once: true });
    });
    return new Cdp(ws);
  }
  send(method, params = {}) {
    const id = ++this.seq;
    this.ws.send(JSON.stringify({ id, method, params }));
    // A dropped DevTools socket can die silently (no close frame), which would leave the
    // top-level flow awaiting forever; every call settles one way or the other.
    const timeout = new Promise((_, reject) => { this.timers.set(id, setTimeout(() => reject(new Error(`CDP ${method} timed out`)), 15000)); });
    return Promise.race([
      new Promise((resolve, reject) => this.pending.set(id, { resolve, reject })),
      timeout
    ]);
  }
  on(fn) { this.listeners.push(fn); }
  close() { try { this.ws.close(); } catch (_) { /* already gone */ } }
}

const evaluate = async (cdp, expression) => {
  const r = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) {
    const d = r.exceptionDetails;
    throw new Error('page eval failed: ' + ((d.exception && d.exception.description) || d.text));
  }
  return r.result ? r.result.value : undefined;
};

// ---- the checks walk.mjs runs, restated over CDP ----
function watchConsole(cdp, problems) {
  cdp.on(m => {
    if (m.method === 'Runtime.consoleAPICalled') {
      const text = (m.params.args || []).map(a => (a.value != null ? String(a.value) : (a.description || ''))).join(' ');
      const type = m.params.type;
      if (type === 'error' && !/Failed to load resource|ERR_UNKNOWN_URL_SCHEME|qwebchannel/i.test(text)) problems.push('console error: ' + text);
      if (/\[port\] dropped/.test(text)) problems.push(text);
    } else if (m.method === 'Runtime.exceptionThrown') {
      const d = m.params.exceptionDetails;
      problems.push('page error: ' + ((d.exception && d.exception.description) || d.text || 'unknown exception'));
    }
  });
}

// Arrows / Escape as real key events; Qt WebEngine's Input domain handles these. If a host ever
// rejects the Input domain, we fall back to synthetic KeyboardEvents through Runtime.evaluate.
const VK = { ArrowRight: 39, ArrowLeft: 37, ArrowUp: 38, ArrowDown: 40, Escape: 27, Enter: 13 };
let inputViaSynth = false;
async function press(cdp, key) {
  if (!inputViaSynth) {
    const base = { key, code: key, windowsVirtualKeyCode: VK[key], nativeVirtualKeyCode: VK[key] };
    try {
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', ...base });
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
      return;
    } catch (e) { inputViaSynth = true; console.log(`note: Input domain unavailable (${e.message}); using synthetic key events`); }
  }
  await evaluate(cdp, `(function(){var ev=new KeyboardEvent('keydown',{key:${JSON.stringify(key)},bubbles:true,cancelable:true});document.dispatchEvent(ev);})()`);
}

async function keyboardWalk(cdp) {
  const ident = () => evaluate(cdp, `(() => {
    const a = document.activeElement;
    if (!a || a === document.body) return null;
    const all = [...document.querySelectorAll('[data-focus]')];
    const label = (a.getAttribute('aria-label') || a.textContent || '').trim().slice(0, 30);
    return a.getAttribute('data-key') || a.id || ('#' + all.indexOf(a) + ':' + label);
  })()`);
  const focusByIdent = id => evaluate(cdp, `(() => {
    const id = ${JSON.stringify(id)};
    const all = [...document.querySelectorAll('[data-focus]')];
    const el = document.querySelector('[data-key="' + CSS.escape(id) + '"]') || document.getElementById(id)
      || (id.startsWith('#') ? all[parseInt(id.slice(1), 10)] : null);
    if (el) el.focus({ preventScroll: false });
    return !!el;
  })()`);
  const visibleFocusables = () => evaluate(cdp, `(() => {
    const vis = el => { if (el.closest('[hidden],[inert]')) return false; const r = el.getClientRects(); return r.length && r[0].width > 0 && r[0].height > 0; };
    const all = [...document.querySelectorAll('[data-focus]')];
    return all.filter(vis).map(a => {
      const label = (a.getAttribute('aria-label') || a.textContent || '').trim().slice(0, 30);
      return a.getAttribute('data-key') || a.id || ('#' + all.indexOf(a) + ':' + label);
    });
  })()`);
  const underTopBar = () => evaluate(cdp, `(() => {
    const a = document.activeElement, board = document.getElementById('board');
    if (!a || !board || !board.contains(a)) return false;
    const r = a.getBoundingClientRect(), b = board.getBoundingClientRect();
    const slack = 14;                                    // a focused card scales to 1.06 — allow its halo
    return r.top < b.top - slack || r.bottom > b.bottom + slack;
  })()`);
  const hiddenAfterSettle = async () => (await underTopBar()) && (await wait(500), await underTopBar());

  await press(cdp, 'ArrowDown');                 // nothing focused → the engine focuses the first element
  const start = await ident();
  const seen = new Set(start ? [start] : []);
  const queue = start ? [start] : [];
  const hidden = new Set();
  let presses = 0;
  while (queue.length && presses < 4000) {
    const from = queue.shift();
    for (const key of ['ArrowRight', 'ArrowLeft', 'ArrowDown', 'ArrowUp']) {
      if (!(await focusByIdent(from))) break;
      await press(cdp, key); presses++;
      await wait(15);
      const to = await ident();
      if (to && await hiddenAfterSettle()) hidden.add(to);
      if (to && !seen.has(to)) { seen.add(to); queue.push(to); }
    }
  }
  const all = await visibleFocusables();
  const unreachable = all.filter(k => !seen.has(k));
  return { reached: seen.size, total: all.length, unreachable, hiddenAfterMove: [...hidden], presses };
}

async function screenshot(cdp, file, width, height) {
  let override = false;
  if (width) {
    try { await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }); override = true; }
    catch (_) { console.log(`note: Emulation metrics override unsupported; skipping ${width}×${height}`); return; }
  }
  try {
    const r = await cdp.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
    fs.writeFileSync(file, Buffer.from(r.data, 'base64'));
    console.log(`screenshot ${file}`);
  } finally {
    if (override) { try { await cdp.send('Emulation.clearDeviceMetricsOverride'); } catch (_) { /* leave as-is */ } }
  }
}

// ---- attach ----
// The shared Arc 54 checkout is edited by other agents while the app runs; its QML live-reload
// watcher recreates the WebEngine page, which kills the target's websocket mid-run. So: connect,
// run, and when the session drops, reattach to the (new) page target and resume — the router
// restores the route from the location hash after a reload, so a resumed run re-asserts the
// route only when it is not already current.
const findTarget = async () => {
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const t = targets.find(t => t.type === 'page' && /developer-webui|developer-colosseum/.test(t.url || ''));
  if (!t) throw new Error(`No Colosseum web layer found on port ${port}. Pages: ${targets.map(t => t.url).join(', ') || 'none'}`);
  return t;
};
async function connect() {
  let lastErr = null;
  // The session wrapper launches the app immediately before attaching. Poll CDP readiness
  // until the WebEngine page target exists; no fixed boot delay is needed.
  for (let attempt = 0; attempt < 60; attempt++) {
    try {
      const cdp = await Cdp.connect((await findTarget()).webSocketDebuggerUrl);
      await cdp.send('Runtime.enable');
      await cdp.send('Page.enable');
      return cdp;
    } catch (e) { lastErr = e; cdpSafeClose(); await wait(500); }
  }
  throw lastErr;
}
let cdp = null;
const cdpSafeClose = () => { try { cdp && cdp.close(); } catch (_) { /* already gone */ } };

async function runChecks(session, failures) {
  const problems = [];
  watchConsole(session, problems);

  // The web view can be mid-(re)load when we attach (app boot, or the QML live-reload watcher
  // firing on a shared checkout edit). Wait for the shell to exist before touching anything.
  for (let i = 0; i < 40; i++) {
    const up = await evaluate(session, `!!(window.CW && CW.router && CW.port && CW.env)`);
    if (up) break;
    await wait(500);
  }

  if (route && route.name) {
    const cur = await evaluate(session, 'JSON.stringify(CW.router.current())');
    const same = cur && JSON.parse(cur);
    const matches = same && same.name === route.name && same.world === route.world && same.page === route.page
      && (!route.tab || !same.tab || same.tab === route.tab);
    if (!matches) await evaluate(session, `window.CW.router.go(${JSON.stringify(route)})`);
  }
  // Live feeds land after the app's catalogs warm up (unlike replayed fixtures); wait for real
  // sections instead of a fixed sleep, then let the checks run.
  let ready = false;
  for (let i = 0; i < 120; i++) {
    const state = await evaluate(session, `(() => ({ count: document.querySelectorAll('#col [data-section]').length,
      hero: document.querySelector('#col [data-section="hero"]')?.dataset.state || '' }))()`);
    if (state.count > 0 && (route?.name !== 'detail' || state.hero === 'ready')) { ready = true; break; }
    await wait(500);
  }
  if (!ready) failures.push('sections did not reach ready state');

  if (probeFile) {
    const result = await evaluate(session, fs.readFileSync(probeFile, 'utf8'));
    console.log('probe ' + JSON.stringify(result));
    if (!result || !result.ok) failures.push('probe failed: ' + JSON.stringify(result));
  }

  await screenshot(session, path.join(OUT, `${label}.png`));          // the page as the app shows it
  const sections = await evaluate(session, `[...document.querySelectorAll('#col [data-section]')].map(s => s.dataset.section + ':' + s.dataset.state)`);
  console.log(`sections (${sections.length}): ${sections.join(', ') || 'none'}`);

  const walk = await keyboardWalk(session);
  console.log(`keyboard: reached ${walk.reached}/${walk.total} focusables in ${walk.presses} presses`);
  if (walk.unreachable.length) failures.push(`unreachable by keyboard (${walk.unreachable.length}): ${walk.unreachable.slice(0, 15).join(' | ')}`);
  if (walk.hiddenAfterMove.length) failures.push(`focus landed hidden under the TopBar/off the board: ${walk.hiddenAfterMove.slice(0, 10).join(' | ')}`);

  // the two walk.mjs sizes, replayed through viewport emulation (the app window itself is
  // untouched). Best-effort: a socket drop here must not sink the already-collected results.
  for (const [w, h] of [[1920, 1080], [1280, 720]]) {
    try { await screenshot(session, path.join(OUT, `${label}-${w}.png`), w, h); }
    catch (e) { console.log(`note: ${w}×${h} shot failed (${e.message})`); }
  }

  if (route && route.name !== 'home') {            // Escape must leave a pushed route (walk.mjs rule)
    const before = await evaluate(session, 'location.hash');
    await press(session, 'Escape');
    await wait(300);
    if (before === await evaluate(session, 'location.hash')) failures.push('Escape did not leave the page');
  }

  await wait(200);                                // let late console messages arrive
  failures.push(...problems);
}

const failures = [];
try {
  let done = false;
  for (let attempt = 1; attempt <= 3 && !done; attempt++) {
    cdp = await connect();
    try {
      await runChecks(cdp, failures);
      done = true;
    } catch (e) {
      cdpSafeClose();
      if (attempt < 3) console.log(`note: session lost (${e.message}); reattaching ${attempt + 1}/3`);
      else failures.push('attach aborted: ' + e.message);
    }
  }
} finally {
  cdpSafeClose();                                  // disconnects only; the app keeps running
}
if (failures.length) { console.log('FAIL'); failures.forEach(f => console.log('  - ' + f)); process.exitCode = 1; }
else console.log('PASS');
