// TTS HUD Bar — bottom transport controller for TTS playback
// Ported from Butterfly listening_player.js, stripped to bar-only (no overlay/card mode).
// Wires #lpTtsBar controls to window.booksTTS and manages auto-hide, settings, diagnostics.
(function () {
  'use strict';

  if (window.__ttsHudBound) return;
  window.__ttsHudBound = true;

  // ── State ─────────────────────────────────────────────────────────────────
  var _ttsActive = false;
  var _ttsCallbackBindings = null;
  var _lastSavedBlockIdx = -1;
  var _saveTimer = null;

  // Sleep timer
  var _sleepMode = 0;      // 0=off, -1=end-of-chapter, >0=minutes
  var _sleepRemaining = 0; // seconds left
  var _sleepInterval = null;

  // Auto-hide
  var _barHideTimer = null;
  var _barLastStatus = 'idle';
  var _barVisibleDesired = true;
  var _barHasPlayed = false;
  var _barAutoHideUiWired = false;
  var _barHideEpoch = 0;
  var BAR_AUTO_HIDE_MS = 3000;
  var BAR_FADE_MS = 300;

  // ── Settings (persisted in localStorage) ──────────────────────────────────
  var _settings = {
    ttsVoice: '', ttsRate: 1.0, ttsPitch: 1.0, ttsPreset: '',
    ttsHlStyle: 'highlight', ttsHlColor: 'grey',
    ttsHlGranularity: 'sentence',
    ttsWordHlStyle: 'highlight', ttsWordHlColor: 'blue',
    ttsEnlargeScale: 1.35, ttsVolume: 1.0,
  };

  function _lsGet(n) { try { return localStorage.getItem('booksListen.' + n); } catch { return null; } }
  function _lsSet(n, v) { try { localStorage.setItem('booksListen.' + n, String(v)); } catch {} }

  function _loadPrefs() {
    var sv;
    sv = _lsGet('Voice');     if (sv) _settings.ttsVoice = sv;
    sv = _lsGet('Rate');      if (sv) _settings.ttsRate = parseFloat(sv) || 1.0;
    sv = _lsGet('Pitch');     if (sv) _settings.ttsPitch = parseFloat(sv) || 1.0;
    sv = _lsGet('Preset');    if (sv) _settings.ttsPreset = sv;
    sv = _lsGet('HlStyle');   if (sv) _settings.ttsHlStyle = sv;
    sv = _lsGet('HlColor');   if (sv) _settings.ttsHlColor = sv;
    sv = _lsGet('HlGran');    if (sv) _settings.ttsHlGranularity = sv;
    sv = _lsGet('WordStyle'); if (sv) _settings.ttsWordHlStyle = sv;
    sv = _lsGet('WordColor'); if (sv) _settings.ttsWordHlColor = sv;
    sv = _lsGet('Enlarge');   if (sv) _settings.ttsEnlargeScale = parseFloat(sv) || 1.35;
    sv = _lsGet('Volume');    if (sv) _settings.ttsVolume = parseFloat(sv) || 1.0;
  }
  _loadPrefs();

  function qs(id) { try { return document.getElementById(id); } catch { return null; } }
  function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

  // ── SVG icons ─────────────────────────────────────────────────────────────
  var SVG_PLAY = '<svg viewBox="0 0 16 18" xmlns="http://www.w3.org/2000/svg"><path d="M14.3195 7.73218L3.06328 0.847019C2.82722 0.703112 2.55721 0.624413 2.2808 0.618957C2.00439 0.6135 1.73148 0.681481 1.48992 0.81596C1.24837 0.950439 1.04682 1.1466 0.905848 1.38442C0.764877 1.62225 0.689531 1.89322 0.6875 2.16968V15.94C0.689531 16.2164 0.764877 16.4874 0.905848 16.7252C1.04682 16.9631 1.24837 17.1592 1.48992 17.2937C1.73148 17.4282 2.00439 17.4962 2.2808 17.4907C2.55721 17.4853 2.82722 17.4066 3.06328 17.2626L14.3195 10.3775C14.5465 10.2393 14.7341 10.0451 14.8643 9.81344C14.9945 9.58179 15.0628 9.32055 15.0628 9.05483C15.0628 8.78912 14.9945 8.52787 14.8643 8.29623C14.7341 8.06458 14.5465 7.87034 14.3195 7.73218ZM2.5625 15.3712V2.73843L12.8875 9.05483L2.5625 15.3712Z" fill="currentColor"/></svg>';
  var SVG_PAUSE = '<svg viewBox="0 0 16 18" xmlns="http://www.w3.org/2000/svg"><rect x="2" y="0.87" width="4" height="16.37" rx="0.75" fill="currentColor"/><rect x="10" y="0.87" width="4" height="16.37" rx="0.75" fill="currentColor"/></svg>';

  // ── Voice display names ───────────────────────────────────────────────────
  var VOICE_DISPLAY = {
    'AnaNeural':           { label: 'Ana',           g: 'F', desc: 'Child' },
    'AndrewNeural':        { label: 'Andrew',        g: 'M', desc: 'Warm, storyteller' },
    'AndrewMultilingualNeural': { label: 'Andrew ML', g: 'M', desc: 'Warm, multilingual' },
    'AriaNeural':          { label: 'Aria',          g: 'F', desc: 'Expressive, versatile' },
    'AvaNeural':           { label: 'Ava',           g: 'F', desc: 'Bright, engaging' },
    'AvaMultilingualNeural': { label: 'Ava ML',      g: 'F', desc: 'Bright, multilingual' },
    'BrianNeural':         { label: 'Brian',         g: 'M', desc: 'Youthful, cheerful' },
    'BrianMultilingualNeural': { label: 'Brian ML',  g: 'M', desc: 'Youthful, multilingual' },
    'ChristopherNeural':   { label: 'Christopher',   g: 'M', desc: 'Authoritative' },
    'EmmaNeural':          { label: 'Emma',          g: 'F', desc: 'Friendly, educational' },
    'EmmaMultilingualNeural': { label: 'Emma ML',    g: 'F', desc: 'Friendly, multilingual' },
    'EricNeural':          { label: 'Eric',          g: 'M', desc: 'Neutral' },
    'GuyNeural':           { label: 'Guy',           g: 'M', desc: 'Professional, mature' },
    'JennyNeural':         { label: 'Jenny',         g: 'F', desc: 'Warm, assistant' },
    'JennyMultilingualNeural': { label: 'Jenny ML',  g: 'F', desc: 'Warm, multilingual' },
    'MichelleNeural':      { label: 'Michelle',      g: 'F', desc: 'Clear, professional' },
    'RogerNeural':         { label: 'Roger',         g: 'M', desc: 'Mature, narrator' },
    'SteffanNeural':       { label: 'Steffan',       g: 'M', desc: 'Smooth' },
    'DavisNeural':         { label: 'Davis',         g: 'M', desc: 'Conversational' },
    'JaneNeural':          { label: 'Jane',          g: 'F', desc: 'Clear, expressive' },
    'JasonNeural':         { label: 'Jason',         g: 'M', desc: 'Steady, articulate' },
    'NancyNeural':         { label: 'Nancy',         g: 'F', desc: 'Warm, approachable' },
    'TonyNeural':          { label: 'Tony',          g: 'M', desc: 'Strong, confident' },
    'SaraNeural':          { label: 'Sara',          g: 'F', desc: 'Soft, gentle' },
  };
  var LOCALE_LABELS = {
    'en-US': 'US', 'en-GB': 'British', 'en-AU': 'Australian', 'en-IN': 'Indian',
    'en-IE': 'Irish', 'en-CA': 'Canadian', 'en-NZ': 'New Zealand',
  };

  function _voiceDisplayName(v) {
    var uri = String(v.voiceURI || v.name || '');
    var parts = uri.split('-');
    var locale = (parts.length >= 2) ? (parts[0] + '-' + parts[1]) : '';
    var shortName = (parts.length >= 3) ? parts.slice(2).join('') : uri.replace(/Neural$/i, '') + 'Neural';
    var entry = VOICE_DISPLAY[shortName];
    var localeTag = LOCALE_LABELS[locale] || locale || '';
    var gender = (entry && entry.g) || '';
    var genderStr = gender === 'F' ? '\u2640' : gender === 'M' ? '\u2642' : '';
    var desc = (entry && entry.desc) || '';
    var baseName = (entry && entry.label) || shortName.replace(/Neural$/i, '').replace(/Multilingual$/i, ' ML');
    var result = baseName;
    if (genderStr) result += ' ' + genderStr;
    if (desc) result += ' \u00b7 ' + desc;
    if (localeTag && localeTag !== 'US') result += ' \u00b7 ' + localeTag;
    return result;
  }

  // ── Status helpers ────────────────────────────────────────────────────────
  function _isPaused(s) {
    s = String(s || '');
    return s === 'paused' || s === 'stop-paused' || s === 'backward-paused'
      || s === 'forward-paused' || s === 'setrate-paused' || s === 'setvoice-paused';
  }
  function _isPlaying(s) { s = String(s || ''); return s === 'playing' || s === 'section_transition'; }

  // ── Auto-hide ─────────────────────────────────────────────────────────────
  function _ensureBarStyles() {
    var bar = qs('lpTtsBar');
    if (!bar) return null;
    if (!bar.dataset.autohideInit) {
      bar.dataset.autohideInit = '1';
      bar.style.transition = 'opacity ' + BAR_FADE_MS + 'ms ease';
      bar.addEventListener('transitionend', function (ev) {
        if (ev.propertyName !== 'opacity' || _barVisibleDesired) return;
        if (bar.style.opacity === '0') bar.classList.add('hidden');
      });
    }
    return bar;
  }

  function _clearHideTimer() {
    _barHideEpoch++;
    if (_barHideTimer) { clearTimeout(_barHideTimer); _barHideTimer = null; }
  }

  function _isMegaOpen() { var m = qs('lpTtsMega'); return !!(m && !m.classList.contains('hidden')); }

  function _setBarVisible(visible, hard) {
    var bar = _ensureBarStyles();
    if (!bar) return;
    if (visible) {
      _barVisibleDesired = true;
      bar.classList.remove('hidden');
      bar.style.opacity = '1';
      bar.style.pointerEvents = 'auto';
    } else {
      _barVisibleDesired = false;
      if (hard) { bar.style.opacity = '0'; bar.style.pointerEvents = 'none'; bar.classList.add('hidden'); return; }
      bar.classList.remove('hidden');
      bar.style.opacity = '0';
      bar.style.pointerEvents = 'none';
    }
  }

  function _refreshAutoHide(forceShow) {
    if (!_ttsActive) { _clearHideTimer(); return; }
    if (forceShow) _setBarVisible(true);
    if (_isMegaOpen() || _isPaused(_barLastStatus)) { _clearHideTimer(); _setBarVisible(true); return; }
    _clearHideTimer();
    if (!_isPlaying(_barLastStatus)) return;
    _setBarVisible(true);
    var epoch = _barHideEpoch;
    _barHideTimer = setTimeout(function () {
      _barHideTimer = null;
      if (epoch !== _barHideEpoch) return;
      if (_isPlaying(_barLastStatus) && !_isMegaOpen()) _setBarVisible(false);
    }, BAR_AUTO_HIDE_MS);
  }

  function _wireAutoHideUi() {
    if (_barAutoHideUiWired) return;
    var bar = qs('lpTtsBar');
    var area = document.querySelector('.br-reading-area');
    if (!bar || !area) return;
    _barAutoHideUiWired = true;
    _ensureBarStyles();
    bar.addEventListener('mousemove', function () { if (_ttsActive) _refreshAutoHide(true); });
    bar.addEventListener('mouseenter', function () { if (_ttsActive) _refreshAutoHide(true); });
    area.addEventListener('mousemove', function () { if (_ttsActive) _refreshAutoHide(true); });
  }

  // ── Sync UI ───────────────────────────────────────────────────────────────
  function syncPlayPause(status) {
    var btn = qs('lpTtsPlayPause');
    if (!btn) return;
    var playing = _isPlaying(status);
    btn.innerHTML = playing ? SVG_PAUSE : SVG_PLAY;
    btn.title = playing ? 'Pause' : 'Play';
  }

  function syncBuffering(buffering) {
    var btn = qs('lpTtsPlayPause');
    if (btn) btn.classList.toggle('lp-buffering', !!buffering);
  }

  function syncSpeed() {
    var tts = window.booksTTS;
    var rate = (tts && tts.getRate) ? tts.getRate() : 1.0;
    var el = qs('lpTtsSpeed');
    if (el) el.textContent = rate.toFixed(1) + '\u00d7';
  }

  function syncEngine() {
    var tts = window.booksTTS;
    var el = qs('lpTtsEngine');
    if (!el) return;
    var eid = tts ? tts.getEngineId() : '';
    el.textContent = eid === 'edge' ? 'Edge Neural' : '';
    el.title = eid ? ('TTS engine: ' + eid) : '';
  }

  // Listen toggle button active state
  function syncListenBtn(active) {
    var btn = qs('booksReaderListenToggle') || qs('booksReaderListenBtn');
    if (btn) btn.classList.toggle('ttsActive', !!active);
  }

  // ── Voice selector ────────────────────────────────────────────────────────
  var _voiceCacheCount = -1;

  function populateVoices() {
    var sel = qs('lpTtsVoice');
    if (!sel) return;
    var tts = window.booksTTS;
    if (!tts || typeof tts.getVoices !== 'function') return;
    var voices = [];
    try { voices = tts.getVoices(); } catch {}
    if (_voiceCacheCount === voices.length && sel.options.length > 0) {
      if (_settings.ttsVoice) sel.value = _settings.ttsVoice;
      return;
    }
    _voiceCacheCount = voices.length;
    var enVoices = voices.filter(function (v) { return /^en[-_]/i.test(v.lang || ''); });
    sel.innerHTML = '';
    if (!enVoices.length) {
      // EDGE_TTS_FIX Phase 5.3: distinguish "engine OK but no voices match
      // the en-* filter" (defensive — the Qt static table guarantees ≥23
      // English voices, so this branch is normally unreachable) from "engine
      // failed to probe" (Listen click will never produce audio; surface the
      // structured failure-reason taxonomy from EdgeTtsClient instead of the
      // misleading "No English voices" string).
      var opt = document.createElement('option');
      opt.value = ''; opt.disabled = true;
      var available = (typeof tts.isAvailable === 'function') ? tts.isAvailable() : true;
      if (!available) {
        var diag = (typeof tts.getLastDiag === 'function') ? tts.getLastDiag() : null;
        var reason = (diag && (diag.detail || diag.code)) || 'engine_unavailable';
        opt.textContent = 'Edge TTS unavailable: ' + reason;
      } else {
        opt.textContent = 'No English voices';
      }
      sel.appendChild(opt); return;
    }
    var groups = {};
    for (var i = 0; i < enVoices.length; i++) {
      var v = enVoices[i];
      var uri = String(v.voiceURI || v.name || '');
      var parts = uri.split('-');
      var loc = (parts.length >= 2) ? (parts[0] + '-' + parts[1]) : 'en-US';
      if (!groups[loc]) groups[loc] = [];
      groups[loc].push(v);
    }
    var order = ['en-US', 'en-GB', 'en-AU', 'en-IN'];
    Object.keys(groups).forEach(function (k) { if (order.indexOf(k) < 0) order.push(k); });
    for (var gi = 0; gi < order.length; gi++) {
      var grp = groups[order[gi]];
      if (!grp || !grp.length) continue;
      var optGroup = document.createElement('optgroup');
      optGroup.label = (LOCALE_LABELS[order[gi]] || order[gi]) + ' (' + grp.length + ')';
      for (var vi = 0; vi < grp.length; vi++) {
        var o = document.createElement('option');
        o.value = grp[vi].voiceURI || grp[vi].name || '';
        o.textContent = _voiceDisplayName(grp[vi]);
        optGroup.appendChild(o);
      }
      sel.appendChild(optGroup);
    }
    sel.value = _settings.ttsVoice || 'en-US-AndrewNeural';
  }

  // ── Highlight controls ────────────────────────────────────────────────────
  var HL_SWATCHES = { grey: '#9a9aa8', blue: '#5a96ff', yellow: '#e6c800', green: '#50b464', pink: '#ff6e96', orange: '#ffa032' };

  function populateHlControls() {
    var tts = window.booksTTS;
    if (!tts) return;
    var hlSel = qs('lpTtsHlStyle');
    if (hlSel) {
      var styles = typeof tts.getHighlightStyles === 'function' ? tts.getHighlightStyles() : [];
      var labels = { highlight: 'Highlight', underline: 'Underline', squiggly: 'Squiggly', strikethrough: 'Strikethrough', enlarge: 'Enlarge' };
      if (hlSel.options.length === 0) {
        for (var i = 0; i < styles.length; i++) {
          var o = document.createElement('option');
          o.value = styles[i]; o.textContent = labels[styles[i]] || styles[i];
          hlSel.appendChild(o);
        }
      }
      hlSel.value = typeof tts.getHighlightStyle === 'function' ? tts.getHighlightStyle() : 'highlight';
    }
    // Color swatches
    var container = qs('lpTtsHlColors');
    if (container && tts) {
      var colors = typeof tts.getHighlightColors === 'function' ? tts.getHighlightColors() : [];
      var curColor = typeof tts.getHighlightColor === 'function' ? tts.getHighlightColor() : 'blue';
      if (!container.children.length) {
        for (var j = 0; j < colors.length; j++) {
          var btn = document.createElement('button');
          btn.className = 'ttsColorSwatch';
          btn.dataset.color = colors[j];
          btn.style.background = HL_SWATCHES[colors[j]] || '#888';
          btn.title = colors[j].charAt(0).toUpperCase() + colors[j].slice(1);
          container.appendChild(btn);
        }
      }
      var btns = container.querySelectorAll('.ttsColorSwatch');
      for (var k = 0; k < btns.length; k++) btns[k].classList.toggle('active', btns[k].dataset.color === curColor);
    }
    // Enlarge row
    var curStyle = typeof tts.getHighlightStyle === 'function' ? tts.getHighlightStyle() : 'highlight';
    var enlargeRow = qs('lpEnlargeRow');
    if (enlargeRow) enlargeRow.style.display = (curStyle === 'enlarge') ? '' : 'none';
    var scaleSlider = qs('lpTtsEnlargeScale');
    if (scaleSlider) scaleSlider.value = typeof tts.getEnlargeScale === 'function' ? tts.getEnlargeScale() : 1.35;
    var scaleVal = qs('lpTtsEnlargeVal');
    if (scaleVal) { var sv2 = typeof tts.getEnlargeScale === 'function' ? tts.getEnlargeScale() : 1.35; scaleVal.textContent = sv2.toFixed(2) + 'x'; }
  }

  // ── Diagnostics ───────────────────────────────────────────────────────────
  function updateDiag() {
    var body = qs('lpTtsDiagBody');
    if (!body) return;
    var tts = window.booksTTS;
    if (!tts) { body.textContent = 'TTS not initialized'; return; }
    var info = tts.getSnippet ? tts.getSnippet() : {};
    var lines = [
      'Engine: ' + (info.engineId || 'none'),
      'Status: ' + (info.status || 'idle'),
      'Rate: ' + (info.rate || 1.0).toFixed(1),
      'Pitch: ' + (info.pitch || 1.0).toFixed(2),
      'Preset: ' + (info.preset || 'custom'),
      'Voice: ' + (_settings.ttsVoice || '(default)'),
      'Block: ' + (info.blockIdx >= 0 ? (info.blockIdx + 1) + '/' + (info.blockCount || '?') : '-'),
    ];
    if (info.lastError) {
      var errStr = typeof info.lastError === 'object'
        ? (info.lastError.error || info.lastError.message || JSON.stringify(info.lastError))
        : String(info.lastError);
      lines.push('Last error: ' + errStr);
    }
    body.textContent = lines.join('\n');
  }

  // ── TTS actions ───────────────────────────────────────────────────────────
  function ttsToggle() {
    _refreshAutoHide(true);
    var tts = window.booksTTS;
    if (!tts) return;
    var st = tts.getState();
    if (st === 'section_transition') return;
    if (st === 'idle') tts.play(0, { startFromVisible: true });
    else if (st === 'playing') tts.pause();
    else if (_isPaused(st)) tts.resume();
  }

  function ttsStop() {
    _clearHideTimer();
    setSleepTimer(0);
    var tts = window.booksTTS;
    if (tts) {
      var stopInfo = null;
      if (typeof tts.getSnippet === 'function') {
        try {
          var sn = tts.getSnippet();
          var snIdx = Number(sn && sn.blockIdx);
          var snCount = Number(sn && sn.blockCount);
          if (Number.isFinite(snIdx) && snIdx >= 0) {
            _lastSavedBlockIdx = Math.floor(snIdx);
            stopInfo = {
              blockIdx: _lastSavedBlockIdx,
              blockCount: Number.isFinite(snCount) && snCount >= 0 ? Math.floor(snCount) : 0,
            };
          }
        } catch {}
      }
      if (!stopInfo && _lastSavedBlockIdx >= 0) {
        stopInfo = { blockIdx: _lastSavedBlockIdx, blockCount: 0 };
      }
      if (stopInfo) saveProgress(stopInfo, true);
      tts.stop();
    }
    _setBarVisible(false, true);
    _ttsActive = false;
    _barHasPlayed = false;
    syncListenBtn(false);
  }

  // ── Sleep timer ───────────────────────────────────────────────────────────
  function setSleepTimer(mode) {
    _sleepMode = mode;
    if (_sleepInterval) { clearInterval(_sleepInterval); _sleepInterval = null; }
    if (mode > 0) {
      _sleepRemaining = mode * 60;
      _sleepInterval = setInterval(_tickSleep, 1000);
    } else {
      _sleepRemaining = 0;
    }
    _syncSleepUi();
  }

  function _tickSleep() {
    if (_sleepRemaining <= 0) {
      setSleepTimer(0);
      ttsStop();
      return;
    }
    _sleepRemaining--;
    _syncSleepBadge();
  }

  function _syncSleepUi() {
    var chips = document.querySelectorAll('.ttsSleepChip');
    for (var i = 0; i < chips.length; i++) {
      var v = parseInt(chips[i].dataset.sleep, 10);
      chips[i].classList.toggle('active', v === _sleepMode);
    }
    _syncSleepBadge();
  }

  function _syncSleepBadge() {
    var badge = document.getElementById('lpTtsSleepBadge');
    if (!badge) return;
    if (_sleepMode === 0) {
      badge.classList.add('hidden');
      badge.textContent = '';
    } else if (_sleepMode === -1) {
      badge.classList.remove('hidden');
      badge.textContent = 'end ch.';
    } else {
      badge.classList.remove('hidden');
      var m = Math.floor(_sleepRemaining / 60);
      var s = _sleepRemaining % 60;
      badge.textContent = m + ':' + String(s).padStart(2, '0');
    }
  }

  function _onTtsChapterEnd() {
    if (_sleepMode === -1) {
      setSleepTimer(0);
      ttsStop();
    }
  }

  function ttsAdjustSpeed(delta) {
    _refreshAutoHide(true);
    var tts = window.booksTTS;
    if (!tts) return;
    var current = tts.getRate();
    var limits = typeof tts.getRateLimits === 'function' ? tts.getRateLimits() : { min: 0.5, max: 3.0 };
    var next = Math.max(limits.min, Math.min(limits.max, Math.round((current + delta) * 10) / 10));
    if (tts.setRate) tts.setRate(next);
    _settings.ttsRate = next;
    _lsSet('Rate', String(next));
    syncSpeed();
  }

  function ttsJump(deltaMs) {
    _refreshAutoHide(true);
    var tts = window.booksTTS;
    if (tts && typeof tts.jumpApproxMs === 'function') tts.jumpApproxMs(deltaMs);
  }

  // ── Progress persistence ──────────────────────────────────────────────────
  function saveProgress(info, immediate) {
    var api = window.electronAPI;
    if (!api || typeof api.saveBooksTtsProgress !== 'function') return;
    var RS = window.booksReaderState;
    var book = RS && RS.state ? RS.state.book : null;
    if (!book) return;
    var bookId = String(book.id || book.path || '');
    if (!bookId) return;
    var entry = {
      blockIdx: info ? info.blockIdx : (_lastSavedBlockIdx >= 0 ? _lastSavedBlockIdx : 0),
      blockCount: info ? info.blockCount : 0,
      title: book.title || '', format: book.format || '', bookPath: book.path || '',
    };
    if (immediate) {
      if (_saveTimer) { clearTimeout(_saveTimer); _saveTimer = null; }
      try { api.saveBooksTtsProgress(bookId, entry); } catch {}
    } else {
      if (_saveTimer) clearTimeout(_saveTimer);
      _saveTimer = setTimeout(function () {
        _saveTimer = null;
        try { api.saveBooksTtsProgress(bookId, entry); } catch {}
      }, 2000);
    }
  }

  // ── TTS wiring ────────────────────────────────────────────────────────────
  function wireTts() {
    var tts = window.booksTTS;
    if (!tts) return;
    unwireTts();

    var onState = function (status, info) {
      _barLastStatus = status || 'idle';
      if (_isPlaying(status)) _barHasPlayed = true;
      syncPlayPause(status);
      syncBuffering(info && info.buffering);
      syncSpeed();
      syncEngine();
      var diagEl = qs('lpTtsDiag');
      if (diagEl && !diagEl.classList.contains('hidden')) updateDiag();
      if (status === 'idle') {
        _clearHideTimer();
        if (_barHasPlayed) { _setBarVisible(false, true); _ttsActive = false; _barHasPlayed = false; syncListenBtn(false); }
      } else {
        _refreshAutoHide(true);
      }
      // FIX-WRITETHROUGH: Immediately save on pause and section transitions
      // so abrupt close/crash doesn't lose the latest TTS position.
      if (_isPaused(status) || status === 'section_transition') {
        if (_lastSavedBlockIdx >= 0) {
          saveProgress({ blockIdx: _lastSavedBlockIdx, blockCount: (info && info.blockCount) || 0 }, true);
        }
      }
    };
    var onProgress = function (info) {
      syncBuffering(info && info.buffering);
      var diagEl = qs('lpTtsDiag');
      if (diagEl && !diagEl.classList.contains('hidden')) updateDiag();
      var blockIdx = (info && info.blockIdx >= 0) ? info.blockIdx : -1;
      if (blockIdx >= 0 && blockIdx !== _lastSavedBlockIdx) {
        _lastSavedBlockIdx = blockIdx;
        saveProgress(info, false);
      }
    };
    var onDocumentEnd = function (info) { saveProgress(info, true); _onTtsChapterEnd(); };

    _ttsCallbackBindings = { tts: tts, onState: onState, onProgress: onProgress, onDocumentEnd: onDocumentEnd, unsubs: [] };
    if (typeof tts.on === 'function') {
      try { _ttsCallbackBindings.unsubs.push(tts.on('stateChange', onState)); } catch {}
      try { _ttsCallbackBindings.unsubs.push(tts.on('progress', onProgress)); } catch {}
      try { _ttsCallbackBindings.unsubs.push(tts.on('documentEnd', onDocumentEnd)); } catch {}
    } else {
      tts.onStateChange = onState;
      tts.onProgress = onProgress;
      tts.onDocumentEnd = onDocumentEnd;
    }
  }

  function unwireTts() {
    var binding = _ttsCallbackBindings;
    _ttsCallbackBindings = null;
    var tts = (binding && binding.tts) || window.booksTTS;
    if (!tts) return;
    if (binding && Array.isArray(binding.unsubs)) {
      for (var i = 0; i < binding.unsubs.length; i++) {
        try { if (typeof binding.unsubs[i] === 'function') binding.unsubs[i](); } catch {}
      }
    }
    if (!binding || tts.onStateChange === binding.onState) tts.onStateChange = null;
    if (!binding || tts.onProgress === binding.onProgress) tts.onProgress = null;
    if (!binding || tts.onDocumentEnd === binding.onDocumentEnd) tts.onDocumentEnd = null;
  }

  // ── TTS loading overlay ──────────────────────────────────────────────────
  function _showTtsLoading(pct, msg) {
    var overlay = qs('brTtsLoadingOverlay');
    if (!overlay) return;
    overlay.classList.remove('hidden');
    var fill = qs('brTtsLoadingBarFill');
    var pctEl = qs('brTtsLoadingPct');
    var statusEl = overlay.querySelector('.br-tts-loading-status');
    if (fill) fill.style.width = (pct || 0) + '%';
    if (pctEl) pctEl.textContent = Math.round(pct || 0) + '%';
    if (statusEl && msg) statusEl.textContent = msg;
  }
  function _hideTtsLoading() {
    var overlay = qs('brTtsLoadingOverlay');
    if (overlay) overlay.classList.add('hidden');
  }

  function _waitForTtsStart(tts, timeoutMs) {
    var waitMs = Math.max(1000, Number(timeoutMs) || 15000);
    var started = false;
    return new Promise(function (resolve) {
      var timer = null;
      var poll = null;
      function done(ok) {
        if (started) return;
        started = true;
        if (timer) clearTimeout(timer);
        if (poll) clearInterval(poll);
        resolve(!!ok);
      }
      poll = setInterval(function () {
        if (!_ttsActive) { done(false); return; }
        var st = '';
        try { st = String(tts && typeof tts.getState === 'function' ? tts.getState() : ''); } catch {}
        if (st && st !== 'idle') done(true);
      }, 80);
      timer = setTimeout(function () { done(false); }, waitMs);
    });
  }

  // ── Init + Start TTS ─────────────────────────────────────────────────────
  function startTts() {
    if (_ttsActive) return;
    _ttsActive = true;
    _barHasPlayed = false;
    _barLastStatus = 'idle';
    _lastSavedBlockIdx = -1;
    _loadPrefs();
    syncListenBtn(true);

    wireTts();
    var tts = window.booksTTS;
    if (!tts || typeof tts.init !== 'function') return;

    var RS = window.booksReaderState;
    var book = RS && RS.state ? RS.state.book : null;
    var bookId = String((book && (book.id || book.path)) || '');
    var api = window.electronAPI;
    var fmt = (book && book.format) ? String(book.format).toLowerCase() : 'epub';
    var progressPromise = Promise.resolve(null);
    if (bookId && api && typeof api.getBooksTtsProgress === 'function') {
      try {
        progressPromise = api.getBooksTtsProgress(bookId).catch(function () { return null; });
      } catch {
        progressPromise = Promise.resolve(null);
      }
    }

    var opts = {
      format: fmt,
      getHost: function () { return RS && RS.state ? RS.state.host : null; },
      getViewEngine: function () { return RS && RS.state ? RS.state.engine : null; },
      onNeedAdvance: function () {
        var eng = RS && RS.state ? RS.state.engine : null;
        if (!eng || typeof eng.advanceSection !== 'function') return Promise.resolve(false);
        return eng.advanceSection(1).then(function () { return true; }).catch(function () { return false; });
      },
      onInitProgress: function (pct, msg) { _showTtsLoading(pct, msg); },
    };

    _showTtsLoading(0, 'Initializing...');
    Promise.all([tts.init(opts), progressPromise]).then(function (res) {
      if (!_ttsActive) { _hideTtsLoading(); return; }
      var saved = res && res.length > 1 ? res[1] : null;
      var savedBlockIdx = -1;
      var parsedSaved = Number(saved && saved.blockIdx);
      if (Number.isFinite(parsedSaved) && parsedSaved >= 0) {
        savedBlockIdx = Math.floor(parsedSaved);
      }
      try { tts.setVoice(_settings.ttsVoice || 'en-US-AndrewNeural'); } catch {}
      if (_settings.ttsPreset) try { tts.setPreset(_settings.ttsPreset); } catch {}
      tts.setRate(_settings.ttsRate || 1.0);
      if (typeof tts.setPitch === 'function') try { tts.setPitch(_settings.ttsPitch || 1.0); } catch {}
      if (_settings.ttsHlStyle && typeof tts.setHighlightStyle === 'function') tts.setHighlightStyle(_settings.ttsHlStyle);
      if (_settings.ttsHlColor && typeof tts.setHighlightColor === 'function') tts.setHighlightColor(_settings.ttsHlColor);
      if (_settings.ttsHlGranularity && typeof tts.setHighlightGranularity === 'function') tts.setHighlightGranularity(_settings.ttsHlGranularity);
      if (_settings.ttsEnlargeScale && typeof tts.setEnlargeScale === 'function') tts.setEnlargeScale(_settings.ttsEnlargeScale);
      if (typeof tts.setVolume === 'function') tts.setVolume(_settings.ttsVolume || 1.0);

      populateVoices();
      populateHlControls();
      syncSpeed();
      syncEngine();
      var presetSel = qs('lpTtsPresetSel');
      if (presetSel && _settings.ttsPreset) presetSel.value = _settings.ttsPreset;
      var volS = qs('lpVolume');
      if (volS) volS.value = String(_settings.ttsVolume || 1.0);

      // Show bar and start playback
      _setBarVisible(true);
      _refreshAutoHide(true);
      syncPlayPause('idle');

      var playPromise = null;
      if (savedBlockIdx >= 0) {
        _lastSavedBlockIdx = savedBlockIdx;
        try { playPromise = tts.play(savedBlockIdx); } catch {}
      } else {
        try { playPromise = tts.play(0, { startFromVisible: true }); } catch {}
      }

      Promise.resolve(playPromise)
        .catch(function () { return null; })
        .then(function () { return _waitForTtsStart(tts, 18000); })
        .finally(function () { _hideTtsLoading(); });
    }).catch(function (e) {
      _hideTtsLoading();
      console.error('[tts-hud] tts.init() failed:', e);
    });
  }

  // ── Listen toggle ─────────────────────────────────────────────────────────
  function toggleListenMode() {
    if (_ttsActive) {
      ttsStop();
    } else {
      startTts();
    }
  }

  // ── Keyboard shortcuts (active during TTS) ────────────────────────────────
  function onKeyDown(e) {
    if (!_ttsActive) return;
    var tts = window.booksTTS;
    if (!tts) return;
    // Don't capture keys when typing in inputs
    var tag = e.target && e.target.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;

    switch (e.key) {
      case ' ':
        e.preventDefault(); e.stopPropagation();
        ttsToggle(); break;
      case 'ArrowLeft':
        e.preventDefault(); e.stopPropagation();
        try { tts.stepSegment(-1); } catch {} break;
      case 'ArrowRight':
        e.preventDefault(); e.stopPropagation();
        try { tts.stepSegment(1); } catch {} break;
      case 's': case 'S':
        e.preventDefault(); e.stopPropagation();
        ttsStop(); break;
      case '+': case '=':
        e.preventDefault(); e.stopPropagation();
        ttsAdjustSpeed(0.1); break;
      case '-':
        e.preventDefault(); e.stopPropagation();
        ttsAdjustSpeed(-0.1); break;
      case 'j': case 'J':
        e.preventDefault(); e.stopPropagation();
        ttsJump(-10000); break;
      case 'l': case 'L':
        e.preventDefault(); e.stopPropagation();
        ttsJump(10000); break;
      case 'v': case 'V':
        e.preventDefault(); e.stopPropagation();
        var mega = qs('lpTtsMega');
        if (mega) { mega.classList.toggle('hidden'); _refreshAutoHide(true); } break;
      case 'm': case 'M':
        e.preventDefault(); e.stopPropagation();
        if (typeof tts.setVolume === 'function' && typeof tts.getVolume === 'function') {
          var vol = tts.getVolume();
          tts.setVolume(vol > 0 ? 0 : (_settings.ttsVolume || 1.0));
          var volS = qs('lpVolume'); if (volS) volS.value = String(tts.getVolume());
        } break;
      case 'Escape':
        e.preventDefault(); e.stopPropagation();
        var m = qs('lpTtsMega');
        if (m && !m.classList.contains('hidden')) { m.classList.add('hidden'); _refreshAutoHide(false); }
        else if (tts && _isPlaying(tts.getState && tts.getState())) tts.pause();
        break;
    }
  }

  // ── Bind DOM ──────────────────────────────────────────────────────────────
  function bind() {
    console.log('[tts-hud] bind()');
    _wireAutoHideUi();

    // Listen toggle (toolbar button)
    var listenBtn = qs('booksReaderListenToggle') || qs('booksReaderListenBtn');
    if (listenBtn) listenBtn.addEventListener('click', function () { toggleListenMode(); });

    // Transport buttons
    var pp = qs('lpTtsPlayPause');
    if (pp) pp.addEventListener('click', function () { ttsToggle(); });

    var stop = qs('lpTtsStop');
    if (stop) stop.addEventListener('click', function () { ttsStop(); });

    var rew = qs('lpTtsRewind');
    if (rew) rew.addEventListener('click', function () {
      var tts = window.booksTTS;
      if (tts && typeof tts.stepSegment === 'function') try { tts.stepSegment(-1); } catch {}
    });

    var fwd = qs('lpTtsForward');
    if (fwd) fwd.addEventListener('click', function () {
      var tts = window.booksTTS;
      if (tts && typeof tts.stepSegment === 'function') try { tts.stepSegment(1); } catch {}
    });

    var b10 = qs('lpTtsBack10');
    if (b10) b10.addEventListener('click', function () { ttsJump(-10000); });

    var f10 = qs('lpTtsFwd10');
    if (f10) f10.addEventListener('click', function () { ttsJump(10000); });

    var slower = qs('lpTtsSlower');
    if (slower) slower.addEventListener('click', function () { ttsAdjustSpeed(-0.1); });

    var faster = qs('lpTtsFaster');
    if (faster) faster.addEventListener('click', function () { ttsAdjustSpeed(0.1); });

    // Theme cycle
    var themeBtn = qs('lpThemeBtn');
    if (themeBtn) themeBtn.addEventListener('click', function () {
      if (window.booksReaderAppearance && typeof window.booksReaderAppearance.cycleTheme === 'function')
        window.booksReaderAppearance.cycleTheme();
    });

    // Settings mega panel
    var settingsBtn = qs('lpTtsSettingsBtn');
    if (settingsBtn) settingsBtn.addEventListener('click', function () {
      var mega = qs('lpTtsMega');
      if (!mega) return;
      mega.classList.toggle('hidden');
      if (!mega.classList.contains('hidden')) { populateVoices(); populateHlControls(); syncSpeed(); syncEngine(); }
      var diag = qs('lpTtsDiag');
      if (diag && !mega.classList.contains('hidden')) diag.classList.add('hidden');
      _refreshAutoHide(true);
    });

    var megaClose = qs('lpTtsMegaClose');
    if (megaClose) megaClose.addEventListener('click', function (e) {
      e.stopPropagation();
      var mega = qs('lpTtsMega'); if (mega) mega.classList.add('hidden');
      _refreshAutoHide(false);
    });

    // Click outside mega to close
    document.addEventListener('mousedown', function (e) {
      var mega = qs('lpTtsMega');
      if (!mega || mega.classList.contains('hidden')) return;
      if (mega.contains(e.target)) return;
      var sBtn = qs('lpTtsSettingsBtn');
      if (sBtn && sBtn.contains(e.target)) return;
      mega.classList.add('hidden');
      _refreshAutoHide(false);
    });

    // Voice picker
    var voiceSel = qs('lpTtsVoice');
    if (voiceSel) voiceSel.addEventListener('change', function () {
      var tts = window.booksTTS;
      if (!tts) return;
      var vid = voiceSel.value;
      if (tts.setVoice) try { tts.setVoice(vid); } catch {}
      _settings.ttsVoice = vid;
      _lsSet('Voice', vid);
    });

    // Preset selector
    var presetSel = qs('lpTtsPresetSel');
    if (presetSel) presetSel.addEventListener('change', function () {
      var tts = window.booksTTS;
      if (!tts) return;
      var pid = presetSel.value;
      if (pid && typeof tts.setPreset === 'function') {
        tts.setPreset(pid);
        _settings.ttsPreset = pid;
        _settings.ttsRate = tts.getRate();
        _lsSet('Preset', pid);
        _lsSet('Rate', String(_settings.ttsRate));
      }
      syncSpeed();
    });

    // Highlight style
    var hlSel = qs('lpTtsHlStyle');
    if (hlSel) hlSel.addEventListener('change', function () {
      var tts = window.booksTTS;
      if (!tts || typeof tts.setHighlightStyle !== 'function') return;
      tts.setHighlightStyle(hlSel.value);
      _settings.ttsHlStyle = hlSel.value;
      _lsSet('HlStyle', hlSel.value);
      var enlargeRow = qs('lpEnlargeRow');
      if (enlargeRow) enlargeRow.style.display = (hlSel.value === 'enlarge') ? '' : 'none';
    });

    // Highlight color swatches
    var hlColors = qs('lpTtsHlColors');
    if (hlColors) hlColors.addEventListener('click', function (ev) {
      var btn = ev.target.closest('.ttsColorSwatch');
      if (!btn || !btn.dataset.color) return;
      var tts = window.booksTTS;
      if (!tts || typeof tts.setHighlightColor !== 'function') return;
      tts.setHighlightColor(btn.dataset.color);
      _settings.ttsHlColor = btn.dataset.color;
      _lsSet('HlColor', btn.dataset.color);
      populateHlControls();
    });

    // Enlarge scale slider
    var scaleSlider = qs('lpTtsEnlargeScale');
    if (scaleSlider) scaleSlider.addEventListener('input', function () {
      var val = parseFloat(scaleSlider.value) || 1.35;
      var tts = window.booksTTS;
      if (tts && typeof tts.setEnlargeScale === 'function') tts.setEnlargeScale(val);
      _settings.ttsEnlargeScale = val;
      _lsSet('Enlarge', String(val));
      var valEl = qs('lpTtsEnlargeVal');
      if (valEl) valEl.textContent = val.toFixed(2) + 'x';
    });

    // Read from selection
    var fromSel = qs('lpTtsFromSel');
    if (fromSel) fromSel.addEventListener('click', function () {
      var tts = window.booksTTS;
      if (!tts) return;
      var RS = window.booksReaderState;
      var selectedText = '';
      if (RS && RS.state && RS.state.engine && typeof RS.state.engine.getSelectedText === 'function') {
        var sel = RS.state.engine.getSelectedText();
        selectedText = String((sel && typeof sel === 'object') ? (sel.text || '') : (sel || ''));
      }
      if (selectedText.trim() && typeof tts.playFromSelection === 'function') tts.playFromSelection(selectedText);
    });

    // Diagnostics
    var diagBtn = qs('lpTtsDiagBtn');
    if (diagBtn) diagBtn.addEventListener('click', function () {
      var diag = qs('lpTtsDiag');
      if (!diag) return;
      diag.classList.toggle('hidden');
      if (!diag.classList.contains('hidden')) updateDiag();
    });
    var diagClose = qs('lpTtsDiagClose');
    if (diagClose) diagClose.addEventListener('click', function () {
      var diag = qs('lpTtsDiag'); if (diag) diag.classList.add('hidden');
    });
    var diagCopy = qs('lpTtsDiagCopy');
    if (diagCopy) diagCopy.addEventListener('click', function () {
      var body = qs('lpTtsDiagBody');
      if (!body) return;
      navigator.clipboard.writeText(body.textContent).then(function () {
        diagCopy.title = 'Copied!';
        setTimeout(function () { diagCopy.title = 'Copy diagnostics'; }, 1500);
      });
    });

    // Volume slider
    var volSlider = qs('lpVolume');
    if (volSlider) {
      volSlider.value = String(_settings.ttsVolume || 1.0);
      volSlider.addEventListener('input', function () {
        var v = parseFloat(volSlider.value) || 1;
        _settings.ttsVolume = v;
        var tts = window.booksTTS;
        if (tts && typeof tts.setVolume === 'function') tts.setVolume(v);
        _lsSet('Volume', String(v));
      });
    }

    // Sleep timer chips
    var sleepChips = document.getElementById('lpTtsSleepChips');
    if (sleepChips) sleepChips.addEventListener('click', function (ev) {
      var chip = ev.target.closest('.ttsSleepChip');
      if (!chip) return;
      setSleepTimer(parseInt(chip.dataset.sleep, 10));
    });

    // Keyboard
    window.addEventListener('keydown', onKeyDown, true);

    console.log('[tts-hud] bind complete');
  }

  // ── Auto-bind on DOMContentLoaded or immediately ──────────────────────────
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bind);
  } else {
    bind();
  }

  // Expose for external use
  window.ttsHud = { toggle: toggleListenMode, start: startTts, stop: ttsStop };
})();
