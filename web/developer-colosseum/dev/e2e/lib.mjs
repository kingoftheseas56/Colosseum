// dev/e2e/lib.mjs — shared Playwright plumbing (Claude). Uses the Edge already installed on Windows
// (playwright-core, channel "msedge"), so there is no browser download.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const REPO = path.resolve(HERE, '../../../..');          // checkout root
export const WEB = '/web/developer-colosseum/';
export const OUT = path.join(HERE, 'out');

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css',
  '.json': 'application/json', '.jpg': 'image/jpeg', '.png': 'image/png', '.svg': 'image/svg+xml', '.woff2': 'font/woff2' };

/** Static server over the checkout root (what `python -m http.server` did), on a free port. */
export function serve() {
  return new Promise(resolve => {
    const server = http.createServer((req, res) => {
      const rel = decodeURIComponent(req.url.split('?')[0].split('#')[0]);
      const file = path.join(REPO, rel);
      if (!file.startsWith(REPO)) { res.writeHead(403).end(); return; }
      fs.readFile(file, (err, body) => {
        if (err) { res.writeHead(404).end(); return; }
        res.writeHead(200, { 'content-type': TYPES[path.extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
        res.end(body);
      });
    });
    server.listen(0, '127.0.0.1', () => resolve({ server, base: `http://127.0.0.1:${server.address().port}` }));
  });
}

export async function launch() {
  return chromium.launch({ channel: 'msedge', headless: true });
}

export const routeHash = route => '#' + encodeURIComponent(JSON.stringify(route));

/** Collect console errors and the port's dropped-event warnings — both are failures. */
export function watchConsole(page) {
  const problems = [];
  page.on('console', m => {
    const t = m.text();
    if (m.type() === 'error' && !/Failed to load resource|ERR_UNKNOWN_URL_SCHEME|qwebchannel/i.test(t)) problems.push('console error: ' + t);
    if (/\[port\] dropped/.test(t)) problems.push(t);
  });
  page.on('pageerror', e => problems.push('page error: ' + e.message));
  return problems;
}

/**
 * Keyboard walk: starting from the first focusable, press every arrow from every element reached, until no new
 * element appears. Reports focusables that can never be reached, focus landing hidden under the TopBar, and
 * whether Escape leaves the page.
 */
export async function keyboardWalk(page) {
  const ident = () => page.evaluate(() => {
    const a = document.activeElement;
    if (!a || a === document.body) return null;
    const all = [...document.querySelectorAll('[data-focus]')];
    return a.getAttribute('data-key') || a.id || `#${all.indexOf(a)}:${(a.getAttribute('aria-label') || a.textContent || '').trim().slice(0, 30)}`;
  });
  const focusByIdent = id => page.evaluate(id => {
    const all = [...document.querySelectorAll('[data-focus]')];
    const el = document.querySelector(`[data-key="${CSS.escape(id)}"]`) || document.getElementById(id)
      || (id.startsWith('#') ? all[parseInt(id.slice(1), 10)] : null);
    if (el) el.focus({ preventScroll: false });
    return !!el;
  }, id);
  const visibleFocusables = () => page.evaluate(() => {
    const vis = el => { if (el.closest('[hidden],[inert]')) return false; const r = el.getClientRects(); return r.length && r[0].width > 0 && r[0].height > 0; };
    const all = [...document.querySelectorAll('[data-focus]')];
    return all.filter(vis).map(a => a.getAttribute('data-key') || a.id || `#${all.indexOf(a)}:${(a.getAttribute('aria-label') || a.textContent || '').trim().slice(0, 30)}`);
  });
  const underTopBar = () => page.evaluate(() => {
    const a = document.activeElement, board = document.getElementById('board');
    if (!a || !board || !board.contains(a)) return false;
    const r = a.getBoundingClientRect(), b = board.getBoundingClientRect();
    const slack = 14;                                    // a focused card scales to 1.06 — allow its halo
    return r.top < b.top - slack || r.bottom > b.bottom + slack;
  });
  // Smooth scrolling is still moving right after a key press: only report positions after it settles.
  const hiddenAfterSettle = async () => (await underTopBar()) && (await page.waitForTimeout(500), await underTopBar());

  await page.keyboard.press('ArrowDown');                 // nothing focused → the engine focuses the first element
  const start = await ident();
  const seen = new Set(start ? [start] : []);
  const queue = start ? [start] : [];
  const hidden = new Set();
  let presses = 0;
  while (queue.length && presses < 4000) {
    const from = queue.shift();
    for (const key of ['ArrowRight', 'ArrowLeft', 'ArrowDown', 'ArrowUp']) {
      if (!(await focusByIdent(from))) break;
      await page.keyboard.press(key); presses++;
      await page.waitForTimeout(15);
      const to = await ident();
      if (to && await hiddenAfterSettle()) hidden.add(to);
      if (to && !seen.has(to)) { seen.add(to); queue.push(to); }
    }
  }
  const all = await visibleFocusables();
  const unreachable = all.filter(k => !seen.has(k));
  return { reached: seen.size, total: all.length, unreachable, hiddenAfterMove: [...hidden], presses };
}
