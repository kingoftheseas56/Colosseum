// panel_search.js — the "search" popover: in-book search off the pill.
//
// Recon (real names, not the plan's guesses):
//   - window.booksReaderSearch (reader_search.js) only exports bind/
//     resetSearchState/clearSearch/onOpen/onClose — all wired to the OLD
//     overlay's DOM (#brOverlaySearch elements, history list, match-case/
//     whole-word toggle buttons). The actual search runner (searchNow) and
//     searchPrev/searchNext are private closures inside that IIFE, reachable
//     from outside only via bus events ('search:run'/'search:prev'/
//     'search:next') — and those fire-and-forget with NO completion signal,
//     so a caller can't await a result that way.
//   - The real, awaitable, per-book-format-consistent surface is the ENGINE
//     itself: window.booksReaderState.state.engine.search(query, opts) ->
//     async, returns { ok, count, hits, groups?, flat? }. This is exactly
//     what reader_search.js's own searchNow() calls internally
//     (`res = await state.engine.search(q, matchOpts)`) — so calling it
//     directly here is reusing the SAME mechanism, just without the old
//     overlay's DOM-coupled rendering wrapped around it.
//   - Engine variance (checked all four engines in this dir):
//       engine_foliate.js (primary, used for real epub/pdf via foliate-js):
//         search() returns { ok, count, hits (CFI strings), groups,
//         flat: [{cfi, excerpt:{pre,match,post}|null, label}] } — the rich
//         shape. searchGoTo(index) and clearSearch() both present, and
//         search() itself already jumps to hit 0 as a side effect.
//       engine_txt.js: search() returns { ok, count, hits: [0..count-1] }
//         (index placeholders, no excerpts). searchGoTo/clearSearch present.
//       engine_pdf.js / engine_epub.js (legacy/compat fallbacks —
//         window.booksReaderEngines.pdf_legacy / presumably epub_legacy):
//         search() returns only { ok, count } — NO hits array at all, and
//         jumps to the first match as a side effect if found. Neither
//         exports searchGoTo nor clearSearch.
//     reader_search.js's own searchNow/jumpToIndex/resetSearchState already
//     guard every engine call with `typeof state.engine.X === 'function'`
//     and fall back to `hits.map(cfi => ({cfi, excerpt:null, label:''}))`
//     when `flat` is absent — this file mirrors that exact guarding so the
//     degraded engines behave the same (no worse, no better) as the old
//     overlay already does for them.
//   - Navigate to a hit: engine.searchGoTo(index) — index into the engine's
//     OWN internal hit list just produced by the search() call above (the
//     engine's private closure `state`, NOT window.booksReaderState.state —
//     two different `state` objects that happen to share a name; reader_
//     search.js's copy is a UI-side mirror of the engine's authoritative
//     one). Mirrors reader_search.js's jumpToIndex: follow the jump with
//     RS.saveProgress() + bus.emit('nav:progress-sync') so a search-hit jump
//     persists position exactly like a TOC jump does (panel_contents.js's
//     'toc:navigate' path gets this for free via reader_toc.js; search needs
//     it done explicitly here since we bypassed reader_search.js entirely).
//   - Clear/close: engine.clearSearch() removes the engine's own highlight
//     marks — mirrors what resetSearchState() does on the old overlay,
//     called directly on the engine since this panel never touches
//     booksReaderState's search fields.
//   - SHARED-STATE FINDING (asked for in the task brief): the engine holds
//     exactly ONE search session in its own closure state, not one per
//     caller. If the old #brOverlaySearch overlay and this popover both run
//     a search, the later search wins for both UIs' prev/next — that
//     coupling is pre-existing in the engine layer, not introduced by this
//     file. Harmless while only one search UI is used at a time (true today,
//     pre-Phase-5-deletion); flagged here for the record per the task brief.
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  var inputEl = null;
  var resultsEl = null;
  var hits = [];      // local mirror of the last search's flat hits, our own order
  var searchSeq = 0;  // guards a stale async response from painting over a newer query

  function escHtml(s) {
    return (RS && typeof RS.escHtml === 'function') ? RS.escHtml(s) : String(s == null ? '' : s);
  }

  function fmtExcerpt(excerpt) {
    if (!excerpt || typeof excerpt !== 'object') return '';
    var match = escHtml(excerpt.match || '');
    if (!match) return '';
    var pre = escHtml(excerpt.pre || '');
    var post = escHtml(excerpt.post || '');
    return pre + '<mark>' + match + '</mark>' + post;
  }

  function renderStatus(msg) {
    if (!resultsEl) return;
    resultsEl.innerHTML = '';
    var row = document.createElement('div');
    row.className = 'room-search-row room-search-row--empty';
    row.textContent = msg;
    resultsEl.appendChild(row);
  }

  function renderHits() {
    if (!resultsEl) return;
    resultsEl.innerHTML = '';
    if (!hits.length) { renderStatus('No matches'); return; }

    hits.forEach(function (hit, i) {
      var row = document.createElement('button');
      row.type = 'button';
      row.className = 'room-search-row';
      var html = fmtExcerpt(hit && hit.excerpt);
      row.innerHTML = html || escHtml((hit && hit.label) || 'Match');
      row.addEventListener('click', function () { jumpTo(i); });
      resultsEl.appendChild(row);
    });
  }

  async function jumpTo(index) {
    try {
      var engine = RS && RS.state && RS.state.engine;
      if (!engine || typeof engine.searchGoTo !== 'function') return;
      await engine.searchGoTo(index);
      if (RS && typeof RS.saveProgress === 'function') { try { await RS.saveProgress(); } catch (e) {} }
      if (bus && typeof bus.emit === 'function') { try { bus.emit('nav:progress-sync'); } catch (e) {} }
    } catch (e) {
      try { console.warn('[room-search] jump failed:', e); } catch (e2) {}
    } finally {
      window.__roomClosePanels && window.__roomClosePanels();
    }
  }

  async function runSearch(query) {
    var mySeq = ++searchSeq;
    var q = String(query || '').trim();
    if (!q) { hits = []; renderStatus('No matches'); return; }

    var engine = RS && RS.state && RS.state.engine;
    if (!engine || typeof engine.search !== 'function') {
      try { console.warn('[room-search] no search-capable engine for this book'); } catch (e) {}
      hits = [];
      renderStatus('No matches');
      return;
    }

    renderStatus('Searching…');

    var res = null;
    try { res = await engine.search(q, { matchCase: false, wholeWords: false }); }
    catch (e) { res = null; }

    if (mySeq !== searchSeq) return; // a newer query landed first — drop this stale response

    var rawHits = (res && Array.isArray(res.hits)) ? res.hits : [];
    var flat = (res && Array.isArray(res.flat)) ? res.flat : rawHits.map(function (h) {
      if (h && typeof h === 'object') return { cfi: String(h.cfi || ''), excerpt: h.excerpt || null, label: String(h.label || '') };
      return { cfi: String(h == null ? '' : h), excerpt: null, label: '' };
    });

    hits = flat;
    renderHits();
  }

  function render(container) {
    container.innerHTML = '';

    var title = document.createElement('div');
    title.className = 'room-panel-title';
    title.textContent = 'Search';
    container.appendChild(title);

    inputEl = document.createElement('input');
    inputEl.type = 'text';
    inputEl.className = 'room-search-input';
    inputEl.placeholder = 'Search this book';
    inputEl.setAttribute('autocomplete', 'off');
    container.appendChild(inputEl);

    resultsEl = document.createElement('div');
    resultsEl.className = 'room-search-results';
    container.appendChild(resultsEl);

    inputEl.addEventListener('keydown', function (e) {
      if (e.key === 'Enter') {
        e.preventDefault();
        runSearch(inputEl.value).catch(function () {});
      }
      // Escape is handled by popovers.js's own capture-phase document
      // listener (closes the whole popover) — no local handler needed here.
    });
  }

  function onOpen() {
    if (inputEl) { try { inputEl.focus(); } catch (e) {} }
  }

  function onClose() {
    searchSeq++; // orphan any in-flight search response
    hits = [];
    inputEl = null;
    resultsEl = null;
    try {
      var engine = RS && RS.state && RS.state.engine;
      if (engine && typeof engine.clearSearch === 'function') engine.clearSearch();
    } catch (e) {}
  }

  window.__roomRegisterPanel && window.__roomRegisterPanel('search', {
    render: render,
    onOpen: onOpen,
    onClose: onClose,
  });
})();
