// chrome_laws.test.mjs — the manga-reader chrome contract, ported.
// Run: node resources/book_reader/tests/chrome_laws.test.mjs
import { createChromeLaws } from '../domains/books/reader/room/chrome_laws.js';

let fails = 0;
const ok = (c, m) => { if (!c) { console.error('FAIL:', m); fails++; } };

// A fake clock so we control idle timing deterministically.
function harness(opts = {}) {
  let now = 0;
  const timers = [];
  const laws = createChromeLaws({
    idleMs: 3000,
    // deferred: edge cooldown (anti-flicker) — not in Phase-1 scope
    setTimer: (fn, ms) => { const t = { fn, at: now + ms, dead: false }; timers.push(t); return t; },
    clearTimer: (t) => { if (t) t.dead = true; },
    ...opts,
  });
  const advance = (ms) => {
    now += ms;
    timers.filter(t => !t.dead && t.at <= now).forEach(t => { t.dead = true; t.fn(); });
  };
  return { laws, advance };
}

// 1) Shown on start.
{ const { laws } = harness(); ok(laws.shown === true, 'chrome shown on start'); }

// 2) Hides after 3s idle.
{ const { laws, advance } = harness(); advance(3000); ok(laws.shown === false, 'hides after 3s idle'); }

// 3) Reading (page turn) does NOT wake it.
{ const { laws, advance } = harness(); advance(3000); laws.onPageTurn(); ok(laws.shown === false, 'page turn never wakes chrome'); }

// 4) Edge-reach reveals.
{ const { laws, advance } = harness(); advance(3000); laws.onEdgeReach(); ok(laws.shown === true, 'edge reach reveals'); }

// 5) After edge-reveal, idles away again after 3s.
{ const { laws, advance } = harness(); advance(3000); laws.onEdgeReach(); advance(3000); ok(laws.shown === false, 're-idles after edge reveal'); }

// 6) Explicit hide (H) sticks — beats pin.
{ const { laws } = harness(); laws.setPinned(true); laws.toggleExplicit(); ok(laws.shown === false, 'explicit hide beats pin'); }

// 7) Explicit hide revived by edge-reach.
{ const { laws } = harness(); laws.toggleExplicit(); laws.onEdgeReach(); ok(laws.shown === true, 'edge reach revives explicit hide'); }

// 8) Pin keeps it shown through idle.
{ const { laws, advance } = harness(); laws.setPinned(true); advance(3000); ok(laws.shown === true, 'pin keeps chrome shown through idle'); }

// 9) Open popover freezes shown (no idle-hide while a panel is open).
{ const { laws, advance } = harness(); laws.setPopoverOpen(true); advance(3000); ok(laws.shown === true, 'open popover freezes chrome shown'); }

// 10) Closing popover lets idle resume.
{ const { laws, advance } = harness(); laws.setPopoverOpen(true); advance(3000); laws.setPopoverOpen(false); advance(3000); ok(laws.shown === false, 'closing popover resumes idle-hide'); }

// 11) Explicit hide survives popover-open (freeze must not reveal — MangaReader.qml:544).
{ const { laws } = harness(); laws.toggleExplicit(); laws.setPopoverOpen(true); ok(laws.shown === false, 'explicit hide survives popover open'); }

// 12) Bare construction (no deps) is safe — real timers; construction-only assertion.
{ let threw = false, laws = null; try { laws = createChromeLaws(); } catch (e) { threw = true; }
  ok(!threw && laws && laws.shown === true, 'bare createChromeLaws() constructs shown'); }

// 13) toggleExplicit while popover open is a no-op (QML toggleChrome guards anyModal — MangaReader.qml:559).
{ const { laws } = harness(); laws.setPopoverOpen(true); laws.toggleExplicit(); ok(laws.shown === true, 'toggle while popover open is a no-op'); }

// 14) toggleExplicit from hidden state reveals (the else-arm = pokeChrome).
{ const { laws, advance } = harness(); advance(3000); laws.toggleExplicit(); ok(laws.shown === true, 'toggle from hidden reveals'); }

// 15) Explicit hide THEN pin-on stays hidden (pin only prevents idle-hide, never reveals over explicit).
{ const { laws } = harness(); laws.toggleExplicit(); laws.setPinned(true); ok(laws.shown === false, 'explicit hide survives pin-on'); }

console.log(fails === 0 ? 'chrome_laws PASS' : `chrome_laws FAIL (${fails})`);
process.exit(fails === 0 ? 0 : 1);
