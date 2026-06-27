// BUILD_OVERHAUL: Dictionary lookup module — Wiktionary REST API, history, cross-references
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  // ── In-module LRU cache ──────────────────────────────────────
  var dictCache = {};
  var dictCacheOrder = [];
  var DICT_CACHE_MAX = 50;

  function dictCacheGet(word) {
    var key = String(word || '').toLowerCase().trim();
    return dictCache[key] || null;
  }

  function dictCacheSet(word, data) {
    var key = String(word || '').toLowerCase().trim();
    if (!key) return;
    dictCache[key] = data;
    dictCacheOrder.push(key);
    if (dictCacheOrder.length > DICT_CACHE_MAX) {
      var old = dictCacheOrder.shift();
      delete dictCache[old];
    }
  }

  // ── History stack ────────────────────────────────────────────
  var dictHistory = { items: [], index: -1 };

  function pushHistory(word) {
    dictHistory.items = dictHistory.items.slice(0, dictHistory.index + 1);
    dictHistory.items.push(word);
    dictHistory.index = dictHistory.items.length - 1;
    updateBackButton();
  }

  function goBack() {
    if (dictHistory.index <= 0) return;
    dictHistory.index--;
    var word = dictHistory.items[dictHistory.index];
    lookupAndRender(word, false);
  }

  function resetHistory() {
    dictHistory.items = [];
    dictHistory.index = -1;
    updateBackButton();
  }

  function updateBackButton() {
    var els = RS.ensureEls();
    if (els.dictBack) {
      els.dictBack.classList.toggle('hidden', dictHistory.index <= 0);
    }
  }

  // ── Wiktionary API lookup ────────────────────────────────────
  var _lookupGen = 0;

  async function lookupWord(word) {
    var w = String(word || '').trim();
    if (!w) return { error: true };
    try {
      var url = 'https://en.wiktionary.org/api/rest_v1/page/definition/' + encodeURIComponent(w);
      var res = await fetch(url, { method: 'GET' });
      if (!res || !res.ok) return { error: true };
      var json = await res.json();
      if (!json || typeof json !== 'object' || Object.keys(json).length === 0) return { error: true };
      return json;
    } catch (e) {
      return { error: true };
    }
  }

  // ── Language-aware result selection ──────────────────────────
  function getBookLanguage() {
    try {
      if (RS.state.engine && RS.state.engine.book && RS.state.engine.book.metadata) {
        return String(RS.state.engine.book.metadata.language || '').split('-')[0].toLowerCase();
      }
    } catch (e) {}
    return '';
  }

  function selectLanguageEntries(data) {
    if (!data || typeof data !== 'object' || data.error) return null;
    var lang = getBookLanguage();
    if (lang && data[lang]) return { entries: data[lang], langCode: lang };
    if (data['en']) return { entries: data['en'], langCode: 'en' };
    var keys = Object.keys(data);
    for (var i = 0; i < keys.length; i++) {
      if (Array.isArray(data[keys[i]]) && data[keys[i]].length > 0) {
        return { entries: data[keys[i]], langCode: keys[i] };
      }
    }
    return null;
  }

  // ── Render dictionary result ─────────────────────────────────
  function renderDictResult(word, data) {
    var els = RS.ensureEls();
    if (!els.dictBody) return;
    if (els.dictWord) els.dictWord.textContent = word;

    var selected = selectLanguageEntries(data);
    if (!selected) {
      renderError(word);
      return;
    }

    var frag = document.createDocumentFragment();

    // Language header
    var langName = '';
    try { langName = selected.entries[0].language || ''; } catch (e) {}
    if (langName) {
      var langDiv = document.createElement('div');
      langDiv.className = 'dictLang';
      langDiv.textContent = langName;
      frag.appendChild(langDiv);
    }

    // Parts of speech
    var entries = selected.entries;
    for (var i = 0; i < entries.length; i++) {
      var entry = entries[i];
      if (!entry) continue;

      // Part of speech heading
      if (entry.partOfSpeech) {
        var posDiv = document.createElement('div');
        posDiv.className = 'dictPos';
        posDiv.textContent = entry.partOfSpeech;
        frag.appendChild(posDiv);
      }

      // Definitions list
      var defs = Array.isArray(entry.definitions) ? entry.definitions : [];
      if (defs.length > 0) {
        var ol = document.createElement('ol');
        ol.className = 'dictDefList';
        for (var j = 0; j < defs.length; j++) {
          var d = defs[j];
          if (!d || !d.definition) continue;
          var li = document.createElement('li');
          li.textContent = stripHtml(d.definition);
          // Examples
          var exArr = Array.isArray(d.parsedExamples) ? d.parsedExamples
                    : Array.isArray(d.examples) ? d.examples : [];
          if (exArr.length > 0) {
            for (var k = 0; k < exArr.length; k++) {
              var exRaw = exArr[k];
              var exText = typeof exRaw === 'object' ? stripHtml(exRaw.example || exRaw.text || '') : stripHtml(exRaw);
              if (!exText) continue;
              var exDiv = document.createElement('div');
              exDiv.className = 'dictExample';
              exDiv.textContent = '\u201c' + exText + '\u201d';
              li.appendChild(exDiv);
            }
          }
          ol.appendChild(li);
        }
        frag.appendChild(ol);
      }
    }

    // Wiktionary search link at bottom
    var linkDiv = document.createElement('div');
    linkDiv.className = 'dictLink';
    linkDiv.textContent = 'View on Wiktionary';
    linkDiv.addEventListener('click', function () {
      try {
        Tanko.api.shell.openExternal('https://en.wiktionary.org/wiki/' + encodeURIComponent(word));
      } catch (e) {}
    });
    frag.appendChild(linkDiv);

    els.dictBody.innerHTML = '';
    els.dictBody.appendChild(frag);
    wireWikiLinks(els.dictBody);
  }

  function renderError(word) {
    var els = RS.ensureEls();
    if (!els.dictBody) return;
    var frag = document.createDocumentFragment();
    var errDiv = document.createElement('div');
    errDiv.className = 'dictError';
    errDiv.textContent = 'No definition found for \u201c' + word + '\u201d';
    frag.appendChild(errDiv);
    var linkDiv = document.createElement('div');
    linkDiv.className = 'dictLink';
    linkDiv.textContent = 'Try searching on Wiktionary';
    linkDiv.addEventListener('click', function () {
      try {
        Tanko.api.shell.openExternal('https://en.wiktionary.org/w/index.php?search=' + encodeURIComponent(word));
      } catch (e) {}
    });
    frag.appendChild(linkDiv);
    els.dictBody.innerHTML = '';
    els.dictBody.appendChild(frag);
  }

  function wireWikiLinks(container) {
    var links = container.querySelectorAll('a[rel="mw:WikiLink"], a[title]');
    for (var i = 0; i < links.length; i++) {
      (function (link) {
        var target = link.getAttribute('title') || '';
        if (!target) return;
        link.addEventListener('click', function (e) {
          e.preventDefault();
          e.stopPropagation();
          var w = normalizeWord(target);
          if (w) lookupAndRender(w, true);
        });
        link.removeAttribute('href');
      })(links[i]);
    }
  }

  // ── Lookup orchestrator ──────────────────────────────────────
  async function lookupAndRender(word, pushToHistory) {
    var els = RS.ensureEls();
    if (!els.dictPopup || !els.dictBody) return;

    var gen = ++_lookupGen;
    if (els.dictWord) els.dictWord.textContent = word;
    els.dictBody.innerHTML = '<div class="dictLoading">Looking up\u2026</div>';
    els.dictBody.scrollTop = 0;

    if (pushToHistory !== false) pushHistory(word);
    updateBackButton();

    var cached = dictCacheGet(word);
    if (cached) { renderDictResult(word, cached); return; }

    var result = await lookupWord(word);
    if (gen !== _lookupGen) return;
    dictCacheSet(word, result);
    renderDictResult(word, result);
  }

  // ── HTML stripping helper ───────────────────────────────────
  var _stripEl = null;
  function stripHtml(raw) {
    var s = String(raw || '');
    if (!s) return '';
    if (!_stripEl) _stripEl = document.createElement('div');
    _stripEl.innerHTML = s;
    return _stripEl.textContent || '';
  }

  // ── Word normalization ───────────────────────────────────────
  function normalizeWord(raw) {
    var s = String(raw || '').trim();
    if (!s) return '';
    s = s.replace(/\s+/g, ' ').split(' ')[0];
    s = s.replace(/^[\s"'`([{<]+/, '').replace(/[\s"'`)\]}>.,;:!?]+$/, '');
    if (s.length > 120) s = s.slice(0, 120);
    return s;
  }

  function eventToViewportPoint(ev) {
    if (!ev) return null;
    var x = Number.NaN;
    var y = Number.NaN;
    try {
      if (Number.isFinite(Number(ev.clientX)) && Number.isFinite(Number(ev.clientY))) {
        x = Number(ev.clientX);
        y = Number(ev.clientY);
      } else if (ev.touches && ev.touches[0]) {
        x = Number(ev.touches[0].clientX);
        y = Number(ev.touches[0].clientY);
      } else if (ev.changedTouches && ev.changedTouches[0]) {
        x = Number(ev.changedTouches[0].clientX);
        y = Number(ev.changedTouches[0].clientY);
      }
      var v = ev.view || (ev.target && ev.target.ownerDocument && ev.target.ownerDocument.defaultView) || null;
      var frame = v && v.frameElement;
      if (frame && typeof frame.getBoundingClientRect === 'function') {
        var fr = frame.getBoundingClientRect();
        x = fr.left + x;
        y = fr.top + y;
      }
    } catch (e) { return null; }
    if (!Number.isFinite(x) || !Number.isFinite(y)) return null;
    return { x: x, y: y };
  }


  var _selMenu = null;
  var _selMenuWord = '';
  var _selMenuRect = null;
  function _ensureSelMenu() {
    if (_selMenu) return _selMenu;
    var el = document.createElement('div');
    el.id = 'booksReaderSelMenu';
    el.style.cssText = 'position:fixed;z-index:10020;display:none;background:rgba(18,18,22,.96);border:1px solid rgba(255,255,255,.12);border-radius:10px;padding:6px;box-shadow:0 10px 24px rgba(0,0,0,.35);gap:6px;align-items:center';
    el.addEventListener('mousedown', function (e) { e.preventDefault(); e.stopPropagation(); });
    var mkBtn = function (label) {
      var b = document.createElement('button');
      b.type = 'button';
      b.textContent = label;
      b.style.cssText = 'background:#23242a;color:#fff;border:1px solid rgba(255,255,255,.12);border-radius:8px;padding:6px 10px;font:12px system-ui;cursor:pointer';
      return b;
    };
    var bTts = mkBtn('Start TTS from here');
    var bDict = mkBtn('Dictionary');
    bTts.addEventListener('click', function () {
      var txt = _selMenuWord;
      _hideSelMenu();
      if (!txt) return;
      try {
        var tts = window.booksTTS;
        var st = RS.state || {};
        if (tts) {
          try { if (typeof tts.stop === 'function') tts.stop(); } catch (e) {}
          if (typeof tts.init === 'function') tts.init({ format: String((st.book && st.book.format) || 'epub').toLowerCase(), getHost: function(){ return st.host || null; }, getViewEngine: function(){ return st.engine || null; }, onNeedAdvance: function(){ var eng = st.engine || null; if (!eng || typeof eng.advanceSection !== 'function') return Promise.resolve(false); return eng.advanceSection(1).then(function(){ return true; }).catch(function(){ return false; }); } }).then(function(){
            try { var _bk2 = ''; try { var _b2 = st.book && (st.book.id || st.book.path); if (_b2) _bk2 = 'bk:' + encodeURIComponent(String(_b2)).slice(0, 180); } catch(e2){} var _vc2 = (_bk2 && localStorage.getItem('booksListen.' + _bk2 + '.Voice')) || localStorage.getItem('booksListen.Voice') || 'en-US-AndrewNeural'; tts.setVoice(_vc2); } catch(e2){}
            try { if (typeof tts.stop === 'function') tts.stop(); } catch (e) {} try { tts.playFromSelection(txt); } catch (e) {} });
        }
      } catch (e) {}
    });
    bDict.addEventListener('click', function () {
      var w = _selMenuWord;
      var r = _selMenuRect;
      _hideSelMenu();
      if (w) triggerDictLookup(w, null);
    });
    el.appendChild(bTts); el.appendChild(bDict);
    document.body.appendChild(el);
    _selMenu = el;
    return el;
  }
  function _hideSelMenu() { if (_selMenu) _selMenu.style.display = 'none'; _selMenuWord = ''; _selMenuRect = null; }
  function _showSelMenu(anchorEvent) {
    var word = getSelectedWord();
    if (!word) { _hideSelMenu(); return; }
    _selMenuWord = word;
    _selMenuRect = getSelectionRect();
    var el = _ensureSelMenu();
    var x = 12, y = 12;
    if (_selMenuRect) { x = Math.max(8, Math.min(_selMenuRect.left, window.innerWidth - 280)); y = Math.max(8, _selMenuRect.bottom + 8); }
    else {
      var pt = eventToViewportPoint(anchorEvent); if (pt) { x = pt.x; y = pt.y; }
    }
    el.style.left = x + 'px'; el.style.top = y + 'px'; el.style.display = 'flex';
  }

  // ── Selection helpers ────────────────────────────────────────

  function getSelectionRect() {
    var state = RS.state;

    if (state.engine && state.engine.renderer && typeof state.engine.renderer.getContents === 'function') {
      try {
        var contents = state.engine.renderer.getContents();
        for (var i = 0; i < contents.length; i++) {
          var c = contents[i];
          if (!c || !c.doc) continue;
          var sel = c.doc.getSelection();
          if (sel && sel.rangeCount > 0 && !sel.isCollapsed) {
            var range = sel.getRangeAt(0);
            var rect = range.getBoundingClientRect();
            if (rect && rect.width > 0) {
              var iframe = c.doc.defaultView && c.doc.defaultView.frameElement;
              if (iframe) {
                var iframeRect = iframe.getBoundingClientRect();
                return {
                  top: rect.top + iframeRect.top,
                  left: rect.left + iframeRect.left,
                  width: rect.width,
                  height: rect.height,
                  bottom: rect.bottom + iframeRect.top,
                  right: rect.right + iframeRect.left,
                };
              }
              return rect;
            }
          }
        }
      } catch (e) {}
    }

    try {
      var mainSel = window.getSelection();
      if (mainSel && mainSel.rangeCount > 0 && !mainSel.isCollapsed) {
        return mainSel.getRangeAt(0).getBoundingClientRect();
      }
    } catch (e) {}

    return null;
  }

  function getSelectedWord() {
    var state = RS.state;

    if (state._dictPendingWord) {
      var pw = normalizeWord(state._dictPendingWord);
      state._dictPendingWord = '';
      if (pw.length > 0) return pw;
    }

    try {
      if (state.engine && typeof state.engine.getSelectedText === 'function') {
        var s0 = state.engine.getSelectedText();
        var t0 = '';
        if (s0 && typeof s0 === 'object') t0 = String(s0.text || '');
        else t0 = String(s0 || '');
        t0 = normalizeWord(t0);
        if (t0.length > 0) return t0;
      }
    } catch (e0) {}

    if (state.engine && state.engine.renderer && typeof state.engine.renderer.getContents === 'function') {
      try {
        var contents = state.engine.renderer.getContents();
        for (var i = 0; i < contents.length; i++) {
          var c = contents[i];
          if (!c || !c.doc) continue;
          var sel = c.doc.getSelection();
          if (sel && !sel.isCollapsed) {
            var text = normalizeWord(sel.toString());
            if (text.length > 0) return text;
          }
        }
      } catch (e) {}
    }

    try {
      var mainSel = window.getSelection();
      if (mainSel && !mainSel.isCollapsed) {
        var t = normalizeWord(mainSel.toString());
        if (t.length > 0) return t;
      }
    } catch (e) {}

    return '';
  }

  // ── Popup visibility ─────────────────────────────────────────

  function hideDictPopup() {
    var els = RS.ensureEls();
    if (els.dictPopup) {
      els.dictPopup.classList.add('hidden');
      els.dictPopup.removeAttribute('data-anchored');
    }
  }

  // ── Lookup trigger ───────────────────────────────────────────

  async function triggerDictLookup(explicitWord, anchorEvent) {
    if (explicitWord && typeof explicitWord === 'object') {
      var maybeEvent = explicitWord;
      var looksLikeEvent = !!(maybeEvent.type || maybeEvent.target
        || maybeEvent.clientX !== undefined || maybeEvent.clientY !== undefined
        || maybeEvent.touches || maybeEvent.changedTouches);
      if (looksLikeEvent) {
        if (!anchorEvent) anchorEvent = maybeEvent;
        explicitWord = '';
      }
    }
    var word = normalizeWord(explicitWord);
    if (!word) word = getSelectedWord();
    var els = RS.ensureEls();
    if (!els.dictPopup) return;
    if (!word) {
      if (els.dictWord) els.dictWord.textContent = 'Dictionary';
      if (els.dictBody) {
        els.dictBody.innerHTML = '<div class="dictError">Select a word first, then try again.</div>';
      }
      resetHistory();
      els.dictPopup.removeAttribute('data-anchored');
      els.dictPopup.style.top = '';
      els.dictPopup.style.left = '';
      els.dictPopup.classList.remove('hidden');
      return;
    }

    // Position near selection
    var rect = getSelectionRect();
    if (!rect) {
      var pt = eventToViewportPoint(anchorEvent);
      if (pt) {
        rect = { left: pt.x, right: pt.x, top: pt.y, bottom: pt.y, width: 1, height: 1 };
      }
    }
    if (rect && els.dictPopup) {
      var top = rect.bottom + 8;
      var popupHeight = 420;
      if (top + popupHeight > window.innerHeight - 8) {
        top = Math.max(8, rect.top - popupHeight - 8);
      }
      els.dictPopup.style.top = Math.max(8, top) + 'px';
      els.dictPopup.style.left = Math.max(8, Math.min(rect.left, window.innerWidth - 396)) + 'px';
      els.dictPopup.setAttribute('data-anchored', 'true');
    } else {
      els.dictPopup.removeAttribute('data-anchored');
      els.dictPopup.style.top = '';
      els.dictPopup.style.left = '';
    }
    els.dictPopup.classList.remove('hidden');

    lookupAndRender(word, true);
  }

  // ── Bind ─────────────────────────────────────────────────────

  function bind() {
    var els = RS.ensureEls();

    if (els.dictClose) {
      els.dictClose.addEventListener('click', hideDictPopup);
    }

    if (els.dictBack) {
      els.dictBack.addEventListener('click', goBack);
    }

    document.addEventListener('mousedown', function (e) {
      if (_selMenu && _selMenu.style.display !== 'none' && !_selMenu.contains(e.target)) _hideSelMenu();
      try { if (e && e.button && e.button !== 0) return; } catch (err) {}
      if (!els.dictPopup) return;
      _hideSelMenu();
      if (els.dictPopup.classList.contains('hidden')) return;
      if (els.dictPopup.contains(e.target)) return;
      hideDictPopup();
    });

    bus.on('dict:lookup', triggerDictLookup);
    bus.on('dict:hide', hideDictPopup);
  }

  // ── Export ───────────────────────────────────────────────────
  window.booksReaderDict = {
    bind: bind,
    hideDictPopup: hideDictPopup,
    hideSelMenu: _hideSelMenu,
    showSelMenu: _showSelMenu,
    triggerDictLookup: triggerDictLookup,
    triggerDictLookupFromText: triggerDictLookup,
    getSelectedWord: getSelectedWord,
    getSelectionRect: getSelectionRect, // FIX-ANN01: shared iframe-aware selection rect
    onOpen: function () {},
    onClose: function () { resetHistory(); hideDictPopup(); _hideSelMenu(); },
  };
})();
