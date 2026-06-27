// BUILD_OVERHAUL: TOC (table of contents) functionality extracted from controller
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  var tocRenderSeq = 0;

  // ── normalizeTocHref ──────────────────────────────────────────
  function normalizeTocHref(href) {
    var h = String(href || '');
    h = h.replace(/^\.\//, '');
    var hashIdx = h.indexOf('#');
    if (hashIdx >= 0) h = h.substring(0, hashIdx);
    try { h = decodeURIComponent(h); } catch (e) {}
    return h.toLowerCase().trim();
  }

  // ── renderToc ─────────────────────────────────────────────────
  async function renderToc() {
    var mySeq = ++tocRenderSeq;
    var state = RS.state;
    console.log('[TOC] renderToc called, engine?', !!state.engine, 'hasToc?', !!(state.engine && typeof state.engine.getToc === 'function'));
    if (!state.engine || typeof state.engine.getToc !== 'function') return;
    var toc = [];
    try { toc = await state.engine.getToc(); } catch (e) { console.warn('[TOC] getToc error:', e); toc = []; }
    state.tocItems = Array.isArray(toc) ? toc : [];
    console.log('[TOC] got', state.tocItems.length, 'items');
    bus.emit('toc:updated', state.tocItems);
    renderTocList('');
  }

  // ── renderTocList ─────────────────────────────────────────────
  function renderTocList(query) {
    var els = RS.ensureEls();
    if (!els.tocList) return;
    els.tocList.innerHTML = '';
    var items = RS.state.tocItems;
    if (!items || !items.length) {
      els.tocList.innerHTML = '<div class="volNavEmpty">No chapters found</div>';
      return;
    }
    var q = String(query || '').toLowerCase().trim();
    var readState = RS.state.chapterReadState || {};
    for (var i = 0; i < items.length; i++) {
      var item = items[i];
      var label = String(item.label || item.title || 'Chapter ' + (i + 1));
      if (q && label.toLowerCase().indexOf(q) < 0) continue;
      var btn = document.createElement('button');
      btn.className = 'br-list-item volNavItem';
      btn.type = 'button';
      btn.title = label;
      var depth = Number(item.depth || item.level || 0);
      if (depth > 0) btn.style.paddingLeft = (10 + depth * 16) + 'px';
      btn.dataset.tocIdx = String(i);
      btn.dataset.href = String(item.href || '');
      if (item && item.dest != null) { try { btn.dataset.destJson = JSON.stringify(item.dest); } catch (e) {} }
      btn.dataset.spineIndex = String(item.spineIndex >= 0 ? item.spineIndex : '');

      // BUILD_CHAP_TOC: read state indicator
      var spIdx = item.spineIndex;
      var readFrac = (spIdx >= 0 && readState[spIdx]) ? readState[spIdx] : 0;
      var isRead = readFrac > 0.95;
      var isPartial = readFrac > 0.02 && !isRead;

      // Status dot
      var dot = document.createElement('span');
      dot.className = 'br-toc-status';
      if (isRead) dot.classList.add('br-toc-read');
      else if (isPartial) dot.classList.add('br-toc-partial');

      // Label
      var labelSpan = document.createElement('span');
      labelSpan.className = 'br-toc-label';
      labelSpan.textContent = label;

      // Progress mini bar
      var prog = document.createElement('div');
      prog.className = 'br-toc-progress';
      var progFill = document.createElement('div');
      progFill.className = 'br-toc-progress-fill';
      progFill.style.width = (RS.clamp01(readFrac) * 100).toFixed(1) + '%';
      prog.appendChild(progFill);

      btn.appendChild(dot);
      btn.appendChild(labelSpan);
      btn.appendChild(prog);

      btn.addEventListener('click', (function (tocItem, idx) {
        return function () { navigateToTocItem(tocItem, idx); };
      })(item, i));
      els.tocList.appendChild(btn);
    }
  }

  // ── navigateToTocItem ─────────────────────────────────────────
  async function navigateToTocItem(itemOrHref, idx) {
    var state = RS.state;
    if (!state.engine || typeof state.engine.goTo !== 'function') return;
    var target = itemOrHref;
    if (typeof itemOrHref === 'string') target = { href: itemOrHref };
    else if (!target || typeof target !== 'object') target = {};
    try { await state.engine.goTo(target); } catch (e) { /* swallow */ }
    try { await RS.saveProgress(); } catch (e) { /* swallow */ }
    bus.emit('nav:progress-sync');
  }

  // ── updateTocActive ───────────────────────────────────────────
  function updateTocActive(detail) {
    if (!detail) return;
    var els = RS.ensureEls();
    var tocHref = '';
    if (detail.tocItem && detail.tocItem.href) {
      tocHref = normalizeTocHref(detail.tocItem.href);
    }
    // Update subtitle
    var chapterLabel = '';
    if (detail.tocItem) chapterLabel = String(detail.tocItem.label || detail.tocItem.title || '');
    if (els.subtitle) els.subtitle.textContent = chapterLabel || '\u2014';

    // Highlight active TOC item
    if (!els.tocList) return;
    var items = els.tocList.querySelectorAll('.volNavItem');
    var activeEl = null;
    for (var i = 0; i < items.length; i++) {
      var itemHref = normalizeTocHref(items[i].dataset.href || '');
      var isActive = tocHref && itemHref === tocHref;
      items[i].classList.toggle('active', isActive);
      if (isActive) activeEl = items[i];
    }
    // Auto-scroll active into view
    if (activeEl) {
      try { activeEl.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); } catch (e) {}
    }
  }

  // ── updateTocItemProgress ───────────────────────────────────
  // BUILD_CHAP_TOC: Live-update progress bar and status dot for a specific spine index
  function updateTocItemProgress(secIdx, frac) {
    var els = RS.ensureEls();
    if (!els.tocList) return;
    var items = els.tocList.querySelectorAll('.volNavItem');
    for (var i = 0; i < items.length; i++) {
      var spineIdx = parseInt(items[i].dataset.spineIndex, 10);
      if (spineIdx !== secIdx) continue;
      var fill = items[i].querySelector('.br-toc-progress-fill');
      if (fill) fill.style.width = (RS.clamp01(frac) * 100).toFixed(1) + '%';
      var dot = items[i].querySelector('.br-toc-status');
      if (dot) {
        dot.classList.toggle('br-toc-read', frac > 0.95);
        dot.classList.toggle('br-toc-partial', frac > 0.02 && frac <= 0.95);
      }
    }
  }

  // ── bind ──────────────────────────────────────────────────────
  function bind() {
    var els = RS.ensureEls();

    // TOC search input — filter list on typing
    if (els.tocSearch) {
      els.tocSearch.addEventListener('input', function () {
        renderTocList(els.tocSearch.value);
      });

      els.tocSearch.addEventListener('keydown', function (e) {
        if (e.key === 'Escape') {
          e.preventDefault();
          bus.emit('sidebar:close');
        }
        if (e.key === 'ArrowDown') {
          e.preventDefault();
          var first = els.tocList && els.tocList.querySelector('.volNavItem');
          if (first) first.focus();
        }
      });
    }

    // TOC list keyboard navigation
    if (els.tocList) {
      els.tocList.addEventListener('keydown', function (e) {
        var target = e.target;
        if (!target || !target.classList.contains('volNavItem')) return;

        if (e.key === 'ArrowDown') {
          e.preventDefault();
          var next = target.nextElementSibling;
          while (next && !next.classList.contains('volNavItem')) next = next.nextElementSibling;
          if (next) next.focus();
        } else if (e.key === 'ArrowUp') {
          e.preventDefault();
          var prev = target.previousElementSibling;
          while (prev && !prev.classList.contains('volNavItem')) prev = prev.previousElementSibling;
          if (prev) prev.focus();
          else if (els.tocSearch) els.tocSearch.focus();
        } else if (e.key === 'Enter') {
          e.preventDefault();
          target.click();
        } else if (e.key === 'Escape') {
          e.preventDefault();
          bus.emit('sidebar:close');
        }
      });
    }

    // Bus events
    bus.on('toc:render', function () { renderToc().catch(function () {}); });
    bus.on('toc:navigate', function (href, idx) { navigateToTocItem(href, idx).catch(function () {}); });
    bus.on('reader:relocated', function (detail) { updateTocActive(detail); });
    // BUILD_CHAP_TOC: live-update per-chapter progress in TOC
    bus.on('chapter:progress', function (secIdx, frac, total) {
      updateTocItemProgress(secIdx, frac);
    });
  }

  // ── onOpen / onClose ──────────────────────────────────────────
  function onOpen() {
    renderToc().catch(function () {});
  }

  function onClose() {
    RS.state.tocItems = [];
    bus.emit('toc:updated', []);
    var els = RS.ensureEls();
    if (els.tocList) els.tocList.innerHTML = '';
  }

  // ── Export ────────────────────────────────────────────────────
  window.booksReaderToc = {
    bind: bind,
    renderToc: renderToc,
    renderTocList: renderTocList,
    updateTocActive: updateTocActive,
    onOpen: onOpen,
    onClose: onClose,
  };
})();
