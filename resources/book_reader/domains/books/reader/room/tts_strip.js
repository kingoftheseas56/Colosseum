// tts_strip.js — the "listen (read aloud)" transport strip.
//
// TTS itself is NOT rebuilt: window.booksTTS (tts_core.js) owns synthesis,
// the queue, and the CSS-Highlight word/line glow on the page. This file is
// ONLY a new bottom-docked transport surface that drives that same engine —
// the old #lpTtsBar (tts_hud.js) stays wired until Phase 5 deletes it.
//
// Recon (real names, not guesses — read tts_core.js + tts_hud.js in full):
//   - window.booksTTS.getState() returns one of: 'idle', 'playing', 'paused',
//     'stop-paused', 'backward-paused', 'forward-paused', 'setrate-paused',
//     'setvoice-paused'. 'playing' = playing, every other non-idle value is a
//     paused variant. No 'section_transition' state exists in tts_core (that
//     string only appears in tts_hud's own defensive OR-check — not a real
//     tts_core status; not reproduced here).
//   - Real start sequence (copied from tts_hud.js startTts(), the only
//     production caller of tts.init()):
//       1. wire tts.on('stateChange', ...) / tts.on('progress', ...) BEFORE
//          init — init's own progress callback (onInitProgress) and the
//          first fireProgress() at the end of init() can land before the
//          caller's own promise-then runs.
//       2. tts.init({ format, getHost, getViewEngine, onNeedAdvance,
//          onInitProgress }) — getHost/getViewEngine/onNeedAdvance read
//          window.booksReaderState.state.{host,engine} and call
//          state.engine.advanceSection(1), exactly as tts_hud.js does. These
//          are the real seams; there is no other documented entry point.
//       3. after init resolves, isInitDone()/isAvailable() distinguish "init
//          finished" from "engine actually usable" (EDGE_TTS_FIX Phase 5.2
//          comment in tts_core.js) — an unusable engine must be reported as
//          an honest error, not a silent no-op.
//       4. tts.play(0, { startFromVisible: true }) — tts_hud.js's own
//          fallback path when there's no saved position. Full saved-position
//          resume (electronAPI.getBooksTtsProgress) is tts_hud's job and out
//          of scope for a transport strip; starting from the visible page is
//          the honest, minimal behavior for "press read-aloud right here."
//   - pause()/resume()/stop()/setRate()/stepSegment() map straight to public
//     booksTTS methods; no hidden preconditions beyond "engine must exist"
//     (already-guarded inside tts_core itself, calling into a null engine is
//     a safe no-op there).
//   - No current-voice getter exists on window.booksTTS (only getVoices() —
//     a list — and getEngineId() — 'edge', not a voice name). tts_hud.js
//     tracks the selected voice itself in a private localStorage-backed
//     variable; tts_core never exposes it back out. Per the task's own
//     "else omit" clause, the strip shows no voice text — a picker/label
//     here would have to duplicate tts_hud's private state or guess, and
//     this file doesn't guess.
//   - Progress must read info.segIdx/info.segCount (format-agnostic — set by
//     snippetInfo() to blockIdx/queue-length for epub/pdf OR the txt-legacy
//     segIdx/segments.length), NOT info.blockIdx/blockCount directly — those
//     two stay -1/0 on the txt path (confirmed in tts_core.js snippetInfo()).
(function () {
  'use strict';

  try {
    var PAUSED_STATES = ['paused', 'stop-paused', 'backward-paused', 'forward-paused', 'setrate-paused', 'setvoice-paused'];
    var RATE_CYCLE = [1.0, 1.25, 1.5, 0.75];
    var DEFAULT_TITLE = 'Read aloud · following the text';

    var stripEl = null;
    var elPlayPause = null;
    var elTitle = null;
    var elProgress = null;
    var elRate = null;
    var elLoadingTrack = null;
    var _wired = false;    // tts.on(...) listeners bound once
    var _starting = false; // init+play sequence in flight — blocks stacked starts
    // Session token (quality review, fix 1): tts_hud guards its async init
    // with _ttsActive (tts_hud.js:654/400); the port dropped that guard, so
    // close-during-init would still start audio with the strip hidden
    // (stop() during 'idle' is a state no-op in tts_core). Open AND stop
    // both bump the token; doInitThenPlay captures it and refuses to play
    // if the session changed while init was pending.
    var _seq = 0;

    function warn(msg, e) {
      try { console.warn('[room-tts] ' + msg, e || ''); } catch (e2) {}
    }

    function isPausedState(s) {
      return PAUSED_STATES.indexOf(String(s || '')) >= 0;
    }

    function fmtRate(r) {
      var n = Number(r);
      if (!isFinite(n) || n <= 0) return '1×';
      var rounded = Math.round(n * 100) / 100;
      return String(rounded) + '×';
    }

    // ── DOM ────────────────────────────────────────────────────────────

    function buildStripDom() {
      var el = document.createElement('div');
      el.id = 'roomTtsStrip';
      el.className = 'room-strip hidden';
      el.setAttribute('role', 'toolbar');
      el.setAttribute('aria-label', 'Read aloud controls');

      var playBtn = document.createElement('button');
      playBtn.type = 'button';
      playBtn.className = 'room-strip-btn room-strip-playpause';
      playBtn.dataset.act = 'playpause';
      playBtn.title = 'Play or pause';
      playBtn.textContent = 'Play';
      el.appendChild(playBtn);

      var info = document.createElement('div');
      info.className = 'room-strip-info';

      var title = document.createElement('div');
      title.className = 'room-strip-title';
      title.textContent = DEFAULT_TITLE;
      info.appendChild(title);

      var progress = document.createElement('div');
      progress.className = 'room-strip-progress is-empty';
      info.appendChild(progress);

      var loadingTrack = document.createElement('div');
      loadingTrack.className = 'room-strip-loading-track hidden';
      var loadingFill = document.createElement('div');
      loadingFill.className = 'room-strip-loading-fill';
      loadingTrack.appendChild(loadingFill);
      info.appendChild(loadingTrack);

      el.appendChild(info);

      var rateBtn = document.createElement('button');
      rateBtn.type = 'button';
      rateBtn.className = 'room-strip-btn room-strip-rate';
      rateBtn.dataset.act = 'rate';
      rateBtn.title = 'Playback speed';
      rateBtn.textContent = '1×';
      el.appendChild(rateBtn);

      var closeBtn = document.createElement('button');
      closeBtn.type = 'button';
      closeBtn.className = 'room-strip-btn room-strip-close';
      closeBtn.dataset.act = 'close';
      closeBtn.title = 'Stop reading';
      closeBtn.setAttribute('aria-label', 'Stop reading');
      closeBtn.textContent = 'Close';
      el.appendChild(closeBtn);

      elPlayPause = playBtn;
      elTitle = title;
      elProgress = progress;
      elRate = rateBtn;
      elLoadingTrack = loadingTrack;

      return el;
    }

    // Own click containment: the strip is a persistent surface docked over
    // the reading iframe's parent document. A stray click must not bubble to
    // whatever the page/global chrome does with clicks (edge-reach, popover
    // backdrop dismissal, etc. — QML floating-panel doctrine, applied to
    // HTML: a panel over a tap-catcher needs its own swallower).
    // Guard keyed on the ELEMENT, not a module flag (quality review, fix 3):
    // a module-level boolean survives strip recreation — if the old node got
    // removed from the DOM, the rebuilt strip would be inert (no handlers,
    // no swallower). Same pattern as popovers.js ensureBackdrop.
    function wireStripClicks(el) {
      if (el.__roomStripWired) return;
      el.__roomStripWired = true;
      el.addEventListener('mousedown', function (e) { e.stopPropagation(); });
      el.addEventListener('click', function (e) {
        e.stopPropagation();
        var btn = e.target.closest('.room-strip-btn');
        if (!btn) return;
        var act = btn.dataset.act;
        if (act === 'playpause') togglePlayPause();
        else if (act === 'rate') cycleRate();
        else if (act === 'close') window.__roomStopTts && window.__roomStopTts();
      });
    }

    function ensureStrip() {
      if (stripEl && stripEl.isConnected) return stripEl;
      var view = document.getElementById('booksReaderView');
      if (!view) return null;
      var existing = document.getElementById('roomTtsStrip');
      if (existing) {
        stripEl = existing;
        elPlayPause = existing.querySelector('.room-strip-playpause');
        elTitle = existing.querySelector('.room-strip-title');
        elProgress = existing.querySelector('.room-strip-progress');
        elRate = existing.querySelector('.room-strip-rate');
        elLoadingTrack = existing.querySelector('.room-strip-loading-track');
      } else {
        stripEl = buildStripDom();
        view.appendChild(stripEl);
      }
      wireStripClicks(stripEl);
      var tts = window.booksTTS;
      if (tts) wireTtsListeners(tts);
      return stripEl;
    }

    // ── Render ─────────────────────────────────────────────────────────

    function renderPlayPause(status) {
      if (!elPlayPause) return;
      var playing = String(status || '') === 'playing';
      elPlayPause.textContent = playing ? 'Pause' : 'Play';
      elPlayPause.title = playing ? 'Pause' : 'Play';
    }

    function renderRate(rate) {
      if (!elRate) return;
      if (!isFinite(Number(rate))) return;
      elRate.textContent = fmtRate(rate);
    }

    function renderInfo(info) {
      if (!elProgress) return;
      if (info && info.lastError) {
        var err = info.lastError;
        var code = (err && (err.error || err.reason)) ? String(err.error || err.reason) : 'error';
        elProgress.textContent = 'Read-aloud error: ' + code;
        elProgress.classList.remove('is-empty');
        elProgress.classList.add('is-error');
        return;
      }
      elProgress.classList.remove('is-error');
      var idx = info ? Number(info.segIdx) : NaN;
      var count = info ? Number(info.segCount) : NaN;
      if (isFinite(idx) && idx >= 0 && isFinite(count) && count > 0) {
        elProgress.textContent = 'Block ' + (idx + 1) + ' of ' + count;
        elProgress.classList.remove('is-empty');
      } else {
        elProgress.textContent = '';
        elProgress.classList.add('is-empty');
      }
    }

    function render(info) {
      if (!stripEl || !info) return;
      renderPlayPause(info.status);
      renderInfo(info);
      renderRate(info.rate);
    }

    function showLoading(show, msg) {
      if (!stripEl) return;
      if (elLoadingTrack) elLoadingTrack.classList.toggle('hidden', !show);
      if (show) {
        if (elTitle) elTitle.textContent = msg || 'Preparing narration…';
        if (elProgress) { elProgress.textContent = ''; elProgress.classList.add('is-empty'); elProgress.classList.remove('is-error'); }
      } else if (elTitle) {
        elTitle.textContent = DEFAULT_TITLE;
      }
    }

    function renderUnavailable(tts) {
      if (!elProgress) return;
      var diag = (typeof tts.getLastDiag === 'function') ? tts.getLastDiag() : null;
      var reason = (diag && (diag.detail || diag.code)) || 'engine unavailable';
      elProgress.textContent = 'Read-aloud unavailable: ' + reason;
      elProgress.classList.remove('is-empty');
      elProgress.classList.add('is-error');
    }

    // ── Listeners (bound once; strip is a create-once singleton) ─────────

    function wireTtsListeners(tts) {
      if (_wired) return;
      _wired = true;
      if (typeof tts.on === 'function') {
        // tts_hud.js's own onState/onProgress bind through the SAME tts.on
        // pub-sub (see tts_hud.js wireTts()) — this is additive, not a
        // clobber; both surfaces can stay live simultaneously.
        tts.on('stateChange', function (status, info) { render(info || (tts.getSnippet && tts.getSnippet())); });
        tts.on('progress', function (info) { render(info); });
      } else {
        warn('booksTTS.on unavailable — live state sync skipped');
      }
      // Seed immediately — covers TTS already active from elsewhere (old
      // toolbar Listen button, 'T' key) by the time this strip opens.
      try { render(tts.getSnippet ? tts.getSnippet() : null); } catch (e) {}
    }

    // ── Real start/resume sequence (recon'd from tts_hud.js startTts()) ──

    function doInitThenPlay(tts) {
      var tok = _seq; // session token — see fix-1 comment at declaration
      _starting = true; // fix 2: block stacked starts while init is in flight
      showLoading(true, 'Warming up the voice…');
      var RS = window.booksReaderState;
      var book = RS && RS.state ? RS.state.book : null;
      var fmt = (book && book.format) ? String(book.format).toLowerCase() : 'epub';
      var opts = {
        format: fmt,
        getHost: function () { return RS && RS.state ? RS.state.host : null; },
        getViewEngine: function () { return RS && RS.state ? RS.state.engine : null; },
        onNeedAdvance: function () {
          var eng = RS && RS.state ? RS.state.engine : null;
          if (!eng || typeof eng.advanceSection !== 'function') return Promise.resolve(false);
          return eng.advanceSection(1).then(function () { return true; }).catch(function () { return false; });
        },
        onInitProgress: function (pct, msg) { showLoading(true, msg); },
      };

      Promise.resolve(tts.init(opts)).then(function () {
        _starting = false;
        // Fix 1: user closed the strip (or a new session superseded this
        // one) while init was pending — clear the loading state and DON'T
        // start audio into a hidden strip.
        if (tok !== _seq) { showLoading(false); return; }
        showLoading(false);
        if (typeof tts.isAvailable === 'function' && !tts.isAvailable()) {
          renderUnavailable(tts);
          return;
        }
        try { tts.play(0, { startFromVisible: true }); } catch (e) { warn('play after init failed', e); }
      }).catch(function (e) {
        _starting = false;
        showLoading(false);
        warn('tts.init failed', e);
        // Fix 4: an init rejection must READ as an error on the strip, not
        // vanish into the console — consistent with renderInfo/renderUnavailable.
        if (tok === _seq && elProgress) {
          elProgress.textContent = 'Read-aloud error: init failed';
          elProgress.classList.remove('is-empty');
          elProgress.classList.add('is-error');
        }
      });
    }

    function startOrResumeTts() {
      var tts = window.booksTTS;
      if (!tts) { warn('booksTTS unavailable'); return; }
      // Fix 2: an init+play sequence is already in flight — a second click
      // would stack another play() when its .then fires (tts.init dedupes
      // via initPromise, but every caller's .then still runs).
      if (_starting) return;
      var st = tts.getState();
      if (isPausedState(st)) { try { tts.resume(); } catch (e) { warn('resume failed', e); } return; }
      if (st === 'playing') return; // already playing — strip just reflects it
      // idle: reuse an already-initialized-but-idle engine if we have one,
      // otherwise run the full init sequence.
      if (typeof tts.isInitDone === 'function' && tts.isInitDone()) {
        if (typeof tts.isAvailable === 'function' && !tts.isAvailable()) {
          renderUnavailable(tts);
          return;
        }
        try { tts.play(0, { startFromVisible: true }); } catch (e) { warn('play failed', e); }
        return;
      }
      doInitThenPlay(tts);
    }

    function togglePlayPause() {
      var tts = window.booksTTS;
      if (!tts) return;
      var st = tts.getState();
      if (st === 'playing') { try { tts.pause(); } catch (e) { warn('pause failed', e); } return; }
      if (isPausedState(st)) { try { tts.resume(); } catch (e) { warn('resume failed', e); } return; }
      startOrResumeTts();
    }

    function cycleRate() {
      var tts = window.booksTTS;
      if (!tts || typeof tts.setRate !== 'function') return;
      var cur = Number(typeof tts.getRate === 'function' ? tts.getRate() : 1.0);
      var idx = -1;
      for (var i = 0; i < RATE_CYCLE.length; i++) {
        if (Math.abs(RATE_CYCLE[i] - cur) < 0.001) { idx = i; break; }
      }
      var next = RATE_CYCLE[(idx + 1) % RATE_CYCLE.length];
      try { tts.setRate(next); } catch (e) { warn('setRate failed', e); return; }
      renderRate(next);
    }

    // ── Public API ─────────────────────────────────────────────────────

    window.__roomOpenTts = function () {
      try {
        // Fix 2 interplay: a repeat click while THIS session's init is still
        // pending must not bump _seq — that would stale the in-flight token
        // and the resolved init would refuse to play (probe (b) caught this).
        // Just make sure the strip is up and let the pending start finish.
        if (_starting) {
          var pendingEl = ensureStrip();
          if (pendingEl) pendingEl.classList.remove('hidden');
          return;
        }
        _seq++; // fix 1: new session — invalidate any prior pending init's play
        if (typeof window.__roomStopAudiobook === 'function') {
          try { window.__roomStopAudiobook(); } catch (e) {}
        }
        var tts = window.booksTTS;
        if (!tts) { warn('booksTTS unavailable — no strip'); return; }
        var el = ensureStrip();
        if (!el) { warn('strip mount failed — #booksReaderView missing'); return; }
        el.classList.remove('hidden');
        startOrResumeTts();
      } catch (e) {
        try { console.error('[room-tts] open failed:', e); } catch (e2) {}
      }
    };

    window.__roomStopTts = function () {
      _seq++; // fix 1: session over — a still-pending init must not play
      try {
        var tts = window.booksTTS;
        if (tts && typeof tts.stop === 'function') { try { tts.stop(); } catch (e) {} }
      } catch (e) {}
      if (stripEl) stripEl.classList.add('hidden');
    };
  } catch (err) {
    try { console.error('[room-tts] module failed to load:', err); } catch (e2) {}
  }
})();
