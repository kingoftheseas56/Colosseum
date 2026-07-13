// panel_contents.js — the "contents" popover: table of contents off the pill.
//
// Recon (real names, not the plan's guesses):
//   - TOC data arrives on bus event 'toc:updated' (reader_toc.js renderToc()),
//     payload = state.tocItems — already a FLAT array (engine-flattened), each
//     item shaped { label|title, href, depth|level, spineIndex, dest }. No tree
//     to flatten.
//   - reader_toc.js's booksReaderToc.onOpen() (called for every module on every
//     book open, reader_core.js ~L800) triggers renderToc() automatically — so
//     toc:updated fires on its own once a book is open; this file doesn't need
//     to poke anything to get data flowing.
//   - There is no window.booksReaderNav.goToHref. The real navigation path is
//     bus.emit('toc:navigate', item) -> reader_toc.js navigateToTocItem() ->
//     state.engine.goTo(target) + saveProgress + 'nav:progress-sync'. Routing
//     through the bus (not calling engine.goTo directly) keeps save/sync intact.
//   - Current chapter isn't in a `currentHref` field. It's tracked from the
//     'reader:relocated' bus event's detail.tocItem.href (same source
//     reader_toc.js's own updateTocActive uses for the OLD sidebar list).
(function () {
  'use strict';

  var bus = window.booksReaderBus;
  var items = [];
  var currentHref = '';
  var listEl = null; // live reference so relocate/update events can refresh in place

  function normalizeHref(href) {
    var h = String(href || '');
    h = h.replace(/^\.\//, '');
    var hashIdx = h.indexOf('#');
    if (hashIdx >= 0) h = h.substring(0, hashIdx);
    try { h = decodeURIComponent(h); } catch (e) {}
    return h.toLowerCase().trim();
  }

  function navigate(item) {
    if (bus && typeof bus.emit === 'function') bus.emit('toc:navigate', item);
    window.__roomClosePanels && window.__roomClosePanels();
  }

  function renderRows() {
    if (!listEl) return;
    listEl.innerHTML = '';

    if (!items.length) {
      var loading = document.createElement('div');
      loading.className = 'room-toc-row room-toc-row--empty';
      loading.textContent = 'Loading…';
      listEl.appendChild(loading);
      return;
    }

    for (var i = 0; i < items.length; i++) {
      (function (item) {
        var row = document.createElement('button');
        row.type = 'button';
        row.className = 'room-toc-row';
        var depth = Number(item.depth || item.level || 0);
        if (depth > 0) row.style.paddingLeft = (12 + depth * 12) + 'px';
        row.textContent = String(item.label || item.title || 'Untitled');

        var href = normalizeHref(item.href || '');
        if (href && currentHref && href === currentHref) row.classList.add('is-current');

        row.addEventListener('click', function () { navigate(item); });
        listEl.appendChild(row);
      })(items[i]);
    }
  }

  // Cache TOC + current-chapter state at load time (not just while the panel
  // is open) so the popover has data the instant it's opened, even if the
  // book finished loading before the pill was ever clicked.
  // Boot-sliver seed (quality review, fix 4): TOC extraction can beat the
  // phase-5 script load — if reader_toc.js already emitted toc:updated before
  // this file existed, the event is gone but the payload lives on in
  // booksReaderState.state.tocItems. Read it once at load; the listeners
  // below take over from there.
  try {
    var RS = window.booksReaderState;
    if (RS && RS.state && Array.isArray(RS.state.tocItems) && RS.state.tocItems.length) {
      items = RS.state.tocItems;
    }
  } catch (e) {}

  if (bus && typeof bus.on === 'function') {
    bus.on('toc:updated', function (incoming) {
      items = Array.isArray(incoming) ? incoming : [];
      // Quality review, fix 1: new TOC = new book. EPUB hrefs are zip-internal
      // filenames that collide across books (every other epub has a
      // ch2.xhtml) — a stale currentHref would paint a confident gold
      // highlight that's a lie until the first relocate. Reset it; the
      // highlight comes back on the book's first relocate.
      currentHref = '';
      renderRows();
    });
    bus.on('reader:relocated', function (detail) {
      if (detail && detail.tocItem && detail.tocItem.href) {
        var href = normalizeHref(detail.tocItem.href);
        // Quality review, fix 2: relocate fires on EVERY page turn, not just
        // chapter changes — re-rendering each time wipes innerHTML and
        // collapses the open popover's scrollTop. Only re-render on genuine
        // chapter change.
        if (href === currentHref) return;
        currentHref = href;
        renderRows();
      }
    });
  }

  window.__roomRegisterPanel && window.__roomRegisterPanel('contents', {
    render: function (container) {
      container.innerHTML = '';

      var title = document.createElement('div');
      title.className = 'room-panel-title';
      title.textContent = 'Contents';
      container.appendChild(title);

      listEl = document.createElement('div');
      listEl.className = 'room-toc';
      container.appendChild(listEl);

      renderRows();
    },
    onClose: function () {
      listEl = null;
    },
  });
})();
