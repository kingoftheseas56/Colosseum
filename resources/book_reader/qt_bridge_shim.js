// qt_bridge_shim.js — injected at DocumentCreation (after qwebchannel.js) by
// BookReader.qml. Builds window.electronAPI + window.__ebookNav from the
// QWebChannel "bridge" object so TB2's foliate reader runs unchanged in Colosseum.
// Ported verbatim from Tankoban 2's BookReader BookBridgeShim.
(function () {
  try {
    var qt_wc = typeof qt !== 'undefined' && qt.webChannelTransport;
    if (!qt_wc) { console.error('[shim] No qt.webChannelTransport'); return; }
    if (typeof QWebChannel === 'undefined') { console.error('[shim] QWebChannel undefined — qwebchannel.js did not load'); return; }
    new QWebChannel(qt_wc, function (channel) {
      var b = channel.objects.bridge;
      window.electronAPI = {
        files: { read: function (path) { return b.filesRead(path); } },
        booksProgress: {
          getAll: function () { return Promise.resolve({}); },
          keyFor: function (p) { return b.progressKey(p); },
          get: function (id) { return b.booksProgressGet(id); },
          save: function (id, d) { return b.booksProgressSave(id, d); },
          clear: function () { return Promise.resolve(); },
          clearAll: function () { return Promise.resolve(); }
        },
        booksSettings: {
          get: function () { return b.booksSettingsGet(); },
          save: function (d) { return b.booksSettingsSave(d); },
          clear: function () { return Promise.resolve(); }
        },
        booksBookmarks: {
          get: function (id) { return b.booksBookmarksGet(id); },
          save: function (id, d) { return b.booksBookmarksSave(id, d); },
          delete: function (id, bmId) { return b.booksBookmarksDelete(id, bmId || ''); },
          clear: function (id) { return b.booksBookmarksClear(id); }
        },
        booksAnnotations: {
          get: function (id) { return b.booksAnnotationsGet(id); },
          save: function (id, d) { return b.booksAnnotationsSave(id, d); },
          delete: function (id, annId) { return b.booksAnnotationsDelete(id, annId || ''); },
          clear: function (id) { return b.booksAnnotationsClear(id); }
        },
        booksDisplayNames: {
          getAll: function () { return b.booksDisplayNamesGetAll(); },
          save: function (id, name) { return b.booksDisplayNamesSave(id, name); },
          delete: function (id) { return b.booksDisplayNamesDelete(id); },
          clear: function () { return Promise.resolve(); }
        },
        window: {
          isFullscreen: function () { return Promise.resolve(b.windowIsFullscreen()); },
          toggleFullscreen: function () { return b.windowToggleFullscreen(); },
          setFullscreen: function (v) { var on = v === true || v === 'true'; if (b.windowIsFullscreen() !== on) b.windowToggleFullscreen(); return Promise.resolve({ ok: true }); },
          minimize: function () { try { b.windowMinimize(); } catch (e) {} return Promise.resolve({ ok: true }); },
          toggleMaximize: function () { try { b.windowToggleMaximize(); } catch (e) {} return Promise.resolve({ ok: true }); },
          isMaximized: function () { return Promise.resolve(b.windowIsMaximized()); },
          close: function () { try { b.windowClose(); } catch (e) {} return Promise.resolve({ ok: true }); },
          _onMaximizeChanged: function (cb) {
            try { if (b.windowMaximizeChanged && typeof b.windowMaximizeChanged.connect === 'function') b.windowMaximizeChanged.connect(function (isMax) { try { cb(isMax); } catch (e) {} }); } catch (e) {}
          }
        },
        clipboard: { copyText: function (t) { return Promise.resolve(); } },
        shell: { revealPath: function () { return Promise.resolve(); }, openExternal: function () { return Promise.resolve(); } },
        booksTtsEdge: (function () {
          var _r = {}, _next = 0;
          function _on(sig) {
            if (sig && typeof sig.connect === 'function') {
              sig.connect(function (reqId, result) {
                var fn = _r[reqId]; delete _r[reqId];
                if (fn) try { fn(result); } catch (e) {}
              });
            }
          }
          _on(b.booksTtsEdgeProbeFinished);
          _on(b.booksTtsEdgeVoicesReady);
          _on(b.booksTtsEdgeSynthFinished);
          _on(b.booksTtsEdgeSynthStreamFinished);
          _on(b.booksTtsEdgeWarmupFinished);
          _on(b.booksTtsEdgeResetFinished);
          function _call(starter, args) {
            return new Promise(function (resolve) {
              var id = ++_next; _r[id] = resolve;
              try { starter.apply(b, [id].concat(args || [])); }
              catch (e) { delete _r[id]; resolve({ ok: false, reason: 'bridge_call_failed' }); }
            });
          }
          return {
            probe: function (opts) { opts = opts || {}; return _call(b.booksTtsEdgeProbeStart, [String(opts.voice || 'en-US-AriaNeural')]); },
            getVoices: function () { return _call(b.booksTtsEdgeGetVoicesStart, []); },
            synth: function (opts) { opts = opts || {}; return _call(b.booksTtsEdgeSynthStart, [String(opts.text || ''), String(opts.voice || ''), Number(opts.rate) || 1.0, Number(opts.pitch) || 1.0]); },
            synthStream: function (opts) { opts = opts || {}; return _call(b.booksTtsEdgeSynthStreamStart, [String(opts.text || ''), String(opts.voice || ''), Number(opts.rate) || 1.0, Number(opts.pitch) || 1.0]); },
            cancelStream: function (streamId) { try { b.booksTtsEdgeCancelStream(Number(streamId) || 0); } catch (e) {} return Promise.resolve({ ok: true }); },
            warmup: function () { return _call(b.booksTtsEdgeWarmupStart, []); },
            resetInstance: function () { return _call(b.booksTtsEdgeResetStart, []); }
          };
        })()
      };
      window.__ebookNav = {
        requestClose: function () { b.requestClose(); },
        markReaderReady: function () { b.markReaderReady(); }
      };
      console.log('[shim] electronAPI + __ebookNav ready');
    });
  } catch (e) { console.error('[shim] threw:', e && (e.stack || e.message || String(e))); }
})();
