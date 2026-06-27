// FIX-TTS05: Edge TTS engine with LRU audio cache and multi-chunk preload
// Replaces single-item preCache with proper LRU (20 items).
// Cache key: voice|rate|pitch|text — replay/seek is instant on cache hit.
(function () {
  'use strict';

  window.booksTTSEngines = window.booksTTSEngines || {};

  // OPT-PERF: Session-level state that persists across create()/destroy cycles.
  // Without this, every init() → create() → probe() does a full Edge TTS synthesis
  // of "Edge probe" text (~1-3s), even though Edge was already confirmed working.
  var _sessionHealth = null;   // { known, available, reason } — survives destroy()
  var _sessionVoices = null;   // voice list array — survives destroy()

  function base64ToBlob(b64, mime) {
    try {
      var bin = atob(String(b64 || ''));
      var len = bin.length;
      var bytes = new Uint8Array(len);
      for (var i = 0; i < len; i++) bytes[i] = bin.charCodeAt(i);
      return new Blob([bytes], { type: mime || 'audio/mpeg' });
    } catch {
      return null;
    }
  }

  function create() {
    var state = {
      audio: null,
      abortCtrl: null,
      blobUrl: null,
      rate: 1.0,
      pitch: 1.0,
      volume: 1.0, // TTS-QOL4
      voiceName: 'en-US-AriaNeural',
      playing: false,
      paused: false,
      requestId: '',
      voiceList: _sessionVoices ? _sessionVoices.slice() : [],
      health: _sessionHealth
        ? { known: _sessionHealth.known, available: _sessionHealth.available, reason: _sessionHealth.reason }
        : { known: false, available: false, reason: 'edge_probe_uninitialized' },
      onBoundary: null,
      onEnd: null,
      onError: null,
      onDiag: null,
      onBuffering: null,
      buffering: false,
      lastDiag: {
        code: '',
        detail: '',
      },
      // Resume hint from TTS core (character index inside the next spoken text).
      resumeCharIndex: -1,
      // Internal: seek target in milliseconds (computed from boundary offsets).
      _resumeSeekMs: null,
    };

    // FIX-TTS05: LRU audio cache — keyed by "voice|rate|pitch|text"
    // Uses Map for insertion-order iteration (oldest first for eviction).
    // OPT1: increased from 20 to 50 for longer listening sessions
    var _lruCache = new Map();
    var LRU_MAX = 50;

    function _lruKey(text) {
      return state.voiceName + '|' + state.rate + '|' + state.pitch + '|' + text;
    }

    function _lruGet(key) {
      if (!_lruCache.has(key)) return null;
      var entry = _lruCache.get(key);
      // Move to end (most recently used)
      _lruCache.delete(key);
      _lruCache.set(key, entry);
      return entry;
    }

    function _lruSet(key, entry) {
      if (_lruCache.has(key)) {
        _lruCache.delete(key);
      } else if (_lruCache.size >= LRU_MAX) {
        // Evict oldest (first entry in Map)
        var oldest = _lruCache.keys().next().value;
        var evicted = _lruCache.get(oldest);
        if (evicted && evicted.blobUrl) {
          try { URL.revokeObjectURL(evicted.blobUrl); } catch {}
        }
        _lruCache.delete(oldest);
      }
      _lruCache.set(key, entry);
    }

    // FIX-TTS-B3 #6: Track preload keys that exhausted all retries
    var _preloadFailed = new Set();

    // FIX-TTS-B5 #8: Pre-buffered second audio element for gapless block transitions
    var _prepAudio = null;
    var _prepCacheKey = null;

    function _clearPrepared() {
      if (!_prepAudio) return;
      try { _prepAudio.pause(); } catch {}
      try { _prepAudio.removeAttribute('src'); } catch {}
      if (_prepAudio._blobUrl) { try { URL.revokeObjectURL(_prepAudio._blobUrl); } catch {} }
      _prepAudio = null;
      _prepCacheKey = null;
    }

    // FIX-GAP: Async prepareNext — polls for cache entry if preload is still
    // in progress, then pre-loads the Audio element to readyState >= 2 so
    // speakGapless can swap it in with near-zero gap.
    var _prepGeneration = 0; // monotonic counter to detect superseded calls
    async function prepareNext(text) {
      var t = String(text || '').trim();
      if (!t) return;
      var key = _lruKey(t);
      if (_prepCacheKey === key && _prepAudio) return;
      _clearPrepared();
      var gen = ++_prepGeneration;

      // Poll for cache entry — preload is likely still in flight
      var cached = _lruCache.has(key) ? _lruCache.get(key) : null;
      if (!cached) {
        for (var i = 0; i < 80; i++) { // up to ~8 seconds
          await new Promise(function (r) { setTimeout(r, 100); });
          if (_prepGeneration !== gen) return; // superseded
          cached = _lruCache.has(key) ? _lruCache.get(key) : null;
          if (cached) break;
        }
        if (!cached) return;
      }

      if (_prepGeneration !== gen) return;
      var audio = new Audio();
      audio.preload = 'auto';
      audio.volume = state.volume;
      if (cached.audioUrl) {
        audio.src = String(cached.audioUrl);
      } else if (cached.blob) {
        audio._blobUrl = URL.createObjectURL(cached.blob);
        audio.src = audio._blobUrl;
      } else {
        return;
      }

      // Wait for audio data to be loaded so playback starts instantly
      await new Promise(function (resolve) {
        if (audio.readyState >= 2) { resolve(); return; }
        var timer = setTimeout(resolve, 3000);
        audio.addEventListener('canplay', function () { clearTimeout(timer); resolve(); }, { once: true });
        audio.addEventListener('error', function () { clearTimeout(timer); resolve(); }, { once: true });
        try { audio.load(); } catch {}
      });

      if (_prepGeneration !== gen) {
        try { audio.pause(); audio.removeAttribute('src'); } catch {}
        if (audio._blobUrl) { try { URL.revokeObjectURL(audio._blobUrl); } catch {} }
        return;
      }
      _prepAudio = audio;
      _prepCacheKey = key;
    }

    function clearPreloadCache() {
      _lruCache.forEach(function (entry) {
        if (entry && entry.blobUrl) {
          try { URL.revokeObjectURL(entry.blobUrl); } catch {}
        }
      });
      _lruCache.clear();
      _preloadFailed.clear();
      _clearPrepared();
    }

    // FIX-TTS05: Background preload — synthesize and cache without playing
    // FIX-TTS-B3 #6: Retry up to 3 attempts with backoff; log final failures
    async function preload(text) {
      var t = String(text || '').trim();
      if (!t) return;
      var key = _lruKey(t);
      if (_lruCache.has(key)) return;
      if (_preloadFailed.has(key)) return;
      var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
      if (!api || typeof api.synth !== 'function') return;
      var reqAtStart = state.requestId;
      var delays = [0, 500, 1000];
      for (var attempt = 0; attempt < delays.length; attempt++) {
        if (attempt > 0) {
          if (state.requestId !== reqAtStart && reqAtStart !== '') return;
          await new Promise(function (resolve) { setTimeout(resolve, delays[attempt]); });
          if (state.requestId !== reqAtStart && reqAtStart !== '') return;
          if (_lruCache.has(key)) return;
        }
        try {
          var res = await api.synth({
            text: t,
            voice: state.voiceName,
            rate: state.rate,
            pitch: state.pitch,
            returnBase64: false,
          });
          if (res && res.ok) {
            if (res.audioUrl) {
              _lruSet(key, { audioUrl: String(res.audioUrl), boundaries: res.boundaries || [] });
              _preloadFailed.delete(key);
              return;
            }
            if (res.audioBase64) {
              var blob = base64ToBlob(res.audioBase64, 'audio/mpeg');
              if (blob) {
                _lruSet(key, { blob: blob, blobUrl: null, boundaries: res.boundaries || [] });
                _preloadFailed.delete(key);
                return;
              }
            }
          }
        } catch (err) {
          if (attempt === delays.length - 1) {
            diag('preload_fail', 'len=' + t.length + ' err=' + String(err && err.message ? err.message : err));
            _preloadFailed.add(key);
          }
        }
      }
    }

    // FIX-TTS06: Track synth failures — reset main-process WS after repeated errors
    var _synthErrorCount = 0;

    function diag(code, detail) {
      state.lastDiag = { code: String(code || ''), detail: String(detail || '') };
      if (typeof state.onDiag === 'function') {
        try { state.onDiag({ code: state.lastDiag.code, detail: state.lastDiag.detail }); } catch {}
      }
      // FIX-TTS06: If we're seeing synth/ws failures, proactively reset the main-process instance
      // so the next speak attempt gets a fresh WebSocket instead of hitting the same dead one.
      if (code && (String(code).indexOf('fail') >= 0 || String(code).indexOf('timeout') >= 0)) {
        _synthErrorCount++;
        if (_synthErrorCount >= 2) {
          _synthErrorCount = 0;
          try {
            var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
            if (api && typeof api.resetInstance === 'function') {
              api.resetInstance().catch(function () {});
            }
          } catch {}
        }
      }
    }

    // OPT-TTS-CTRL: Buffering flag — fires callback so UI can show a loading indicator.
    // Only set during network synthesis in speak()/speakGapless(), NOT during preload().
    function _setBuffering(val) {
      var next = !!val;
      if (state.buffering === next) return;
      state.buffering = next;
      if (typeof state.onBuffering === 'function') {
        try { state.onBuffering(next); } catch {}
      }
    }

    function ensureAudio() {
      if (!state.audio) {
        try {
          state.audio = new Audio();
          state.audio.preload = 'auto';
          state.audio.volume = state.volume; // TTS-QOL4
        } catch {
          state.audio = null;
        }
      }
      return state.audio;
    }

    function _revokeBlobUrl() {
      if (state.blobUrl) {
        try { URL.revokeObjectURL(state.blobUrl); } catch {}
        state.blobUrl = null;
      }
    }

    async function loadVoices(opts) {
      diag('edge_voices_fetch_start', '');
      try {
        var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
        if (!api || typeof api.getVoices !== 'function') {
          state.voiceList = [];
          diag('edge_voices_fetch_fail', 'booksTtsEdge_api_missing');
          return state.voiceList;
        }
        var res = await api.getVoices(opts || {});
        if (res && res.ok && Array.isArray(res.voices) && res.voices.length) {
          state.voiceList = res.voices.slice();
          // OPT-PERF: Persist voices to session level so future create() calls reuse them
          _sessionVoices = state.voiceList.slice();
          diag('edge_voices_fetch_ok', String(state.voiceList.length));
          return state.voiceList;
        }
        state.voiceList = [];
        diag('edge_voices_fetch_fail', String(res && (res.reason || 'voices_empty') || 'voices_fetch_failed'));
        return state.voiceList;
      } catch (err) {
        state.voiceList = [];
        diag('edge_voices_fetch_fail', String(err && err.message ? err.message : err));
        return state.voiceList;
      }
    }

    async function probe(payload) {
      // OPT2: skip re-probe if already confirmed available this session
      if (state.health.known && state.health.available) {
        diag('edge_probe_cached', 'skipped');
        // OPT-PERF: Ensure voices are loaded even when probe is cached
        if (!state.voiceList.length) { try { await loadVoices({ maxAgeMs: 60000 }); } catch {} }
        return true;
      }
      diag('edge_probe_start', '');
      try {
        var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
        if (!api || typeof api.probe !== 'function') {
          state.health.known = true;
          state.health.available = false;
          state.health.reason = 'edge_probe_api_missing';
          diag('edge_probe_fail', state.health.reason);
          return state.health.available;
        }
        var probeReq = Object.assign({ requireSynthesis: true, timeoutMs: 5000 }, payload || {}); // OPT2: reduced from 10s
        var res = await api.probe(probeReq);
        var ok = !!(res && res.ok && res.available);
        state.health.known = true;
        state.health.available = ok;
        state.health.reason = ok ? 'edge_probe_ok' : String(res && res.reason || 'edge_probe_failed');
        // OPT-PERF: Persist health to session level so future create() calls skip probe
        if (ok) _sessionHealth = { known: true, available: true, reason: state.health.reason };
        diag(ok ? 'edge_probe_ok' : 'edge_probe_fail', state.health.reason);
        if (ok) {
          await loadVoices({ maxAgeMs: 0 });
        } else if (res && res.details && res.details.synth && res.details.synth.errorCode) {
          diag('edge_probe_fail', String(res.details.synth.errorCode || state.health.reason));
        }
        return ok;
      } catch (err) {
        state.health.known = true;
        state.health.available = false;
        state.health.reason = String(err && err.message ? err.message : err);
        diag('edge_probe_fail', state.health.reason);
        return false;
      }
    }

    function getVoices() {
      if (!state.voiceList.length) return [];
      return state.voiceList.map(function (v) {
        return {
          voiceURI: String(v.voiceURI || v.name || ''),
          name: String(v.name || v.voiceURI || ''),
          lang: String(v.lang || ''),
          gender: String(v.gender || ''),
          localService: false,
          default: !!v.default,
          engine: 'edge',
        };
      }).filter(function (v) { return !!v.voiceURI; });
    }

    function setRate(rate) {
      state.rate = Math.max(0.5, Math.min(2.0, Number(rate) || 1.0));
    }

    // TTS-QOL4: Volume control via HTMLAudioElement.volume (0–1)
    function setVolume(vol) {
      state.volume = Math.max(0, Math.min(1, Number(vol) || 1.0));
      if (state.audio) state.audio.volume = state.volume;
      if (_prepAudio) _prepAudio.volume = state.volume; // FIX-TTS-B5 #8
    }

    function setPitch(pitch) {
      state.pitch = Math.max(0.5, Math.min(2.0, Number(pitch) || 1.0));
    }

    function setVoice(voiceURI) {
      state.voiceName = String(voiceURI || 'en-US-AriaNeural');
    }

    function setResumeHint(charIndex) {
      var v = (typeof charIndex === 'number' && isFinite(charIndex)) ? Math.max(0, Math.floor(charIndex)) : -1;
      state.resumeCharIndex = v;
    }

    // FIX-TTS08: Boundary scheduling state — survives pause/resume cycles.
    // Stores pre-computed boundary entries and a polling timer that checks
    // audio.currentTime to fire them at the right moment, even after pauses.
    var _bd = {
      entries: [],     // [{ offsetMs, charIndex, charLength }]
      nextIdx: 0,      // next entry to fire
      rafId: null,     // requestAnimationFrame / setTimeout id
      reqId: '',       // matches state.requestId to invalidate on cancel
    };

    function _bdStop() {
      if (_bd.rafId) { clearTimeout(_bd.rafId); _bd.rafId = null; }
      _bd.entries = [];
      _bd.nextIdx = 0;
      _bd.reqId = '';
    }

    // FIX-TTS-B1: pause/resume boundary polling without clearing entries
    function _bdPause() {
      if (_bd.rafId) { clearTimeout(_bd.rafId); _bd.rafId = null; }
    }

    function _bdResume() {
      if (!_bd.rafId && _bd.nextIdx < _bd.entries.length && _bd.reqId === state.requestId) {
        _bd.rafId = setTimeout(_bdPoll, 16);
      }
    }

    // FIX-SYNC: Fire boundaries slightly early to compensate for DOM update
    // latency, and poll more tightly so highlights track the spoken word.
    var BD_LEAD_MS = 20;
    var BD_MAX_INTERVAL = 30;

    function _bdPoll() {
      _bd.rafId = null;
      if (_bd.reqId !== state.requestId) return;
      if (!state.playing) return;
      if (state.paused) return; // FIX-TTS-B1: don't reschedule while paused — _bdResume() restarts on resume
      var audio = state.audio;
      if (!audio) return;
      var currentMs = (audio.currentTime || 0) * 1000;
      // Fire all boundaries whose offset has been reached (with lead-time compensation)
      while (_bd.nextIdx < _bd.entries.length) {
        var entry = _bd.entries[_bd.nextIdx];
        if (entry.offsetMs - BD_LEAD_MS <= currentMs) {
          _bd.nextIdx++;
          if (typeof state.onBoundary === 'function') {
            try { state.onBoundary(entry.charIndex, entry.charLength, 'word'); } catch {}
          }
        } else {
          break;
        }
      }
      // Schedule next poll if there are remaining entries
      if (_bd.nextIdx < _bd.entries.length) {
        var nextDelay = Math.max(16, _bd.entries[_bd.nextIdx].offsetMs - BD_LEAD_MS - currentMs);
        _bd.rafId = setTimeout(_bdPoll, Math.min(nextDelay, BD_MAX_INTERVAL));
      }
    }

    // FIX-LISTEN-STAB: Edge wordBoundary `text` values don't always match the exact
    // substring in our spoken text (punctuation, quotes, repeated words). Doing a
    // naive indexOf() causes highlight drift. We instead search in a normalized
    // stream and map back to original char indices.
    function _buildNormMap(original) {
      var src = String(original || '');
      var norm = '';
      var map = [];
      var lastWasSpace = false;
      for (var i = 0; i < src.length; i++) {
        var ch = src[i];
        var lower = ch.toLowerCase();
        // Normalize curly apostrophes
        if (lower === '’') lower = "'";

        var isWs = (lower === ' ' || lower === '\n' || lower === '\r' || lower === '\t' || lower === '\f');
        if (isWs) {
          if (!lastWasSpace) {
            norm += ' ';
            map.push(i);
            lastWasSpace = true;
          }
          continue;
        }

        lastWasSpace = false;
        var code = lower.charCodeAt(0);
        var isAZ = (code >= 97 && code <= 122);
        var is09 = (code >= 48 && code <= 57);
        var isKeep = isAZ || is09 || lower === "'" || lower === '-';
        if (!isKeep) continue;
        norm += lower;
        map.push(i);
      }
      return { norm: norm, map: map };
    }

    function _normWord(word) {
      var w = String(word || '').toLowerCase();
      w = w.replace(/’/g, "'");
      // Keep only [a-z0-9' -] then trim & collapse spaces
      w = w.replace(/[^a-z0-9\s'\-]+/g, ' ');
      w = w.replace(/\s+/g, ' ').trim();
      return w;
    }

    function fireBoundaries(reqId, boundaries, spokenText) {
      _bdStop();
      var list = Array.isArray(boundaries) ? boundaries : [];
      var txt = String(spokenText || '');
      var normInfo = _buildNormMap(txt);
      var normTxt = normInfo.norm;
      var normMap = normInfo.map;
      var normPos = 0;
      var lastCharIndex = 0;
      var entries = [];
      for (var i = 0; i < list.length; i++) {
        var word = (list[i] && list[i].text) ? String(list[i].text) : '';
        var charIndex = lastCharIndex;
        var charLength = 0;

        var nw = _normWord(word);
        if (nw && normTxt) {
          var idx2 = normTxt.indexOf(nw, normPos);
          // FIX-TTS-B5 #9: Fallback — if forward search fails, look back slightly
          // to recover from boundary text mismatches at high speed
          if (idx2 < 0 && normPos > 0) {
            idx2 = normTxt.indexOf(nw, Math.max(0, normPos - nw.length * 2));
          }
          // Cap max forward jump to prevent matching a distant repeated word
          if (idx2 >= 0 && idx2 > normPos + 200) idx2 = -1;
          if (idx2 >= 0) {
            var startOrig = normMap[idx2] != null ? normMap[idx2] : lastCharIndex;
            var endNorm = idx2 + nw.length - 1;
            var endOrig = normMap[endNorm] != null ? normMap[endNorm] : startOrig;
            charIndex = startOrig;
            charLength = Math.max(0, (endOrig - startOrig) + 1);
            lastCharIndex = charIndex;
            normPos = idx2 + nw.length;
          }
        }
        entries.push({
          offsetMs: Math.max(0, Number(list[i] && list[i].offsetMs || 0)),
          charIndex: charIndex,
          charLength: charLength,
        });
      }

      // Sort by offset to schedule
      entries.sort(function (a, b) { return a.offsetMs - b.offsetMs; });

      // Optional resume: skip earlier boundaries and compute a seek target.
      state._resumeSeekMs = null;
      var nextIdx = 0;
      var resumeChar = (typeof state.resumeCharIndex === 'number' && isFinite(state.resumeCharIndex)) ? Math.max(0, Math.floor(state.resumeCharIndex)) : -1;
      if (resumeChar >= 0 && entries.length) {
        var ridx = -1;
        for (var j = 0; j < entries.length; j++) {
          var e = entries[j];
          if (resumeChar <= (e.charIndex + e.charLength - 1)) { ridx = j; break; }
        }
        if (ridx < 0) ridx = entries.length - 1;
        nextIdx = ridx;
        state._resumeSeekMs = Math.max(0, entries[ridx].offsetMs || 0);
      }

      _bd.entries = entries;
      _bd.nextIdx = nextIdx;
      _bd.reqId = reqId;
      // Start polling
      if (entries.length) {
        _bd.rafId = setTimeout(_bdPoll, 16);
      }
    }

    function _applyResumeSeekIfAny(myReq, audio) {
      if (myReq !== state.requestId) return;
      if (!audio) return;
      if (typeof state._resumeSeekMs !== 'number' || !isFinite(state._resumeSeekMs) || state._resumeSeekMs == null) return;
      var seekMs = Math.max(0, state._resumeSeekMs);
      state._resumeSeekMs = null;
      try { audio.currentTime = seekMs / 1000; } catch {}
    }

    function _playBlob(myReq, blob, boundaries, text) {
      var audio = ensureAudio();
      if (!audio) {
        state.playing = false;
        diag('edge_play_fail', 'no_audio_element');
        if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: 'no_audio_element' });
        return;
      }

      _revokeBlobUrl();
      state.blobUrl = URL.createObjectURL(blob);
      audio.src = state.blobUrl;

      audio.onloadedmetadata = function () {
        _applyResumeSeekIfAny(myReq, audio);
      };

      audio.onended = function () {
        if (myReq !== state.requestId) return;
        state.playing = false;
        state.paused = false;
        _synthErrorCount = 0; // FIX-TTS06: reset error counter on successful playback
        diag('edge_play_ok', '');
        if (typeof state.onEnd === 'function') state.onEnd();
      };

      audio.onerror = function () {
        if (myReq !== state.requestId) return;
        state.playing = false;
        var reason = audio.error ? String(audio.error.message || audio.error.code || 'unknown') : 'unknown';
        diag('edge_play_fail', reason);
        if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: reason });
      };

      fireBoundaries(myReq, boundaries, text);

      try {
        var playPromise = audio.play();
        if (playPromise && typeof playPromise.catch === 'function') {
          playPromise.catch(function (err) {
            if (myReq !== state.requestId) return;
            state.playing = false;
            diag('edge_play_fail', String(err && err.message ? err.message : err));
            if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
          });
        }
      } catch (err) {
        state.playing = false;
        diag('edge_play_fail', String(err && err.message ? err.message : err));
        if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
      }
    }

    function _playUrl(myReq, srcUrl, boundaries, text) {
      var audio = ensureAudio();
      if (!audio) {
        state.playing = false;
        diag('edge_play_fail', 'no_audio_element');
        if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: 'no_audio_element' });
        return;
      }

      // If the previous segment used a blob URL, release it.
      _revokeBlobUrl();

      audio.src = String(srcUrl || '');

      audio.onloadedmetadata = function () {
        _applyResumeSeekIfAny(myReq, audio);
      };

      audio.onended = function () {
        if (myReq !== state.requestId) return;
        state.playing = false;
        state.paused = false;
        _synthErrorCount = 0;
        diag('edge_play_ok', '');
        if (typeof state.onEnd === 'function') state.onEnd();
      };

      audio.onerror = function () {
        if (myReq !== state.requestId) return;
        state.playing = false;
        var reason = audio.error ? String(audio.error.message || audio.error.code || 'unknown') : 'unknown';
        diag('edge_play_fail', reason);
        if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: reason });
      };

      fireBoundaries(myReq, boundaries, text);

      try {
        var playPromise = audio.play();
        if (playPromise && typeof playPromise.catch === 'function') {
          playPromise.catch(function (err) {
            if (myReq !== state.requestId) return;
            state.playing = false;
            diag('edge_play_fail', String(err && err.message ? err.message : err));
            if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
          });
        }
      } catch (err) {
        state.playing = false;
        diag('edge_play_fail', String(err && err.message ? err.message : err));
        if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
      }
    }

    function _synthWithTimeout(api, payload, timeoutMs) {
      return new Promise(function (resolve, reject) {
        var timer = setTimeout(function () {
          reject(new Error('edge_synth_timeout'));
        }, timeoutMs || 15000);
        api.synth(payload).then(function (res) {
          clearTimeout(timer);
          resolve(res);
        }).catch(function (err) {
          clearTimeout(timer);
          reject(err);
        });
      });
    }

    // ── True audio streaming via MediaSource API ──
    // Starts playback as soon as the first audio chunk arrives from Python,
    // instead of waiting for the entire synthesis to complete.
    var _streamSupported = null; // lazy-checked

    function _checkStreamSupport() {
      if (_streamSupported !== null) return _streamSupported;
      try {
        _streamSupported = (typeof MediaSource !== 'undefined' &&
          typeof MediaSource.isTypeSupported === 'function' &&
          MediaSource.isTypeSupported('audio/mpeg') &&
          typeof window.__ttsEdgeStream === 'object');
      } catch (e) {
        _streamSupported = false;
      }
      return _streamSupported;
    }

    // Active stream cancellation helper
    var _activeStreamId = null;

    function _cancelActiveStream() {
      if (_activeStreamId) {
        var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
        if (api && typeof api.cancelStream === 'function') {
          try { api.cancelStream(_activeStreamId); } catch (e) {}
        }
        _activeStreamId = null;
      }
    }

    async function _speakStream(text, myReq) {
      var t = String(text || '').trim();
      if (!t) return false;
      if (!_checkStreamSupport()) return false;

      var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
      var stream = window.__ttsEdgeStream;
      if (!api || typeof api.synthStream !== 'function' || !stream) return false;

      // Don't use streaming if there's a resume seek (needs full audio for seeking)
      if (typeof state.resumeCharIndex === 'number' && state.resumeCharIndex >= 0) return false;

      // FIX-RACE: Collect early events that arrive before the Promise resolves.
      // Listeners MUST be registered before synthStream() to prevent chunk loss.
      var earlyChunks = [];
      var earlyBounds = [];
      var earlyEnd = null;
      var earlyError = null;
      var earlyDone = false;
      function onEarlyChunk(d) { earlyChunks.push(d); }
      function onEarlyBound(d) { earlyBounds.push(d); }
      function onEarlyEnd(d) { earlyEnd = d; earlyDone = true; }
      function onEarlyError(d) { earlyError = d; earlyDone = true; }
      stream.on('chunk', onEarlyChunk);
      stream.on('bound', onEarlyBound);
      stream.on('end', onEarlyEnd);
      stream.on('error', onEarlyError);

      var res;
      try {
        res = await api.synthStream({
          text: t,
          voice: state.voiceName,
          rate: state.rate,
          pitch: state.pitch,
        });
      } catch (e) {
        stream.off('chunk', onEarlyChunk);
        stream.off('bound', onEarlyBound);
        stream.off('end', onEarlyEnd);
        stream.off('error', onEarlyError);
        return false;
      }
      if (!res || !res.ok || !res.streamId) {
        stream.off('chunk', onEarlyChunk);
        stream.off('bound', onEarlyBound);
        stream.off('end', onEarlyEnd);
        stream.off('error', onEarlyError);
        return false;
      }
      if (myReq !== state.requestId) {
        stream.off('chunk', onEarlyChunk);
        stream.off('bound', onEarlyBound);
        stream.off('end', onEarlyEnd);
        stream.off('error', onEarlyError);
        try { api.cancelStream(res.streamId); } catch (e) {}
        return true; // consumed but cancelled
      }

      var sid = res.streamId;
      _activeStreamId = sid;

      var audio = ensureAudio();
      if (!audio) return false;

      // Build norm map for incremental boundary processing
      var normInfo = _buildNormMap(t);
      var normPos = 0;
      var lastCharIndex = 0;

      // Collect all audio chunks for LRU caching after stream ends
      var allChunks = [];
      var allBoundaries = [];

      // Set up MediaSource
      var ms = new MediaSource();
      var msUrl = URL.createObjectURL(ms);
      _revokeBlobUrl();
      state.blobUrl = msUrl;
      audio.src = msUrl;

      // Set up boundary tracking (incremental — entries grow as boundaries arrive)
      _bdStop();
      _bd.entries = [];
      _bd.nextIdx = 0;
      _bd.reqId = myReq;

      return new Promise(function (resolve) {
        var sourceBuffer = null;
        var appendQueue = [];
        var appending = false;
        var streamDone = false;
        var msEnded = false;
        var playStarted = false;
        var cleaned = false;

        function tryAppend() {
          if (appending || !sourceBuffer || msEnded) return;
          if (appendQueue.length === 0) {
            if (streamDone && !msEnded) {
              msEnded = true;
              try { if (ms.readyState === 'open') ms.endOfStream(); } catch (e) {}
            }
            return;
          }
          appending = true;
          // FIX-APPEND: Only remove the chunk after a successful append.
          // Previously shift() ran before appendBuffer, so a thrown error
          // would permanently lose the chunk.
          var chunk = appendQueue[0];
          try {
            sourceBuffer.appendBuffer(chunk);
            appendQueue.shift(); // success — safe to dequeue
          } catch (e) {
            appending = false;
            // Chunk stays at front of queue for retry on next updateend
          }
        }

        function onSourceOpen() {
          try {
            sourceBuffer = ms.addSourceBuffer('audio/mpeg');
          } catch (e) {
            cleanup();
            resolve(false);
            return;
          }
          sourceBuffer.addEventListener('updateend', function () {
            appending = false;
            // Start playback as soon as first chunk is appended
            if (!playStarted) {
              playStarted = true;
              _setBuffering(false);
              diag('edge_stream_play_start', 'latency_ms=' + (Date.now() - _streamStartTime));
              try {
                var pp = audio.play();
                if (pp && typeof pp.catch === 'function') {
                  pp.catch(function (err) {
                    if (myReq !== state.requestId) return;
                    state.playing = false;
                    diag('edge_play_fail', String(err && err.message ? err.message : err));
                    if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
                  });
                }
              } catch (err) {
                state.playing = false;
                diag('edge_play_fail', String(err && err.message ? err.message : err));
              }
            }
            tryAppend();
          });
          // Append any chunks that arrived before sourceopen
          tryAppend();
        }

        ms.addEventListener('sourceopen', onSourceOpen);

        // Audio element events
        audio.onended = function () {
          if (myReq !== state.requestId) return;
          state.playing = false;
          state.paused = false;
          _synthErrorCount = 0;
          diag('edge_play_ok', 'stream');
          if (typeof state.onEnd === 'function') state.onEnd();
        };
        audio.onerror = function () {
          if (myReq !== state.requestId) return;
          state.playing = false;
          var reason = audio.error ? String(audio.error.message || audio.error.code || 'unknown') : 'unknown';
          diag('edge_play_fail', reason);
          if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: reason });
        };

        // Stream signal handlers
        function onChunk(data) {
          if (data.id !== sid || myReq !== state.requestId) return;
          try {
            var bin = atob(data.b64);
            var bytes = new Uint8Array(bin.length);
            for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
            allChunks.push(bytes);
            appendQueue.push(bytes.buffer);
            tryAppend();
          } catch (e) {}
        }

        function onBound(data) {
          if (data.id !== sid || myReq !== state.requestId) return;
          var word = String(data.w || '');
          var charIndex = lastCharIndex;
          var charLength = 0;
          var nw = _normWord(word);
          if (nw && normInfo.norm) {
            var idx2 = normInfo.norm.indexOf(nw, normPos);
            if (idx2 < 0 && normPos > 0) {
              idx2 = normInfo.norm.indexOf(nw, Math.max(0, normPos - nw.length * 2));
            }
            if (idx2 >= 0 && idx2 > normPos + 200) idx2 = -1;
            if (idx2 >= 0) {
              var startOrig = normInfo.map[idx2] != null ? normInfo.map[idx2] : lastCharIndex;
              var endNorm = idx2 + nw.length - 1;
              var endOrig = normInfo.map[endNorm] != null ? normInfo.map[endNorm] : startOrig;
              charIndex = startOrig;
              charLength = Math.max(0, (endOrig - startOrig) + 1);
              lastCharIndex = charIndex;
              normPos = idx2 + nw.length;
            }
          }
          var entry = {
            offsetMs: Math.max(0, Number(data.ms || 0)),
            charIndex: charIndex,
            charLength: charLength,
          };
          allBoundaries.push({ offsetMs: entry.offsetMs, text: word });
          _bd.entries.push(entry);
          // Restart boundary poller if it stopped (ran out of entries)
          if (!_bd.rafId && _bd.reqId === myReq && state.playing && !state.paused) {
            _bd.rafId = setTimeout(_bdPoll, 16);
          }
        }

        function onEnd(data) {
          if (data.id !== sid) return;
          cleanup();
          streamDone = true;
          _activeStreamId = null;
          // Cache full audio in LRU for future replay/gapless
          try {
            var totalLen = 0;
            for (var i = 0; i < allChunks.length; i++) totalLen += allChunks[i].length;
            var full = new Uint8Array(totalLen);
            var off = 0;
            for (var j = 0; j < allChunks.length; j++) {
              full.set(allChunks[j], off);
              off += allChunks[j].length;
            }
            var blob = new Blob([full], { type: 'audio/mpeg' });
            var cacheKey = _lruKey(t);
            _lruSet(cacheKey, { blob: blob, blobUrl: null, boundaries: allBoundaries });
          } catch (e) {}
          // Finalize MediaSource if no more chunks to append
          if (appendQueue.length === 0 && !appending && !msEnded) {
            msEnded = true;
            try { if (ms.readyState === 'open') ms.endOfStream(); } catch (e) {}
          }
          resolve(true);
        }

        function onStreamError(data) {
          if (data.id !== sid) return;
          cleanup();
          _activeStreamId = null;
          diag('edge_stream_error', String(data.err || 'unknown'));
          resolve(false);
        }

        function cleanup() {
          if (cleaned) return;
          cleaned = true;
          stream.off('chunk', onChunk);
          stream.off('bound', onBound);
          stream.off('end', onEnd);
          stream.off('error', onStreamError);
        }

        // FIX-RACE: Swap from early-capture listeners to permanent handlers,
        // then replay any events that arrived during the synthStream await.
        stream.off('chunk', onEarlyChunk);
        stream.off('bound', onEarlyBound);
        stream.off('end', onEarlyEnd);
        stream.off('error', onEarlyError);
        stream.on('chunk', onChunk);
        stream.on('bound', onBound);
        stream.on('end', onEnd);
        stream.on('error', onStreamError);

        // Replay buffered early events
        for (var ei = 0; ei < earlyChunks.length; ei++) onChunk(earlyChunks[ei]);
        for (var bi = 0; bi < earlyBounds.length; bi++) onBound(earlyBounds[bi]);
        if (earlyError) { onStreamError(earlyError); }
        else if (earlyEnd) { onEnd(earlyEnd); }
      });
    }

    var _streamStartTime = 0;

    async function speak(text) {
      cancel();
      var t = String(text || '').trim();
      if (!t) return;

      state.playing = true;
      state.paused = false;
      state.requestId = Math.random().toString(36).slice(2);
      state.abortCtrl = new AbortController();
      var myReq = state.requestId;
      var signal = state.abortCtrl.signal;

      // FIX-TTS05: Check LRU cache first — instant playback on hit
      var cacheKey = _lruKey(t);
      var cached = _lruGet(cacheKey);
      if (cached) {
        diag('edge_preload_hit', '');
        if (cached.audioUrl) {
          _playUrl(myReq, cached.audioUrl, cached.boundaries, t);
        } else {
          _playBlob(myReq, cached.blob, cached.boundaries, t);
        }
        return;
      }

      _setBuffering(true);
      diag('edge_ws_open_start', '');

      // Try streaming path first (plays audio as chunks arrive)
      _streamStartTime = Date.now();
      var streamOk = false;
      try {
        streamOk = await _speakStream(t, myReq);
      } catch (e) {
        streamOk = false;
      }
      if (streamOk) {
        _setBuffering(false);
        return;
      }
      // If stream was not supported or failed, check if we were cancelled
      if (signal.aborted || myReq !== state.requestId) {
        _setBuffering(false);
        return;
      }

      // Fallback: blocking synth (full audio at once)
      try {
        var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
        if (!api || typeof api.synth !== 'function') {
          state.playing = false;
          diag('edge_ws_open_fail', 'booksTtsEdge_api_missing');
          if (typeof state.onError === 'function') state.onError({ error: 'edge_ws_open_fail', stage: 'edge_ws_open', reason: 'booksTtsEdge_api_missing' });
          return;
        }

        var res = await _synthWithTimeout(api, {
          text: t,
          voice: state.voiceName,
          rate: state.rate,
          pitch: state.pitch,
          returnBase64: false,
        }, 15000);
        if (signal.aborted || myReq !== state.requestId) return;

        if (!res || !res.ok || (!res.audioUrl && !res.audioBase64)) {
          state.playing = false;
          var code = String(res && (res.errorCode || res.reason) || 'edge_audio_chunk_recv_none');
          diag(code, String(res && res.reason || 'synth_failed'));
          if (typeof state.onError === 'function') state.onError({ error: code, stage: 'edge_synth', reason: String(res && res.reason || '') });
          return;
        }

        diag('edge_audio_chunk_recv_ok', '');
        if (res.audioUrl) {
          _lruSet(cacheKey, { audioUrl: String(res.audioUrl), boundaries: res.boundaries || [] });
          _playUrl(myReq, String(res.audioUrl), res.boundaries || [], t);
          return;
        }

        var blob = base64ToBlob(res.audioBase64, 'audio/mpeg');
        if (!blob) {
          state.playing = false;
          diag('edge_decode_fail', 'invalid_audio_payload');
          if (typeof state.onError === 'function') state.onError({ error: 'edge_decode_fail', stage: 'edge_decode', reason: 'invalid_audio_payload' });
          return;
        }
        if (signal.aborted || myReq !== state.requestId) return;
        diag('edge_decode_ok', '');

        _lruSet(cacheKey, { blob: blob, blobUrl: null, boundaries: res.boundaries || [] });
        _playBlob(myReq, blob, res.boundaries || [], t);
      } catch (err3) {
        if (signal.aborted || myReq !== state.requestId) return;
        state.playing = false;
        diag('edge_ws_open_fail', String(err3 && err3.message ? err3.message : err3));
        if (typeof state.onError === 'function') state.onError({ error: 'edge_ws_open_fail', stage: 'edge_ws_open', reason: String(err3 && err3.message ? err3.message : err3) });
      } finally {
        _setBuffering(false);
      }
    }

    // FIX-TTS05: Gapless re-speak — keep current audio playing until new audio is ready
    // FIX-TTS-B5 #8: Use pre-buffered audio element when available for near-zero gap
    async function speakGapless(text) {
      var t = String(text || '').trim();
      if (!t) return;
      if (state.abortCtrl) {
        try { state.abortCtrl.abort(); } catch {}
      }
      var newReqId = Math.random().toString(36).slice(2);
      state.requestId = newReqId;
      state.abortCtrl = new AbortController();
      var signal = state.abortCtrl.signal;

      var cacheKey = _lruKey(t);

      // FIX-TTS-B5 #8: If next block was pre-buffered and ready, use it directly
      if (_prepAudio && _prepCacheKey === cacheKey && _prepAudio.readyState >= 2) {
        var pAudio = _prepAudio;
        var pBlobUrl = pAudio._blobUrl || null;
        _prepAudio = null;
        _prepCacheKey = null;
        var pCached = _lruGet(cacheKey);
        _revokeBlobUrl();
        if (state.audio) { try { state.audio.pause(); state.audio.removeAttribute('src'); } catch {} }
        state.audio = pAudio;
        if (pBlobUrl) state.blobUrl = pBlobUrl;
        state.playing = true;
        state.paused = false;
        pAudio.onended = function () {
          if (newReqId !== state.requestId) return;
          state.playing = false;
          state.paused = false;
          _synthErrorCount = 0;
          diag('edge_play_ok', '');
          if (typeof state.onEnd === 'function') state.onEnd();
        };
        pAudio.onerror = function () {
          if (newReqId !== state.requestId) return;
          state.playing = false;
          var reason = pAudio.error ? String(pAudio.error.message || pAudio.error.code || 'unknown') : 'unknown';
          diag('edge_play_fail', reason);
          if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: reason });
        };
        fireBoundaries(newReqId, pCached ? pCached.boundaries : [], t);
        try {
          var pp = pAudio.play();
          if (pp && typeof pp.catch === 'function') {
            pp.catch(function (err) {
              if (newReqId !== state.requestId) return;
              state.playing = false;
              diag('edge_play_fail', String(err && err.message ? err.message : err));
              if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
            });
          }
        } catch (err) {
          state.playing = false;
          diag('edge_play_fail', String(err && err.message ? err.message : err));
          if (typeof state.onError === 'function') state.onError({ error: 'edge_play_fail', stage: 'edge_play', reason: String(err && err.message ? err.message : err) });
        }
        diag('edge_preload_hit', 'prepared');
        return;
      }
      _clearPrepared();

      // Check LRU cache
      var cached = _lruGet(cacheKey);
      if (cached) {
        diag('edge_preload_hit', '');
        if (cached.audioUrl) {
          _playUrl(newReqId, cached.audioUrl, cached.boundaries, t);
        } else {
          _playBlob(newReqId, cached.blob, cached.boundaries, t);
        }
        return;
      }

      _setBuffering(true);
      try {
        var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksTtsEdge;
        if (!api || typeof api.synth !== 'function') return;
        var res = await _synthWithTimeout(api, {
          text: t,
          voice: state.voiceName,
          rate: state.rate,
          pitch: state.pitch,
          returnBase64: false,
        }, 15000);
        if (signal.aborted || newReqId !== state.requestId) return;
        if (!res || !res.ok) return;
        if (res.audioUrl) {
          _lruSet(_lruKey(t), { audioUrl: String(res.audioUrl), boundaries: res.boundaries || [] });
          _playUrl(newReqId, String(res.audioUrl), res.boundaries || [], t);
          return;
        }
        if (!res.audioBase64) return;
        var blob = base64ToBlob(res.audioBase64, 'audio/mpeg');
        if (!blob) return;
        if (signal.aborted || newReqId !== state.requestId) return;
        _lruSet(_lruKey(t), { blob: blob, blobUrl: null, boundaries: res.boundaries || [] });
        _playBlob(newReqId, blob, res.boundaries || [], t);
      } catch {} finally {
        _setBuffering(false);
      }
    }

    function pause() {
      if (!state.audio || !state.playing || state.paused) return;
      state.audio.pause();
      state.paused = true;
      _bdPause(); // FIX-TTS-B1: stop boundary polling on pause
    }

    // FIX-TTS-B1: Optimistic paused=false (required by tts_core.js synchronous isPaused check)
    // with 5s timeout safety net for the rare case where audio.play() promise never settles.
    function resume() {
      if (!state.audio || !state.paused) return;
      try {
        var playPromise = state.audio.play();
        if (playPromise && typeof playPromise.then === 'function') {
          var settled = false;
          state.paused = false;
          _bdResume();
          var safetyTimer = setTimeout(function () {
            if (!settled) {
              settled = true;
              state.paused = true;
              _bdPause();
              diag('edge_resume_timeout', 'play_promise_stuck');
            }
          }, 5000);
          playPromise.then(function () {
            settled = true;
            clearTimeout(safetyTimer);
          }).catch(function (err) {
            if (!settled) {
              settled = true;
              clearTimeout(safetyTimer);
              state.paused = true;
              _bdPause();
              diag('edge_resume_fail', String(err && err.message ? err.message : err));
            }
          });
        } else {
          state.paused = false;
          _bdResume();
        }
      } catch (err) {
        diag('edge_resume_fail', String(err && err.message ? err.message : err));
      }
    }

    function cancel() {
      _setBuffering(false); // OPT-TTS-CTRL: clear buffering on any cancellation
      _cancelActiveStream(); // Cancel any in-flight streaming synthesis
      _bdStop(); // FIX-TTS08: stop boundary poller
      _clearPrepared(); // FIX-TTS-B5 #8
      state._resumeSeekMs = null;
      state.resumeCharIndex = -1;
      if (state.abortCtrl) {
        try { state.abortCtrl.abort(); } catch {}
        state.abortCtrl = null;
      }
      state.requestId = '';
      state.playing = false;
      state.paused = false;
      if (state.audio) {
        try { state.audio.pause(); } catch {}
        try { state.audio.removeAttribute('src'); } catch {}
        try { state.audio.load(); } catch {}
      }
      _revokeBlobUrl();
    }

    function isSpeaking() {
      return !!(state.playing && !state.paused);
    }

    function isPaused() {
      return !!state.paused;
    }

    function isAvailable() {
      return !!(state.health && state.health.known && state.health.available);
    }

    function getHealth() {
      return {
        known: !!state.health.known,
        available: !!state.health.available,
        reason: String(state.health.reason || ''),
      };
    }

    function getLastDiag() {
      return {
        code: String(state.lastDiag.code || ''),
        detail: String(state.lastDiag.detail || ''),
      };
    }

    return {
      getVoices: getVoices,
      setRate: setRate,
      setVolume: setVolume, // TTS-QOL4
      setVoice: setVoice,
      setResumeHint: setResumeHint,
      setPitch: setPitch,
      speak: speak,
      speakGapless: speakGapless,
      pause: pause,
      resume: resume,
      cancel: cancel,
      preload: preload,
      prepareNext: prepareNext, // FIX-TTS-B5 #8
      clearPreloadCache: clearPreloadCache,
      isSpeaking: isSpeaking,
      isPaused: isPaused,
      isAvailable: isAvailable,
      probe: probe,
      loadVoices: loadVoices,
      getHealth: getHealth,
      getLastDiag: getLastDiag,
      engineId: 'edge',
      set onBoundary(fn) { state.onBoundary = (typeof fn === 'function') ? fn : null; },
      set onEnd(fn) { state.onEnd = (typeof fn === 'function') ? fn : null; },
      set onError(fn) { state.onError = (typeof fn === 'function') ? fn : null; },
      set onDiag(fn) { state.onDiag = (typeof fn === 'function') ? fn : null; },
      set onBuffering(fn) { state.onBuffering = (typeof fn === 'function') ? fn : null; },
    };
  }

  window.booksTTSEngines.edge = { create: create };
})();
