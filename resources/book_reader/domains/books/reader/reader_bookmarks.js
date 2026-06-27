// BUILD_OVERHAUL: Bookmark functionality extracted from controller.js
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  // ── loadBookmarks ─────────────────────────────────────────────
  async function loadBookmarks() {
    var state = RS.state;
    state.bookmarks = [];
    if (!state.book) return;
    try {
      var res = await Tanko.api.booksBookmarks.get(state.book.id);
      state.bookmarks = Array.isArray(res) ? res : [];
      state.bookmarks.sort(function (a, b) {
        return Number((b && (b.updatedAt || b.createdAt)) || 0) - Number((a && (a.updatedAt || a.createdAt)) || 0);
      });
    } catch (e) { state.bookmarks = []; }
    renderUtilBookmarks();
  }

  // ── buildBookmarkSnippet ──────────────────────────────────────
  async function buildBookmarkSnippet() {
    var state = RS.state;
    if (!state.engine || typeof state.engine.getLocator !== 'function') return 'Bookmark';
    try {
      var loc = await state.engine.getLocator();
      if (RS.isPdfOpen()) {
        var hint = (typeof state.engine.getPageHint === 'function') ? state.engine.getPageHint() : null;
        var page = Number((hint && hint.page) || (loc && loc.page) || (loc && loc.pageIndex) || 0);
        return page > 0 ? ('Page ' + page) : 'Bookmark';
      }
      var frac = Number(loc && loc.fraction);
      var pct = Number.isFinite(frac) ? Math.round(frac * 100) : 0;
      var els = RS.ensureEls();
      var chapter = (els.subtitle && els.subtitle.textContent && els.subtitle.textContent !== '\u2014') ? els.subtitle.textContent.trim() : '';
      if (chapter) return chapter + ' \u00b7 ' + pct + '%';
      return pct + '%';
    } catch (e) { return 'Bookmark'; }
  }

  // ── bmLocationKey ─────────────────────────────────────────────
  function bmLocationKey(loc) {
    if (!loc) return '';
    if (loc.cfi) return 'cfi:' + String(loc.cfi);
    if (loc.page) return 'page:' + String(loc.page);
    var f = Number(loc.fraction);
    if (Number.isFinite(f)) return 'frac:' + f.toFixed(6);
    return '';
  }

  // ── toggleBookmark ────────────────────────────────────────────
  async function toggleBookmark() {
    var state = RS.state;
    if (!state.open || !state.engine || !state.book) return;

    var locator = null;
    try { locator = await state.engine.getLocator(); } catch (e) { /* swallow */ }
    if (!locator) return;

    var key = bmLocationKey(locator);
    var existing = key ? null : null;
    if (key) {
      for (var i = 0; i < state.bookmarks.length; i++) {
        if (bmLocationKey(state.bookmarks[i].locator) === key) {
          existing = state.bookmarks[i];
          break;
        }
      }
    }

    if (existing) {
      try { await Tanko.api.booksBookmarks.delete(state.book.id, existing.id); } catch (e) { /* swallow */ }
      await loadBookmarks();
      RS.showToast('Bookmark removed');
    } else {
      var snippet = await buildBookmarkSnippet();
      try {
        await Tanko.api.booksBookmarks.save(state.book.id, { locator: locator, snippet: snippet, label: snippet });
      } catch (e) { /* swallow */ }
      await loadBookmarks();
      RS.showToast('Bookmark added');
    }
  }

  // ── goToBookmark ──────────────────────────────────────────────
  async function goToBookmark(bm) {
    var state = RS.state;
    if (!state.engine || typeof state.engine.goTo !== 'function') return;
    if (!bm || !bm.locator) return;
    try {
      await state.engine.goTo(bm.locator);
      await RS.saveProgress();
      bus.emit('nav:progress-sync');
    } catch (e) { /* swallow */ }
  }

  // ── renderUtilBookmarks ───────────────────────────────────────
  function renderUtilBookmarks() {
    var els = RS.ensureEls();
    var state = RS.state;
    var list = els.utilBookmarkList;
    if (!list) return;
    list.innerHTML = '';

    if (!state.bookmarks.length) {
      var empty = document.createElement('div');
      empty.className = 'booksReaderBookmarkItem';
      empty.textContent = 'No bookmarks yet. Press B or click the star to add one.';
      list.appendChild(empty);
    } else {
      for (var i = 0; i < state.bookmarks.length; i++) {
        (function (bm) {
          var row = document.createElement('div');
          row.className = 'booksReaderBookmarkItem';

          var goBtn = document.createElement('button');
          goBtn.className = 'bmGo';
          goBtn.textContent = bm.snippet || bm.label || 'Bookmark';
          goBtn.addEventListener('click', function () {
            goToBookmark(bm).catch(function () {});
          });
          row.appendChild(goBtn);

          var delBtn = document.createElement('button');
          delBtn.className = 'bmDel';
          delBtn.textContent = '\u2715';
          delBtn.title = 'Remove bookmark';
          delBtn.addEventListener('click', function (e) {
            e.stopPropagation();
            Tanko.api.booksBookmarks.delete(state.book.id, bm.id).catch(function () {}).then(function () {
              loadBookmarks().catch(function () {});
            });
          });
          row.appendChild(delBtn);

          list.appendChild(row);
        })(state.bookmarks[i]);
      }
    }

    // Update toggle label: filled/unfilled star based on current location
    updateToggleStar();
  }

  // ── updateToggleStar (helper) ─────────────────────────────────
  function updateToggleStar() {
    var els = RS.ensureEls();
    var state = RS.state;
    if (!els.utilBookmarkToggle) return;
    if (!state.engine || typeof state.engine.getLocator !== 'function') {
      els.utilBookmarkToggle.textContent = '\u2606'; // unfilled star
      return;
    }
    state.engine.getLocator().then(function (loc) {
      var key = bmLocationKey(loc);
      var found = false;
      if (key) {
        for (var i = 0; i < state.bookmarks.length; i++) {
          if (bmLocationKey(state.bookmarks[i].locator) === key) {
            found = true;
            break;
          }
        }
      }
      els.utilBookmarkToggle.textContent = found ? '\u2605' : '\u2606'; // filled or unfilled star
    }).catch(function () {
      els.utilBookmarkToggle.textContent = '\u2606';
    });
  }

  // ── bind ──────────────────────────────────────────────────────
  function bind() {
    var els = RS.ensureEls();

    if (els.utilBookmarkToggle) {
      els.utilBookmarkToggle.addEventListener('click', function () {
        toggleBookmark().catch(function () {});
      });
    }

    bus.on('bookmark:toggle', function () {
      toggleBookmark().catch(function () {});
    });

    bus.on('bookmark:goto', function (bm) {
      goToBookmark(bm).catch(function () {});
    });

    bus.on('bookmarks:render', function () {
      renderUtilBookmarks();
    });

    // FIX_AUDIT: Keep bookmark toggle state synced with live reading position.
    bus.on('reader:relocated', function () {
      updateToggleStar();
    });

    bus.on('sidebar:tab-changed', function (tab) {
      if (tab === 'bookmarks') renderUtilBookmarks();
    });
  }

  // ── Lifecycle ─────────────────────────────────────────────────
  function onOpen() {
    loadBookmarks().catch(function () {});
  }

  function onClose() {
    RS.state.bookmarks = [];
  }

  // ── Export ────────────────────────────────────────────────────
  window.booksReaderBookmarks = {
    bind: bind,
    loadBookmarks: loadBookmarks,
    toggleBookmark: toggleBookmark,
    goToBookmark: goToBookmark,
    renderUtilBookmarks: renderUtilBookmarks,
    onOpen: onOpen,
    onClose: onClose,
  };
})();
