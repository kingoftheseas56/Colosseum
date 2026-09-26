// core/focus.js — the ONE focus engine (CONTRACT §4.4). Keyboard, D-pad and controller all arrive as
// arrow keys / Enter / Escape. Surfaces only mark elements with data-focus (+ data-key on items); they
// never handle arrow keys themselves.
//
//   data-focus            element can take focus
//   data-key="<Item.key>" focus is restored to the same item after a section re-renders
//   data-focus-scope      while an element with this attribute is shown, focus stays inside it (overlays)
(function (CW) {
  'use strict';

  const DIRS = { ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down' };
  let enabled = true;
  let backHandler = () => false;

  function scopeRoot() {
    const scopes = [...document.querySelectorAll('[data-focus-scope]')].filter(visible);
    return scopes.length ? scopes[scopes.length - 1] : document;
  }

  function visible(el) {
    if (!el.isConnected || el.closest('[hidden],[inert]')) return false;
    const r = el.getClientRects();
    return r.length > 0 && r[0].width > 0 && r[0].height > 0;
  }

  function focusables() {
    return [...scopeRoot().querySelectorAll('[data-focus]')].filter(visible);
  }

  // TV rules: a candidate must lie in the pressed direction. Left/Right stay in the same row when any
  // candidate overlaps it vertically. Then the NEAREST row/column wins (smallest edge gap, within a band),
  // and inside that row the one best aligned with the current element.
  const ROW_BAND = 32;

  function measure(a, b, dir) {
    const acx = a.left + a.width / 2, acy = a.top + a.height / 2;
    const bcx = b.left + b.width / 2, bcy = b.top + b.height / 2;
    const horizontal = dir === 'right' || dir === 'left';
    const ahead = horizontal ? (dir === 'right' ? bcx - acx : acx - bcx) : (dir === 'down' ? bcy - acy : acy - bcy);
    if (ahead <= 1) return null;
    const gap = horizontal ? (dir === 'right' ? b.left - a.right : a.left - b.right)
                           : (dir === 'down' ? b.top - a.bottom : a.top - b.bottom);
    return {
      primary: Math.max(0, gap),
      ortho: horizontal ? Math.abs(bcy - acy) : Math.abs(bcx - acx),
      sameRow: horizontal && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 0
    };
  }

  function place(el) {
    if (!el) return;
    el.focus({ preventScroll: true });
    el.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'smooth' });
  }

  function move(dir) {
    const all = focusables();
    if (!all.length) return;
    const cur = document.activeElement;
    if (!cur || cur === document.body || !all.includes(cur)) { place(all[0]); return; }
    const a = cur.getBoundingClientRect();
    let cands = [];
    for (const el of all) {
      if (el === cur) continue;
      const m = measure(a, el.getBoundingClientRect(), dir);
      if (m) cands.push({ el, ...m });
    }
    if (!cands.length) return;
    if (dir === 'left' || dir === 'right') {
      cands = cands.filter(c => c.sameRow);   // end of a row: stay put, never jump rows sideways
      if (!cands.length) return;
    }
    const nearest = Math.min(...cands.map(c => c.primary));
    const row = cands.filter(c => c.primary <= nearest + ROW_BAND);
    row.sort((x, y) => x.ortho - y.ortho || x.primary - y.primary);
    place(row[0].el);
  }

  function isTextInput(el) {
    return el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
  }

  document.addEventListener('keydown', e => {
    document.body.classList.add('keys');
    document.body.classList.remove('mouse');
    if (!enabled) { e.preventDefault(); return; }
    const t = document.activeElement;
    const dir = DIRS[e.key];
    if (dir) {
      if (isTextInput(t) && (dir === 'left' || dir === 'right')) return;
      e.preventDefault();
      // A component may own arrows inside it (e.g. the carousel pages on Left/Right): it listens for
      // 'cw-arrow' on its [data-arrows] host and calls preventDefault() when it handled the key.
      const host = t && t.closest && t.closest('[data-arrows]');
      if (host) {
        const ev = new CustomEvent('cw-arrow', { detail: dir, cancelable: true });
        host.dispatchEvent(ev);
        if (ev.defaultPrevented) return;
      }
      move(dir);
    } else if (e.key === 'Escape' || (e.key === 'Backspace' && !isTextInput(t))) {
      e.preventDefault();
      const scope = scopeRoot();                     // an open menu/overlay closes first (§15.1)
      if (scope !== document && typeof scope.__close === 'function') scope.__close();
      else backHandler();
    } else if ((e.key === 'Enter' || e.key === ' ') && t && t.matches('[data-focus]') && !isTextInput(t)
               && t.tagName !== 'BUTTON') {
      e.preventDefault();
      t.click();
    }
  }, true);

  document.addEventListener('mousemove', () => {
    document.body.classList.add('mouse');
    document.body.classList.remove('keys');
  }, { passive: true });

  CW.focus = {
    /** Run a DOM replacement and keep focus on the same item (by data-key) or the same slot. */
    preserve(root, replace) {
      const active = document.activeElement;
      const inside = active && root.contains(active);
      const key = inside ? active.getAttribute('data-key') : null;
      const slot = inside ? [...root.querySelectorAll('[data-focus]')].indexOf(active) : -1;
      replace();
      if (!inside) return;
      let target = key ? root.querySelector(`[data-key="${CSS.escape(key)}"]`) : null;
      if (!target && slot >= 0) {
        const list = root.querySelectorAll('[data-focus]');
        target = list[Math.min(slot, list.length - 1)];
      }
      if (target) target.focus({ preventScroll: true });
    },
    first(root) { const el = (root || document).querySelector('[data-focus]'); if (el) place(el); },
    setEnabled(v) { enabled = !!v; },
    onBack(fn) { backHandler = fn; },
    move
  };
})(window.CW = window.CW || {});
