// pill.js — mounts the pill, binds chrome laws to paint, dispatches button acts.
// House rule: SVG glyphs (grayscale + gold accent), never emoji/unicode in prod —
// the glyph <use> refs are filled by frontend-design; this file owns behavior only.
(function () {
  'use strict';
  try {
    const bus = window.booksReaderBus;
    const laws = window.__roomLaws = window.createChromeLaws
      ? window.createChromeLaws({ /* real DOM clock via defaults */ })
      : null;

    const pill = document.getElementById('roomPill');
    const view = document.getElementById('booksReaderView');
    if (!pill || !view || !laws) return;

    function paint() {
      view.setAttribute('data-chrome', laws.shown ? 'shown' : 'hidden');
    }
    // Repaint on every state transition: the machine is authoritative, paint mirrors `.shown`.
    let last = null;
    (function loop() {
      if (laws.shown !== last) { last = laws.shown; paint(); }
      requestAnimationFrame(loop);
    })();

    // Edge band: cursor within 60px of top OR bottom = reach.
    document.addEventListener('mousemove', function (e) {
      const h = window.innerHeight;
      if (e.clientY <= 60 || e.clientY >= h - 60) laws.onEdgeReach();
    });
    // Reading inputs feed onPageTurn (no-op for visibility) so intent is explicit.
    // Real event (confirmed via recon, reader_nav.js:433 handleRelocate): 'reader:relocated' —
    // fired on every navigation (page turn, chapter change, TOC/search jump). There is no
    // 'nav:pageTurn' event in this codebase; this is the correct analog. No-op-safe if the
    // bus isn't present yet (bookless/standalone probe contexts).
    if (bus && typeof bus.on === 'function') {
      bus.on('reader:relocated', function () { laws.onPageTurn(); });
    }
    // H key = explicit toggle.
    document.addEventListener('keydown', function (e) {
      if (e.key === 'h' || e.key === 'H') laws.toggleExplicit();
    });

    pill.addEventListener('click', function (e) {
      const btn = e.target.closest('.room-pill-btn'); if (!btn) return;
      laws.poke();
      window.__roomOpenPanel && window.__roomOpenPanel(btn.dataset.act, btn);
    });

    // Show/hide the audiobook button per pairing state (set by QML via bridge shim).
    window.__roomSetHasAudiobook = function (has) {
      const b = pill.querySelector('[data-act="listen-audio"]');
      if (b) b.toggleAttribute('hidden', !has);
    };
  } catch (err) {
    // The pill layer must never throw and take down reader boot. Old chrome
    // still works even if the pill fails to mount.
    try { console.error('[room-pill] mount failed:', err); } catch (e2) {}
  }
})();
