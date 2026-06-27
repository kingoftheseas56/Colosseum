/**
 * Tankoban Groundwork - Book Reader API Gateway
 *
 * Slim version of the Butterfly api_gateway.js.
 * Only exposes book-reader-relevant APIs.
 * window.electronAPI is injected by the Python QWebChannel bridge.
 */

(function() {
  'use strict';

  var _DEFAULT_TIMEOUT = 15000;

  /**
   * withTimeout - reject a promise if it does not settle within ms.
   */
  function withTimeout(promise, ms, label) {
    if (!(ms > 0)) return promise;
    return new Promise(function(resolve, reject) {
      var done = false;
      var timer = setTimeout(function() {
        if (!done) { done = true; reject(new Error((label || 'api') + '_timeout')); }
      }, ms);
      promise.then(
        function(v) { if (!done) { done = true; clearTimeout(timer); resolve(v); } },
        function(e) { if (!done) { done = true; clearTimeout(timer); reject(e); } }
      );
    });
  }

  /**
   * safe - wrap an API call so it never throws; returns fallback on error.
   */
  function safe(fn, fallback) {
    return function() {
      try {
        var result = fn.apply(this, arguments);
        if (result && typeof result.then === 'function') {
          return result.catch(function(err) {
            console.warn('[api_gateway] call failed:', err);
            return fallback;
          });
        }
        return result;
      } catch (err) {
        console.warn('[api_gateway] call failed:', err);
        return typeof fallback !== 'undefined' && fallback !== null && typeof fallback.then === 'function'
          ? fallback : Promise.resolve(fallback);
      }
    };
  }

  var ea = window.electronAPI;

  if (!ea) {
    console.error('[api_gateway] window.electronAPI not available - bridge not injected.');
    window.Tanko = window.Tanko || {};
    window.Tanko.api = {};
    return;
  }

  window.Tanko = window.Tanko || {};
  window.Tanko.features = window.Tanko.features || {};

  function tw(fn, fb, label, ms) {
    return safe(function() {
      return withTimeout(fn.apply(null, arguments), ms || _DEFAULT_TIMEOUT, label);
    }, fb);
  }

  window.Tanko.api = {

    // Window management (fullscreen)
    window: {
      isFullscreen: tw(function() { return ea.window && ea.window.isFullscreen ? ea.window.isFullscreen() : Promise.resolve(false); }, false, 'window.isFullscreen'),
      toggleFullscreen: tw(function() { return ea.window && ea.window.toggleFullscreen ? ea.window.toggleFullscreen() : Promise.resolve({ ok: false, error: 'window_toggle_unavailable' }); }, { ok: false, error: 'window_toggle_unavailable' }, 'window.toggleFullscreen'),
      setFullscreen: tw(function() { var a = arguments; return ea.window && ea.window.setFullscreen ? ea.window.setFullscreen.apply(ea.window, a) : Promise.resolve({ ok: false, error: 'window_set_unavailable' }); }, { ok: false, error: 'window_set_unavailable' }, 'window.setFullscreen'),
      minimize: tw(function() { return ea.window && ea.window.minimize ? ea.window.minimize() : Promise.resolve({ ok: false, error: 'window_minimize_unavailable' }); }, { ok: false, error: 'window_minimize_unavailable' }, 'window.minimize'),
      close: tw(function() { return ea.window && ea.window.close ? ea.window.close() : Promise.resolve({ ok: false, error: 'window_close_unavailable' }); }, { ok: false, error: 'window_close_unavailable' }, 'window.close'),
      // PER_VIEW_CHROME_FIX 2026-05-02 P5 — chrome Max-toggle + state query
      // + change-subscription, routed through electronAPI shim to BookBridge.
      toggleMaximize: tw(function() { return ea.window && ea.window.toggleMaximize ? ea.window.toggleMaximize() : Promise.resolve({ ok: false, error: 'window_toggle_max_unavailable' }); }, { ok: false, error: 'window_toggle_max_unavailable' }, 'window.toggleMaximize'),
      isMaximized: tw(function() { return ea.window && ea.window.isMaximized ? ea.window.isMaximized() : Promise.resolve(false); }, false, 'window.isMaximized'),
      onMaximizeChanged: function(cb) { try { if (ea.window && typeof ea.window._onMaximizeChanged === 'function') ea.window._onMaximizeChanged(cb); } catch (e) {} },
    },

    // File reading
    files: {
      read: tw(function() { var a = arguments; return ea.files && ea.files.read ? ea.files.read.apply(ea.files, a) : Promise.resolve(null); }, null, 'files.read', 30000),
    },

    // Clipboard
    clipboard: {
      copyText: safe(function() { var a = arguments; return ea.clipboard && ea.clipboard.copyText ? ea.clipboard.copyText.apply(ea.clipboard, a) : undefined; }, undefined),
    },

    // Book progress
    booksProgress: {
      getAll: tw(function() { return ea.booksProgress && ea.booksProgress.getAll ? ea.booksProgress.getAll() : Promise.resolve({}); }, {}, 'booksProgress.getAll'),
      keyFor: tw(function() { var a = arguments; return ea.booksProgress && ea.booksProgress.keyFor ? ea.booksProgress.keyFor.apply(ea.booksProgress, a) : Promise.resolve(''); }, '', 'booksProgress.keyFor'),
      get: tw(function() { var a = arguments; return ea.booksProgress && ea.booksProgress.get ? ea.booksProgress.get.apply(ea.booksProgress, a) : Promise.resolve(null); }, null, 'booksProgress.get'),
      save: tw(function() { var a = arguments; return ea.booksProgress && ea.booksProgress.save ? ea.booksProgress.save.apply(ea.booksProgress, a) : Promise.resolve(); }, undefined, 'booksProgress.save'),
      clear: tw(function() { var a = arguments; return ea.booksProgress && ea.booksProgress.clear ? ea.booksProgress.clear.apply(ea.booksProgress, a) : Promise.resolve(); }, undefined, 'booksProgress.clear'),
      clearAll: tw(function() { return ea.booksProgress && ea.booksProgress.clearAll ? ea.booksProgress.clearAll() : Promise.resolve(); }, undefined, 'booksProgress.clearAll'),
    },

    // Book settings (per-book + global)
    booksSettings: {
      get: tw(function() { var a = arguments; return ea.booksSettings && ea.booksSettings.get ? ea.booksSettings.get.apply(ea.booksSettings, a) : Promise.resolve(null); }, null, 'booksSettings.get'),
      save: tw(function() { var a = arguments; return ea.booksSettings && ea.booksSettings.save ? ea.booksSettings.save.apply(ea.booksSettings, a) : Promise.resolve(); }, undefined, 'booksSettings.save'),
      clear: tw(function() { var a = arguments; return ea.booksSettings && ea.booksSettings.clear ? ea.booksSettings.clear.apply(ea.booksSettings, a) : Promise.resolve(); }, undefined, 'booksSettings.clear'),
    },

    // Bookmarks
    booksBookmarks: {
      get: tw(function() { var a = arguments; return ea.booksBookmarks && ea.booksBookmarks.get ? ea.booksBookmarks.get.apply(ea.booksBookmarks, a) : Promise.resolve(null); }, null, 'booksBookmarks.get'),
      save: tw(function() { var a = arguments; return ea.booksBookmarks && ea.booksBookmarks.save ? ea.booksBookmarks.save.apply(ea.booksBookmarks, a) : Promise.resolve(); }, undefined, 'booksBookmarks.save'),
      delete: tw(function() { var a = arguments; return ea.booksBookmarks && ea.booksBookmarks.delete ? ea.booksBookmarks.delete.apply(ea.booksBookmarks, a) : Promise.resolve(); }, undefined, 'booksBookmarks.delete'),
      clear: tw(function() { var a = arguments; return ea.booksBookmarks && ea.booksBookmarks.clear ? ea.booksBookmarks.clear.apply(ea.booksBookmarks, a) : Promise.resolve(); }, undefined, 'booksBookmarks.clear'),
    },

    // Annotations
    booksAnnotations: {
      get: tw(function() { var a = arguments; return ea.booksAnnotations && ea.booksAnnotations.get ? ea.booksAnnotations.get.apply(ea.booksAnnotations, a) : Promise.resolve(null); }, null, 'booksAnnotations.get'),
      save: tw(function() { var a = arguments; return ea.booksAnnotations && ea.booksAnnotations.save ? ea.booksAnnotations.save.apply(ea.booksAnnotations, a) : Promise.resolve(); }, undefined, 'booksAnnotations.save'),
      delete: tw(function() { var a = arguments; return ea.booksAnnotations && ea.booksAnnotations.delete ? ea.booksAnnotations.delete.apply(ea.booksAnnotations, a) : Promise.resolve(); }, undefined, 'booksAnnotations.delete'),
      clear: tw(function() { var a = arguments; return ea.booksAnnotations && ea.booksAnnotations.clear ? ea.booksAnnotations.clear.apply(ea.booksAnnotations, a) : Promise.resolve(); }, undefined, 'booksAnnotations.clear'),
    },

    // Display names
    booksDisplayNames: {
      getAll: tw(function() { return ea.booksDisplayNames && ea.booksDisplayNames.getAll ? ea.booksDisplayNames.getAll() : Promise.resolve(null); }, null, 'booksDisplayNames.getAll'),
      save: tw(function() { var a = arguments; return ea.booksDisplayNames && ea.booksDisplayNames.save ? ea.booksDisplayNames.save.apply(ea.booksDisplayNames, a) : Promise.resolve(); }, undefined, 'booksDisplayNames.save'),
      clear: tw(function() { var a = arguments; return ea.booksDisplayNames && ea.booksDisplayNames.clear ? ea.booksDisplayNames.clear.apply(ea.booksDisplayNames, a) : Promise.resolve(); }, undefined, 'booksDisplayNames.clear'),
    },

    // Shell
    shell: {
      revealPath: safe(function() { var a = arguments; return ea.shell && ea.shell.revealPath ? ea.shell.revealPath.apply(ea.shell, a) : undefined; }, undefined),
      openExternal: safe(function() { var a = arguments; return ea.shell && ea.shell.openExternal ? ea.shell.openExternal.apply(ea.shell, a) : undefined; }, undefined),
    },

    // TTS Edge (wired to Qt-side EdgeTtsClient via QWebChannel BookBridge,
    // EDGE_TTS_FIX Phase 1.3+). Direct WebSocket to Microsoft Edge Read
    // Aloud — see src/core/tts/EdgeTtsClient.{h,cpp}.
    booksTtsEdge: {
      probe: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.probe ? ea.booksTtsEdge.probe.apply(ea.booksTtsEdge, a) : Promise.resolve({ ok: false }); }, { ok: false }, 'booksTtsEdge.probe'),
      getVoices: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.getVoices ? ea.booksTtsEdge.getVoices.apply(ea.booksTtsEdge, a) : Promise.resolve([]); }, [], 'booksTtsEdge.getVoices', 45000),
      synth: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.synth ? ea.booksTtsEdge.synth.apply(ea.booksTtsEdge, a) : Promise.resolve(null); }, null, 'booksTtsEdge.synth', 30000),
      synthStream: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.synthStream ? ea.booksTtsEdge.synthStream.apply(ea.booksTtsEdge, a) : Promise.resolve({ ok: false, error: 'tts_stream_unavailable' }); }, { ok: false, error: 'tts_stream_unavailable' }, 'booksTtsEdge.synthStream'),
      cancelStream: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.cancelStream ? ea.booksTtsEdge.cancelStream.apply(ea.booksTtsEdge, a) : Promise.resolve({ ok: true }); }, { ok: true }, 'booksTtsEdge.cancelStream'),
      warmup: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.warmup ? ea.booksTtsEdge.warmup.apply(ea.booksTtsEdge, a) : Promise.resolve({ ok: false, error: 'tts_warmup_unavailable' }); }, { ok: false, error: 'tts_warmup_unavailable' }, 'booksTtsEdge.warmup'),
      resetInstance: tw(function() { var a = arguments; return ea.booksTtsEdge && ea.booksTtsEdge.resetInstance ? ea.booksTtsEdge.resetInstance.apply(ea.booksTtsEdge, a) : Promise.resolve({ ok: true }); }, { ok: true }, 'booksTtsEdge.resetInstance'),
    },
  };

  window.Tanko.apiReady = true;
  console.log('[api_gateway] Book reader API gateway initialized.');
})();
