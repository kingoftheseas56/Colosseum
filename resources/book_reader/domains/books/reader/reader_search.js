// BUILD_OVERHAUL: In-book search functionality extracted from controller
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  var searchSeq = 0;

  // PATCH4: Search history + scope toggle + progress indicator (Readest-ish)
  var HISTORY_KEY = 'brSearchHistory:v1';
  var SEARCH_MATCH_CASE_KEY = 'books_searchMatchCase';
  var SEARCH_WHOLE_WORDS_KEY = 'books_searchWholeWords';
  function loadHistory() {
    try {
      var raw = localStorage.getItem(HISTORY_KEY);
      var arr = raw ? JSON.parse(raw) : [];
      return Array.isArray(arr) ? arr.slice(0, 30) : [];
    } catch (e) { return []; }
  }
  function saveHistory(arr) {
    try { localStorage.setItem(HISTORY_KEY, JSON.stringify((arr || []).slice(0, 30))); } catch (e) {}
  }
  function rememberQuery(q) {
    q = String(q || '').trim();
    if (!q) return;
    var h = loadHistory();
    var low = q.toLowerCase();
    h = h.filter(function (x) { return String(x || '').toLowerCase() !== low; });
    h.unshift(q);
    saveHistory(h);
  }
  function getScope() {
    var state = RS.state;
    if (!state.searchScope) state.searchScope = 'book';
    return state.searchScope === 'section' ? 'section' : 'book';
  }
  function setScope(scope) {
    var state = RS.state;
    state.searchScope = (scope === 'section') ? 'section' : 'book';
    try { localStorage.setItem('brSearchScope:v1', state.searchScope); } catch (e) {}
  }
  function loadScope() {
    try {
      var s = localStorage.getItem('brSearchScope:v1');
      if (s === 'section' || s === 'book') RS.state.searchScope = s;
    } catch (e) {}
  }

  function readSearchBool(key, fallback) {
    try {
      var raw = localStorage.getItem(key);
      if (raw == null || raw === '') return !!fallback;
      if (raw === '1' || raw === 'true') return true;
      if (raw === '0' || raw === 'false') return false;
    } catch (e) {}
    return !!fallback;
  }
  function getSearchMatchOptions() {
    var state = RS.state;
    if (typeof state.searchMatchCase !== 'boolean') state.searchMatchCase = readSearchBool(SEARCH_MATCH_CASE_KEY, false);
    if (typeof state.searchWholeWords !== 'boolean') state.searchWholeWords = readSearchBool(SEARCH_WHOLE_WORDS_KEY, false);
    return { matchCase: !!state.searchMatchCase, wholeWords: !!state.searchWholeWords };
  }
  function setSearchMatchOptions(patch) {
    var state = RS.state;
    var cur = getSearchMatchOptions();
    state.searchMatchCase = (patch && typeof patch.matchCase === 'boolean') ? patch.matchCase : cur.matchCase;
    state.searchWholeWords = (patch && typeof patch.wholeWords === 'boolean') ? patch.wholeWords : cur.wholeWords;
    try { localStorage.setItem(SEARCH_MATCH_CASE_KEY, state.searchMatchCase ? '1' : '0'); } catch (e) {}
    try { localStorage.setItem(SEARCH_WHOLE_WORDS_KEY, state.searchWholeWords ? '1' : '0'); } catch (e) {}
  }
  function updateSearchMatchToggleUi() {
    var els = RS.ensureEls();
    var root = els.overlaySearch;
    if (!root) return;
    var opts = getSearchMatchOptions();
    var mc = root.querySelector('.br-search-toggle[data-opt="matchCase"]');
    var ww = root.querySelector('.br-search-toggle[data-opt="wholeWords"]');
    if (mc) { mc.classList.toggle('is-active', !!opts.matchCase); mc.setAttribute('aria-pressed', opts.matchCase ? 'true' : 'false'); }
    if (ww) { ww.classList.toggle('is-active', !!opts.wholeWords); ww.setAttribute('aria-pressed', opts.wholeWords ? 'true' : 'false'); }
  }
  function ensureSearchMatchOptionUi() {
    var els = RS.ensureEls();
    if (!els.overlaySearch || !els.utilSearchInput) return;

    if (!document.getElementById('brSearchMatchOptionsStyle')) {
      var st3 = document.createElement('style');
      st3.id = 'brSearchMatchOptionsStyle';
      st3.textContent = [
        '#brOverlaySearch .br-search-row{display:flex;align-items:center;gap:6px;}',
        '#brOverlaySearch .br-search-input{flex:1 1 auto;min-width:0;}',
        '#brOverlaySearch .br-search-match-toggles{display:flex;gap:4px;align-items:center;}',
        '#brOverlaySearch .br-search-toggle{min-width:24px;height:24px;padding:0 6px;border-radius:4px;border:1px solid transparent;background:transparent;color:inherit;font-size:11px;font-weight:600;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;opacity:0.4;transition:opacity 0.15s,background 0.15s;}',
        '#brOverlaySearch .br-search-toggle:hover{opacity:0.7;background:rgba(255,255,255,0.06);}',
        '#brOverlaySearch .br-search-toggle.is-active{opacity:1;background:rgba(255,255,255,0.12);border-color:rgba(255,255,255,0.18);}',
      ].join('\n');
      document.head.appendChild(st3);
    }

    var row = null;
    try { row = els.utilSearchInput.closest ? els.utilSearchInput.closest('.br-search-row') : null; } catch (e) { row = null; }
    if (!row) row = els.utilSearchInput.parentNode;
    if (!row) return;

    var wrap = row.querySelector('.br-search-match-toggles');
    if (!wrap) {
      wrap = document.createElement('div');
      wrap.className = 'br-search-match-toggles';
      wrap.innerHTML = '' +
        '<button type="button" class="br-search-toggle" data-opt="matchCase" title="Case-sensitive match" aria-label="Case-sensitive match">Aa</button>' +
        '<button type="button" class="br-search-toggle" data-opt="wholeWords" title="Whole-word match" aria-label="Whole-word match">W</button>';
      if (els.utilSearchBtn && els.utilSearchBtn.parentNode === row) row.insertBefore(wrap, els.utilSearchBtn);
      else row.appendChild(wrap);
    }

    var btns = wrap.querySelectorAll('.br-search-toggle');
    for (var i = 0; i < btns.length; i++) {
      (function (btn) {
        if (btn._wired) return;
        btn._wired = true;
        btn.addEventListener('click', function () {
          var opts = getSearchMatchOptions();
          if (btn.dataset.opt === 'matchCase') setSearchMatchOptions({ matchCase: !opts.matchCase });
          if (btn.dataset.opt === 'wholeWords') setSearchMatchOptions({ wholeWords: !opts.wholeWords });
          updateSearchMatchToggleUi();
          var q = (els.utilSearchInput && els.utilSearchInput.value) ? String(els.utilSearchInput.value).trim() : '';
          if (q) { try { bus.emit('search:run', q); } catch (e) {} }
        });
      })(btns[i]);
    }

    updateSearchMatchToggleUi();
  }


  function ensureSearchResultsUi() {
    var els = RS.ensureEls();
    if (!els.overlaySearch) return { root: null };

    // One-time style injection
    if (!document.getElementById('brSearchResultsStyle')) {
      var st = document.createElement('style');
      st.id = 'brSearchResultsStyle';
      st.textContent = [
        '#brOverlaySearch .br-search-results{max-height:45vh;overflow:auto;border-top:1px solid rgba(255,255,255,0.08);padding:6px 12px;}',
        '#brOverlaySearch .br-search-group{margin:8px 0 4px 0;opacity:0.7;font-size:11px;font-weight:600;letter-spacing:0.3px;text-transform:uppercase;}',
        '#brOverlaySearch .br-search-item{display:block;width:100%;text-align:left;padding:6px 8px;border-radius:6px;border:none;background:transparent;margin:2px 0;cursor:pointer;transition:background 0.1s;}',
        '#brOverlaySearch .br-search-item:hover{background:rgba(255,255,255,0.06);}',
        '#brOverlaySearch .br-search-item.is-active{background:rgba(255,255,255,0.10);}',
        '#brOverlaySearch .br-search-excerpt{font-size:12px;line-height:1.4;opacity:0.9;}',
        '#brOverlaySearch .br-search-excerpt mark{background:rgba(255,255,255,0.18);color:inherit;padding:0 2px;border-radius:3px;}',
        '#brOverlaySearch .br-search-meta{font-size:10px;opacity:0.5;margin-top:2px;}',
      ].join('\n');
      document.head.appendChild(st);
    }

    var body = els.overlaySearch.querySelector('.br-overlay-body');
    if (!body) return { root: null };

    ensureSearchMatchOptionUi();

    var root = body.querySelector('.br-search-results');
    if (!root) {
      root = document.createElement('div');
      root.className = 'br-search-results';
      body.appendChild(root);
    }
    return { root: root };
  }

  function setProgressText() {
    // No-op: progress text removed in minimalist redesign
  }

  function fmtExcerpt(excerpt) {
    if (!excerpt || typeof excerpt !== 'object') return '';
    var pre = String(excerpt.pre || '');
    var match = String(excerpt.match || '');
    var post = String(excerpt.post || '');
    // Escape minimal
    pre = RS.escHtml(pre);
    match = RS.escHtml(match);
    post = RS.escHtml(post);
    return pre + '<mark>' + match + '</mark>' + post;
  }

  function renderResultsList() {
    var ui = ensureSearchResultsUi();
    if (!ui.root) return;

    var state = RS.state;
    var flat = Array.isArray(state.searchFlat) ? state.searchFlat : [];
    var groups = Array.isArray(state.searchGroups) ? state.searchGroups : [];

    ui.root.innerHTML = '';

    if (!flat.length) return;

    // Build an index map for active highlight
    var activeCfi = state.searchHits && state.searchHits[state.searchActiveIndex] ? String(state.searchHits[state.searchActiveIndex]) : '';

    // Prefer grouped display when available
    if (groups.length) {
      var idx = 0;
      groups.forEach(function (g) {
        var title = document.createElement('div');
        title.className = 'br-search-group';
        title.textContent = String(g.label || '');
        ui.root.appendChild(title);

        (g.subitems || []).forEach(function (it) {
          var cfi = String(it && it.cfi || '');
          var excerpt = it && it.excerpt ? it.excerpt : null;

          // Map to flat index (walk-forward; preserves order)
          var myIndex = -1;
          for (var k = idx; k < flat.length; k++) {
            if (String(flat[k].cfi || '') === cfi) { myIndex = k; idx = k + 1; break; }
          }
          if (myIndex < 0) return;

          var btn = document.createElement('button');
          btn.type = 'button';
          btn.className = 'br-search-item' + (cfi && cfi === activeCfi ? ' is-active' : '');
          btn.dataset.index = String(myIndex);
          btn.innerHTML = '<div class="br-search-excerpt">' + (fmtExcerpt(excerpt) || RS.escHtml('Match')) + '</div>' +
            '<div class="br-search-meta">Result ' + (myIndex + 1) + ' of ' + flat.length + '</div>';
          btn.addEventListener('click', function () {
            var i = Number(btn.dataset.index);
            if (!Number.isFinite(i)) return;
            jumpToIndex(i);
          });
          ui.root.appendChild(btn);
        });
      });
      return;
    }

    // Fallback: flat list
    flat.forEach(function (hit, i) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'br-search-item' + (hit && String(hit.cfi || '') === activeCfi ? ' is-active' : '');
      btn.dataset.index = String(i);
      btn.innerHTML = '<div class="br-search-excerpt">' + (fmtExcerpt(hit.excerpt) || RS.escHtml('Match')) + '</div>' +
        '<div class="br-search-meta">Result ' + (i + 1) + ' of ' + flat.length + '</div>';
      btn.addEventListener('click', function () {
        var ix = Number(btn.dataset.index);
        if (!Number.isFinite(ix)) return;
        jumpToIndex(ix);
      });
      ui.root.appendChild(btn);
    });
  }

  async function jumpToIndex(i) {
    var state = RS.state;
    if (!state.searchHits.length || !state.engine) return;
    state.searchActiveIndex = Math.max(0, Math.min(state.searchHits.length - 1, i));
    if (typeof state.engine.searchGoTo === 'function') {
      try { await state.engine.searchGoTo(state.searchActiveIndex); } catch (e) { /* swallow */ }
    }
    updateSearchUI();
    renderResultsList();
    setProgressText('');
    await RS.saveProgress();
    bus.emit('nav:progress-sync');
  }

  // ── resetSearchState ─────────────────────────────────────────
  function resetSearchState() {
    var state = RS.state;
    state.searchHits = [];
    state.searchActiveIndex = -1;
    state.searchGroups = [];
    state.searchFlat = [];
    if (state.engine && typeof state.engine.clearSearch === 'function') {
      try { state.engine.clearSearch(); } catch (e) { /* swallow */ }
    }
    updateSearchUI();
    renderResultsList();
    setProgressText('');
    var els = RS.ensureEls();
    if (els.utilSearchInput) els.utilSearchInput.value = '';
  }

  // ── updateSearchUI ───────────────────────────────────────────
  function updateSearchUI() {
    var els = RS.ensureEls();
    var state = RS.state;
    var hasHits = state.searchHits.length > 0;

    if (els.utilSearchPrev) els.utilSearchPrev.disabled = !hasHits;
    if (els.utilSearchNext) els.utilSearchNext.disabled = !hasHits;

    if (els.utilSearchCount) {
      if (hasHits) {
        els.utilSearchCount.textContent = (state.searchActiveIndex + 1) + '/' + state.searchHits.length;
      } else {
        var query = els.utilSearchInput ? els.utilSearchInput.value.trim() : '';
        els.utilSearchCount.textContent = query ? 'No matches for \u201c' + query + '\u201d' : '';
      }
    }
  }

  // ── searchNow ────────────────────────────────────────────────
  async function searchNow(queryOverride) {
    var mySeq = ++searchSeq;
    var els = RS.ensureEls();
    var state = RS.state;
    var q = String(queryOverride || (els.utilSearchInput && els.utilSearchInput.value) || '').trim();

    if (!q) {
      resetSearchState();
      return;
    }
    if (!state.engine || typeof state.engine.search !== 'function') return;

    RS.setStatus('Searching...', true);
    var res = null;
    var matchOpts = getSearchMatchOptions();
    try { res = await state.engine.search(q, matchOpts); } catch (e) { res = null; }
    if (mySeq !== searchSeq) return;

    var count = Number(res && res.count || 0);
    var hits = (res && Array.isArray(res.hits)) ? res.hits : [];
    state.searchHits = hits;
    state.searchGroups = (res && Array.isArray(res.groups)) ? res.groups : [];
    state.searchFlat = (res && Array.isArray(res.flat)) ? res.flat : hits.map(function (cfi) { return { cfi: String(cfi || ''), excerpt: null, label: '' }; });
    state.searchActiveIndex = hits.length > 0 ? 0 : -1;

    if (hits.length > 0 && state.engine && typeof state.engine.searchGoTo === 'function') {
      try { await state.engine.searchGoTo(0); } catch (e) { /* swallow */ }
    }

    if (count > 0) {
      RS.setStatus(count + ' match' + (count !== 1 ? 'es' : ''));
      setProgressText(count + ' match' + (count !== 1 ? 'es' : ''));
    } else {
      RS.setStatus('No matches');
      setProgressText('No matches');
    }
    updateSearchUI();
    renderResultsList();
    setProgressText('');
  }

  // ── searchPrev ───────────────────────────────────────────────
  async function searchPrev() {
    var state = RS.state;
    if (!state.searchHits.length || !state.engine) return;
    state.searchActiveIndex = (state.searchActiveIndex - 1 + state.searchHits.length) % state.searchHits.length;
    if (typeof state.engine.searchGoTo === 'function') {
      try { await state.engine.searchGoTo(state.searchActiveIndex); } catch (e) { /* swallow */ }
    }
    updateSearchUI();
    renderResultsList();
    setProgressText('');
    await RS.saveProgress();
    bus.emit('nav:progress-sync');
  }

  // ── searchNext ───────────────────────────────────────────────
  async function searchNext() {
    var state = RS.state;
    if (!state.searchHits.length || !state.engine) return;
    state.searchActiveIndex = (state.searchActiveIndex + 1) % state.searchHits.length;
    if (typeof state.engine.searchGoTo === 'function') {
      try { await state.engine.searchGoTo(state.searchActiveIndex); } catch (e) { /* swallow */ }
    }
    updateSearchUI();
    renderResultsList();
    setProgressText('');
    await RS.saveProgress();
    bus.emit('nav:progress-sync');
  }

  // ── clearSearch ──────────────────────────────────────────────
  function clearSearch() {
    resetSearchState();
    RS.setStatus('');
  }

  // ── bind ─────────────────────────────────────────────────────
  function bind() {
    var els = RS.ensureEls();

    ensureSearchMatchOptionUi();

    if (els.utilSearchBtn) {
      els.utilSearchBtn.addEventListener('click', function () { searchNow().catch(function () {}); });
    }

    if (els.utilSearchInput) {
      els.utilSearchInput.addEventListener('keydown', function (e) {
        if (e.key === 'Enter') { e.preventDefault(); searchNow().catch(function () {}); }
        if (e.key === 'Escape') { e.preventDefault(); bus.emit('overlay:close'); }
      });
      // Readest-ish: debounce search on input for quicker iteration
      var t = null;
      els.utilSearchInput.addEventListener('input', function () {
        clearTimeout(t);
        t = setTimeout(function () {
          var q = String(els.utilSearchInput.value || '').trim();
          if (q.length >= 2) searchNow(q).catch(function () {});
          if (!q) resetSearchState();
        }, 220);
      });
    }

    if (els.utilSearchPrev) {
      els.utilSearchPrev.addEventListener('click', function () { searchPrev().catch(function () {}); });
    }

    if (els.utilSearchNext) {
      els.utilSearchNext.addEventListener('click', function () { searchNext().catch(function () {}); });
    }

    // Bus events
    bus.on('search:run', function (query) { searchNow(query).catch(function () {}); });
    bus.on('search:prev', function () { searchPrev().catch(function () {}); });
    bus.on('search:next', function () { searchNext().catch(function () {}); });
    bus.on('search:clear', function () { clearSearch(); });
  }

  // ── Export ───────────────────────────────────────────────────
  window.booksReaderSearch = {
    bind: bind,
    resetSearchState: resetSearchState,
    clearSearch: clearSearch,
    onOpen: function () { ensureSearchMatchOptionUi(); renderResultsList(); },
    onClose: resetSearchState,
  };
})();
