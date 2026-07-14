// Reader sidebar "Audio" tab — book↔audiobook chapter pairing.
//
// Colosseum port of Tankoban-Max's reader_audiobook_pairing.js. The shape is the
// same (library dropdown, Auto 1:1 or manual per-chapter mapping, save/unlink),
// but the guts differ from TB-Max:
//   - API: window.Tanko.api.audiobook (→ BookBridge → AudiobookDownloader +
//     AudioPairingStore), NOT TB-Max's window.Tanko.api.audiobooks.
//   - Player: there is NO JS-side audiobook player here. The docked player is QML
//     (AudiobookStrip/AudiobookSession, hoisted in Main.qml). We summon it through
//     the bridge command `audiobook.loadAtChapter(pairKey, idx)` — QML owns play/seek.
//
// Book chapters come from the reader TOC (RS.state.tocItems); a mapping links each
// book chapter's href to an audiobook chapter index. Persisted per bookId
// (state.book.id = the SHA1[:20] path key, same identity bookmarks/progress use).
(function () {
  'use strict';

  if (window.__booksReaderAudiobookPairingBound) return;
  window.__booksReaderAudiobookPairingBound = true;

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  function api() { return window.Tanko && window.Tanko.api && window.Tanko.api.audiobook; }

  // ── State ──────────────────────────────────────────────────────────────────
  var _audiobooks = [];       // library: [ { id, title, author, chapters:[{title}] } ]
  var _selectedAbId = '';     // audiobook id in the dropdown
  var _selectedAb = null;     // the selected audiobook record
  var _mappings = [];         // [ { bookChapterHref, bookChapterLabel, abChapterIndex, abChapterTitle } ]
  var _bookId = '';           // current book id
  var _savedPairing = null;   // pairing loaded from the store
  var _lastReaderHref = '';   // last chapter href the reader relocated to
  var _syncBaselineHref = null; // read-along: the chapter we're synced to; null until the
                                // book settles, so the open-at-last-spot summon isn't
                                // immediately overridden by the initial relocate.

  // ── DOM refs ─────────────────────────────────────────────────────────────────
  var el = {};
  function qs(id) { return document.getElementById(id); }
  function ensureEls() {
    el.status    = qs('abPairStatus');
    el.select    = qs('abPairSelect');
    el.autoBtn   = qs('abPairAutoBtn');
    el.saveBtn   = qs('abPairSaveBtn');
    el.unlinkBtn = qs('abPairUnlinkBtn');
    el.list      = qs('abPairList');
  }

  // ── Helpers ───────────────────────────────────────────────────────────────────
  function getBookId() {
    var state = RS && RS.state;
    if (!state || !state.book) return '';
    return String(state.book.id || state.book.path || '');
  }
  function getBookToc() {
    var state = RS && RS.state;
    return (state && Array.isArray(state.tocItems)) ? state.tocItems : [];
  }
  function findAudiobook(id) {
    for (var i = 0; i < _audiobooks.length; i++) {
      if (_audiobooks[i].id === id) return _audiobooks[i];
    }
    return null;
  }
  function updateStatus(text) { if (el.status) el.status.textContent = text; }

  function normalizeHref(href) {
    var h = String(href || '');
    h = h.replace(/^\.\//, '');
    var hashIdx = h.indexOf('#');
    if (hashIdx >= 0) h = h.substring(0, hashIdx);
    try { h = decodeURIComponent(h); } catch (_) {}
    return h.toLowerCase().trim();
  }

  // Build { normalizedBookHref -> abChapterIndex } from the saved/edited mappings.
  function buildMappingIndex() {
    var out = Object.create(null);
    for (var i = 0; i < _mappings.length; i++) {
      var m = _mappings[i] || {};
      var key = normalizeHref(m.bookChapterHref || '');
      if (!key) continue;
      var idx = parseInt(m.abChapterIndex, 10);
      if (!isFinite(idx) || idx < 0) continue;
      out[key] = idx;
    }
    return out;
  }
  // The audiobook chapter mapped to whatever chapter the reader is currently on,
  // or null if unmapped / unknown.
  function mappedChapterForCurrentReaderLocation() {
    var href = normalizeHref(_lastReaderHref);
    if (!href) return null;
    var idx = buildMappingIndex();
    return Object.prototype.hasOwnProperty.call(idx, href) ? idx[href] : null;
  }

  // ── Load the audiobook library ────────────────────────────────────────────────
  function loadAudiobooksList() {
    var a = api();
    if (!a || !a.library) { _audiobooks = []; return Promise.resolve(); }
    return Promise.resolve(a.library()).then(function (list) {
      _audiobooks = Array.isArray(list) ? list : [];
      populateSelect();
    }).catch(function () { _audiobooks = []; populateSelect(); });
  }
  function populateSelect() {
    if (!el.select) return;
    while (el.select.options.length > 1) el.select.remove(1); // keep the placeholder
    for (var i = 0; i < _audiobooks.length; i++) {
      var ab = _audiobooks[i];
      var nCh = (ab.chapters && ab.chapters.length) || 0;
      var opt = document.createElement('option');
      opt.value = ab.id;
      opt.textContent = (ab.title || ab.id) + ' (' + nCh + ' ch)';
      el.select.appendChild(opt);
    }
    if (_selectedAbId) el.select.value = _selectedAbId;
  }

  // ── Load the saved pairing for this book ───────────────────────────────────────
  function loadSavedPairing() {
    _bookId = getBookId();
    var a = api();
    if (!_bookId || !a || !a.pairingGet) return Promise.resolve();
    return Promise.resolve(a.pairingGet(_bookId)).then(function (pairing) {
      _savedPairing = (pairing && pairing.audiobookId) ? pairing : null;
      if (_savedPairing) {
        _selectedAbId = _savedPairing.audiobookId;
        _selectedAb = findAudiobook(_selectedAbId);
        _mappings = Array.isArray(_savedPairing.mappings) ? _savedPairing.mappings : [];
        if (el.select) el.select.value = _selectedAbId;
        updateStatus('Linked: ' + (_selectedAb ? _selectedAb.title : _selectedAbId));
      } else {
        _selectedAbId = ''; _selectedAb = null; _mappings = [];
        updateStatus('No audiobook linked');
      }
      renderMappings();
    }).catch(function () {
      _savedPairing = null; updateStatus('No audiobook linked'); renderMappings();
    });
  }

  // ── Render the per-chapter mapping list ────────────────────────────────────────
  function renderMappings() {
    if (!el.list) return;
    el.list.innerHTML = '';
    var toc = getBookToc();
    if (!toc.length) {
      el.list.innerHTML = '<div class="ab-pair-empty">No book chapters yet (contents still loading)</div>';
      return;
    }
    if (!_selectedAb) {
      el.list.innerHTML = '<div class="ab-pair-empty">Pick an audiobook above to map its chapters</div>';
      return;
    }
    var abChapters = _selectedAb.chapters || [];
    var mappingByHref = {};
    for (var m = 0; m < _mappings.length; m++) {
      if (_mappings[m].bookChapterHref != null) {
        mappingByHref[normalizeHref(_mappings[m].bookChapterHref)] = _mappings[m].abChapterIndex;
      }
    }
    for (var i = 0; i < toc.length; i++) {
      var ch = toc[i];
      var row = document.createElement('div');
      row.className = 'ab-pair-row';

      var bookLabel = document.createElement('span');
      bookLabel.className = 'ab-pair-book-ch';
      bookLabel.textContent = ch.label || ch.title || ('Chapter ' + (i + 1));
      bookLabel.title = bookLabel.textContent;
      row.appendChild(bookLabel);

      var arrow = document.createElement('span');
      arrow.className = 'ab-pair-arrow';
      arrow.textContent = '→';
      row.appendChild(arrow);

      var sel = document.createElement('select');
      sel.className = 'ab-pair-ch-select';
      sel.dataset.href = ch.href || '';

      var noneOpt = document.createElement('option');
      noneOpt.value = '-1';
      noneOpt.textContent = '— none —';
      sel.appendChild(noneOpt);
      for (var j = 0; j < abChapters.length; j++) {
        var abOpt = document.createElement('option');
        abOpt.value = j;
        abOpt.textContent = (j + 1) + '. ' + (abChapters[j].title || ('Track ' + (j + 1)));
        sel.appendChild(abOpt);
      }
      var key = normalizeHref(ch.href || '');
      if (key in mappingByHref) sel.value = mappingByHref[key];

      sel.addEventListener('change', rebuildMappingsFromUI);
      row.appendChild(sel);
      el.list.appendChild(row);
    }
  }

  function rebuildMappingsFromUI() {
    if (!el.list) return;
    var toc = getBookToc();
    var tocByHref = {};
    for (var t = 0; t < toc.length; t++) tocByHref[normalizeHref(toc[t].href || '')] = toc[t];
    var selects = el.list.querySelectorAll('.ab-pair-ch-select');
    _mappings = [];
    for (var i = 0; i < selects.length; i++) {
      var sel = selects[i];
      var abIdx = parseInt(sel.value, 10);
      if (!isFinite(abIdx) || abIdx < 0) continue;
      var href = sel.dataset.href || '';
      var tocItem = tocByHref[normalizeHref(href)] || {};
      var abTitle = (_selectedAb && _selectedAb.chapters[abIdx]) ? _selectedAb.chapters[abIdx].title : '';
      _mappings.push({
        bookChapterHref: href,
        bookChapterLabel: tocItem.label || tocItem.title || '',
        abChapterIndex: abIdx,
        abChapterTitle: abTitle || ''
      });
    }
  }

  // ── Auto-pair (index-for-index) ────────────────────────────────────────────────
  function autoPair() {
    if (!_selectedAb) return;
    var toc = getBookToc();
    var abChapters = _selectedAb.chapters || [];
    _mappings = [];
    var count = Math.min(toc.length, abChapters.length);
    for (var i = 0; i < count; i++) {
      _mappings.push({
        bookChapterHref: toc[i].href || '',
        bookChapterLabel: toc[i].label || toc[i].title || '',
        abChapterIndex: i,
        abChapterTitle: abChapters[i].title || ''
      });
    }
    renderMappings();
    updateStatus('Auto-mapped ' + count + ' chapter' + (count === 1 ? '' : 's') + ' — press Save');
  }

  // ── Save / unlink ──────────────────────────────────────────────────────────────
  function savePairing() {
    _bookId = getBookId();
    var a = api();
    if (!_bookId || !_selectedAbId || !a || !a.pairingSave) return;
    rebuildMappingsFromUI();
    var pairing = { bookId: _bookId, audiobookId: _selectedAbId, mappings: _mappings };
    Promise.resolve(a.pairingSave(_bookId, pairing)).then(function () {
      _savedPairing = pairing;
      updateStatus('Saved: ' + (_selectedAb ? _selectedAb.title : ''));
      // Summon the docked player at the chapter mapped to the current reading spot
      // (or resume from wherever it left off if this spot isn't mapped). Anchor the
      // sync baseline here so this same-chapter position isn't instantly re-synced.
      var mapped = mappedChapterForCurrentReaderLocation();
      a.loadAtChapter(_selectedAbId, (mapped == null ? -1 : mapped));
      _syncBaselineHref = normalizeHref(_lastReaderHref) || _syncBaselineHref;
    }).catch(function (err) { updateStatus('Save failed'); if (window.console) console.warn('[ab-pair] save', err); });
  }

  function unlinkPairing() {
    _bookId = getBookId();
    var a = api();
    if (!_bookId || !a || !a.pairingDelete) return;
    Promise.resolve(a.pairingDelete(_bookId)).then(function () {
      _savedPairing = null; _selectedAbId = ''; _selectedAb = null; _mappings = [];
      _syncBaselineHref = null;
      if (el.select) el.select.value = '';
      updateStatus('No audiobook linked');
      renderMappings();
      if (a.close) a.close();
    }).catch(function () {});
  }

  // ── Lifecycle (called by reader_core's module loop) ────────────────────────────
  function bind() {
    ensureEls();
    if (!el.select) return;
    el.select.addEventListener('change', function () {
      _selectedAbId = el.select.value;
      _selectedAb = findAudiobook(_selectedAbId);
      _mappings = [];
      updateStatus(_selectedAb ? ('Selected: ' + _selectedAb.title) : 'No audiobook linked');
      renderMappings();
    });
    if (el.autoBtn)   el.autoBtn.addEventListener('click', autoPair);
    if (el.saveBtn)   el.saveBtn.addEventListener('click', savePairing);
    if (el.unlinkBtn) el.unlinkBtn.addEventListener('click', unlinkPairing);

    if (bus) {
      bus.on('toc:updated', function () { renderMappings(); });
      bus.on('reader:relocated', onReaderRelocated);
    }
  }

  // Read-along page-turn sync. The FIRST relocate after a book opens just sets the
  // baseline (so the summon's "open at last spot" holds); moving into a DIFFERENT
  // mapped chapter afterwards nudges the audiobook to follow — preserving play state
  // (QML sees the pairKey already open → goToChapterKeepState).
  function onReaderRelocated(detail) {
    var href = (detail && detail.tocItem && detail.tocItem.href) ? detail.tocItem.href : '';
    if (href) _lastReaderHref = href;
    if (!_savedPairing || !_selectedAbId) return;
    var norm = normalizeHref(href);
    if (!norm) return;
    if (_syncBaselineHref == null) { _syncBaselineHref = norm; return; } // open-settle: baseline only
    if (norm === _syncBaselineHref) return;                              // still the same chapter
    _syncBaselineHref = norm;
    var idx = buildMappingIndex();
    if (!Object.prototype.hasOwnProperty.call(idx, norm)) return;        // this chapter isn't mapped
    var a = api();
    if (a && a.loadAtChapter) a.loadAtChapter(_selectedAbId, idx[norm]);
  }

  function onOpen() {
    ensureEls();
    _bookId = getBookId();
    _selectedAbId = ''; _selectedAb = null; _mappings = []; _savedPairing = null;
    _lastReaderHref = ''; _syncBaselineHref = null;
    updateStatus('Loading…');
    loadAudiobooksList().then(loadSavedPairing).then(function () {
      // Opening a paired book auto-summons its audiobook at the exact last spot, PAUSED
      // (chapterIndex -1 = restore last spot, no jump). You press play to begin reading
      // along; from there, page turns nudge the audio along (onReaderRelocated).
      if (_savedPairing && _selectedAbId) {
        var a = api();
        if (a && a.loadAtChapter) a.loadAtChapter(_selectedAbId, -1);
      }
    }).catch(function () { updateStatus('No audiobook linked'); });
  }

  function onClose() {
    _audiobooks = []; _selectedAbId = ''; _selectedAb = null; _mappings = [];
    _savedPairing = null; _bookId = ''; _lastReaderHref = ''; _syncBaselineHref = null;
    if (el.list) el.list.innerHTML = '';
    if (el.select) { while (el.select.options.length > 1) el.select.remove(1); el.select.value = ''; }
    updateStatus('No audiobook linked');
  }

  window.booksReaderAudiobookPairing = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    hasSavedPairing: function () { return !!_savedPairing; }
  };
})();
