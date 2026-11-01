// Shared verification harness: owned loopback static server rooted at the handoff
// directory, isolated headless Chromium, external requests blocked and recorded.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
// Uses the environment's preinstalled Playwright (CommonJS resolution honours NODE_PATH / global installs).
const { chromium } = createRequire(import.meta.url)('playwright');

export const HANDOFF = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg', '.webp': 'image/webp', '.gif': 'image/gif', '.ttf': 'font/ttf', '.otf': 'font/otf', '.woff2': 'font/woff2',
  '.woff': 'font/woff', '.md': 'text/markdown; charset=utf-8', '.csv': 'text/csv; charset=utf-8', '.ico': 'image/x-icon' };

export function serve() {
  return new Promise(resolve => {
    const server = http.createServer((req, res) => {
      const u = new URL(req.url, 'http://x');
      const p = path.join(HANDOFF, decodeURIComponent(u.pathname));
      if (!p.startsWith(HANDOFF)) { res.writeHead(403); return res.end(); }
      fs.stat(p, (e, st) => {
        if (e || !st.isFile()) { res.writeHead(404); return res.end('not found'); }
        res.writeHead(200, { 'content-type': TYPES[path.extname(p).toLowerCase()] || 'application/octet-stream', 'cache-control': 'no-store' });
        fs.createReadStream(p).pipe(res);
      });
    });
    server.listen(0, '127.0.0.1', () => resolve({ server, origin: 'http://127.0.0.1:' + server.address().port }));
  });
}

export async function launch() {
  const browser = await chromium.launch();
  return { browser, version: browser.version() };
}

// Segoe binaries are intentionally omitted from the packet (BASELINE-GAPS.md).
export const intentionalMissing = u => /\/reference\/local-fonts\/(segoeui|segoeuib|seguisb)\.ttf$/.test(u) || /favicon\.ico$/.test(u);

export async function openPage(browser, origin, rel, { w, h, query = {} } = {}) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  const log = { errors: [], failed: [], intentional: [], external: [], console: [] };
  await ctx.route('**/*', r => {
    const u = r.request().url();
    if (!u.startsWith(origin) && !u.startsWith('data:') && !u.startsWith('blob:')) { log.external.push(u); return r.abort(); }
    return r.continue();
  });
  page.on('pageerror', e => log.errors.push(String(e.message || e)));
  page.on('console', m => { if (m.type() === 'error') log.console.push(m.text()); });
  page.on('response', r => { if (r.status() >= 400) (intentionalMissing(r.url()) ? log.intentional : log.failed).push(r.status() + ' ' + r.url().replace(origin, '')); });
  page.on('requestfailed', r => { const u = r.url(); if (u.startsWith(origin)) (intentionalMissing(u) ? log.intentional : log.failed).push('ERR ' + u.replace(origin, '')); });
  const qs = new URLSearchParams(query).toString();
  await page.goto(origin + '/' + rel + (qs ? (rel.includes('?') ? '&' : '?') + qs : ''), { waitUntil: 'load' });
  await page.waitForTimeout(450);
  return { ctx, page, log };
}

export const sha256 = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export const sizeQuery = (w, h, android, extra = {}) => ({ capture: '1', w, h, width: w, height: h, vw: w, vh: h, view: w + 'x' + h, vp: w + 'x' + h,
  platform: android ? 'android' : 'desktop', mode: android ? 'android' : 'desktop', host: android ? 'android' : 'desktop', ...extra });

// Visible, reachable desktop window-action invokers (Minimize / Fullscreen / Close / Quit / Window menu).
export async function windowActions(page) {
  return page.evaluate(() => {
    const rx = /^(minimi[sz]e|fullscreen|toggle fullscreen|exit fullscreen|enter fullscreen|close( colosseum| window)?|quit( colosseum)?…?|window and app|window controls)$/i;
    const out = [];
    for (const el of document.querySelectorAll('button,[role=button],a,[tabindex]')) {
      const name = (el.getAttribute('aria-label') || el.getAttribute('title') || el.textContent || '').trim();
      if (!rx.test(name)) continue;
      if (el.closest('.lab,.studio-controls,#controls,.labfields,.review-controls,.controls-panel,[data-lab],.mock-lab,.workbench,.toolbar-lab,aside.studio,.studio')) continue;
      const r = el.getBoundingClientRect(), cs = getComputedStyle(el);
      const shown = r.width > 0 && r.height > 0 && cs.visibility !== 'hidden' && cs.display !== 'none' && !el.closest('[hidden],[aria-hidden=true]') && !el.disabled;
      if (shown) out.push({ name, tag: el.tagName, cls: String(el.className).slice(0, 60) });
    }
    return out;
  });
}

export async function layoutFacts(page) {
  return page.evaluate(() => {
    const de = document.documentElement;
    const broken = [...document.images].filter(i => i.complete && i.currentSrc && i.naturalWidth === 0).map(i => i.getAttribute('src'));
    return { innerWidth, innerHeight, docScrollWidth: de.scrollWidth, horizontalOverflow: de.scrollWidth > innerWidth + 1, brokenImages: broken,
      fontsLoaded: [...document.fonts].filter(f => f.status === 'loaded').map(f => f.family + ' ' + f.weight + ' ' + f.style) };
  });
}

// Per-family viewport conventions (read from each family's own script). Keys:
// P 412x900, L 900x412, SP 354x792, SL 792x354, D 1280x700, TP 800x1280, TL 1280x800.
export const SIZES = { P: [412, 900], L: [900, 412], SP: [354, 792], SL: [792, 354], D: [1280, 700], TP: [800, 1280], TL: [1280, 800] };
const NAMED = {
  '06-biblio-book': ['preset', { P: 'portrait', L: 'landscape', SP: 'portraitSmall', SL: 'landscapeSmall', D: 'desktop', TP: 'tabletPortrait', TL: 'tabletLandscape' }],
  '19-generic-universes': ['v', { P: 'portrait', L: 'landscape', SP: 'narrow', SL: 'narrow-land', D: 'desktop', TP: 'tablet', TL: 'tablet-land' }],
  '20-search-browse': ['preset', { P: 'portrait', L: 'landscape', SP: 'smallPortrait', SL: 'smallLandscape', D: 'desktop', TP: 'tabletPortrait', TL: 'tabletLandscape' }],
  '04-manga-series': ['preset', { P: 'portrait', L: 'landscape', SP: 'compact', SL: 'short', D: 'desktop', TP: 'tablet', TL: 'tabletLandscape' }],
  '05-volume-flow': ['preset', { P: 'portrait', L: 'landscape', SP: 'compact', SL: 'short', D: 'desktop', TP: 'tablet', TL: 'tabletLandscape' }],
  '11-account-entry': ['v', { P: 'portrait', L: 'landscape', SP: 'narrow', SL: 'narrow-landscape', D: 'desktop', TP: 'tablet', TL: 'tablet-landscape' }],
  '12-account-forms': ['preset', { P: 'phone412', L: 'landscape900', SP: 'phone354', SL: 'landscape792', D: 'desktop', TP: 'tablet800', TL: 'tablet1280' }],
  '13-stremio-sync': ['capture', { P: 'portrait', L: 'landscape', SP: 'smallPortrait', SL: 'smallLandscape', D: 'desktop', TP: 'tabletPortrait', TL: 'tabletLandscape' }],
  '14-extension-setup': ['preset', { P: 'portrait', L: 'landscape', SP: 'small', SL: 'smallLandscape', D: 'desktop', TP: 'tablet', TL: 'tabletLandscape' }],
};
export function familyQuery(family, key, extra = {}) {
  const [w, h] = SIZES[key], android = key !== 'D';
  const q = sizeQuery(w, h, android, extra);
  const named = NAMED[family]; if (named) q[named[0]] = named[1][key];
  return q;
}
// How much of the requested width the app's visible content actually spans (catches a phone-sized
// frame rendered inside a tablet/desktop viewport) and whether an element is laid out at w x h.
export async function frameFacts(page, w, h) {
  return page.evaluate(([w, h]) => {
    const lab = '.lab,.studio-controls,#controls,.labfields,.review-controls,.controls-panel,[data-lab],.mock-lab,.workbench,aside.studio,.studio,.reviewer,.review-panel,.mock-controls,#lab,#review,#reviewer,.small-note';
    const pick = exclude => [...document.querySelectorAll('button,img,h1,h2,h3,a,input,p,span')].filter(e => { if (exclude && e.closest(lab)) return false; const b = e.getBoundingClientRect(); return b.width > 2 && b.height > 2 && b.right > 0 && b.left < innerWidth && b.top < innerHeight && b.bottom > 0 && getComputedStyle(e).visibility !== 'hidden'; });
    let vis = pick(true); if (!vis.length) vis = pick(false); // app hosted inside a reviewer container
    const L = Math.min(...vis.map(e => e.getBoundingClientRect().left)), R = Math.max(...vis.map(e => e.getBoundingClientRect().right));
    const sized = [...document.querySelectorAll('body *')].some(e => !e.closest(lab) && Math.abs(e.offsetWidth - w) <= 2 && Math.abs(e.offsetHeight - h) <= 2);
    return { contentSpan: Math.round((R - L) / w * 100) / 100, elementAtRequestedSize: sized };
  }, [w, h]);
}
