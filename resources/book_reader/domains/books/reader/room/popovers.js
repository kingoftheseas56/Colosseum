// popovers.js — the one summoning grammar: a glass card off the tapped pill button.
// Loads BEFORE panel_contents.js in the phase5 boot group (ebook_reader.html) so
// window.__roomRegisterPanel exists when panel_contents.js registers itself at
// load. pill.js only calls window.__roomOpenPanel on click (async, post-boot),
// so its position relative to these two doesn't matter — but it's kept last so
// the pill is the final thing wired.
(function () {
  'use strict';

  var registry = {};      // act -> { render(container), onOpen?, onClose? }
  var open = null;        // { el, act } | null
  var backdropEl = null;

  // laws() is created by pill.js (window.__roomLaws = createChromeLaws(...)).
  // Late-bind on every call — never capture at load, since popovers.js loads
  // BEFORE pill.js and window.__roomLaws would still be undefined at IIFE time.
  function laws() { return window.__roomLaws; }

  function view() { return document.getElementById('booksReaderView'); }

  function ensureBackdrop() {
    if (backdropEl && backdropEl.isConnected) return backdropEl;
    var v = view();
    if (!v) return null;
    backdropEl = document.getElementById('roomBackdrop');
    if (!backdropEl) {
      backdropEl = document.createElement('div');
      backdropEl.id = 'roomBackdrop';
      backdropEl.className = 'room-backdrop hidden';
      backdropEl.addEventListener('click', function () { window.__roomClosePanels(); });
      v.appendChild(backdropEl);
    }
    return backdropEl;
  }

  function showBackdrop() {
    var b = ensureBackdrop();
    if (b) b.classList.remove('hidden');
  }

  function hideBackdrop() {
    if (backdropEl) backdropEl.classList.add('hidden');
  }

  // Clamp under the anchor button; 8px margins on every viewport edge.
  function positionUnder(el, anchorBtn) {
    if (!anchorBtn || typeof anchorBtn.getBoundingClientRect !== 'function') {
      el.style.top = '48px';
      el.style.left = '8px';
      return;
    }
    var r = anchorBtn.getBoundingClientRect();
    var margin = 8;
    var width = el.offsetWidth || 220;
    var height = el.offsetHeight || 0;

    var left = r.left;
    left = Math.max(margin, Math.min(left, window.innerWidth - width - margin));

    var top = r.bottom + margin;
    if (height && top + height > window.innerHeight - margin) {
      top = Math.max(margin, window.innerHeight - margin - height);
    }

    el.style.left = left + 'px';
    el.style.top = top + 'px';
  }

  window.__roomRegisterPanel = function (act, def) {
    if (!act || !def || typeof def.render !== 'function') return;
    registry[act] = def;
  };

  window.__roomOpenPanel = function (act, anchorBtn) {
    // Same-act toggle: opening an already-open act closes it instead.
    if (open && open.act === act) {
      window.__roomClosePanels();
      return;
    }
    // Only one popover at a time — close whatever else is open first.
    if (open) window.__roomClosePanels();

    var def = registry[act];
    var v = view();
    if (!def || !v) return;

    var el = document.createElement('div');
    el.className = 'room-popover';
    el.setAttribute('data-act', act);
    v.appendChild(el);

    // Quality review, fix 3: a broken panel must fail INVISIBLE — bail out
    // BEFORE showBackdrop/setPopoverOpen(true), else the user gets a blank
    // frozen card with a backdrop swallowing every click.
    try {
      def.render(el);
    } catch (e) {
      try { console.error('[room-popover] render failed:', act, e); } catch (e2) {}
      if (el.parentNode) el.parentNode.removeChild(el);
      return;
    }

    positionUnder(el, anchorBtn);
    showBackdrop();

    var L = laws();
    if (L && typeof L.setPopoverOpen === 'function') L.setPopoverOpen(true);

    open = { el: el, act: act };
    if (typeof def.onOpen === 'function') { try { def.onOpen(); } catch (e) {} }
  };

  window.__roomClosePanels = function () {
    if (!open) return;
    var def = registry[open.act];
    if (def && typeof def.onClose === 'function') { try { def.onClose(); } catch (e) {} }
    if (open.el && open.el.parentNode) open.el.parentNode.removeChild(open.el);
    open = null;
    hideBackdrop();

    var L = laws();
    if (L && typeof L.setPopoverOpen === 'function') L.setPopoverOpen(false);
  };

  // Escape closes the popover, not the reader. reader_keyboard.js binds its
  // own document keydown listener (bubble phase) in boot phase 3, well before
  // this file loads in phase 5 — if we also bound on bubble, its Escape chain
  // would run FIRST (registration order) and, seeing no room-popover state,
  // fall through to closing the whole reader. So this listener is capture-phase
  // (`true`): it always runs before any bubble-phase listener regardless of
  // load order, and only when a room popover is actually open does it
  // preventDefault + stopPropagation — otherwise it's a no-op passthrough and
  // reader_keyboard's own Escape chain (goto/annot/dict/overlays/sidebar/close)
  // is untouched.
  document.addEventListener('keydown', function (e) {
    if (e.key !== 'Escape' || !open) return;
    e.preventDefault();
    e.stopPropagation();
    window.__roomClosePanels();
  }, true);
})();
