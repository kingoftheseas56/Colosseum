// pill.js — mounts the pill, binds chrome laws to paint, dispatches button acts.
// House rule: SVG glyphs (grayscale + gold accent), never emoji/unicode in prod —
// the glyph <use> refs are filled by frontend-design; this file owns behavior only.
//
// Input routing contract (quality review, Phase 2): the reading surface is an
// IFRAME, so parent-document listeners never see events that originate inside
// it. Anything the pill needs from inside the book arrives via the reader's own
// forwarding paths instead:
//   - H key: handled in reader_keyboard.js handleKeyEvent (the single decision
//     tree both the parent document AND engine_foliate's iframe listener call).
//     pill.js deliberately has NO keydown listener of its own.
//   - Edge-reach from inside the book: reader_core.js onEngineUserActivity
//     rebroadcasts iframe activity as bus 'reader:activity' with PARENT-space
//     coords; we run the same edge test on those.
//   - Edge-reach over parent chrome/margins: our own document mousemove
//     (parent-space already) — kept alongside the bus feed.
(function () {
  'use strict';
  try {
    const bus = window.booksReaderBus;
    const laws = window.__roomLaws = window.createChromeLaws
      ? window.createChromeLaws({ /* real DOM clock via defaults */ })
      : null;

    const pill = document.getElementById('roomPill');
    const view = document.getElementById('booksReaderView');
    if (!pill || !view || !laws) {
      try { console.warn('[room] pill prerequisites missing', { pill: !!pill, view: !!view, laws: !!laws }); } catch (e2) {}
      return;
    }

    function paint() {
      view.setAttribute('data-chrome', laws.shown ? 'shown' : 'hidden');
    }
    // Repaint on every state transition: the machine is authoritative, paint
    // mirrors `.shown`. Next frame is registered BEFORE paint so a paint throw
    // can never kill the loop permanently.
    let last = null;
    (function loop() {
      requestAnimationFrame(loop);
      if (laws.shown !== last) { last = laws.shown; paint(); }
    })();

    // Edge band: one zone, owned by reader_core (REVEAL_ZONE = 48px top/bottom).
    // Read from the controller export so the pill and the HUD can't drift apart.
    const EDGE = (window.booksReaderController && Number(window.booksReaderController.REVEAL_ZONE)) || 48;
    function edgeReachAt(clientY) {
      if (!Number.isFinite(Number(clientY))) return;
      if (clientY <= EDGE || clientY >= window.innerHeight - EDGE) laws.onEdgeReach();
    }
    // Feed 1: parent-space mousemove (cursor over parent chrome/margins).
    document.addEventListener('mousemove', function (e) { edgeReachAt(e.clientY); });
    // Feed 2: iframe-origin activity, already translated to parent space by
    // reader_core's _activityClientX/Y before the bus emit.
    if (bus && typeof bus.on === 'function') {
      bus.on('reader:activity', function (detail) {
        if (detail) edgeReachAt(detail.clientY);
      });
      // Reading inputs feed onPageTurn (no-op for visibility) so intent is
      // explicit. Real event (recon, reader_nav.js handleRelocate):
      // 'reader:relocated' fires on every navigation — page turn, chapter
      // change, TOC/search jump. No 'nav:pageTurn' exists in this codebase.
      bus.on('reader:relocated', function () { laws.onPageTurn(); });
    }

    pill.addEventListener('click', function (e) {
      const btn = e.target.closest('.room-pill-btn'); if (!btn) return;
      laws.poke();
      const act = btn.dataset.act;
      // Listen buttons don't open a popover — they mount their own docked
      // strip (tts_strip.js / audiobook strip, Task 4.4). Both handlers are
      // guarded no-ops until their owning task lands.
      if (act === 'listen-tts') { window.__roomOpenTts && window.__roomOpenTts(); return; }
      if (act === 'listen-audio') { window.__roomRequestAudiobook && window.__roomRequestAudiobook(); return; }
      window.__roomOpenPanel && window.__roomOpenPanel(act, btn);
    });

    // Show/hide the audiobook button per pairing state (set by QML via bridge shim).
    window.__roomSetHasAudiobook = function (has) {
      const b = pill.querySelector('[data-act="listen-audio"]');
      if (b) b.toggleAttribute('hidden', !has);
    };

    // Ask QML for the audiobook strip (Task 4.4 catches BookBridge::listenRequested).
    // Pure signal — no payload; QML already knows the open book's pairKey. Surface is
    // read at CALL time (__ebookNav appears only after the async QWebChannel init).
    window.__roomRequestAudiobook = function () {
      try {
        if (window.__ebookNav && typeof window.__ebookNav.requestListen === 'function')
          window.__ebookNav.requestListen();
      } catch (e) {}
    };
  } catch (err) {
    // The pill layer must never throw and take down reader boot. Old chrome
    // still works even if the pill fails to mount.
    try { console.error('[room-pill] mount failed:', err); } catch (e2) {}
  }
})();
