// BUILD_OVERHAUL: Shared state, constants, helpers, ensureEls, settings persistence
(function () {
  'use strict';

  var bus = window.booksReaderBus;

  // ── Constants ──────────────────────────────────────────────────
  // RCSS_INTEGRATION: ReadiumCSS-based settings model
  var DEFAULT_SETTINGS = {
    theme: 'light',           // light|sepia|dark|paper|contrast1|contrast2|contrast3|contrast4
    fontSize: 100,            // percentage 75-250 (ReadiumCSS --USER__fontSize)
    fontFamily: 'publisher',  // publisher|oldStyleTf|modernTf|sansTf|humanistTf|monospaceTf|AccessibleDfA|IAWriterDuospace
    lineHeight: 1.5,          // 1.0-2.0 (ReadiumCSS --USER__lineHeight)
    margin: 1,                // pageMargins factor 0-4.0 (controls side padding + page gap)
    maxLineWidth: 960,        // max readable line width in px (ReadiumCSS --USER__maxLineLength)
    flowMode: 'paginated',
    columnMode: 'auto',
    textAlign: '',            // ''|left|right|justify (ReadiumCSS --USER__textAlign)
    letterSpacing: 0,         // 0-0.5 rem (ReadiumCSS --USER__letterSpacing)
    wordSpacing: 0,           // 0-1.0 rem (ReadiumCSS --USER__wordSpacing)
    paraSpacing: 0,           // 0-2.0 rem (ReadiumCSS --USER__paraSpacing)
    paraIndent: '',           // ''|0|1em|1.5em|2em (ReadiumCSS --USER__paraIndent)
    bodyHyphens: '',          // ''|auto|none (ReadiumCSS --USER__bodyHyphens)
  }; // LISTEN_P0: tts* settings removed — owned by Listening mode

  var DEFAULT_SHORTCUTS = {
    // LISTEN_P0: ttsToggle/voiceNext/voicePrev removed — owned by Listening mode
    tocToggle: 'o',
    bookmarkToggle: 'b',
    dictLookup: 'd',
    fullscreen: 'f',
    sidebarToggle: 'h',
    themeToggle: 'm',
    gotoPage: 'ctrl+g',
  };

  var ANNOT_COLORS = [
    { id: 'yellow', hex: '#FEF3BD' },
    { id: 'pink', hex: '#EB9694' },
    { id: 'orange', hex: '#FAD0C3' },
    { id: 'green', hex: '#C1EAC5' },
    { id: 'blue', hex: '#BED3F3' },
    { id: 'purple', hex: '#D4C4FB' },
  ];
  var ANNOT_STYLES = ['highlight', 'underline', 'strikethrough', 'outline'];

  // ── Shared mutable state ───────────────────────────────────────
  var state = {
    bound: false,
    open: false,
    opening: false,
    book: null,
    engine: null,
    settings: Object.assign({}, DEFAULT_SETTINGS),
    saveTimer: null,
    relocateSaveTimer: null,
    suspendProgressSave: false,
    suspendProgressSaveReason: '',
    pendingProgressSave: false,
    progressSaveInFlight: false,
    progressSaveReschedule: false,
    sidebarOpen: false,
    sidebarTab: 'toc',
    lastError: '',
    els: null,
    searchHits: [],
    searchActiveIndex: -1,
    tocItems: [],
    bookmarks: [],
    dictCache: {},
    dictCacheOrder: [],
    lastBookInput: null,
    progressFraction: 0,
    progressDragFraction: 0,
    shortcuts: Object.assign({}, DEFAULT_SHORTCUTS),
    // BUILD_ANNOT
    annotations: [],
    annotEditId: null,
    annotPendingCfi: null,
    annotPendingText: '',
    annotColor: '#FEF3BD',
    annotStyle: 'highlight',
    // BUILD_CHAP: per-chapter read state (maps spine index → max read fraction)
    chapterReadState: {},
  };

  // ── DOM helpers ────────────────────────────────────────────────
  function qs(id) {
    try { return document.getElementById(id); } catch (e) { return null; }
  }

  function qsv(sel) {
    try {
      var v = document.getElementById('booksReaderView');
      return v ? v.querySelector(sel) : null;
    } catch (e) { return null; }
  }

  function ensureEls() {
    if (state.els) return state.els;
    state.els = {
      readerView: qs('booksReaderView'),
      host: qs('booksReaderHost'),
      title: qs('booksReaderTitle'),
      subtitle: qs('booksReaderSubtitle'),
      status: qs('booksReaderStatus'),
      // Toolbar
      backBtn: qs('booksReaderBackBtn'),
      searchBtn: qs('booksReaderSearchBtn'),
      bookmarksBtn: qs('booksReaderBookmarksBtn'),
      annotBtn: qs('booksReaderAnnotBtn'),
      sidebarToggle: qs('booksReaderTocNavBtn'),
      histBackBtn: qs('booksReaderHistBackBtn'),
      histFwdBtn: qs('booksReaderHistFwdBtn'),
      fontBtn: qs('booksReaderFontBtn'),
      themeBtn: qs('booksReaderThemeBtn'),
      // FIX_TTSH: settings button triggers mega panel
      listenBtn: qs('booksReaderListenToggle') || qs('booksReaderListenBtn'),
      minBtn: qs('booksReaderMinBtn'),
      maxBtn: qs('booksReaderMaxBtn'),
      maxIcon: qs('booksReaderMaxIcon'),
      fsBtn: qs('booksReaderFsBtn'),
      closeBtn: qs('booksReaderCloseBtn'),
      // Overlay panels
      overlayBackdrop: qs('brOverlayBackdrop'),
      overlaySearch: qs('brOverlaySearch'),
      overlayBookmarks: qs('brOverlayBookmarks'),
      overlayAnnotations: qs('brOverlayAnnotations'),
      overlayFont: qs('brOverlayFont'),
      overlayTheme: qs('brOverlayTheme'),
      // Sidebar (TOC only)
      sidebar: qs('booksSidebar'),
      tocSearch: qs('booksTocSearch'),
      tocList: qs('booksTocList'),
      // Search (in overlay)
      utilSearchInput: qs('booksUtilSearchInput'),
      utilSearchBtn: qs('booksUtilSearchBtn'),
      utilSearchCount: qs('booksUtilSearchCount'),
      utilSearchPrev: qs('booksUtilSearchPrev'),
      utilSearchNext: qs('booksUtilSearchNext'),
      // Sidebar bookmarks
      utilBookmarkToggle: qs('booksUtilBookmarkToggle'),
      utilBookmarkList: qs('booksUtilBookmarkList'),
      // Sidebar annotations
      annotList: qs('booksUtilAnnotationList'),
      // Settings controls (inside sidebar settings pane)
      theme: qs('booksReaderTheme'),
      fontSizeSlider: qs('booksReaderFontSizeSlider'),
      fontSizeValue: qs('booksReaderFontSizeValue'),
      fontFamily: qs('booksReaderFontFamily'),
      lineHeightSlider: qs('booksReaderLineHeightSlider'),
      lineHeightValue: qs('booksReaderLineHeightValue'),
      marginSlider: qs('booksReaderMarginSlider'),
      marginValue: qs('booksReaderMarginValue'),
      maxLineWidthSlider: qs('booksReaderMaxLineWidthSlider'),
      maxLineWidthValue: qs('booksReaderMaxLineWidthValue'),
      columnToggle: qs('booksReaderColumnToggle'),
      // RCSS_INTEGRATION: new typography controls
      letterSpacingSlider: qs('booksReaderLetterSpacingSlider'),
      letterSpacingValue: qs('booksReaderLetterSpacingValue'),
      wordSpacingSlider: qs('booksReaderWordSpacingSlider'),
      wordSpacingValue: qs('booksReaderWordSpacingValue'),
      paraSpacingSlider: qs('booksReaderParaSpacingSlider'),
      paraSpacingValue: qs('booksReaderParaSpacingValue'),
      paraIndent: qs('booksReaderParaIndent'),
      hyphens: qs('booksReaderHyphens'),
      fitPageBtn: qs('booksReaderFitPageBtn'),
      fitWidthBtn: qs('booksReaderFitWidthBtn'),
      zoomDown: qs('booksReaderZoomDown'),
      zoomUp: qs('booksReaderZoomUp'),
      pdfGroup: qs('booksMegaPdfGroup'),
      // Nav arrows
      prevBtn: qs('booksReaderPrevBtn'),
      nextBtn: qs('booksReaderNextBtn'),
      // Corner progress overlays (footer removed)
      cornerLeft: qs('booksReaderCornerLeft'),
      cornerRight: qs('booksReaderCornerRight'),
      // FIX_NAV_SCRUB: progress scrub bar handle for nav + chapter markers
      progress: qs('booksReaderScrub') || qsv('#booksReaderScrub') || qsv('.br-scrub'),
      // Chapter transition
      chapterTransition: qs('booksChapterTransition'),
      chapterTransCurrent: qs('booksChapterTransCurrent'),
      chapterTransNext: qs('booksChapterTransNext'),
      chapterTransContinue: qs('booksChapterTransContinue'),
      chapterTransCountdown: qs('booksChapterTransCountdown'),
      // Dictionary
      dictPopup: qs('booksReaderDictPopup'),
      dictWord: qs('booksReaderDictWord'),
      dictBody: qs('booksReaderDictBody'),
      dictClose: qs('booksReaderDictClose'),
      dictBack: qs('booksReaderDictBack'),
      ttsBar: qs('lpTtsBar'),
      flowToggle: qs('brSettingsFlowToggle'),
      invertDarkImagesToggle: qs('brSettingsInvertDarkImagesToggle'),
      flowBtn: qs('booksReaderFlowBtn'), // FIX-TTS03: toolbar flow mode toggle
      shortcutsList: qs('brSettingsShortcuts'),
      // Annotation popup
      annotPopup: qs('booksAnnotPopup'),
      annotClose: qs('booksAnnotClose'),
      annotColorPicker: qs('booksAnnotColorPicker'),
      annotStylePicker: qs('booksAnnotStylePicker'),
      annotNote: qs('booksAnnotNote'),
      annotSave: qs('booksAnnotSave'),
      annotDelete: qs('booksAnnotDelete'),
      // Goto
      gotoOverlay: qs('booksGotoOverlay'),
      gotoInput: qs('booksGotoInput'),
      gotoSubmit: qs('booksGotoSubmit'),
      gotoCancel: qs('booksGotoCancel'),
      gotoHint: qs('booksGotoHint'),
      // Error banner
      errorBanner: qs('booksReaderErrorBanner'),
      errorTitle: qs('booksReaderErrorTitle'),
      errorDetail: qs('booksReaderErrorDetail'),
      errorRetry: qs('booksReaderErrorRetry'),
      errorClose: qs('booksReaderErrorClose'),
      // Hidden compat stubs
      aaBtn: qs('booksReaderAaBtn'),
      aaPanel: qs('booksReaderAaPanel'),
      playBtn: qs('booksReaderPlayBtn'),
    };
    return state.els;
  }

  // ── Utility helpers ────────────────────────────────────────────
  function escHtml(s) {
    return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function cleanErrMessage(err, fallback) {
    var s = String(err && err.message ? err.message : err || '').trim();
    if (!s) return String(fallback || 'Failed to open book');
    return s.replace(/\s+/g, ' ');
  }

  function clamp01(v) {
    var n = Number(v);
    if (!Number.isFinite(n)) return 0;
    return Math.max(0, Math.min(1, n));
  }

  function isPdfOpen() {
    return !!(state.book && String(state.book.format || '').toLowerCase() === 'pdf');
  }

  function isEpubOrTxtOpen() {
    if (!state.book) return false;
    var f = String(state.book.format || '').toLowerCase();
    return f === 'epub' || f === 'txt' || f === 'mobi' || f === 'fb2';
  }

  function ttsSupported() {
    // EDGE_TTS_FIX Phase 5.2: factory registration alone ≠ working bridge —
    // the JS engine factory may be present while the Qt bridge backing it
    // returns failures. After init has run, trust booksTTS.isAvailable()
    // (true only when probe succeeded). Before init, fall back to factory
    // presence so the Listen UI renders and the user can trigger init in
    // the first place. Web Speech fallback intentionally removed (Edge-only
    // per tts_core.js:1173 standing direction + project_tts_kokoro memory).
    if (window.booksTTS &&
        typeof window.booksTTS.isInitDone === 'function' &&
        window.booksTTS.isInitDone()) {
      return !!(typeof window.booksTTS.isAvailable === 'function' &&
                window.booksTTS.isAvailable());
    }
    return !!(window.booksTTSEngines && window.booksTTSEngines.edge);
  }

  function normalizeBookInput(book) {
    if (!book || typeof book !== 'object') return null;
    var id = String(book.id || '');
    var pathVal = String(book.path || '');
    var format = String(book.format || '').toLowerCase();
    if (!id || !pathVal) return null;
    if (['epub', 'pdf', 'txt', 'mobi', 'fb2'].indexOf(format) < 0) return null;
    return {
      id: id,
      title: String(book.title || ''),
      path: pathVal,
      format: format,
      series: String(book.series || ''),
      seriesId: String(book.seriesId || ''),
    };
  }

  function getEngineCandidates(format, engines) {
    var f = String(format || '').toLowerCase();
    var out = [];
    var add = function (id) {
      var fac = engines && engines[id];
      if (fac && typeof fac.create === 'function') out.push({ id: id, factory: fac });
    };
    if (f === 'epub') { add('epub'); return out; }
    if (f === 'pdf') { add('pdf'); return out; }
    if (f === 'txt') { add('txt'); return out; }
    if (f === 'mobi' || f === 'fb2') { add(f); add('epub'); return out; }
    return out;
  }

  function matchShortcut(e, action) {
    var key = String(state.shortcuts[action] || DEFAULT_SHORTCUTS[action] || '').trim();
    if (!key) return false;
    var parts = key.toLowerCase().split('+').map(function (p) { return p.trim(); }).filter(Boolean);
    var main = parts.length ? parts[parts.length - 1] : '';
    if (!main) return false;
    var needCtrl = parts.indexOf('ctrl') !== -1 || parts.indexOf('control') !== -1;
    var needShift = parts.indexOf('shift') !== -1;
    var needAlt = parts.indexOf('alt') !== -1;
    var needMeta = parts.indexOf('meta') !== -1 || parts.indexOf('cmd') !== -1 || parts.indexOf('command') !== -1;
    if (!!e.ctrlKey !== needCtrl) return false;
    if (!!e.shiftKey !== needShift) return false;
    if (!!e.altKey !== needAlt) return false;
    if (!!e.metaKey !== needMeta) return false;
    var pressed = (e.key && e.key.length === 1) ? (e.shiftKey ? e.key : e.key.toLowerCase()) : String(e.key || '').toLowerCase();
    return pressed === main;
  }

  // ── Status & toast ─────────────────────────────────────────────
  var statusTimer = null;
  function setStatus(msg, persistent) {
    var els = ensureEls();
    if (!els.status) return;
    if (statusTimer) { clearTimeout(statusTimer); statusTimer = null; }
    var text = String(msg || '');
    els.status.textContent = text;
    if (text) {
      els.status.classList.add('visible');
      if (!persistent) {
        statusTimer = setTimeout(function () {
          els.status.classList.remove('visible');
        }, 4000);
      }
    } else {
      els.status.classList.remove('visible');
    }
  }

  function showToast(msg, durationMs) {
    var toast = document.getElementById('booksReaderToast');
    if (!toast) {
      toast = document.createElement('div');
      toast.id = 'booksReaderToast';
      toast.className = 'booksReaderToast';
      var rv = ensureEls().readerView;
      if (rv) rv.appendChild(toast);
    }
    toast.textContent = String(msg || '');
    toast.classList.add('visible');
    clearTimeout(toast._timer);
    toast._timer = setTimeout(function () { toast.classList.remove('visible'); }, durationMs || 2000);
  }

  // ── Error banner ───────────────────────────────────────────────
  function showErrorBanner(title, detail) {
    var els = ensureEls();
    if (!els.errorBanner) return;
    if (els.errorTitle) els.errorTitle.textContent = String(title || 'Unable to open book');
    if (els.errorDetail) els.errorDetail.textContent = String(detail || '');
    if (els.errorDetail) els.errorDetail.title = String(detail || '');
    els.errorBanner.classList.remove('hidden');
  }

  function hideErrorBanner() {
    var els = ensureEls();
    if (els.errorBanner) els.errorBanner.classList.add('hidden');
  }

  // ── Settings persistence ───────────────────────────────────────
  async function loadSettings() {
    try {
      var res = await Tanko.api.booksSettings.get();
      var incoming = (res && typeof res === 'object' && res.settings && typeof res.settings === 'object') ? res.settings : {};
      // RCSS_INTEGRATION: migrate old pixel-based fontSize to percentage
      if (incoming.fontSize && incoming.fontSize <= 30) {
        incoming.fontSize = Math.round((incoming.fontSize / 16) * 100);
      }
      // RCSS_INTEGRATION: migrate old pixel margin to factor
      if (incoming.margin && incoming.margin > 4) {
        incoming.margin = Math.max(0.5, Math.min(4.0, 0.5 + (incoming.margin / 80) * 1.5));
        incoming.margin = Math.round(incoming.margin * 4) / 4;
      }
      // RCSS_INTEGRATION: migrate old fontFamily identifiers
      if (incoming.fontFamily === 'serif') incoming.fontFamily = 'oldStyleTf';
      if (incoming.fontFamily === 'sans-serif') incoming.fontFamily = 'sansTf';
      if (incoming.fontFamily === 'monospace') incoming.fontFamily = 'monospaceTf';
      // Max line width persisted in localStorage for quick UI restore across sessions/builds
      try {
        var _mlwRaw = localStorage.getItem('books_maxLineWidth');
        if (_mlwRaw != null && _mlwRaw !== '') {
          var _mlw = Number(_mlwRaw);
          if (Number.isFinite(_mlw)) {
            _mlw = Math.round(_mlw / 50) * 50;
            incoming.maxLineWidth = Math.max(400, Math.min(1600, _mlw));
          }
        }
      } catch (e) { /* swallow */ }
      state.settings = Object.assign({}, DEFAULT_SETTINGS, incoming);
    } catch (e) {
      state.settings = Object.assign({}, DEFAULT_SETTINGS);
    }
  }

  // Readest-ish: per-book view settings (font/theme/layout) stored locally.
  // This keeps global defaults, but lets each book remember its own reading vibe.
  function bookSettingsKey(book) {
    try {
      if (book && book.id) return 'tankoban.bookViewSettings:' + String(book.id);
      if (book && book.path) return 'tankoban.bookViewSettings:' + String(book.path);
    } catch (e) {}
    return '';
  }

  function pickViewSettings(settingsObj) {
    var s = (settingsObj && typeof settingsObj === 'object') ? settingsObj : {};
    return {
      theme: s.theme,
      fontFamily: s.fontFamily,
      fontSize: s.fontSize,
      lineHeight: s.lineHeight,
      margin: s.margin,
      maxLineWidth: s.maxLineWidth,
      columnMode: s.columnMode,
      flowMode: s.flowMode,
      letterSpacing: s.letterSpacing,
      wordSpacing: s.wordSpacing,
      paraSpacing: s.paraSpacing,
      paraIndent: s.paraIndent,
      bodyHyphens: s.bodyHyphens,
      textAlign: s.textAlign,
      zoom: s.zoom,
      fitMode: s.fitMode,
    };
  }

  function loadBookViewSettings(book) {
    var key = bookSettingsKey(book);
    if (!key) return;
    try {
      var raw = localStorage.getItem(key);
      // FIX_AUDIT: if no per-book settings, fall back to global defaults
      if (!raw) {
        try { raw = localStorage.getItem('tankoban.globalViewSettings'); } catch (e2) {}
      }
      if (!raw) return;
      var parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== 'object') return;
      // FIX-READER-GAPS: migrate old wrong-name keys from earlier pickViewSettings bug
      if (parsed.columns !== undefined && parsed.columnMode === undefined) parsed.columnMode = parsed.columns;
      if (parsed.hyphens !== undefined && parsed.bodyHyphens === undefined) parsed.bodyHyphens = parsed.hyphens;
      // Merge per-book over global settings
      state.settings = Object.assign({}, state.settings, parsed);
      if (state.settings && state.settings.maxLineWidth != null) {
        var _bw = Number(state.settings.maxLineWidth);
        if (Number.isFinite(_bw)) {
          _bw = Math.round(_bw / 50) * 50;
          state.settings.maxLineWidth = Math.max(400, Math.min(1600, _bw));
        }
      }
    } catch (e) { /* swallow */ }
  }

  function persistBookViewSettings() {
    var key = bookSettingsKey(state.book);
    var payload = pickViewSettings(state.settings);
    var payloadStr = JSON.stringify(payload);
    if (key) {
      try { localStorage.setItem(key, payloadStr); } catch (e) { /* swallow */ }
    }
    // FIX_AUDIT: always update global defaults so new books inherit last-used settings
    try { localStorage.setItem('tankoban.globalViewSettings', payloadStr); } catch (e) { /* swallow */ }
  }

  async function persistSettings() {
    try {
      await Tanko.api.booksSettings.save(Object.assign({}, state.settings));
    } catch (e) { /* swallow */ }

    // Also persist per-book view settings (safe no-op if no book is open)
    persistBookViewSettings();
  }

  // ── Progress save ──────────────────────────────────────────────
  async function saveProgress() {
    if (!state.open || !state.engine || !state.book) return;
    if (typeof state.engine.getLocator !== 'function') return;
    if (state.suspendProgressSave) { state.pendingProgressSave = true; return; }
    if (state.progressSaveInFlight) { state.progressSaveReschedule = true; return; }

    state.progressSaveInFlight = true;
    try {
      var locator = null;
      try { locator = await state.engine.getLocator(); } catch (e) { /* swallow */ }
      var pageHint = (state.engine && typeof state.engine.getPageHint === 'function') ? state.engine.getPageHint() : null;

      if (locator) {
        locator.flowMode = String(state.settings.flowMode || 'paginated');
        locator.chapterReadState = Object.assign({}, state.chapterReadState || {}); // BUILD_CHAP_PERSIST
      }

      // BOOK_FIX 1.1: derive flat agents/CONTRACTS.md fields from the locator so
      // the library continue strip can read the same record the reader writes.
      var loc = locator && typeof locator === 'object' ? locator : {};
      var locInner = loc.location && typeof loc.location === 'object' ? loc.location : {};
      var rawFrac = Number(loc.fraction);
      var scrollFraction = Number.isFinite(rawFrac) ? Math.max(0, Math.min(1, rawFrac)) : 0;
      var rawCurrent = Number(locInner.current);
      var rawTotal = Number(locInner.total);
      var chapter = Number.isFinite(rawCurrent) ? rawCurrent : 0;
      var chapterCount = Number.isFinite(rawTotal) ? rawTotal : 0;
      var finished = scrollFraction >= 0.97 ||
        (chapterCount > 0 && chapter >= chapterCount - 1 && scrollFraction >= 0.999);
      var normPath = String(state.book.path || '').replace(/\\/g, '/');
      var bookmarksFlat = Array.isArray(state.bookmarks) ? state.bookmarks.slice() : [];

      var payload = {
        chapter: chapter,
        chapterCount: chapterCount,
        scrollFraction: scrollFraction,
        percent: scrollFraction * 100,
        finished: !!finished,
        path: normPath,
        bookmarks: bookmarksFlat,
        locator: locator || null,
        format: state.book.format,
        mediaType: 'book',
        pageHint: pageHint || null,
        bookMeta: {
          title: state.book.title,
          path: normPath,
          format: state.book.format,
          mediaType: 'book',
          series: state.book.series || '',
          seriesId: state.book.seriesId || '',
        },
      };

      try { await Tanko.api.booksProgress.save(state.book.id, payload); } catch (e) { /* swallow */ }
    } finally {
      state.progressSaveInFlight = false;
      if (state.progressSaveReschedule) {
        state.progressSaveReschedule = false;
        if (state.suspendProgressSave) state.pendingProgressSave = true;
        else queueSaveProgress(60);
      }
    }
  }

  function queueSaveProgress(delayMs) {
    if (state.saveTimer) { clearTimeout(state.saveTimer); state.saveTimer = null; }
    state.saveTimer = setTimeout(function () { state.saveTimer = null; saveProgress(); }, delayMs || 60);
  }

  function setSuspendProgressSave(suspend, reason) {
    state.suspendProgressSave = !!suspend;
    state.suspendProgressSaveReason = String(reason || '');
    if (!suspend && state.pendingProgressSave) {
      state.pendingProgressSave = false;
      queueSaveProgress(60);
    }
  }

  // ── Shortcuts persistence ──────────────────────────────────────
  function saveShortcuts() {
    try {
      Tanko.api.booksUi.save({ shortcuts: Object.assign({}, state.shortcuts) }).catch(function () {});
    } catch (e) { /* swallow */ }
  }

  // ── Export via window ──────────────────────────────────────────
  window.booksReaderState = {
    state: state,
    bus: bus,
    ensureEls: ensureEls,
    qs: qs,
    qsv: qsv,
    // Constants
    DEFAULT_SETTINGS: DEFAULT_SETTINGS,
    DEFAULT_SHORTCUTS: DEFAULT_SHORTCUTS,
    ANNOT_COLORS: ANNOT_COLORS,
    ANNOT_STYLES: ANNOT_STYLES,
    // Helpers
    escHtml: escHtml,
    cleanErrMessage: cleanErrMessage,
    clamp01: clamp01,
    isPdfOpen: isPdfOpen,
    isEpubOrTxtOpen: isEpubOrTxtOpen,
    ttsSupported: ttsSupported,
    normalizeBookInput: normalizeBookInput,
    getEngineCandidates: getEngineCandidates,
    matchShortcut: matchShortcut,
    // Status
    setStatus: setStatus,
    showToast: showToast,
    showErrorBanner: showErrorBanner,
    hideErrorBanner: hideErrorBanner,
    // Settings
    loadSettings: loadSettings,
    persistSettings: persistSettings,
    loadBookViewSettings: loadBookViewSettings,
    // Progress
    saveProgress: saveProgress,
    setSuspendProgressSave: setSuspendProgressSave,
    saveShortcuts: saveShortcuts,
  };
})();
