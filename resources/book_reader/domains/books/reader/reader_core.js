// BUILD_OVERHAUL: Core orchestrator — open/close lifecycle, module wiring
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  var hudHideTimer = null;
  var isReadingAction = false;   // TASK2: suppress HUD during scroll/page-turn
  var REVEAL_ZONE = 48; // FIX_HUD: top/bottom edge zone in px that triggers HUD
  var _lastMouseActivityMs = 0; // OPT1: throttle mousemove to reduce getBoundingClientRect calls

  // ── Module registry ──────────────────────────────────────────

  var modules = [
    window.booksReaderOverlays,
    window.booksReaderAppearance,
    window.booksReaderDict,
    window.booksReaderSearch,
    window.booksReaderBookmarks,
    window.booksReaderAnnotations,
    window.booksReaderToc,
    window.booksReaderNav,
    window.booksReaderSidebar,
    window.booksReaderRuler,
    // LISTEN_P0: TTS removed from reader — moved to dedicated Listening mode
    window.booksReaderKeyboard,
  ];



  // ── TTS state bridge — emits bus event so bar shows/hides ───
  // Transport buttons, mega panel, voices, highlights are all wired by listening_player.js.
  // This IIFE only sets onStateChange to bridge TTS events into the reader bus.
  (function bindReaderTtsState() {
    var tts = window.booksTTS;
    console.log('[TTS-BAR] bindReaderTtsState: tts=' + !!tts);
    if (!tts) return;
    if (window.__readerTtsBridgeBound) return;
    window.__readerTtsBridgeBound = true;

    var onState = function (status, info) {
      console.log('[TTS-BAR] onStateChange: ' + status);
      try { bus.emit('reader:tts-state', status); } catch (e) {}
      try { handleReaderTtsState(status); } catch (e2) {}
      // Sync play/pause icon directly (listening_player.js overwrites this on Listen click,
      // but before that the "Start TTS from here" path needs icons to update)
      try {
        var ppBtn = document.getElementById('lpTtsPlayPause');
        if (ppBtn) {
          var isPlaying = (status === 'playing' || status === 'section_transition');
          var isPaused = isTtsPausedState(status);
          ppBtn.innerHTML = isPlaying
            ? '<svg viewBox="0 0 16 18" xmlns="http://www.w3.org/2000/svg"><rect x="2" y="0.87" width="4" height="16.37" rx="0.75" fill="currentColor"/><rect x="10" y="0.87" width="4" height="16.37" rx="0.75" fill="currentColor"/></svg>'
: '<svg viewBox="0 0 16 18" xmlns="http://www.w3.org/2000/svg"><path d="M14.3195 7.73218L3.06328 0.847019C2.82722 0.703112 2.55721 0.624413 2.2808 0.618957C2.00439 0.6135 1.73148 0.681481 1.48992 0.81596C1.24837 0.950439 1.04682 1.1466 0.905848 1.38442C0.764877 1.62225 0.689531 1.89322 0.6875 2.16968V15.94C0.689531 16.2164 0.764877 16.4874 0.905848 16.7252C1.04682 16.9631 1.24837 17.1592 1.48992 17.2937C1.73148 17.4282 2.00439 17.4962 2.2808 17.4907C2.55721 17.4853 2.82722 17.4066 3.06328 17.2626L14.3195 10.3775C14.5465 10.2393 14.7341 10.0451 14.8643 9.81344C14.9945 9.58179 15.0628 9.32055 15.0628 9.05483C15.0628 8.78912 14.9945 8.52787 14.8643 8.29623C14.7341 8.06458 14.5465 7.87034 14.3195 7.73218ZM2.5625 15.3712V2.73843L12.8875 9.05483L2.5625 15.3712Z" fill="currentColor"/></svg>';
          ppBtn.title = isPlaying ? 'Pause' : (isPaused ? 'Resume' : 'Play');
        }
      } catch (e3) {}
    };

    var onProgress = function (info) {
      try { handleReaderTtsProgress(info); } catch (e3) {}
    };

    if (typeof tts.on === 'function') {
      try { tts.on('stateChange', onState); } catch (e4) {}
      try { tts.on('progress', onProgress); } catch (e5) {}
    } else {
      tts.onStateChange = onState;
      tts.onProgress = onProgress;
    }
  })();


  // ── Back to TTS Location floating button ──────────────────────

  var ttsReturnUi = {
    btn: null,
    visible: false,
    ttsLocator: null,
    readerLocator: null,
    lastTtsLocatorKey: '',
    graceUntilMs: 0,
    timer: 0,
  };



  function isTtsPausedState(status) {
    status = String(status || '');
    return status === 'paused' || /-paused$/.test(status);
  }

  function getTtsReturnBtn() {
    if (ttsReturnUi.btn && document.body.contains(ttsReturnUi.btn)) return ttsReturnUi.btn;
    ttsReturnUi.btn = document.getElementById('booksReaderReturnTts');
    return ttsReturnUi.btn;
  }

  function clearTtsReturnTimer() {
    if (!ttsReturnUi.timer) return;
    try { clearTimeout(ttsReturnUi.timer); } catch (e) {}
    ttsReturnUi.timer = 0;
  }

  function cloneLocator(loc) {
    if (!loc || typeof loc !== 'object') return null;
    var out = {};
    if (loc.cfi != null) out.cfi = String(loc.cfi);
    if (loc.href != null) out.href = String(loc.href);
    if (Number.isFinite(Number(loc.fraction))) out.fraction = Number(loc.fraction);
    return (out.cfi || out.href || Number.isFinite(out.fraction)) ? out : null;
  }

  function normalizeLocatorCandidate(input) {
    var src = input && input.locator ? input.locator : input;
    if (!src || typeof src !== 'object') return null;
    var out = {};

    if (src.cfi != null) out.cfi = String(src.cfi);
    if (src.href != null) out.href = String(src.href);
    if (Number.isFinite(Number(src.fraction))) out.fraction = Number(src.fraction);

    if (src.current && typeof src.current === 'object') {
      if (!out.cfi && src.current.cfi != null) out.cfi = String(src.current.cfi);
      if (!out.href && src.current.href != null) out.href = String(src.current.href);
    }

    if (src.location && typeof src.location === 'object') {
      if (!out.cfi && src.location.cfi != null) out.cfi = String(src.location.cfi);
      if (!out.href && src.location.href != null) out.href = String(src.location.href);
    }

    return (out.cfi || out.href || Number.isFinite(out.fraction)) ? out : null;
  }

  function locatorKey(loc) {
    var l = normalizeLocatorCandidate(loc);
    if (!l) return '';
    if (l.cfi) return 'cfi:' + l.cfi;
    var frac = Number.isFinite(Number(l.fraction)) ? Math.round(Number(l.fraction) * 10000) / 10000 : NaN;
    return 'href:' + String(l.href || '') + '|f:' + (Number.isFinite(frac) ? String(frac) : '');
  }

  function sameLocator(a, b) {
    var la = normalizeLocatorCandidate(a);
    var lb = normalizeLocatorCandidate(b);
    if (!la || !lb) return false;

    if (la.cfi && lb.cfi && la.cfi === lb.cfi) return true;

    var hrefA = String(la.href || '');
    var hrefB = String(lb.href || '');
    if (hrefA && hrefB && hrefA !== hrefB) return false;

    var fa = Number(la.fraction);
    var fb = Number(lb.fraction);
    var hasFa = Number.isFinite(fa);
    var hasFb = Number.isFinite(fb);

    if (hasFa && hasFb) return Math.abs(fa - fb) <= 0.0025;

    if (hrefA && hrefB) return true;
    return false;
  }

  function isReaderTtsPausedVariant(status) {
    return /(?:^|-)paused$/.test(String(status || ''));
  }

  function isReaderTtsActiveState(status) {
    var s = String(status || '');
    return s === 'playing' || s === 'section_transition' || isReaderTtsPausedVariant(s);
  }

  function isReaderTtsActive() {
    try {
      var tts = window.booksTTS;
      if (!tts || typeof tts.getState !== 'function') return false;
      var s = String(tts.getState() || '');
      return isReaderTtsActiveState(s);
    } catch (e) {
      return false;
    }
  }

  function setTtsReturnButtonVisible(show) {
    var btn = getTtsReturnBtn();
    if (!btn) return;
    var on = !!show;
    ttsReturnUi.visible = on;
    btn.setAttribute('aria-hidden', on ? 'false' : 'true');
    btn.classList.toggle('is-visible', on);
  }

  function resetTtsReturnUi(clearLocators) {
    clearTtsReturnTimer();
    ttsReturnUi.graceUntilMs = 0;
    if (clearLocators) {
      ttsReturnUi.ttsLocator = null;
      ttsReturnUi.readerLocator = null;
      ttsReturnUi.lastTtsLocatorKey = '';
    }
    setTtsReturnButtonVisible(false);
  }

  function scheduleEvaluateTtsReturn(delayMs) {
    clearTtsReturnTimer();
    var delay = Math.max(0, Number(delayMs) || 0);
    ttsReturnUi.timer = setTimeout(function () {
      ttsReturnUi.timer = 0;
      evaluateTtsReturnButton();
    }, delay);
  }

  function evaluateTtsReturnButton() {
    if (!RS.state.open) {
      setTtsReturnButtonVisible(false);
      return;
    }
    if (!isReaderTtsActive()) {
      setTtsReturnButtonVisible(false);
      return;
    }

    var ttsLoc = normalizeLocatorCandidate(ttsReturnUi.ttsLocator);
    if (!ttsLoc) {
      setTtsReturnButtonVisible(false);
      return;
    }

    var readerLoc = normalizeLocatorCandidate(ttsReturnUi.readerLocator);
    if (!readerLoc) {
      try {
        var eng = RS.state.engine;
        if (eng && typeof eng.getLocator === 'function') {
          eng.getLocator().then(function (loc) {
            ttsReturnUi.readerLocator = normalizeLocatorCandidate(loc);
            evaluateTtsReturnButton();
          }).catch(function () {});
        }
      } catch (e) {}
      return;
    }

    if (sameLocator(readerLoc, ttsLoc)) {
      setTtsReturnButtonVisible(false);
      return;
    }

    var now = Date.now();
    if (ttsReturnUi.graceUntilMs && now < ttsReturnUi.graceUntilMs) {
      scheduleEvaluateTtsReturn(ttsReturnUi.graceUntilMs - now + 20);
      return;
    }

    setTtsReturnButtonVisible(true);
  }

  function handleReaderTtsProgress(info) {
    if (!RS.state.open) return;
    var loc = normalizeLocatorCandidate(info && info.locator ? info.locator : null);
    if (!loc) {
      try {
        var tts = window.booksTTS;
        if (tts && typeof tts.getStateSnapshot === 'function') {
          var snap = tts.getStateSnapshot();
          loc = normalizeLocatorCandidate(snap && snap.locator ? snap.locator : null);
        }
      } catch (e) {}
    }
    if (!loc) return;

    var nextKey = locatorKey(loc);
    var changed = !!nextKey && nextKey !== ttsReturnUi.lastTtsLocatorKey;
    ttsReturnUi.ttsLocator = cloneLocator(loc);

    if (changed) {
      ttsReturnUi.lastTtsLocatorKey = nextKey;
      ttsReturnUi.graceUntilMs = Date.now() + 2000; // ignore TTS-initiated follow/relocate briefly
    }

    evaluateTtsReturnButton();
  }

  function handleReaderTtsState(status) {
    var s = String(status || '');
    if (!isReaderTtsActiveState(s)) {
      resetTtsReturnUi(true);
      return;
    }

    try {
      var tts = window.booksTTS;
      if (tts && typeof tts.getStateSnapshot === 'function') {
        var snap = tts.getStateSnapshot();
        var loc = normalizeLocatorCandidate(snap && snap.locator ? snap.locator : null);
        if (loc) {
          ttsReturnUi.ttsLocator = cloneLocator(loc);
          if (!ttsReturnUi.lastTtsLocatorKey) ttsReturnUi.lastTtsLocatorKey = locatorKey(loc);
        }
      }
    } catch (e) {}

    evaluateTtsReturnButton();
  }

  function handleReaderRelocated(detail) {
    ttsReturnUi.readerLocator = normalizeLocatorCandidate(detail);
    evaluateTtsReturnButton();
  }

  function handleBackToTtsLocationClick(ev) {
    if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();
    if (ev && typeof ev.stopPropagation === 'function') ev.stopPropagation();

    var state = RS.state;
    if (!state || !state.open || !state.engine || typeof state.engine.goTo !== 'function') return;

    var target = cloneLocator(ttsReturnUi.ttsLocator);
    if (!target) return;

    setTtsReturnButtonVisible(false);
    ttsReturnUi.graceUntilMs = Date.now() + 2000;

    try {
      Promise.resolve(state.engine.goTo(target)).catch(function () {});
    } catch (e) {}
  }

  // ── Show / hide reader view ──────────────────────────────────

  function showReader(show) {
    var els = RS.ensureEls();
    if (!els.readerView) return;
    els.readerView.classList.toggle('hidden', !show);
    document.body.classList.toggle('inBooksReader', !!show);
    if (!show && els.readerView) {
      els.readerView.classList.remove('br-hud-hidden');
    }
  }

  // ── HUD auto-hide ────────────────────────────────────────────

  function _isTtsPausedLike(status) {
    var s = String(status || '');
    return s === 'paused'
      || s === 'stop-paused'
      || s === 'backward-paused'
      || s === 'forward-paused'
      || s === 'setrate-paused'
      || s === 'setvoice-paused';
  }

  function _isTtsPlayingLike(status) {
    var s = String(status || '');
    return s === 'playing' || s === 'section_transition';
  }

  function _isTtsActiveLike(status) {
    var s = String(status || '');
    return _isTtsPlayingLike(s) || _isTtsPausedLike(s);
  }

  function shouldKeepHudVisible() {
    var els = RS.ensureEls();
    var OV = window.booksReaderOverlays;
    if (OV && OV.isOpen && OV.isOpen()) return true;
    if (els.gotoOverlay && !els.gotoOverlay.classList.contains('hidden')) return true;
    if (els.chapterTransition && !els.chapterTransition.classList.contains('hidden')) return true;
    if (els.annotPopup && !els.annotPopup.classList.contains('hidden')) return true;
    if (els.dictPopup && !els.dictPopup.classList.contains('hidden')) return true;
    // LISTEN_P3: ttsDiag/ttsMega removed in P0 — no longer checked here
    return false;
  }

  function setHudVisible(visible) {
    var els = RS.ensureEls();
    if (!els.readerView) return;
    // FIX_HUD: don't show toolbar/footer while sidebar is open — it overlaps sidebar tabs
    if (visible && RS.state.sidebarOpen) return;
    // BOOK_FIX 5.1: pinned HUD never hides. `visible===true` still applies —
    // pin keeps the toolbar up, not nothing.
    if (!visible && RS.state.hudPinned) return;
    els.readerView.classList.toggle('br-hud-hidden', !visible);
  }

  function scheduleHudAutoHide() {
    if (hudHideTimer) clearTimeout(hudHideTimer);
    if (!RS.state.open) return;
    // BOOK_FIX 5.1: pinned HUD disables the inactivity timer — Calibre + Apple
    // Books parity for "keep chrome visible while I'm doing something that
    // needs it" (e.g. adjusting settings, scanning TOC).
    if (RS.state.hudPinned) return;
    hudHideTimer = setTimeout(function () {
      if (!RS.state.open) return;
      if (shouldKeepHudVisible()) {
        scheduleHudAutoHide();
        return;
      }
      // FIX_AUDIT: hide HUD automatically after 3 seconds of inactivity.
      setHudVisible(false);
    }, 3000);
  }

  // BOOK_FIX 5.1: pin/unpin toggle — called from reader_keyboard.js when the
  // user presses Shift+T. Returns the new pinned state so the caller can show
  // a toast. Setting pin-on keeps HUD up indefinitely; pin-off restarts the
  // auto-hide timer so the HUD fades naturally after 3s.
  function toggleHudPin() {
    var state = RS.state;
    state.hudPinned = !state.hudPinned;
    if (state.hudPinned) {
      setHudVisible(true);
      if (hudHideTimer) { clearTimeout(hudHideTimer); hudHideTimer = null; }
    } else {
      scheduleHudAutoHide();
    }
    return !!state.hudPinned;
  }

  function onAnyUserActivity() {
    if (!RS.state.open) return;
    setHudVisible(true);
    scheduleHudAutoHide();
    bus.emit('reader:user-activity');
  }

  // TASK2: reading actions (scroll, page-turn) hide the HUD instead of showing it
  function onReadingAction() {
    if (!RS.state.open) return;
    if (shouldKeepHudVisible()) return;
    isReadingAction = true;
    setHudVisible(false);
    if (hudHideTimer) { clearTimeout(hudHideTimer); hudHideTimer = null; }
  }

  function _activityClientY(e) {
    if (!e) return NaN;
    var y = Number.NaN;
    try {
      if (Number.isFinite(Number(e.clientY))) {
        y = Number(e.clientY);
      } else if (e.touches && e.touches[0] && Number.isFinite(Number(e.touches[0].clientY))) {
        y = Number(e.touches[0].clientY);
      } else if (e.changedTouches && e.changedTouches[0] && Number.isFinite(Number(e.changedTouches[0].clientY))) {
        y = Number(e.changedTouches[0].clientY);
      }
    } catch (err) {}
    if (!Number.isFinite(y)) return Number.NaN;
    try {
      var v = e.view || (e.target && e.target.ownerDocument && e.target.ownerDocument.defaultView) || null;
      var frame = v && v.frameElement;
      if (frame && typeof frame.getBoundingClientRect === 'function') {
        var fr = frame.getBoundingClientRect();
        y = fr.top + y;
      }
    } catch (err2) {}
    return y;
  }

  function _activityClientX(e) {
    if (!e) return Number.NaN;
    var x = Number.NaN;
    try {
      if (Number.isFinite(Number(e.clientX))) {
        x = Number(e.clientX);
      } else if (e.touches && e.touches[0] && Number.isFinite(Number(e.touches[0].clientX))) {
        x = Number(e.touches[0].clientX);
      } else if (e.changedTouches && e.changedTouches[0] && Number.isFinite(Number(e.changedTouches[0].clientX))) {
        x = Number(e.changedTouches[0].clientX);
      }
    } catch (err) {}
    if (!Number.isFinite(x)) return Number.NaN;
    try {
      var v = e.view || (e.target && e.target.ownerDocument && e.target.ownerDocument.defaultView) || null;
      var frame = v && v.frameElement;
      if (frame && typeof frame.getBoundingClientRect === 'function') {
        var fr = frame.getBoundingClientRect();
        x = fr.left + x;
      }
    } catch (err2) {}
    return x;
  }

  function _isRevealZonePoint(clientX, clientY) {
    var els = RS.ensureEls();
    if (!els.readerView || !Number.isFinite(Number(clientY))) return false;
    var rect = els.readerView.getBoundingClientRect();
    if (Number.isFinite(Number(clientX))) {
      if (Number(clientX) < rect.left || Number(clientX) > rect.right) return false;
    }
    if (Number(clientY) < rect.top || Number(clientY) > rect.bottom) return false;
    var localY = Number(clientY) - rect.top;
    var inTopZone = localY < REVEAL_ZONE;
    var inBottomZone = localY > (rect.height - REVEAL_ZONE);
    // Don't trigger HUD from bottom zone when TTS bar is visible
    if (inBottomZone && els.ttsBar && !els.ttsBar.classList.contains('hidden')) {
      return false;
    }
    return !!(inTopZone || inBottomZone);
  }

  function onEngineUserActivity(e) {
    if (!RS.state.open) return;
    // FIX_HUD: click/tap inside reading content closes sidebar
    if (RS.state.sidebarOpen && e && e.type === 'pointerdown') {
      bus.emit('sidebar:close');
      return;
    }
    var els = RS.ensureEls();
    if (!els.readerView) return;
    var hidden = els.readerView.classList.contains('br-hud-hidden');
    if (!hidden) {
      // While HUD is visible, any activity should reset inactivity timer.
      onAnyUserActivity();
      return;
    }
    // When hidden, only edge-zone motion reveals HUD.
    var x = _activityClientX(e);
    var y = _activityClientY(e);
    if (_isRevealZonePoint(x, y)) onAnyUserActivity();
    else bus.emit('reader:user-activity');
  }

  // FIX_HUD: show HUD only on top/bottom edge when hidden; otherwise keep-alive on movement
  function onMouseActivity(e) {
    if (!RS.state.open) return;
    // OPT1: throttle to ~20/sec to reduce getBoundingClientRect calls
    var now = Date.now();
    if (now - _lastMouseActivityMs < 50) return;
    _lastMouseActivityMs = now;
    if (isReadingAction) { isReadingAction = false; }
    var els = RS.ensureEls();
    if (!els.readerView) return;
    var hidden = els.readerView.classList.contains('br-hud-hidden');
    if (!hidden) {
      onAnyUserActivity();
      return;
    }
    if (_isRevealZonePoint(e && e.clientX, e && e.clientY)) onAnyUserActivity();
    else bus.emit('reader:user-activity');
  }

  // ── Fullscreen ───────────────────────────────────────────────

  async function toggleReaderFullscreen() {
    try { await Tanko.api.window.toggleFullscreen(); } catch (e) {}
  }

  // ── Destroy engine ───────────────────────────────────────────

  async function destroyCurrentEngine() {
    var state = RS.state;
    if (!state.engine) return;
    try {
      if (typeof state.engine.destroy === 'function') await state.engine.destroy();
    } catch (e) {}
    state.engine = null;
  }

  // ── Close ────────────────────────────────────────────────────

  async function close(opts) {
    var options = (opts && typeof opts === 'object') ? opts : {};
    var save = options.save !== false;
    var state = RS.state;
    var wasActive = !!state.open || !!state.opening || !!state.engine;

    if (!wasActive) {
      showReader(false);
      bus.emit('appearance:sync');
      return false;
    }

    resetTtsReturnUi(true);

    // Notify modules before cleanup
    for (var i = 0; i < modules.length; i++) {
      if (modules[i] && typeof modules[i].onClose === 'function') {
        try { modules[i].onClose(); } catch (e) {}
      }
    }

    // Hard-stop narration on reader close (prevents background audio).
    try {
      var _bar = document.getElementById('lpTtsBar');
      if (_bar) { _bar.classList.add('hidden'); _bar.style.opacity = '0'; _bar.style.pointerEvents = 'none'; }
    } catch (e) {}
    try {
      var tts = window.booksTTS;
      if (tts) {
        try { if (typeof tts.stop === "function") tts.stop(); } catch (e) {}
        try { if (typeof tts.destroy === "function") tts.destroy(); } catch (e) {}
      }
    } catch (e) {}

    if (save && state.open) {
      try { await RS.saveProgress(); } catch (e) {}
    }

    await destroyCurrentEngine();

    state.open = false;
    state.opening = false;
    state.book = null;
    state.hudDragProgress = false;
    state.searchHits = [];
    state.searchActiveIndex = -1;
    state.tocItems = [];
    state.bookmarks = [];
    state.annotations = [];
    state.chapterReadState = {}; // BUILD_CHAP_PERSIST: reset between books
    if (hudHideTimer) { clearTimeout(hudHideTimer); hudHideTimer = null; }

    var els = RS.ensureEls();
    if (els.host) els.host.innerHTML = '';
    RS.hideErrorBanner();
    RS.setStatus('');
    showReader(false);
    bus.emit('appearance:sync');
    // BUILD_OVERHAUL: clear cached DOM refs between sessions
    state.els = null;

    try {
      if (options.silent !== true) {
        window.dispatchEvent(new CustomEvent('books-reader-closed'));
      }
    } catch (e) {}
    return true;
  }

  // ── Open ─────────────────────────────────────────────────────

  async function open(bookInput) {
    var book = RS.normalizeBookInput(bookInput);
    if (!book) throw new Error('invalid_book');

    var engines = window.booksReaderEngines || {};
    var candidates = RS.getEngineCandidates(book.format, engines);
    if (!candidates.length) {
      throw new Error('unsupported_format_' + book.format);
    }

    var els = RS.ensureEls();
    var state = RS.state;
    if (!els.host) throw new Error('reader_host_missing');

    await RS.loadSettings();
    await close({ save: false, silent: true });
    resetTtsReturnUi(true);

    // BOOK_FIX 1.1: resolve book.id to canonical SHA1[:20] of the absolute path
    // so the reader and library share a single record key (matches BooksPage
    // and BookSeriesView key shape). Keep the incoming id as a fallback if the
    // bridge call fails — better to write somewhere than nowhere.
    try {
      var canonicalId = await Tanko.api.booksProgress.keyFor(book.path);
      if (canonicalId && typeof canonicalId === 'string') book.id = canonicalId;
    } catch (e) { /* swallow — keep incoming id */ }

    state.book = book;
    // Per-book view settings (font/theme/layout)
    try { if (typeof RS.loadBookViewSettings === 'function') RS.loadBookViewSettings(book); } catch (e) {}
    state.opening = true;
    state.lastError = '';
    state.lastBookInput = bookInput;
    RS.hideErrorBanner();
    state.engine = null;

    var progress = null;
    try { progress = await Tanko.api.booksProgress.get(book.id); } catch (e) {}
    // FIX-PROG-ID: fallback to path-based key (older migrations / differing normalization)
    if (!progress && book && book.path) {
      try { progress = await Tanko.api.booksProgress.get(book.path); } catch (e2) {}
    }

    // BUILD_CHAP_PERSIST: restore chapter read state before renderToc() runs
    if (progress && progress.locator && progress.locator.chapterReadState &&
        typeof progress.locator.chapterReadState === 'object') {
      state.chapterReadState = Object.assign({}, progress.locator.chapterReadState);
    } else {
      state.chapterReadState = {};
    }

    if (els.title) {
      var fallbackTitle = String(book.path || '').split(/[\\/]/).pop() || 'Book';
      els.title.textContent = book.title || fallbackTitle;
    }
    RS.setStatus('Opening ' + book.format.toUpperCase() + '...', true);

    // Restore shortcuts
    try {
      var uiRes = await Tanko.api.booksUi.get();
      if (uiRes && uiRes.ui && uiRes.ui.shortcuts && typeof uiRes.ui.shortcuts === 'object') {
        Object.assign(state.shortcuts, uiRes.ui.shortcuts);
      }
    } catch (e) {}

    showReader(true);
    bus.emit('appearance:sync');

    try {
      var usedEngineId = '';
      var openErrors = [];
      var handleDictLookup = function (selectedText, ev) {
        var text = String(selectedText || '').trim();
        if (!text) {
          try {
            if (state.engine && typeof state.engine.getSelectedText === 'function') {
              text = String(state.engine.getSelectedText() || '').trim();
            }
          } catch (e2) {}
        }
        if (text) state._dictPendingWord = text;
        var Dict = window.booksReaderDict;
        if (Dict && typeof Dict.triggerDictLookupFromText === 'function') {
          try { Dict.triggerDictLookupFromText(text, ev); return; } catch (e3) {}
        }
        bus.emit('dict:lookup');
      };
      for (var i = 0; i < candidates.length; i++) {
        var candidate = candidates[i];
        try {
          state.engine = candidate.factory.create({
            filesRead: function (filePath) { return Tanko.api.files.read(filePath); },
            // Engine activity now feeds HUD keep-alive/edge-reveal logic.
            onUserActivity: function (ev) { onEngineUserActivity(ev); },
            onReadingAction: function () { onReadingAction(); }, // TASK2
            onDictLookup: function (selectedText, ev) { handleDictLookup(selectedText, ev); },
          });

          // HOTFIX: avoid indefinite "Opening EPUB" hangs.
          // Some engine failures can result in a promise that never resolves.
          // Use a watchdog timeout so the UI can recover and show an error.
          var OPEN_TIMEOUT_MS = 20000;
          var __openTimeoutId = 0;
          try {
            await Promise.race([
              state.engine.open({
                book: book,
                host: els.host,
                locator: progress && progress.locator ? progress.locator : null,
                settings: Object.assign({}, state.settings),
              }),
              new Promise(function (_resolve, reject) {
                __openTimeoutId = setTimeout(function () {
                  reject(new Error('open_timeout_' + String(book.format || 'book')));
                }, OPEN_TIMEOUT_MS);
              }),
            ]);
          } finally {
            if (__openTimeoutId) { try { clearTimeout(__openTimeoutId); } catch (e) {} }
          }
          usedEngineId = String(candidate.id || '');
          break;
        } catch (errOpen) {
          openErrors.push({ engineId: String(candidate.id || ''), err: errOpen });
          try { await destroyCurrentEngine(); } catch (e) {}
          state.engine = null;
        }
      }
      if (!state.engine) {
        // Throw the first (primary engine) error, not the last fallback error
        var first = openErrors.length ? openErrors[0] : null;
        if (first && first.err) throw first.err;
        throw new Error('open_failed_' + book.format);
      }

      state.open = true;
      state.opening = false;
      setHudVisible(true);
      scheduleHudAutoHide();

      // Apply column mode on open
      var Appearance = window.booksReaderAppearance;
      if (Appearance) {
        Appearance.applySettings();
      }
      if (state.engine && typeof state.engine.setColumnMode === 'function') {
        state.engine.setColumnMode(state.settings.columnMode || 'auto');
      }

      // BUILD_OVERHAUL: wire iframe callbacks (annotation first, dictionary fallback)
      try {
        // FIX_DICT: text is captured at the iframe event source for reliable access
        state.engine.onDblClick = function (selectedText, ev) { handleDictLookup(selectedText, ev); };
        state.engine.onContextMenu = function (ev, selectedText) {
          if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();
          handleDictLookup(selectedText, ev);
        };
      } catch (e) {}

      // Notify modules
      for (var m = 0; m < modules.length; m++) {
        if (modules[m] && typeof modules[m].onOpen === 'function') {
          try { modules[m].onOpen(); } catch (e) {}
        }
      }

      if (usedEngineId === 'epub_legacy' || usedEngineId === 'pdf_legacy') {
        RS.setStatus('Compatibility engine active', false);
      } else {
        RS.setStatus('');
      }

      try {
        window.dispatchEvent(new CustomEvent('books-reader-opened', { detail: { bookId: book.id } }));
      } catch (e) {}
      return true;
    } catch (err) {
      var msg = RS.cleanErrMessage(err, 'Failed to open ' + book.format.toUpperCase());
      state.lastError = msg;
      try { console.error('[books-reader] open failed:', err); } catch (e) {}

      await destroyCurrentEngine();
      state.open = false;
      state.opening = false;
      state.book = null;

      var fileName = String(bookInput && bookInput.path || '').split(/[\\/]/).pop() || '';
      RS.showErrorBanner('Unable to open book', fileName ? (msg + ' \u2014 ' + fileName) : msg);
      RS.setStatus('');
      bus.emit('appearance:sync');

      try {
        window.dispatchEvent(new CustomEvent('books-reader-error', {
          detail: { bookId: book.id, format: book.format, message: msg },
        }));
      } catch (e) {}
      throw new Error(msg);
    }
  }

  // ── Bind ─────────────────────────────────────────────────────

  function bind() {
    var els = RS.ensureEls();

    // Back / close / minimize buttons
    if (els.backBtn && !els.backBtn.__booksBackBound) {
      els.backBtn.addEventListener('click', function () {
        try {
          if (window.__ebookNav && typeof window.__ebookNav.requestClose === 'function') {
            window.__ebookNav.requestClose();
            return;
          }
        } catch (e) {}
        close().catch(function () {});
      });
      els.backBtn.__booksBackBound = true;
    }
    els.minBtn && els.minBtn.addEventListener('click', function () { try { Tanko.api.window.minimize(); } catch (e) {} });
    // PER_VIEW_CHROME_FIX 2026-05-02 P5 — chrome Max-toggle button + icon
    // swap. Subscribe to windowMaximizeChanged so the icon (single square
    // ↔ overlapping squares) reflects the live MainWindow state, even when
    // toggled via Win+Up / Win+Down / OS taskbar / drag-to-screen-edge.
    els.maxBtn && els.maxBtn.addEventListener('click', function () { try { Tanko.api.window.toggleMaximize(); } catch (e) {} });
    function _setMaxIcon(isMax) {
      var ic = els.maxIcon;
      if (!ic) return;
      // Restore icon = two overlapping squares; Max icon = single square.
      // Both use stroke="currentColor" so they pick up theme + br-btn hover.
      if (isMax) {
        ic.innerHTML = '<rect x="6" y="9" width="9" height="9" rx="1"/><path d="M9 9V6h9v9h-3"/>';
        if (els.maxBtn) els.maxBtn.title = 'Restore';
      } else {
        ic.innerHTML = '<rect x="6" y="6" width="12" height="12" rx="1"/>';
        if (els.maxBtn) els.maxBtn.title = 'Maximize';
      }
    }
    try {
      Tanko.api.window.isMaximized().then(function (v) { _setMaxIcon(!!v); });
      if (typeof Tanko.api.window.onMaximizeChanged === 'function') {
        Tanko.api.window.onMaximizeChanged(_setMaxIcon);
      }
    } catch (e) {}
    els.closeBtn && els.closeBtn.addEventListener('click', function () { try { Tanko.api.window.close(); } catch (e) {} });

    // Safety-net progress saves: catch window close, app quit, tab switch
    window.addEventListener('beforeunload', function () {
      var state = RS.state;
      if (state.open && !state.suspendProgressSave) {
        try { RS.saveProgress(); } catch (e) {}
      }
    });
    document.addEventListener('visibilitychange', function () {
      if (document.hidden) {
        var state = RS.state;
        if (state.open && !state.suspendProgressSave) {
          try { RS.saveProgress(); } catch (e) {}
        }
      }
    });

    // Error banner actions
    els.errorRetry && els.errorRetry.addEventListener('click', function () {
      var state = RS.state;
      if (state.lastBookInput) open(state.lastBookInput).catch(function () {});
    });
    els.errorClose && els.errorClose.addEventListener('click', function () {
      RS.hideErrorBanner();
      close().catch(function () {});
    });

    var ttsReturnBtn = getTtsReturnBtn();
    if (ttsReturnBtn && !ttsReturnBtn.__booksReaderBound) {
      ttsReturnBtn.addEventListener('click', function (ev) { handleBackToTtsLocationClick(ev); });
      ttsReturnBtn.__booksReaderBound = true;
    }

    // Fullscreen button
    els.fsBtn && els.fsBtn.addEventListener('click', function () { toggleReaderFullscreen().catch(function () {}); });

    // Reader-owned narration toggle (no legacy listening HUD).
    els.listenBtn && els.listenBtn.addEventListener('click', function () {
      var tts = window.booksTTS;
      if (!tts) return;
      var st = RS.state;
      if (!st || !st.book) return;

      // Pause/resume active playback without re-initializing (avoids duplicate sessions).
      try {
        var preState = (typeof tts.getState === 'function') ? (tts.getState() || 'idle') : 'idle';
        if (preState === 'playing') { try { tts.pause(); } catch (e) {} return; }
        // FIX_TTS_STATE_VARIANTS: resume all paused variants to avoid duplicate init/play sessions.
        var isPausedVariant = (preState === 'paused') || /-paused$/.test(String(preState || ''));
        if (isPausedVariant) { try { tts.resume(); } catch (e) {} return; }
      } catch (e) {}

      // Ensure the narration engine is initialized against the active reader engine.
      // Stop any existing playback BEFORE init(), since init() can rebuild engine refs.
      try {
        try { if (typeof tts.stop === 'function') tts.stop(); } catch (e) {}
        tts.init({
          format: String((st.book && st.book.format) || 'epub').toLowerCase(),
          getHost: function () { return st.host || null; },
          getViewEngine: function () { return st.engine || null; },
          onNeedAdvance: function () {
            var eng = st.engine || null;
            if (!eng || typeof eng.advanceSection !== 'function') return Promise.resolve(false);
            return eng.advanceSection(1).then(function () { return true; }).catch(function () { return false; });
          },
        }).then(function () {
          // Restore saved rate/volume/voice (per-book with global fallback)
          var _bkKey = '';
          try { var _bid = st.book && (st.book.id || st.book.path); if (_bid) _bkKey = 'bk:' + encodeURIComponent(String(_bid)).slice(0, 180); } catch (e) {}
          try { var _r = parseFloat((_bkKey && localStorage.getItem('booksListen.' + _bkKey + '.Rate')) || localStorage.getItem('booksListen.Rate')); if (_r >= 0.5 && _r <= 2.0) tts.setRate(_r); } catch (e) {}
          try { var _v = parseFloat((_bkKey && localStorage.getItem('booksListen.' + _bkKey + '.Volume')) || localStorage.getItem('booksListen.Volume')); if (_v >= 0 && _v <= 1) tts.setVolume(_v); } catch (e) {}
          try {
            var _vc = (_bkKey && localStorage.getItem('booksListen.' + _bkKey + '.Voice')) || localStorage.getItem('booksListen.Voice') || 'en-US-AndrewNeural';
            tts.setVoice(_vc);
          } catch (e) {}
          var s = (typeof tts.getState === 'function') ? (tts.getState() || 'idle') : 'idle';
          // If there is a selection, start from selection. Otherwise start from current position.
          try {
            if (typeof tts.playFromSelection === 'function') {
              var selText = '';
              try {
                if (st.engine && typeof st.engine.getSelectedText === 'function') {
                  var _sel = st.engine.getSelectedText();
                  selText = String((_sel && typeof _sel === 'object') ? (_sel.text || '') : (_sel || '')).trim();
                }
              } catch (e) {}
              if (selText) {
                try { if (typeof tts.stop === 'function') tts.stop(); } catch (e) {}
                if (tts.playFromSelection(selText)) return;
              }
            }
          } catch (e) {}
          try { if (typeof tts.stop === 'function') tts.stop(); } catch (e) {}
          try { tts.play(0, { startFromVisible: true }); } catch (e) {}
        }).catch(function () {});
      } catch (e) {}
    });

    // BUILD_OVERHAUL: host-level dict/annot handlers live in dedicated modules

    // Bus events
    bus.on('reader:close', function () { close().catch(function () {}); });
    bus.on('reader:fullscreen', function () { toggleReaderFullscreen().catch(function () {}); });
    bus.on('reader:relocated', function (detail) { onReadingAction(); handleReaderRelocated(detail); }); // TASK2 + PROMPT2
    bus.on('reader:tts-state', function (status) {
      console.log('[TTS-BAR] bus reader:tts-state → ' + status);
      if (_isTtsPlayingLike(status)) scheduleHudAutoHide();
      try {
        var bar = document.getElementById('lpTtsBar');
        if (bar) {
          var show = _isTtsActiveLike(status);
          bar.classList.toggle('hidden', !show);
          if (!show) {
            bar.style.opacity = '0';
            bar.style.pointerEvents = 'none';
          } else {
            bar.style.pointerEvents = 'auto';
            bar.style.opacity = '1';
          }
          console.log('[TTS-BAR] bar show=' + show);
        }
      } catch (e) {}
    });
    bus.on('tts:show-return', function () {
      evaluateTtsReturnButton();
    });
    // FIX_HUD: hide toolbar when sidebar opens, restore when it closes
    bus.on('sidebar:toggled', function (isOpen) {
      if (isOpen) {
        setHudVisible(false);
        if (hudHideTimer) { clearTimeout(hudHideTimer); hudHideTimer = null; }
      } else {
        setHudVisible(true);
        scheduleHudAutoHide();
      }
    });

    if (els.readerView) {
      // FIX_HUD: only significant mouse movement triggers HUD show.
      // pointerdown / keydown / wheel / touchstart / scroll do NOT show HUD —
      // after auto-hide, reading actions keep HUD hidden until the user moves the mouse.
      els.readerView.addEventListener('mousemove', onMouseActivity, { passive: true });
      // Mouse movement inside reader iframes may not bubble to readerView; capture at document level too.
      document.addEventListener('mousemove', onMouseActivity, { passive: true, capture: true });
    }

    // Bind all sub-modules
    for (var i = 0; i < modules.length; i++) {
      if (modules[i] && typeof modules[i].bind === 'function') {
        try { modules[i].bind(); } catch (e) {}
      }
    }
  }

  // ── Initialize ───────────────────────────────────────────────

  bind();

  // ── Export (backwards-compatible API) ────────────────────────

  window.booksReaderController = {
    open: open,
    close: close,
    isOpen: function () { return !!RS.state.open; },
    isBusy: function () { return !!RS.state.opening; },
    getLastError: function () { return String(RS.state.lastError || ''); },
    saveProgress: RS.saveProgress,
    renderToc: function () {
      var Toc = window.booksReaderToc;
      if (Toc && typeof Toc.renderToc === 'function') return Toc.renderToc();
    },
    // BOOK_FIX 5.1: toolbar pin toggle — keyboard handler calls this on Shift+T.
    toggleHudPin: toggleHudPin,
    isHudPinned: function () { return !!RS.state.hudPinned; },
  };
})();
