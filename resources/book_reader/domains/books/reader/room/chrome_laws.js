// chrome_laws.js — the manga reader's auto-hide contract as pure logic.
// Ported from qml/MangaReader.qml:528-560. No DOM: caller injects clock+timers
// and reads `.shown` to paint. Reading NEVER wakes chrome (scroll/keys/page turns).
// NOTE: plain-script-parseable on purpose (no `export` keyword) — ebook_reader.html
// loads this via <script src>; the Node test imports the CJS named export below
// (resolved by cjs-module-lexer).
function createChromeLaws(deps = {}) {
  const idleMs = deps.idleMs ?? 3000;
  const setTimer = deps.setTimer ?? ((fn, ms) => setTimeout(fn, ms));
  const clearTimer = deps.clearTimer ?? ((t) => clearTimeout(t));

  let idleTimer = null;
  let pinned = false;
  let explicitlyHidden = false;   // H / center-tap — sticks until a reach
  let popoverOpen = false;

  const self = {
    shown: true,
    onReach, onEdgeReach: onReach, onPageTurn, toggleExplicit,
    setPinned, setPopoverOpen, poke: onReach,
  };

  function frozen() { return popoverOpen; }               // panel open = never idle-hide

  function reschedule() {
    if (idleTimer) { clearTimer(idleTimer); idleTimer = null; }
    if (self.shown && !pinned && !frozen()) {
      idleTimer = setTimer(() => {
        idleTimer = null;   // fired = no longer pending
        if (!pinned && !frozen()) { self.shown = false; }
      }, idleMs);
    }
  }

  // A deliberate reach for the chrome: edge band, click, hover — reveals + restarts idle.
  function onReach() {
    explicitlyHidden = false;
    self.shown = true;
    reschedule();
  }

  // Reading input: explicitly does nothing to visibility (the whole point).
  function onPageTurn() { /* reading never wakes chrome */ }

  function toggleExplicit() {
    if (frozen()) return;
    if (self.shown) { explicitlyHidden = true; self.shown = false; if (idleTimer) { clearTimer(idleTimer); idleTimer = null; } }
    else { onReach(); }
  }

  function setPinned(v) {
    pinned = !!v;
    if (pinned && !explicitlyHidden) { self.shown = true; }
    reschedule();
  }

  function setPopoverOpen(v) {
    popoverOpen = !!v;
    if (popoverOpen) {
      // Freeze never overrides an explicit hide (MangaReader.qml:544 —
      // chromeShown: hudExplicitlyHidden ? false : (frozen || hudShown || pinned)).
      if (!explicitlyHidden) { self.shown = true; }
      if (idleTimer) { clearTimer(idleTimer); idleTimer = null; }
    } else { reschedule(); }
  }

  reschedule();
  return self;
}

// end of chrome_laws.js — dual export: global for the reader's plain <script src>,
// CJS named export for the Node test (`import { createChromeLaws }` via cjs-module-lexer).
if (typeof window !== 'undefined') window.createChromeLaws = createChromeLaws;
if (typeof module !== 'undefined' && module.exports) module.exports = { createChromeLaws };
