// FIX-TTS05: TTS Pipeline Overhaul — Thorium queue model + CSS Highlight API + cancel flag
// Replaces ad-hoc iterator approach with deterministic pre-generated queue,
// CSS Custom Highlight API for BOTH sentence and word (no overlayer for TTS),
// and explicit cancel semantics (Thorium r2_cancel pattern).
// Edge-only: webspeech fallback removed entirely.
(function () {
  'use strict';

  var IDLE = 'idle';
  var PLAYING = 'playing';
  var PAUSED = 'paused';
  var STOP_PAUSED = 'stop-paused';
  var BACKWARD_PAUSED = 'backward-paused';
  var FORWARD_PAUSED = 'forward-paused';
  var SETRATE_PAUSED = 'setrate-paused';
  var SETVOICE_PAUSED = 'setvoice-paused';
  var PAUSED_STATES = [PAUSED, STOP_PAUSED, BACKWARD_PAUSED, FORWARD_PAUSED, SETRATE_PAUSED, SETVOICE_PAUSED];
  var ALL_TTS_STATES = [IDLE, PLAYING].concat(PAUSED_STATES);

  // TTS-F05
  var TTS_PRESETS = {
    natural: { rate: 1.0, pitch: 1.0, label: 'Natural' },
    clear: { rate: 0.9, pitch: 1.05, label: 'Clear' },
    fast: { rate: 1.4, pitch: 1.0, label: 'Fast Study' },
    slow: { rate: 0.7, pitch: 0.95, label: 'Slow & Steady' },
  };

  var state = {
    status: IDLE,
    engine: null,
    engineId: '',
    allEngines: {},
    engineUsable: {},

    currentText: '',
    wordStart: -1,
    wordEnd: -1,
    blockIdx: -1,

    rate: 1.0,
    pitch: 1.0,
    volume: 1.0, // TTS-QOL4
    preset: '',
    voiceId: '',

    lastError: null,
    lastDiag: null,
    buffering: false, // OPT-TTS-CTRL: true while engine awaits network synthesis
    selectionReason: '',

    onStateChange: null,
    onProgress: null,
    onNeedAdvance: null,
    hostFn: null,
    viewEngineFn: null,
    format: '',
    ttsHlStyle: 'highlight',
    ttsHlColor: 'grey',
    ttsHlGranularity: 'both', // 'both' | 'block' | 'word'
    lastLocator: null,
    onDocumentEnd: null,
    _listeners: {
      stateChange: [],
      progress: [],
      documentEnd: [],
    },
    ttsEnlargeScale: 1.35, // FIX-TTS08: configurable enlarge factor (1.1–2.0)

    _pauseStartedAt: 0,
    // FIX-TTS05: Thorium-style cancel flag — set BEFORE engine.cancel(), checked in all callbacks
    _cancelFlag: false,

    // PATCH2: suppress auto-scroll briefly after user interaction
    suppressAutoScrollUntil: 0,

    initDone: false,
    initPromise: null,
    _destroyed: false, // FIX-LISTEN-STAB3: set by destroy() to cancel pending init()
  };

  // FIX-TTS05: Thorium-style pre-generated text queue
  var _queue = {
    items: [],       // Array of { text, ssml, range, blockRange, parentEl, lang, marks }
    index: -1,       // Current playback position
    length: 0,       // items.length
  };

  // FIX-TTS05: foliate-js bridge state (simplified — no preload timer, no saved ranges)
  var _fol = {
    tts: null,          // foliate TTS class instance (per section)
    renderer: null,      // foliate renderer ref
  };

  // FIX-TTS05: CSS Custom Highlight API for BOTH word and sentence highlighting
  // No SVG overlayer for TTS — eliminates blue selection flash entirely.
  var _cssHl = {
    word: null,       // Highlight object for current word
    sentence: null,   // Highlight object for current sentence/block
    doc: null,        // iframe document ref
    styleEl: null,    // injected <style> in iframe
  };

  // TTS_REWRITE: TXT legacy state (only used for format === 'txt')
  var _txt = {
    blocks: [],
    segments: [],
    segIdx: -1,
    activeEl: null,
  };

  // FIX-TTS05: highlight color presets — used for CSS Highlight API only
  var TTS_HL_COLORS = {
    grey:   { sentence: '#8c8c9b', word: '#9a9aa8',  line: '#9a9aa8' },
    blue:   { sentence: '#64a0ff', word: '#7ab0ff',  line: '#5a96ff' },
    yellow: { sentence: '#ffe664', word: '#fff090',  line: '#e6c800' },
    green:  { sentence: '#64c878', word: '#7ad890',  line: '#50b464' },
    pink:   { sentence: '#ff82aa', word: '#ff9abe',  line: '#ff6e96' },
    orange: { sentence: '#ffb450', word: '#ffc878',  line: '#ffa032' },
  };

  // FIX-PAUSE: Removed shared cooldown — status guards (PLAYING/PAUSED) already
  // prevent double-fire. The shared cooldown silently blocked rapid pause→resume.

  // GAP2: Screen Wake Lock
  var _wakeLock = null;
  async function acquireWakeLock() {
    if (_wakeLock) return;
    try {
      if (navigator && navigator.wakeLock) {
        _wakeLock = await navigator.wakeLock.request('screen');
        _wakeLock.addEventListener('release', function () { _wakeLock = null; });
      }
    } catch (e) { _wakeLock = null; }
  }
  function releaseWakeLock() {
    if (_wakeLock) { try { _wakeLock.release(); } catch {} _wakeLock = null; }
  }
  // FIX-TTS-B2: named reference for cleanup in destroy()
  var _onVisibilityChange = function () {
    if (state._destroyed) return;
    if (document.visibilityState === 'visible' && state.status === PLAYING) acquireWakeLock();
  };
  var _visibilityListenerBound = true;
  document.addEventListener('visibilitychange', _onVisibilityChange);

  // ── Utility ──────────────────────────────────────────────────

  function normalizeErr(err, fallbackCode) {
    if (!err) return { error: String(fallbackCode || 'unknown') };
    if (typeof err === 'string') return { error: String(err) };
    if (typeof err === 'object') {
      var out = {};
      var keys = Object.keys(err);
      for (var i = 0; i < keys.length; i++) out[keys[i]] = err[keys[i]];
      if (!out.error) out.error = String(fallbackCode || 'unknown');
      return out;
    }
    return { error: String(fallbackCode || 'unknown'), reason: String(err) };
  }

  function getState() {
    var cur = String(state.status || IDLE);
    return (ALL_TTS_STATES.indexOf(cur) >= 0) ? cur : IDLE;
  }

  function _isPausedState(status) {
    return PAUSED_STATES.indexOf(String(status || '')) >= 0;
  }

  function _isTtsActiveState(status) {
    var s = String(status || '');
    return s === PLAYING || _isPausedState(s);
  }

  function _pausedStateForDirection(delta) {
    return Number(delta) < 0 ? BACKWARD_PAUSED : FORWARD_PAUSED;
  }

  function _currentResumeIndex() {
    if (state.format === 'txt') {
      return (_txt.segIdx >= 0) ? _txt.segIdx : 0;
    }
    return (_queue.index >= 0) ? _queue.index : ((state.blockIdx >= 0) ? state.blockIdx : 0);
  }

  function _restartFromCurrentPosition() {
    if (!state.engine) return;

    state._cancelFlag = false;
    state._pauseStartedAt = 0;
    state.status = PLAYING;
    acquireWakeLock();
    fire();

    if (state.format === 'txt') {
      if (!_txt.segments || !_txt.segments.length) {
        _txtExtractBlocks();
      }
      if (_txt.segIdx < 0 && _txt.segments.length) _txt.segIdx = 0;
      _txtSpeakSegment();
      return;
    }

    if (!_queue.length) {
      play();
      return;
    }

    var idx = _currentResumeIndex();
    if (idx < 0) idx = 0;
    if (idx >= _queue.length) idx = _queue.length - 1;
    _speakQueueItem(idx);
  }


  function _listenerBucket(type) {
    var map = { state: 'stateChange', statechange: 'stateChange', onstatechange: 'stateChange', progress: 'progress', onprogress: 'progress', documentend: 'documentEnd', ondocumentend: 'documentEnd' };
    var key = map[String(type || '').toLowerCase()] || null;
    if (!key) return null;
    var ls = state._listeners || null;
    return (ls && Array.isArray(ls[key])) ? ls[key] : null;
  }

  function _emitListeners(type, args) {
    var list = _listenerBucket(type);
    if (!list || !list.length) return;
    var copy = list.slice();
    for (var i = 0; i < copy.length; i++) {
      try { copy[i].apply(null, args || []); } catch (e) {}
    }
  }

  function _on(type, fn) {
    if (typeof fn !== 'function') return function () {};
    var list = _listenerBucket(type);
    if (!list) return function () {};
    if (list.indexOf(fn) < 0) list.push(fn);
    return function () { _off(type, fn); };
  }

  function _off(type, fn) {
    if (typeof fn !== 'function') return;
    var list = _listenerBucket(type);
    if (!list) return;
    var i = list.indexOf(fn);
    if (i >= 0) list.splice(i, 1);
  }

  function _fireDocumentEnd(info) {
    if (typeof state.onDocumentEnd === 'function') {
      try { state.onDocumentEnd(info); } catch {}
    }
    _emitListeners('documentEnd', [info]);
  }

  function fire() {
    state.status = getState();
    if (typeof state.onStateChange === 'function') {
      try { state.onStateChange(state.status, snippetInfo()); } catch {}
    }
    _emitListeners('stateChange', [state.status, snippetInfo()]);
  }

  function _hardCancelAllEngines() {
    try {
      var ids = Object.keys(state.allEngines || {});
      for (var i = 0; i < ids.length; i++) {
        var eng = state.allEngines[ids[i]];
        if (!eng || typeof eng.cancel !== 'function') continue;
        try { eng.cancel(); } catch (e) {}
      }
      try { if (window.speechSynthesis && window.speechSynthesis.cancel) window.speechSynthesis.cancel(); } catch (e) {}
    } catch (e) {}
  }

  function fireProgress() {
    if (typeof state.onProgress === 'function') {
      try { state.onProgress(snippetInfo()); } catch {}
    }
    _emitListeners('progress', [snippetInfo()]);
  }

  var _locatorRefreshTimer = 0;
  function _scheduleRefreshLocator() {
    try {
      if (_locatorRefreshTimer) return;
      _locatorRefreshTimer = setTimeout(async function () {
        _locatorRefreshTimer = 0;
        try {
          var eng = _getViewEngine();
          if (eng && typeof eng.getLocator === 'function') {
            var loc = await eng.getLocator();
            state.lastLocator = (loc && typeof loc === 'object') ? { ...loc } : null;
          }
        } catch {}
      }, 50);
    } catch {}
  }

  function snippetInfo() {
    return {
      status: state.status,
      text: state.currentText,
      wordStart: state.wordStart,
      wordEnd: state.wordEnd,
      blockIdx: state.blockIdx,
      blockCount: _queue.length,
      locator: state.lastLocator ? { ...state.lastLocator } : null,
      highlightGranularity: state.ttsHlGranularity || 'both',
      segIdx: state.format === 'txt' ? _txt.segIdx : state.blockIdx,
      segCount: state.format === 'txt' ? _txt.segments.length : _queue.length,
      rate: state.rate,
      pitch: state.pitch,
      preset: state.preset,
      engineId: state.engineId,
      selectionReason: state.selectionReason,
      lastError: state.lastError,
      lastDiag: state.lastDiag,
      buffering: !!state.buffering,
    };
  }

  // TTS-QOL-V1: Provide a window of segments around the current playback position
  // so the listening player can render a scrolling prose "stage".
  function getSegmentWindow(beforeCount, afterCount) {
    var before = (typeof beforeCount === 'number' ? beforeCount : 3) | 0;
    var after = (typeof afterCount === 'number' ? afterCount : 3) | 0;
    if (before < 0) before = 0;
    if (after < 0) after = 0;

    var total, active, src, getText;
    if (state.format === 'txt') {
      src = _txt.segments || [];
      total = src.length;
      active = _txt.segIdx | 0;
      getText = function (seg) { return seg && seg.text ? String(seg.text) : ''; };
    } else {
      src = _queue.items || [];
      total = src.length;
      active = state.blockIdx | 0;
      getText = function (item) { return item && item.text ? String(item.text) : ''; };
    }

    if (!total || active < 0) {
      return { total: total || 0, activeIdx: active, startIdx: 0, endIdx: -1, segments: [] };
    }

    if (active >= total) active = total - 1;
    var startIdx = active - before;
    if (startIdx < 0) startIdx = 0;
    var endIdx = active + after;
    if (endIdx >= total) endIdx = total - 1;

    var out = [];
    for (var i = startIdx; i <= endIdx; i++) {
      out.push({ idx: i, text: getText(src[i]), isActive: i === active });
    }
    return { total: total, activeIdx: active, startIdx: startIdx, endIdx: endIdx, segments: out };
  }


  // ── SSML Parser ──────────────────────────────────────────────

  function parseSSML(ssmlString) {
    var plain = '';
    var marks = [];
    if (!ssmlString) return { plainText: plain, marks: marks };

    var s = String(ssmlString);
    var i = 0;
    var len = s.length;
    while (i < len) {
      if (s[i] === '<') {
        var tagEnd = s.indexOf('>', i);
        if (tagEnd < 0) break;
        var tagContent = s.substring(i + 1, tagEnd);
        var markMatch = tagContent.match(/^mark\s+name\s*=\s*["']([^"']*)["']/i);
        if (markMatch) {
          marks.push({ name: markMatch[1], offset: plain.length });
        }
        i = tagEnd + 1;
      } else if (s[i] === '&') {
        var semi = s.indexOf(';', i);
        if (semi > i && semi - i < 10) {
          var ent = s.substring(i + 1, semi);
          if (ent === 'amp') { plain += '&'; }
          else if (ent === 'lt') { plain += '<'; }
          else if (ent === 'gt') { plain += '>'; }
          else if (ent === 'apos') { plain += "'"; }
          else if (ent === 'quot') { plain += '"'; }
          else { plain += s.substring(i, semi + 1); }
          i = semi + 1;
        } else {
          plain += s[i];
          i++;
        }
      } else {
        plain += s[i];
        i++;
      }
    }
    return { plainText: plain, marks: marks };
  }

  function findNearestMark(marks, charIndex) {
    if (!marks || !marks.length) return null;
    var best = null;
    for (var i = 0; i < marks.length; i++) {
      if (marks[i].offset <= charIndex) {
        best = marks[i].name;
      } else {
        break;
      }
    }
    if (best === null && marks.length > 0) {
      best = marks[0].name;
    }
    return best;
  }

  // ── Block parent detection ───────────────────────────────────

  var _blockTags = new Set([
    'article', 'aside', 'blockquote', 'div', 'dl', 'dt', 'dd',
    'figure', 'figcaption', 'footer', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
    'header', 'li', 'main', 'nav', 'ol', 'p', 'pre', 'section', 'tr',
  ]);

  function _findBlockParent(node) {
    var el = (node && node.nodeType === 3) ? node.parentElement : node;
    while (el && el !== el.ownerDocument.body) {
      if (_blockTags.has((el.tagName || '').toLowerCase())) return el;
      el = el.parentElement;
    }
    return null;
  }

  // FIX-TTS08: Clear native browser selection in both the main document and the
  // EPUB iframe to remove the blue selection glitch caused by paginator's
  // setSelectionTo() firing on 'selection' reason scrolls.
  function _clearNativeSelection() {
    try { window.getSelection().removeAllRanges(); } catch {}
    if (_cssHl.doc) {
      try {
        var win = _cssHl.doc.defaultView;
        if (win) win.getSelection().removeAllRanges();
      } catch {}
    }
  }

  // ── CSS Custom Highlight API (sentence + word) ─────────────────

  function _hexToRgba(hex, alpha) {
    var h = String(hex || '#999').replace('#', '');
    var r = parseInt(h.substring(0, 2), 16) || 0;
    var g = parseInt(h.substring(2, 4), 16) || 0;
    var b = parseInt(h.substring(4, 6), 16) || 0;
    return 'rgba(' + r + ',' + g + ',' + b + ',' + alpha + ')';
  }

  // FIX-TTS08: Build the word ::highlight CSS rule based on current style + color.
  // FIX-TTS-HL: Electron 30 (Chromium 124) does NOT support text-decoration in ::highlight().
  // That requires Chrome 131+. Always include a visible background-color as the primary
  // indicator and add text-decoration as a progressive enhancement for future Electron upgrades.
  function _buildHighlightCss() {
    var colors = TTS_HL_COLORS[state.ttsHlColor] || TTS_HL_COLORS.grey;
    var sentenceCss = '::highlight(tts-sentence) { background-color: ' + _hexToRgba(colors.sentence, 0.18) + '; }';
    var wordCss = '';
    var style = state.ttsHlStyle;
    var wordBg = _hexToRgba(colors.word, 0.38);
    if (style === 'underline') {
      // Visible bg + underline (text-decoration is progressive enhancement)
      wordCss = '::highlight(tts-word) { background-color: ' + _hexToRgba(colors.word, 0.22) + '; text-decoration-line: underline; text-decoration-color: ' + colors.word + '; }';
    } else if (style === 'squiggly') {
      // Visible bg + wavy underline (progressive enhancement)
      wordCss = '::highlight(tts-word) { background-color: ' + _hexToRgba(colors.word, 0.22) + '; text-decoration-line: underline; text-decoration-style: wavy; text-decoration-color: ' + colors.word + '; }';
    } else if (style === 'strikethrough') {
      // Visible bg + line-through (progressive enhancement)
      wordCss = '::highlight(tts-word) { background-color: ' + _hexToRgba(colors.word, 0.22) + '; text-decoration-line: line-through; text-decoration-color: ' + colors.word + '; }';
    } else {
      // 'highlight' (default) and 'enlarge' — enlarge uses span wrapping, not CSS highlight
      wordCss = '::highlight(tts-word) { background-color: ' + wordBg + '; }';
    }
    return sentenceCss + '\n' + wordCss;
  }

  // FIX-TTS08: Build the .tts-enlarge class CSS using the configurable scale.
  function _buildEnlargeCss() {
    var scale = state.ttsEnlargeScale || 1.35;
    return '.tts-enlarge { font-size: ' + scale + 'em; display: inline; transition: font-size 0.12s ease-out; }';
  }

  // FIX-TTS05: Initialize BOTH sentence and word CSS highlights in the EPUB iframe
  function _ensureCssHighlights(iframeDoc) {
    if (!iframeDoc) return false;
    try {
      var win = iframeDoc.defaultView || window;
      if (!win.CSS || !win.CSS.highlights) return false;

      // Already initialized for this document?
      if (_cssHl.doc === iframeDoc && _cssHl.word && _cssHl.sentence) return true;

      // Clean up previous highlights
      _clearCssHighlights();

      _cssHl.doc = iframeDoc;
      _cssHl.word = new win.Highlight();
      _cssHl.sentence = new win.Highlight();
      win.CSS.highlights.set('tts-word', _cssHl.word);
      win.CSS.highlights.set('tts-sentence', _cssHl.sentence);

      // FIX-TTS08: Inject style-aware ::highlight rules + enlarge class
      var style = iframeDoc.createElement('style');
      style.setAttribute('data-tts-hl', '1');
      style.textContent = _buildHighlightCss() + '\n' + _buildEnlargeCss();
      iframeDoc.head.appendChild(style);
      _cssHl.styleEl = style;

      return true;
    } catch {}
    return false;
  }

  function _clearCssHighlights() {
    _clearEnlargeSpan(); // FIX-TTS08: remove any enlarge span
    if (_cssHl.word) { try { _cssHl.word.clear(); } catch {} }
    if (_cssHl.sentence) { try { _cssHl.sentence.clear(); } catch {} }
    if (_cssHl.styleEl) { try { _cssHl.styleEl.remove(); } catch {} }
    if (_cssHl.doc) {
      try {
        var win = _cssHl.doc.defaultView;
        if (win && win.CSS && win.CSS.highlights) {
          win.CSS.highlights.delete('tts-word');
          win.CSS.highlights.delete('tts-sentence');
        }
      } catch {}
    }
    _cssHl.word = null;
    _cssHl.sentence = null;
    _cssHl.doc = null;
    _cssHl.styleEl = null;
  }

  // FIX-TTS08: Update both style and color in the injected CSS
  function _updateCssHighlightColors() {
    if (!_cssHl.styleEl || !_cssHl.doc) return;
    try {
      _cssHl.styleEl.textContent = _buildHighlightCss() + '\n' + _buildEnlargeCss();
    } catch {}
  }

  // OPT1: RAF-throttle boundary updates to reduce layout thrashing
  var _pendingBoundary = null;
  var _boundaryRafId = null;

  // TTS-THROTTLE: Coalesce rapid rate/voice changes during active playback.
  // Prevents live engine reconfiguration glitches when dragging controls.
  // OPT-TTS-CTRL: Reduced from 3000ms — only applies to coalescing rapid burst changes.
  // First change in a burst fires immediately; this window catches follow-up changes.
  var TTS_RECONFIG_THROTTLE_MS = 1500;
  var _ttsReconfigThrottle = {
    timer: 0,
    pendingRate: false,
    pendingVoice: false,
  };

  // FIX-TTS08: Enlarge style — wrap current word range in a <span class="tts-enlarge">,
  // remove the previous one. This is the only style that mutates the DOM.
  var _enlargeSpan = null;

  function _applyEnlargeSpan(range) {
    if (!range) { _clearEnlargeSpan(); return; }
    // OPT1: skip DOM teardown+rebuild if enlarge span already wraps the same range
    // FIX-TTS-B5 #13: Also verify span is still in the live DOM (not orphaned by reflow)
    if (_enlargeSpan && _enlargeSpan.isConnected) {
      try {
        var txt = _enlargeSpan.textContent || '';
        var rangeText = range.toString() || '';
        if (txt === rangeText) return;
      } catch {}
    }
    _clearEnlargeSpan();
    try {
      var doc = range.startContainer.ownerDocument || document;
      var span = doc.createElement('span');
      span.className = 'tts-enlarge';
      span.setAttribute('data-tts-tmp', '1');
      range.surroundContents(span);
      _enlargeSpan = span;
    } catch {
      // surroundContents fails if range crosses element boundaries — fall back to highlight
      _enlargeSpan = null;
    }
  }

  function _clearEnlargeSpan() {
    if (!_enlargeSpan) return;
    // FIX-TTS-B5 #13: If span was orphaned by DOM mutation, skip unwrap
    if (!_enlargeSpan.isConnected) { _enlargeSpan = null; return; }
    try {
      var parent = _enlargeSpan.parentNode;
      if (parent) {
        while (_enlargeSpan.firstChild) {
          parent.insertBefore(_enlargeSpan.firstChild, _enlargeSpan);
        }
        parent.removeChild(_enlargeSpan);
        // NOTE: do NOT call parent.normalize() — merging text nodes would
        // invalidate Range references held in the pre-generated TTS queue.
      }
    } catch {}
    _enlargeSpan = null;
  }

  // FIX-TTS05: Highlight the sentence (block range) via CSS Highlight API
  function highlightSentence(range) {
    if (!range) return;
    try {
      var doc = range.startContainer.ownerDocument || document;
      if (_ensureCssHighlights(doc) && _cssHl.sentence) {
        _cssHl.sentence.clear();
        _cssHl.sentence.add(range);
      }
    } catch {}
  }

  // FIX-TTS08: Highlight the current word — dispatches to enlarge or CSS Highlight API
  function highlightWord(range) {
    if (!range) return;
    try {
      var doc = range.startContainer.ownerDocument || document;
      _ensureCssHighlights(doc);
      if (state.ttsHlStyle === 'enlarge') {
        // Enlarge mode: wrap word in a scaled span, clear CSS word highlight
        if (_cssHl.word) _cssHl.word.clear();
        _applyEnlargeSpan(range);
      } else {
        // All other styles: use CSS Highlight API
        _clearEnlargeSpan();
        if (_cssHl.word) {
          _cssHl.word.clear();
          _cssHl.word.add(range);
        }
      }
    } catch {}
  }

  // FIX-TTS05: Highlight callback passed to view.initTTS() — handles both sentence and word
  function highlightRange(range) {
    if (!range) return;

    var gran = state.ttsHlGranularity || 'both';

    // Word highlight
    if (gran !== 'block') {
      highlightWord(range);
    } else {
      // Block-only: clear any active word highlight/enlarge span
      try { if (_cssHl.word) _cssHl.word.clear(); } catch {}
      try { _clearEnlargeSpan(); } catch {}
    }

    // Sentence highlight: detect parent block, highlight when block changes
    if (gran !== 'word') {
      var blockEl = _findBlockParent(range.startContainer);
      if (blockEl) {
        try {
          var doc = blockEl.ownerDocument || document;
          var blockRange = doc.createRange();
          blockRange.selectNodeContents(blockEl);
          highlightSentence(blockRange);
        } catch {}
      }
    }
    // FIX-TTS07: scroll via shared helper
    _scrollToRange(range);
  }

  function clearTtsHighlights() {
    // FIX-TTS05: only clear CSS highlights — no overlayer for TTS
    if (_cssHl.word) { try { _cssHl.word.clear(); } catch {} }
    if (_cssHl.sentence) { try { _cssHl.sentence.clear(); } catch {} }
    _clearEnlargeSpan(); // FIX-TTS08: also remove enlarge span
    // FIX-TTS-B5 #13: Sweep any orphaned tts-enlarge spans left by DOM mutations
    try {
      var eng = _getViewEngine();
      if (eng && eng.doc) {
        var orphans = eng.doc.querySelectorAll('[data-tts-tmp]');
        for (var i = 0; i < orphans.length; i++) {
          var sp = orphans[i];
          var par = sp.parentNode;
          if (par) {
            while (sp.firstChild) par.insertBefore(sp.firstChild, sp);
            par.removeChild(sp);
          }
        }
      }
    } catch {}
  }

  // FIX-TTS-RESCROLL: Re-enabled auto-scroll via scrollToAnchorCentered.
  // Root cause of FIX-TTS12 shaking: paginated mode always called #scrollToRect →
  // #afterScroll → relocate on every word boundary, even for words on the current page.
  // Fix: paginator.js now checks if rect is already in the visible page before navigating.
  // In scrolled mode, scrollToAnchorCentered already had a comfort-zone check.
  function _scrollToRange(range) {
    if (!range) return;
    var renderer = _fol.renderer;
    if (!renderer || typeof renderer.scrollToAnchorCentered !== 'function') return;
    try { renderer.scrollToAnchorCentered(range); } catch {}
  }

  // ── Queue Generation (Thorium-style) ───────────────────────────

  // FIX-TTS05: Build the entire TTS queue for the current section upfront.
  // Each item = one foliate block with its SSML, plain text, marks, and ranges.
  // This replaces the lazy ListIterator approach with a deterministic, indexable array.
  function _generateQueue() {
    _queue.items = [];
    _queue.index = -1;
    _queue.length = 0;

    if (!_fol.tts) return;

    // Walk through all blocks using foliate's existing next() mechanism.
    // First, start from the beginning.
    // FIX-TTS-B4 #12: Wrap in try/catch — keep partial queue on error
    try {
      var ssml = _fol.tts.start();
      var idx = 0;
      while (ssml) {
        var parsed = parseSSML(ssml);
        // Snapshot the mark→Range map for this block
        var ranges = null;
        if (typeof _fol.tts.snapshotRanges === 'function') {
          ranges = _fol.tts.snapshotRanges();
        }
        _queue.items.push({
          ssml: ssml,
          text: parsed.plainText,
          marks: parsed.marks,
          ranges: ranges, // Map of mark name → Range for word highlighting
        });
        idx++;
        ssml = _fol.tts.next();
      }
    } catch (err) {
      state.lastError = normalizeErr(err, 'queue_generation_error');
    }
    _queue.length = _queue.items.length;
  }

  // ── foliate-js TTS init ──────────────────────────────────────

  function _getViewEngine() {
    return (typeof state.viewEngineFn === 'function') ? state.viewEngineFn() : null;
  }

  async function _initFoliateTTS() {
    var eng = _getViewEngine();
    if (!eng) return false;
    var view = typeof eng.getFoliateView === 'function' ? eng.getFoliateView() : null;
    if (!view) return false;
    var renderer = typeof eng.getFoliateRenderer === 'function' ? eng.getFoliateRenderer() : null;
    if (!renderer) return false;

    try {
      await view.initTTS('word', highlightRange);
    } catch (e) {
      return false;
    }

    _fol.tts = view.tts || null;
    _fol.renderer = renderer;

    // FIX-TTS08: Clear any native selection left by initTTS to prevent blue glitch
    _clearNativeSelection();

    return !!_fol.tts;
  }

  async function _reinitFoliateTTS() {
    var eng = _getViewEngine();
    if (!eng) return false;
    var view = typeof eng.getFoliateView === 'function' ? eng.getFoliateView() : null;
    if (!view) return false;

    view.tts = null;
    return _initFoliateTTS();
  }

  // ── Block Playback (EPUB via queue) ─────────────────────────────

  // FIX-TTS05: Play the queue item at _queue.index
  function _speakQueueItem(queueIdx, opts) {
    if (queueIdx < 0 || queueIdx >= _queue.length) {
      handleAllBlocksDone();
      return;
    }
    if (!state.engine) { stop(); return; }

    var useGapless = !!(opts && opts.gapless);

    var item = _queue.items[queueIdx];
    // FIX-TTS06: Skip empty blocks AND whitespace-only / trivially short text.
    // Very short text (e.g. "1", "I") can cause Edge TTS to fail or produce empty audio.
    var itemText = item ? (item.text || '').trim() : '';
    if (!item || !itemText || itemText.length < 2) {
      _queue.index = queueIdx + 1;
      if (_queue.index < _queue.length) {
        _speakQueueItem(_queue.index);
      } else {
        handleAllBlocksDone();
      }
      return;
    }

    _queue.index = queueIdx;
    state.currentText = item.text;
    state.wordStart = -1;
    state.wordEnd = -1;
    state.blockIdx = queueIdx;
    _scheduleRefreshLocator();

    // Highlight sentence for this block
    if (item.ranges && item.ranges.size > 0) {
      // Use the first mark's range to find the block parent for sentence highlight
      var firstRange = item.ranges.values().next().value;
      if (firstRange) {
        var blockEl = _findBlockParent(firstRange.startContainer);
        if (blockEl) {
          try {
            var doc = blockEl.ownerDocument || document;
            var blockRange = doc.createRange();
            blockRange.selectNodeContents(blockEl);
            highlightSentence(blockRange);
          } catch {}
        }
        // FIX-TTS07: Scroll to the start of the new block immediately so the
        // reader viewport tracks the narrated text before the first word boundary fires.
        _scrollToRange(firstRange.cloneRange());
      }
    }

    fireProgress();

    // FIX-TTS05: Pre-synthesize ahead (multi-chunk lookahead)
    // OPT1: adaptive count based on playback rate
    _preloadAhead(queueIdx + 1, _adaptivePreloadCount());
    _preloadBehind(queueIdx, 3);

    // PATCH3: Use gapless speak when advancing sequentially.
    // This keeps the current audio playing until the next block audio is ready,
    // reducing micro-gaps in long listening sessions.
    if (useGapless && typeof state.engine.speakGapless === 'function') {
      state.engine.speakGapless(item.text);
    } else {
      state.engine.speak(item.text);
    }

    // FIX-TTS-B5 #8: Pre-buffer next block's audio element for near-zero transition gap
    if (typeof state.engine.prepareNext === 'function') {
      var nextForPrep = _queue.items[queueIdx + 1];
      if (nextForPrep && nextForPrep.text) {
        try { state.engine.prepareNext(nextForPrep.text); } catch {}
      }
    }
  }

  // OPT1: Adaptive preload count — more blocks at higher playback rates
  // OPT-PERF: Increased base from 3→5 and cap from 8→12 for smoother transitions
  function _adaptivePreloadCount() {
    var base = 5;
    var extra = Math.ceil((state.rate - 1.0) * 4);
    if (extra < 0) extra = 0;
    return Math.min(base + extra, 12);
  }

  // FIX-TTS05: Multi-chunk lookahead — synthesize next N blocks in background
  function _preloadAhead(startIdx, count) {
    if (!state.engine || typeof state.engine.preload !== 'function') return;
    for (var i = 0; i < count; i++) {
      var idx = startIdx + i;
      if (idx >= _queue.length) break;
      var item = _queue.items[idx];
      if (item && item.text) {
        try { state.engine.preload(item.text); } catch {}
      }
    }
  }

  // OPT-TTS-CTRL: Preload blocks behind current position for instant rewind.
  // Uses a smaller count (3) since forward preload is more important.
  // engine.preload() no-ops on cache hits, so no duplicate work.
  function _preloadBehind(currentIdx, count) {
    if (!state.engine || typeof state.engine.preload !== 'function') return;
    for (var i = 1; i <= count; i++) {
      var idx = currentIdx - i;
      if (idx < 0) break;
      var item = _queue.items[idx];
      if (item && item.text) {
        try { state.engine.preload(item.text); } catch {}
      }
    }
  }

  function handleBlockEnd() {
    // FIX-TTS05: cancel flag check — first line of every callback
    if (state._cancelFlag) return;
    if (state.status !== PLAYING) return;

    // FIX-TTS06: successful block playback resets consecutive error counter
    _consecutiveErrors = 0;

    // TXT legacy path
    if (state.format === 'txt') {
      _txtHandleBlockEnd();
      return;
    }

    // Advance to next queue item
    var nextIdx = _queue.index + 1;
    if (nextIdx < _queue.length) {
      _speakQueueItem(nextIdx, { gapless: true });
    } else {
      handleAllBlocksDone();
    }
  }

  function handleAllBlocksDone() {
    if (typeof state.onNeedAdvance === 'function') {
      state.onNeedAdvance().then(async function (advanced) {
        if (!advanced || state.status !== PLAYING) {
          try { if (!advanced) _fireDocumentEnd(snippetInfo()); } catch {}
          stop();
          return;
        }
        // FIX-LISTEN-STAB3: After section advance, the old _fol.tts is stale (bound to
        // the previous section's document). Must re-init foliate TTS for the new section
        // before generating queue. Without this, next() returns null → empty queue → stop.
        var ok = await _reinitFoliateTTS();
        if (!ok) { stop(); return; }
        _generateQueue();
        if (_queue.length > 0) {
          _speakQueueItem(0);
        } else {
          stop();
        }
      }).catch(function () { stop(); });
    } else {
      try { _fireDocumentEnd(snippetInfo()); } catch {}
      stop();
    }
  }

  // OPT1: Flush pending boundary — runs inside requestAnimationFrame to batch DOM writes
  function _flushBoundary() {
    _boundaryRafId = null;
    var bd = _pendingBoundary;
    if (!bd) return;
    _pendingBoundary = null;
    if (state._cancelFlag || state.status !== PLAYING) return;

    var item = _queue.items[_queue.index];
    if (item && item.marks && item.marks.length && item.ranges) {
      var markName = findNearestMark(item.marks, bd.charIndex);
      if (markName) {
        var range = item.ranges.get(markName);
        if (range) {
          try {
            var cloned = range.cloneRange();
            highlightWord(cloned);
            _scrollToRange(cloned);
          } catch {}
        }
      }
    }
    fireProgress();
  }

  function handleBoundary(charIndex, charLength, name) {
    // FIX-TTS05: cancel flag check — first line
    if (state._cancelFlag) return;
    if (name !== 'word' || state.status !== PLAYING) return;

    state.wordStart = charIndex;
    state.wordEnd = charIndex + Math.max(charLength, 1);
    if (charLength === 0 && state.currentText) {
      var rest = state.currentText.slice(charIndex);
      var m = rest.match(/^\S+/);
      if (m) state.wordEnd = charIndex + m[0].length;
    }

    // FIX-SYNC: Apply highlight/scroll immediately — the engine's boundary
    // poller already rate-limits dispatch, so RAF deferral only adds latency.
    _pendingBoundary = { charIndex: charIndex, charLength: charLength };
    _flushBoundary();
  }

  // FIX-TTS06: Track consecutive errors to detect persistent failures
  var _consecutiveErrors = 0;
  var MAX_CONSECUTIVE_ERRORS = 3;

  // OPT1: Known transient error codes — skip block immediately without counting
  var _SKIP_IMMEDIATELY_CODES = ['edge_synth_timeout', 'edge_audio_chunk_recv_none'];

  function _isSkipImmediatelyError(err) {
    if (!err) return false;
    var code = String(err.error || err.code || err.reason || err || '');
    for (var i = 0; i < _SKIP_IMMEDIATELY_CODES.length; i++) {
      if (code.indexOf(_SKIP_IMMEDIATELY_CODES[i]) >= 0) return true;
    }
    return false;
  }

  function handleError(err) {
    // FIX-TTS05: cancel flag check
    if (state._cancelFlag) return;
    state.lastError = normalizeErr(err, 'tts_error');

    // OPT1: known transient errors skip block immediately without counting
    // toward consecutive error threshold — prevents hanging on bad blocks
    if (!_isSkipImmediatelyError(err)) {
      _consecutiveErrors++;
      if (_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
        _consecutiveErrors = 0;
        // FIX-TTS-B4 #7: Tag the error so UI can show feedback
        state.lastError = normalizeErr(err, 'max_errors_reached');
        stop();
        return;
      }
    }

    if (state.status !== PLAYING) { stop(); return; }

    // Try advancing to next block
    if (state.format === 'txt') {
      _txtHandleBlockEnd();
      return;
    }
    var nextIdx = _queue.index + 1;
    if (nextIdx < _queue.length) {
      _speakQueueItem(nextIdx);
    } else {
      handleAllBlocksDone();
    }
  }

  // ── Engine Management ──────────────────────────────────────

  function bindEngine(engine) {
    engine.onEnd = handleBlockEnd;
    engine.onError = handleError;
    engine.onBoundary = handleBoundary;

    if ('onDiag' in engine) {
      engine.onDiag = function (d) {
        if (!d) return;
        state.lastDiag = {
          code: String(d.code || ''),
          detail: String(d.detail || ''),
          at: Date.now(),
        };
        fireProgress();
      };
    }

    // OPT-TTS-CTRL: Propagate buffering flag from engine to core state → UI
    if ('onBuffering' in engine) {
      engine.onBuffering = function (val) {
        state.buffering = !!val;
        fire();
      };
    }

    if (state.rate) engine.setRate(state.rate);
    if (state.voiceId) engine.setVoice(state.voiceId);
    if (state.pitch !== 1.0 && typeof engine.setPitch === 'function') {
      engine.setPitch(state.pitch);
    }
    // TTS-QOL4: apply saved volume to engine
    if (state.volume < 1.0 && typeof engine.setVolume === 'function') {
      engine.setVolume(state.volume);
    }
  }

  function isEngineUsable(id) {
    return !!state.engineUsable[String(id || '')];
  }

  function switchEngine(id) {
    var eid = String(id || '');
    var inst = state.allEngines[eid];
    if (!inst) return false;
    if (!isEngineUsable(eid)) return false;
    if (state.engine && state.engine !== inst) {
      try { state.engine.cancel(); } catch {}
    }
    state.engine = inst;
    state.engineId = eid;
    bindEngine(inst);
    return true;
  }

  async function probeEngine(eid, eng) {
    if (!eng) return false;
    if (typeof eng.probe === 'function') {
      try {
        var ok = !!(await eng.probe({ text: 'Edge probe', voice: 'en-US-AriaNeural', requireSynthesis: true }));
        if (!ok && typeof eng.getHealth === 'function') {
          try {
            var h = eng.getHealth();
            state.lastDiag = {
              code: 'edge_probe_fail',
              detail: String(h && h.reason || 'edge_probe_failed'),
              at: Date.now(),
            };
          } catch {}
        }
        return ok;
      } catch {
        if (String(eid || '') === 'edge') {
          state.lastDiag = {
            code: 'edge_probe_fail',
            detail: 'edge_probe_exception',
            at: Date.now(),
          };
        }
        return false;
      }
    }
    try {
      return !!(eng.isAvailable && eng.isAvailable());
    } catch {
      return false;
    }
  }

  async function init(opts) {
    if (!_visibilityListenerBound) {
      try { document.addEventListener('visibilitychange', _onVisibilityChange); } catch (e) {}
      _visibilityListenerBound = true;
    }
    if (state.initPromise) return state.initPromise;

    // FIX-LISTEN-STAB3: clear destroyed flag — a new init supersedes previous destroy
    state._destroyed = false;

    state.initPromise = (async function () {
      var o = (opts && typeof opts === 'object') ? opts : {};
      state.hostFn = o.getHost || null;
      state.viewEngineFn = o.getViewEngine || null;
      state.format = String(o.format || '').toLowerCase();
      state.onNeedAdvance = o.onNeedAdvance || null;
      state.lastError = null;
      state.lastDiag = null;
      state.selectionReason = '';
      state.initDone = false;
      state._cancelFlag = false;

      var _initProgress = (typeof o.onInitProgress === 'function') ? o.onInitProgress : null;
      function _reportProgress(pct, msg) { if (_initProgress) try { _initProgress(pct, msg); } catch (e) {} }
      // Yield JS thread so browser can paint DOM updates before next blocking call
      function _yieldForPaint() { return new Promise(function(r) { requestAnimationFrame(function() { setTimeout(r, 0); }); }); }
      _reportProgress(0, 'Initializing...');
      await _yieldForPaint();

      var factories = window.booksTTSEngines || {};
      state.allEngines = {};
      state.engineUsable = {};
      state.engine = null;
      state.engineId = '';

      // EDGE_TTS_FIX Phase 5.1: removed dead `edgeDirect` factory branch.
      // It was a speculative entry point for renderer-direct WSS (audit
      // Option C), structurally rejected because QtWebEngine blocks local
      // content remote URLs + browser WebSockets cannot set Edge headers.
      // Sole supported engine is `edge` (Qt-side WSS via booksTtsEdge bridge).
      if (factories.edge) {
        try {
          var inst = factories.edge.create();
          if (inst) {
            state.allEngines.edge = inst;
            state.engineUsable.edge = false;
          }
        } catch {}
      }

      _reportProgress(50, 'Probing engine...');
      await _yieldForPaint();

      // FIX-LISTEN-STAB3: bail out if destroyed during engine creation
      if (state._destroyed) return;

      if (state.allEngines.edge) {
        state.engineUsable.edge = !!(await probeEngine('edge', state.allEngines.edge));
        // OPT2: voices already loaded inside probe() on success — no need to call again
      }

      _reportProgress(70, 'Selecting engine...');
      await _yieldForPaint();

      // FIX-LISTEN-STAB3: bail out if destroyed during probe/voice loading
      if (state._destroyed) return;

      if (state.allEngines.edge && isEngineUsable('edge')) {
        switchEngine('edge');
      }

      // FIX-TTS05: pre-warm Edge TTS WebSocket for faster first playback
      if (state.engineUsable.edge) {
        try {
          var ttsApi = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
          if (ttsApi && typeof ttsApi.warmup === 'function') {
            ttsApi.warmup({ voice: state.voiceId || 'en-US-AriaNeural' }).catch(function () {});
          }
        } catch {}
      }

      _reportProgress(85, 'Warming up...');
      await _yieldForPaint();

      if (!state.engine) {
        state.lastError = {
          error: 'tts_no_usable_engine',
          usable: state.engineUsable,
        };
      } else if (!state.selectionReason) {
        state.selectionReason = 'selected_' + String(state.engineId || 'unknown');
      }

      state.initDone = true;
      _reportProgress(100, 'Ready');
      fireProgress();
    })();

    try {
      await state.initPromise;
    } finally {
      state.initPromise = null;
    }
  }

  // ── Play / Pause / Resume / Stop ─────────────────────────────

  // FIX-LISTEN-STAB3: optional startBlockIdx — resume from saved position instead of always block 0
  async function play(startBlockIdx, opts) {
    if (!state.engine) return;

    if (_isPausedState(state.status)) {
      resume();
      return;
    }

    // TXT legacy path
    if (state.format === 'txt') {
      _txtPlay();
      return;
    }

    // FIX-TTS05: Clear cancel flag
    state._cancelFlag = false;

    var resumeIdx = (typeof startBlockIdx === 'number' && startBlockIdx > 0) ? startBlockIdx : 0;
    var startFromVisible = !!(opts && opts.startFromVisible);

    // EPUB/PDF: initialize foliate-js TTS
    var ok = await _initFoliateTTS();
    if (!ok) {
      var retries = 0;
      var tryInit = async function () {
        retries++;
        ok = await _initFoliateTTS();
        if (ok && _fol.tts) {
          _generateQueue();
          if (_queue.length > 0) {
            var idx = resumeIdx;
            if (startFromVisible) { var vi = getFirstVisibleBlockIndex(); if (vi > 0) idx = vi; }
            state.status = PLAYING;
            acquireWakeLock();
            fire();
            _speakQueueItem(Math.min(idx, _queue.length - 1));
          }
        } else if (retries < 3) {
          setTimeout(tryInit, 300 * retries);
        }
      };
      setTimeout(tryInit, 300);
      return;
    }

    _generateQueue();
    if (!_queue.length) return;

    if (startFromVisible) { var vi = getFirstVisibleBlockIndex(); if (vi > 0) resumeIdx = vi; }

    state.status = PLAYING;
    acquireWakeLock();
    fire();
    _speakQueueItem(Math.min(resumeIdx, _queue.length - 1));
  }

  function pause() {
    if (!state.engine || state.status !== PLAYING) return;

    // FIX-TTS05: Set cancel flag BEFORE engine.cancel() — Thorium r2_cancel pattern
    // This prevents handleBoundary/handleBlockEnd from firing during the cancel transition
    state._cancelFlag = true;
    state._pauseStartedAt = Date.now();

    try { state.engine.pause(); } catch {}

    var supportsTruePause = false;
    try {
      supportsTruePause = (typeof state.engine.isPaused === 'function') ? !!state.engine.isPaused() : false;
    } catch {}

    if (supportsTruePause) {
      state.status = PAUSED;
    } else {
      // Pause fallback: cancel audio but keep queue/index so resume() can restart from current segment
      try { state.engine.cancel(); } catch {}
      _hardCancelAllEngines();
      state.status = STOP_PAUSED;
    }

    // NOTE: highlights are NOT cleared on pause — they persist (Step 7)
    releaseWakeLock();
    fire();
  }

  function resume() {
    if (!state.engine || !_isPausedState(state.status)) return;

    // Unified resume path for all paused variants: restart current segment from the saved queue position.
    state._cancelFlag = true;
    try { state.engine.cancel(); } catch {}
    _hardCancelAllEngines();
    state._cancelFlag = false;
    _restartFromCurrentPosition();
  }

  function stop() {
    _clearReconfigThrottle();
    // FIX-TTS05: Set cancel flag before cancel
    state._cancelFlag = true;
    // OPT1: cancel pending RAF boundary flush
    if (_boundaryRafId) { cancelAnimationFrame(_boundaryRafId); _boundaryRafId = null; }
    _pendingBoundary = null;
    if (state.engine) {
      try { state.engine.cancel(); } catch {}
    }
    _hardCancelAllEngines();
    _hardCancelAllEngines();
    // FIX-TTS05: Clear ALL highlights on stop (and only on stop)
    clearTtsHighlights();
    // TXT cleanup
    if (_txt.activeEl) {
      try { _txt.activeEl.classList.remove('booksReaderTtsActive'); } catch {}
      _txt.activeEl = null;
    }
    _queue.index = -1;
    state.blockIdx = -1;
    if (_locatorRefreshTimer) { clearTimeout(_locatorRefreshTimer); _locatorRefreshTimer = 0; }
    state.currentText = '';
    state.wordStart = -1;
    state.wordEnd = -1;
    state.status = IDLE;
    state.buffering = false;
    state._pauseStartedAt = 0;
    state._cancelFlag = false;
    _txt.segIdx = -1;
    releaseWakeLock();
    fire();
  }

  // ── Navigation ───────────────────────────────────────────────

  function stepBlock(delta) {
    if (!_isTtsActiveState(state.status)) return;

    // TXT legacy
    if (state.format === 'txt') {
      _txtStepSegment(delta);
      return;
    }

    if (!_queue.length) return;

    // FIX-TTS05: cancel flag before engine cancel
    state._cancelFlag = true;
    if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();

    var targetIdx = _queue.index + delta;
    targetIdx = Math.max(0, Math.min(_queue.length - 1, targetIdx));

    state._cancelFlag = false;

    if (_isPausedState(state.status)) {
      state.status = _pausedStateForDirection(delta);
      var item = _queue.items[targetIdx];
      if (item) {
        _queue.index = targetIdx;
        state.currentText = item.text;
        state.blockIdx = targetIdx;
        _scheduleRefreshLocator();
        state.wordStart = -1;
        state.wordEnd = -1;
        // Highlight sentence for new position
        if (item.ranges && item.ranges.size > 0) {
          var firstRange = item.ranges.values().next().value;
          if (firstRange) highlightRange(firstRange.cloneRange());
        }
        fireProgress();
        fire();
      }
    } else {
      _speakQueueItem(targetIdx);
      fire();
    }
  }

  // TTS-QOL: Seek to an absolute segment index (0-based).
  // If autoplay is true, begins playback from that position (even if currently paused).
  function seekSegment(targetIdx, autoplay) {
    if (!_isTtsActiveState(state.status)) return;

    // TXT legacy
    if (state.format === 'txt') {
      _txtSeekSegment(targetIdx, autoplay);
      return;
    }

    if (!_queue.length) return;

    var idx2 = (targetIdx | 0);
    if (idx2 < 0) idx2 = 0;
    if (idx2 >= _queue.length) idx2 = _queue.length - 1;

    // cancel current synthesis before seeking
    state._cancelFlag = true;
    if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
    state._cancelFlag = false;

    var shouldPlay = !!autoplay;
    if (state.status === PLAYING) shouldPlay = true;

    if (!shouldPlay) {
      if (_isPausedState(state.status) && state.status !== PAUSED) state.status = PAUSED;
      var item = _queue.items[idx2];
      if (!item) return;
      _queue.index = idx2;
      state.currentText = item.text;
      state.blockIdx = idx2;
      _scheduleRefreshLocator();
      state.wordStart = -1;
      state.wordEnd = -1;
      if (item.ranges && item.ranges.size > 0) {
        var r = item.ranges.values().next().value;
        if (r) highlightRange(r.cloneRange());
      }
      fireProgress();
      fire();
      return;
    }

    _speakQueueItem(idx2);
    fire();
  }

  // TTS-QOL: Get segment text at index (for UI previews).
  function getSegmentText(idx) {
    var i = (idx | 0);
    if (state.format === 'txt') {
      if (!_txt.segments || i < 0 || i >= _txt.segments.length) return '';
      return String(_txt.segments[i].text || '');
    }
    if (!_queue.items || i < 0 || i >= _queue.items.length) return '';
    return String(_queue.items[i].text || '');
  }


  function jumpApproxMs(deltaMs) {
    if (!_isTtsActiveState(state.status)) return;

    // TXT legacy
    if (state.format === 'txt') {
      _txtJumpApproxMs(deltaMs);
      return;
    }

    if (!_queue.length) return;
    var cps = 15 * (state.rate || 1.0);
    var deltaChars = Math.abs(deltaMs / 1000) * cps;
    var blocksToSkip = Math.max(1, Math.round(deltaChars / 150));
    var dir = deltaMs > 0 ? 1 : -1;

    state._cancelFlag = true;
    if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();

    var targetIdx = _queue.index + (blocksToSkip * dir);
    targetIdx = Math.max(0, Math.min(_queue.length - 1, targetIdx));

    state._cancelFlag = false;

    if (_isPausedState(state.status)) {
      state.status = _pausedStateForDirection(deltaMs);
      var item = _queue.items[targetIdx];
      if (item) {
        _queue.index = targetIdx;
        state.currentText = item.text;
        state.blockIdx = targetIdx;
        _scheduleRefreshLocator();
        state.wordStart = -1;
        state.wordEnd = -1;
        fireProgress();
        fire();
      }
    } else {
      _speakQueueItem(targetIdx);
      fire();
    }
  }

  function playFromSelection(selectedText) {
    if (!state.engine) return false;

    // TXT legacy
    if (state.format === 'txt') {
      return _txtPlayFromSelection(selectedText);
    }

    if (!_queue.length) return false;
    // Find queue item containing the selected text
    var needle = String(selectedText || '').trim().toLowerCase();
    if (!needle) return false;
    for (var i = 0; i < _queue.length; i++) {
      if ((_queue.items[i].text || '').toLowerCase().indexOf(needle) >= 0) {
        state._cancelFlag = true;
        if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
        clearTtsHighlights();
        state._cancelFlag = false;
        state.status = PLAYING;
        acquireWakeLock();
        fire();
        _speakQueueItem(i);
        return true;
      }
    }
    return false;
  }

  function playFromElement(el) {
    if (!el || !state.engine) return false;

    // TXT legacy
    if (state.format === 'txt') {
      return _txtPlayFromElement(el);
    }

    if (!_queue.length) return false;
    // Find queue item whose ranges contain this element
    var text = (el.textContent || '').trim().toLowerCase();
    if (!text) return false;
    for (var i = 0; i < _queue.length; i++) {
      if ((_queue.items[i].text || '').toLowerCase().indexOf(text.substring(0, 40)) >= 0) {
        state._cancelFlag = true;
        if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
        clearTtsHighlights();
        state._cancelFlag = false;
        state.status = PLAYING;
        acquireWakeLock();
        fire();
        _speakQueueItem(i);
        return true;
      }
    }
    return false;
  }

  // FIX-TTS06: Click-to-speak — find queue item containing a DOM node and jump to it
  function playFromNode(node) {
    if (!node || !state.engine) return false;
    if (state.format === 'txt') return false;
    if (!_queue.length) return false;

    // Walk up to find the block parent
    var blockEl = _findBlockParent(node);
    if (!blockEl) return false;

    // Find the queue item whose ranges overlap this block
    for (var i = 0; i < _queue.length; i++) {
      var item = _queue.items[i];
      if (!item || !item.ranges || !item.ranges.size) continue;
      // Check if any mark range is inside the same block element
      var firstRange = item.ranges.values().next().value;
      if (!firstRange) continue;
      var itemBlock = _findBlockParent(firstRange.startContainer);
      if (itemBlock === blockEl) {
        // Found matching queue item — jump to it
        var wasPlaying = (state.status === PLAYING);
        state._cancelFlag = true;
        if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
        clearTtsHighlights();
        state._cancelFlag = false;
        state.status = PLAYING;
        acquireWakeLock();
        fire();
        _speakQueueItem(i);
        return true;
      }
    }
    return false;
  }

  // ── Rate / Pitch / Voice / Preset / Highlight ────────────────

  // FIX-TTS05: Re-speak current block with gapless transition
  function _respeakCurrentBlock() {
    if (state.status !== PLAYING || !state.engine) return;
    if (_queue.index < 0 || _queue.index >= _queue.length) return;
    var item = _queue.items[_queue.index];
    if (!item || !item.text) return;
    state.wordStart = -1;
    state.wordEnd = -1;
    fireProgress();
    if (typeof state.engine.speakGapless === 'function') {
      state.engine.speakGapless(item.text);
    } else {
      try { state.engine.cancel(); } catch {}
      state.engine.speak(item.text);
    }
    _preloadAhead(_queue.index + 1, _adaptivePreloadCount());
    _preloadBehind(_queue.index, 3);
  }

function _clearReconfigThrottle() {
  if (_ttsReconfigThrottle.timer) {
    try { clearTimeout(_ttsReconfigThrottle.timer); } catch {}
    _ttsReconfigThrottle.timer = 0;
  }
  _ttsReconfigThrottle.pendingRate = false;
  _ttsReconfigThrottle.pendingVoice = false;
}

function _resolveVoiceEngineId(voiceId) {
  var target = String(voiceId || '');
  if (!target) return '';
  var voices = [];
  try { voices = getVoices(); } catch { voices = []; }
  for (var i = 0; i < voices.length; i++) {
    var v = voices[i];
    if (!v) continue;
    var vid = String(v.voiceURI || v.name || '');
    if (vid === target) return String(v.engine || '');
  }
  return '';
}

function _capturePlaybackResumePoint() {
  var charIndex = (typeof state.wordStart === 'number' && state.wordStart >= 0)
    ? state.wordStart
    : ((typeof state.wordEnd === 'number' && state.wordEnd >= 0) ? state.wordEnd : -1);

  if (state.format === 'txt') {
    if (!_txt.segments || !_txt.segments.length) return null;
    var segIdx = (_txt.segIdx | 0);
    if (segIdx < 0) segIdx = 0;
    if (segIdx >= _txt.segments.length) segIdx = _txt.segments.length - 1;
    return { format: 'txt', segIdx: segIdx, charIndex: charIndex };
  }

  if (!_queue.items || !_queue.length) return null;
  var idx = (_queue.index | 0);
  if (idx < 0) idx = 0;
  if (idx >= _queue.length) idx = _queue.length - 1;
  return { format: 'queue', idx: idx, charIndex: charIndex };
}

function _applyVoiceRateSelectionToEngine() {
  if (!state.engine) return;
  var targetEngineId = _resolveVoiceEngineId(state.voiceId);
  if (targetEngineId && targetEngineId !== state.engineId) {
    switchEngine(targetEngineId);
  }
  if (!state.engine) return;
  try { if (typeof state.engine.setVoice === 'function' && state.voiceId) state.engine.setVoice(state.voiceId); } catch {}
  try { if (typeof state.engine.setRate === 'function') state.engine.setRate(state.rate); } catch {}
}

function _restartPlaybackFromResumePoint(point) {
  if (!point || !state.engine) return;
  try {
    if (typeof state.engine.setResumeHint === 'function' && point.charIndex >= 0) {
      state.engine.setResumeHint(point.charIndex);
    }
  } catch {}

  if (point.format === 'txt') {
    _txt.segIdx = point.segIdx;
    _txtSpeakSegment();
    fire();
    return;
  }

  _speakQueueItem(point.idx);
  fire();
}

function _isReconfigActivePlaybackStatus(status) {
  var s = String(status || '');
  return s === PLAYING || s === 'section_transition';
}

function _isReconfigPausedStatus(status) {
  return /(?:^|-)paused$/.test(String(status || ''));
}

function _runThrottledRateVoiceChange() {
  if (!_ttsReconfigThrottle.pendingRate && !_ttsReconfigThrottle.pendingVoice) return;

  _ttsReconfigThrottle.pendingRate = false;
  _ttsReconfigThrottle.pendingVoice = false;

  if (!state.engine) {
    fire();
    return;
  }

  if (!_isReconfigActivePlaybackStatus(state.status)) {
    _applyVoiceRateSelectionToEngine();
    if (_isReconfigPausedStatus(state.status)) fireProgress();
    fire();
    return;
  }

  var resumePoint = _capturePlaybackResumePoint();

  state._cancelFlag = true;
  try { state.engine.cancel(); } catch {}
  _hardCancelAllEngines();
  state._cancelFlag = false;

  _applyVoiceRateSelectionToEngine();

  if (!_isReconfigActivePlaybackStatus(state.status)) {
    fire();
    return;
  }

  _restartPlaybackFromResumePoint(resumePoint);

  if (state.format !== 'txt' && _queue.index >= 0) {
    _preloadAhead(_queue.index + 1, _adaptivePreloadCount());
    _preloadBehind(_queue.index, 3);
  }
}

// OPT-TTS-CTRL: Smart throttle — first change fires immediately, rapid follow-ups coalesce.
function _queueThrottledRateVoiceChange(kind) {
  if (kind === 'rate') _ttsReconfigThrottle.pendingRate = true;
  if (kind === 'voice') _ttsReconfigThrottle.pendingVoice = true;

  // If a timer is already pending, a burst is in progress.
  // Reset the timer to extend the coalesce window — the latest pending
  // flags will be picked up when the timer fires.
  if (_ttsReconfigThrottle.timer) {
    try { clearTimeout(_ttsReconfigThrottle.timer); } catch {}
    _ttsReconfigThrottle.timer = setTimeout(function () {
      _ttsReconfigThrottle.timer = 0;
      _runThrottledRateVoiceChange();
    }, TTS_RECONFIG_THROTTLE_MS);
    return;
  }

  // No timer pending — first change in a new burst. Execute immediately.
  _runThrottledRateVoiceChange();

  // Set a cooldown timer to coalesce any rapid follow-up changes.
  // If no second change arrives, the timer fires harmlessly (both pending flags are false).
  _ttsReconfigThrottle.timer = setTimeout(function () {
    _ttsReconfigThrottle.timer = 0;
    if (_ttsReconfigThrottle.pendingRate || _ttsReconfigThrottle.pendingVoice) {
      _runThrottledRateVoiceChange();
    }
  }, TTS_RECONFIG_THROTTLE_MS);
}

  var TTS_RATE_MIN = 0.5;
  // FIX-TTS-B4 #2: Edge TTS silently caps at 2.0x — match the real limit
  var TTS_RATE_MAX = 2.0;

  // TTS-QOL4: removed _respeakCurrentBlock() — rate change applies to next block,
  // avoids restarting the current sentence from the beginning.
  function setRate(r) {
    state.rate = Math.max(TTS_RATE_MIN, Math.min(TTS_RATE_MAX, Number(r) || 1.0));

    // TTS-THROTTLE: During active playback, avoid live engine rate changes.
    // Save the new rate and perform a throttled stop-restart from current position.
    if (state.status === PLAYING) {
      _queueThrottledRateVoiceChange('rate');
      fire();
      return;
    }

    if (state.engine) state.engine.setRate(state.rate);

    // FIX-TTS-B3 #14: Pre-fetch upcoming blocks at the new rate
    if (state.status === PLAYING && _queue.index >= 0) {
      _preloadAhead(_queue.index + 1, _adaptivePreloadCount());
    }
  }

  // TTS-QOL4: Volume control — passes through to engine's HTMLAudioElement.volume
  function setVolume(v) {
    state.volume = Math.max(0, Math.min(1, Number(v) || 1.0));
    if (state.engine && typeof state.engine.setVolume === 'function') {
      state.engine.setVolume(state.volume);
    }
  }

  // TTS-QOL4: removed _respeakCurrentBlock() — pitch change applies to next block
  function setPitch(p) {
    state.pitch = Math.max(0.5, Math.min(2.0, Number(p) || 1.0));
    if (state.engine && typeof state.engine.setPitch === 'function') {
      state.engine.setPitch(state.pitch);
    }
    // FIX-TTS-B3 #14: Pre-fetch upcoming blocks at the new pitch
    if (state.status === PLAYING && _queue.index >= 0) {
      _preloadAhead(_queue.index + 1, _adaptivePreloadCount());
    }
  }

  // TTS-QOL4: removed _respeakCurrentBlock() — preset change applies to next block
  function setPreset(presetId) {
    var p = TTS_PRESETS[presetId];
    if (!p) return;
    state.preset = presetId;
    state.rate = Math.max(TTS_RATE_MIN, Math.min(TTS_RATE_MAX, Number(p.rate) || 1.0));
    if (state.engine) state.engine.setRate(state.rate);
    state.pitch = Math.max(0.5, Math.min(2.0, Number(p.pitch) || 1.0));
    if (state.engine && typeof state.engine.setPitch === 'function') {
      state.engine.setPitch(state.pitch);
    }
    // FIX-TTS-B3 #14: Pre-fetch upcoming blocks at the new rate/pitch
    if (state.status === PLAYING && _queue.index >= 0) {
      _preloadAhead(_queue.index + 1, _adaptivePreloadCount());
    }
  }

  function setHighlightStyle(style) {
    var valid = ['highlight', 'underline', 'squiggly', 'strikethrough', 'enlarge'];
    if (valid.indexOf(style) < 0) return;
    // FIX-TTS08: When switching away from enlarge, remove the active span
    if (state.ttsHlStyle === 'enlarge' && style !== 'enlarge') {
      _clearEnlargeSpan();
    }
    state.ttsHlStyle = style;
    // FIX-TTS08: Rebuild CSS rules — style determines word highlight decoration
    _updateCssHighlightColors();
  }

  function setHighlightColor(colorName) {
    if (!TTS_HL_COLORS[colorName]) return;
    state.ttsHlColor = colorName;
    _updateCssHighlightColors();
  }

  function setHighlightGranularity(mode) {
    var m = String(mode || '').toLowerCase();
    if (m !== 'both' && m !== 'block' && m !== 'word') return;
    state.ttsHlGranularity = m;
    // Re-apply current highlight mode by forcing a progress refresh
    try { fireProgress(); } catch {}
  }

  function getHighlightGranularity() {
    return state.ttsHlGranularity || 'both';
  }

  // Compatibility aliases used by listening_player.js
  function setWordHighlightStyle(style) { setHighlightStyle(style); }
  function getWordHighlightStyle() { return state.ttsHlStyle; }
  function setWordHighlightColor(color) { setHighlightColor(color); }
  function getWordHighlightColor() { return state.ttsHlColor; }
  // FIX-TTS08: Set the enlarge scale factor (1.1–2.5)
  function setEnlargeScale(scale) {
    var s = Math.max(1.1, Math.min(2.5, Number(scale) || 1.35));
    state.ttsEnlargeScale = s;
    _updateCssHighlightColors(); // Rebuilds the .tts-enlarge CSS class
  }

  function setVoice(voiceId) {
    state.voiceId = String(voiceId || '');

    // TTS-THROTTLE: Voice changes while speaking can glitch or duplicate audio.
    // Coalesce rapid changes and restart from the current word/mark after switching.
    if (state.status === PLAYING) {
      _queueThrottledRateVoiceChange('voice');
      fire();
      return;
    }

    _applyVoiceRateSelectionToEngine();
    fire();
  }

  function getVoices() {
    var all = [];
    var seen = Object.create(null);
    var order = ['edge'];
    for (var i = 0; i < order.length; i++) {
      var engineId = order[i];
      var eng = state.allEngines[engineId];
      if (!eng || !isEngineUsable(engineId)) continue;
      var voices = [];
      try { voices = eng.getVoices ? eng.getVoices() : []; } catch { voices = []; }
      for (var v = 0; v < voices.length; v++) {
        var voice = voices[v];
        if (!voice) continue;
        var key = String(voice.voiceURI || voice.name || '') + '|' + String(voice.lang || '');
        if (seen[key]) continue;
        seen[key] = true;
        if (!voice.engine) voice.engine = engineId;
        all.push(voice);
      }
    }
    return all;
  }

  // ── Destroy ──────────────────────────────────────────────────

  function destroy() {
    // FIX-LISTEN-STAB3: signal any pending init() to bail out
    state._destroyed = true;
    _clearReconfigThrottle();
    try { stop(); } catch {}
    // FIX-TTS-B2: explicit wake lock release + listener cleanup
    releaseWakeLock();
    document.removeEventListener('visibilitychange', _onVisibilityChange);
    _visibilityListenerBound = false;
    var ids = Object.keys(state.allEngines);
    for (var i = 0; i < ids.length; i++) {
      try { state.allEngines[ids[i]].cancel(); } catch {}
      try { if (state.allEngines[ids[i]].clearPreloadCache) state.allEngines[ids[i]].clearPreloadCache(); } catch {}
    }
    state.engine = null;
    state.engineId = '';
    state.allEngines = {};
    state.engineUsable = {};
    _fol.tts = null;
    _fol.renderer = null;
    _queue.items = [];
    _queue.index = -1;
    _queue.length = 0;
    _clearCssHighlights();
    _txt.blocks = [];
    _txt.segments = [];
    _txt.segIdx = -1;
    state.hostFn = null;
    state.viewEngineFn = null;
    state.onNeedAdvance = null;
    state.initDone = false;
    state.initPromise = null;
  }

  // ── TXT Legacy Path ──────────────────────────────────────────

  var BLOCK_TAGS = ['p', 'div', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li', 'blockquote', 'figcaption', 'pre', 'td', 'th', 'dt', 'dd'];

  function _txtIsBlockTag(tagName) {
    return BLOCK_TAGS.indexOf(String(tagName || '').toLowerCase()) >= 0;
  }

  function _txtExtractBlocks() {
    _txt.blocks = [];
    _txt.segments = [];
    var host = (typeof state.hostFn === 'function') ? state.hostFn() : null;
    if (!host) return;
    var txtDoc = host.querySelector('.booksReaderTextDoc');
    if (!txtDoc) return;
    var doc = txtDoc.ownerDocument || document;
    var NF = (doc.defaultView && doc.defaultView.NodeFilter) ? doc.defaultView.NodeFilter : NodeFilter;
    var walker = doc.createTreeWalker(txtDoc, NF.SHOW_ELEMENT);
    var node = walker.nextNode();
    while (node) {
      var tag = (node.tagName || '').toLowerCase();
      if (_txtIsBlockTag(tag)) {
        var text = (node.textContent || '').trim();
        if (text.length > 1) {
          _txt.blocks.push({ text: text, element: node });
          _txt.segments.push({ text: text, blockIdx: _txt.blocks.length - 1, element: node });
        }
      }
      node = walker.nextNode();
    }
  }

  function _txtPlay() {
    _txtExtractBlocks();
    if (!_txt.segments.length) return;
    _txt.segIdx = 0;
    state.status = PLAYING;
    acquireWakeLock();
    fire();
    _txtSpeakSegment();
  }

  function _txtSpeakSegment() {
    if (_txt.segIdx < 0 || _txt.segIdx >= _txt.segments.length) {
      handleAllBlocksDone();
      return;
    }
    if (!state.engine) { stop(); return; }
    var seg = _txt.segments[_txt.segIdx];
    state.currentText = seg.text;
    state.blockIdx = seg.blockIdx;
    _scheduleRefreshLocator();
    state.wordStart = -1;
    state.wordEnd = -1;
    if (_txt.activeEl) {
      try { _txt.activeEl.classList.remove('booksReaderTtsActive'); } catch {}
    }
    try {
      seg.element.classList.add('booksReaderTtsActive');
      if (!state.suppressAutoScrollUntil || Date.now() > state.suppressAutoScrollUntil) {
        seg.element.scrollIntoView({ block: 'center', behavior: 'smooth' });
      }
    } catch {}
    _txt.activeEl = seg.element;
    fireProgress();
    state.engine.speak(seg.text);
  }

  function _txtHandleBlockEnd() {
    if (state._cancelFlag) return;
    if (state.status !== PLAYING) return;
    _txt.segIdx++;
    if (_txt.segIdx < _txt.segments.length) {
      _txtSpeakSegment();
    } else {
      handleAllBlocksDone();
    }
  }

  function _txtStepSegment(delta) {
    if (!_isTtsActiveState(state.status)) return;
    if (!_txt.segments.length) return;
    var target = _txt.segIdx + delta;
    target = Math.max(0, Math.min(_txt.segments.length - 1, target));
    if (target === _txt.segIdx) return;
    state._cancelFlag = true;
    if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
    state._cancelFlag = false;
    _txt.segIdx = target;
    if (_isPausedState(state.status)) {
      state.status = _pausedStateForDirection(delta);
      var seg = _txt.segments[_txt.segIdx];
      state.currentText = seg.text;
      state.blockIdx = seg.blockIdx;
      _scheduleRefreshLocator();
      state.wordStart = -1;
      state.wordEnd = -1;
      fireProgress();
      fire();
    } else {
      _txtSpeakSegment();
      fire();
    }
  }

  function _txtSeekSegment(targetIdx, autoplay) {
    if (!_isTtsActiveState(state.status)) return;
    if (!_txt.segments.length) return;
    var idx = (targetIdx | 0);
    if (idx < 0) idx = 0;
    if (idx >= _txt.segments.length) idx = _txt.segments.length - 1;

    state._cancelFlag = true;
    if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
    state._cancelFlag = false;

    _txt.segIdx = idx;
    var shouldPlay = !!autoplay;
    if (state.status === PLAYING) shouldPlay = true;

    if (!shouldPlay) {
      if (_isPausedState(state.status) && state.status !== PAUSED) state.status = PAUSED;
      var seg = _txt.segments[_txt.segIdx];
      state.currentText = seg.text;
      state.blockIdx = seg.blockIdx;
      _scheduleRefreshLocator();
      state.wordStart = -1;
      state.wordEnd = -1;
      fireProgress();
      fire();
      return;
    }

    _txtSpeakSegment();
    fire();
  }


  function _txtJumpApproxMs(deltaMs) {
    var cps = 15 * (state.rate || 1.0);
    var deltaChars = Math.abs(deltaMs / 1000) * cps;
    var dir = deltaMs > 0 ? 1 : -1;
    var remaining = deltaChars;
    var idx = _txt.segIdx;
    while (remaining > 0 && idx >= 0 && idx < _txt.segments.length) {
      remaining -= (_txt.segments[idx].text || '').length;
      if (remaining > 0) idx += dir;
    }
    idx = Math.max(0, Math.min(_txt.segments.length - 1, idx));
    if (idx === _txt.segIdx) idx = Math.max(0, Math.min(_txt.segments.length - 1, _txt.segIdx + dir));
    if (idx === _txt.segIdx) return;
    state._cancelFlag = true;
    if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
    state._cancelFlag = false;
    _txt.segIdx = idx;
    if (_isPausedState(state.status)) {
      state.status = _pausedStateForDirection(deltaMs);
      var seg = _txt.segments[_txt.segIdx];
      state.currentText = seg.text;
      state.blockIdx = seg.blockIdx;
      _scheduleRefreshLocator();
      state.wordStart = -1;
      state.wordEnd = -1;
      fireProgress();
      fire();
    } else {
      _txtSpeakSegment();
      fire();
    }
  }

  function _txtPlayFromSelection(selectedText) {
    if (!selectedText || !selectedText.trim()) return false;
    _txtExtractBlocks();
    if (!_txt.segments.length) return false;
    var needle = selectedText.trim().toLowerCase();
    for (var i = 0; i < _txt.segments.length; i++) {
      if ((_txt.segments[i].text || '').toLowerCase().indexOf(needle) >= 0) {
        _txt.segIdx = i;
        state.status = PLAYING;
        acquireWakeLock();
        fire();
        _txtSpeakSegment();
        return true;
      }
    }
    return false;
  }

  function _txtPlayFromElement(el) {
    _txtExtractBlocks();
    if (!_txt.segments.length) return false;
    var target = el;
    for (var depth = 0; depth < 8 && target; depth++) {
      for (var i = 0; i < _txt.segments.length; i++) {
        if (_txt.segments[i].element === target) {
          state._cancelFlag = true;
          if (state.engine) { try { state.engine.cancel(); } catch {} }
    _hardCancelAllEngines();
          state._cancelFlag = false;
          _txt.segIdx = i;
          state.status = PLAYING;
          acquireWakeLock();
          fire();
          _txtSpeakSegment();
          return true;
        }
      }
      target = target.parentElement;
    }
    return false;
  }

  // TTS-RESUME-HL: Return range info for the current TTS block (must be called before destroy)
  function getLastBlockInfo() {
    if (state.format === 'txt') return null;
    var idx = _queue.index;
    if (idx < 0 || idx >= _queue.length) return null;
    var item = _queue.items[idx];
    if (!item || !item.ranges || !item.ranges.size) return null;

    var firstRange = item.ranges.values().next().value;
    if (!firstRange) return null;

    var blockEl = _findBlockParent(firstRange.startContainer);
    var blockRange = null;
    if (blockEl) {
      try {
        var doc = blockEl.ownerDocument || document;
        blockRange = doc.createRange();
        blockRange.selectNodeContents(blockEl);
      } catch {}
    }

    return {
      range: firstRange,
      blockRange: blockRange,
      renderer: _fol.renderer || null,
    };
  }

  // ── Find first queue block visible on the current page ──────
  function getFirstVisibleBlockIndex() {
    if (!_queue.length) return 0;
    var renderer = _fol.renderer;
    if (!renderer) return 0;
    try {
      var start = typeof renderer.start !== 'undefined' ? renderer.start : 0;
      var size = typeof renderer.size !== 'undefined' ? renderer.size : 0;
      if (!size) return 0;
      // In paginated mode, the visible page spans [start - size, start) in document coords.
      // The renderer uses a mapRect function internally; for LTR horizontal it's identity.
      var pageStart = start - size;
      var pageEnd = start;
      for (var i = 0; i < _queue.length; i++) {
        var item = _queue.items[i];
        if (!item || !item.ranges || !item.ranges.size) continue;
        var firstRange = item.ranges.values().next().value;
        if (!firstRange) continue;
        try {
          var rect = firstRange.getBoundingClientRect();
          if (!rect) continue;
          // Block is visible if its left edge is within the current page
          if (rect.left >= pageStart && rect.left < pageEnd) return i;
          // Also check if block spans across the page boundary
          if (rect.left < pageStart && rect.right > pageStart) return i;
        } catch (e) { continue; }
      }
    } catch (e) {}
    return 0;
  }

  // ── Public API ───────────────────────────────────────────────

  window.booksTTS = {
    init: init,
    play: play,
    pause: pause,
    resume: resume,
    stop: stop,
    destroy: destroy,
    notifyUserInteraction: function (ms) { try { state.suppressAutoScrollUntil = Date.now() + (ms || 2500); } catch {} },
    setRate: setRate,
    setVolume: setVolume, // TTS-QOL4
    getVolume: function () { return state.volume; }, // TTS-QOL4
    setVoice: setVoice,
    getVoices: getVoices,
    setPitch: setPitch,
    setPreset: setPreset,
    getPresets: function () { return TTS_PRESETS; },
    getPitch: function () { return state.pitch; },
    stepSegment: stepBlock,
    seekSegment: seekSegment,
    getSegmentText: getSegmentText,
    playFromSelection: playFromSelection,
    playFromElement: playFromElement,
    playFromNode: playFromNode,
    jumpApproxMs: jumpApproxMs,
    getRateLimits: function () { return { min: TTS_RATE_MIN, max: TTS_RATE_MAX }; },
    getState: getState,
    getSnippet: function () { return snippetInfo(); },
    getRate: function () { return state.rate; },
    isAvailable: function () { return !!(state.engine && isEngineUsable(state.engineId)); },
    // EDGE_TTS_FIX Phase 5.2: separates "init has completed" from "engine is
    // usable". reader_state.js ttsSupported() needs both: pre-init it falls
    // back to factory presence; post-init it trusts isAvailable().
    isInitDone: function () { return !!state.initDone; },
    getEngineId: function () { return state.engineId; },
    getAvailableEngines: function () { return Object.keys(state.allEngines).filter(isEngineUsable); },
    getEngineUsableMap: function () { return { ...state.engineUsable }; },
    getLastDiag: function () { return state.lastDiag ? { ...state.lastDiag } : null; },
    switchEngine: switchEngine,
    setHighlightStyle: setHighlightStyle,
    setHighlightColor: setHighlightColor,
    getHighlightStyles: function () { return ['highlight', 'underline', 'squiggly', 'strikethrough', 'enlarge']; },
    getHighlightColors: function () { return Object.keys(TTS_HL_COLORS); },
    getHighlightStyle: function () { return state.ttsHlStyle; },
    getHighlightColor: function () { return state.ttsHlColor; },
    setHighlightGranularity: setHighlightGranularity,
    getHighlightGranularity: getHighlightGranularity,
    setWordHighlightStyle: setWordHighlightStyle,
    getWordHighlightStyle: getWordHighlightStyle,
    setWordHighlightColor: setWordHighlightColor,
    getWordHighlightColor: getWordHighlightColor,
    getSegmentWindow: getSegmentWindow,
    setEnlargeScale: setEnlargeScale,
    getEnlargeScale: function () { return state.ttsEnlargeScale; },
    getLastBlockInfo: getLastBlockInfo, // TTS-RESUME-HL
    getFirstVisibleBlockIndex: getFirstVisibleBlockIndex,
    _reinitFoliateTTS: _reinitFoliateTTS,
    // FIX-TTS05: expose queue regeneration for section transitions
    _regenerateQueue: _generateQueue,
    set onStateChange(fn) { state.onStateChange = typeof fn === 'function' ? fn : null; },
    set onProgress(fn) { state.onProgress = typeof fn === 'function' ? fn : null; },
    set onDocumentEnd(fn) { state.onDocumentEnd = typeof fn === 'function' ? fn : null; },
    on: _on,
    off: _off,
  };
})();
