// Standalone Ebook Reader — Boot Script
// Loaded LAST by ebook_reader.html after all reader modules are ready.
// Provides window.__ebookOpenBook(filePath) for Python to call via runJavaScript.
//
// In standalone mode the reader is always visible (CSS override), so we skip
// the shell, mode router, and library — just wire the open path.

(function () {
  'use strict';

  var controller = window.booksReaderController;
  if (!controller || typeof controller.open !== 'function') {
    console.error('[ebook-standalone] booksReaderController not found — reader modules may have failed to load.');
    return;
  }

  // Wire the back button to close the reader (return to app shell)
  var backBtn = document.getElementById('booksReaderBackBtn');
  if (backBtn && !backBtn.__booksBackBound) {
    backBtn.addEventListener('click', function () {
      if (window.__ebookNav && typeof window.__ebookNav.requestClose === 'function') {
        window.__ebookNav.requestClose();
      }
    });
    backBtn.__booksBackBound = true;
  }

  /**
   * Open a book by file path.
   * Called from Python via QWebEngineView.runJavaScript():
   *   window.__ebookOpenBook('C:\\Users\\...\\book.epub')
   *
   * @param {string} filePath - absolute path to the book file
   */
  async function openBook(filePath, bookId, displayTitle) {
    console.log('[ebook-standalone] Opening: ' + filePath + (bookId ? ' (id=' + bookId + ')' : ''));

    // Determine format from extension
    var ext = (filePath.split('.').pop() || '').toLowerCase();
    var formatMap = { epub: 'epub', pdf: 'pdf', txt: 'txt', mobi: 'mobi', fb2: 'fb2' };
    var format = formatMap[ext];
    if (!format) {
      console.error('[ebook-standalone] Unknown format: ' + ext);
      return;
    }

    // Build book input (matches normalizeBookInput expectations).
    // BOOK_FIX 1.1: id must be the canonical SHA1[:20] of the normalized path so
    // reader saves land in the same record the library reads. If the library
    // already threaded a bookId through, prefer that; otherwise compute via the
    // bridge. reader_core.js::open() re-resolves as a belt-and-suspenders pass.
    var fileName = filePath.replace(/\\/g, '/').split('/').pop() || 'book';
    var resolvedId = bookId && String(bookId).trim();
    if (!resolvedId) {
      try {
        var api = window.Tanko && window.Tanko.api && window.Tanko.api.booksProgress;
        if (api && typeof api.keyFor === 'function') {
          resolvedId = await api.keyFor(filePath);
        }
      } catch (e) { /* swallow — leaves resolvedId empty, handled below */ }
    }
    if (!resolvedId) {
      console.error('[ebook-standalone] Could not resolve SHA1 key for: ' + filePath);
      return;
    }
    var bookInput = {
      id: resolvedId,
      path: filePath,
      title: (displayTitle && displayTitle.trim()) ? displayTitle.trim() : fileName.replace(/\.\w+$/, ''),
      format: format,
    };

    try {
      await controller.open(bookInput);
      console.log('[ebook-standalone] Book opened successfully.');
    } catch (err) {
      console.error('[ebook-standalone] Failed to open book:', err);
    }
  }

  // Expose to Python bridge
  window.__ebookOpenBook = openBook;

  // Notify Python host that reader is fully ready (deterministic handshake).
  // This replaces the polling boot-check loop — Python listens for this
  // event via the ReaderHostBridge and opens queued books immediately.
  if (typeof window.__readerHostEmit === 'function') {
    window.__readerHostEmit('reader:ready', { boot: true });
  }

  // TTS warm-up: fire a lightweight probe + voice fetch in the background
  // so the first TTS play doesn't incur a cold-start delay.
  try {
    var ttsApi = window.electronAPI && window.electronAPI.booksTtsEdge;
    if (ttsApi && typeof ttsApi.warmup === 'function') {
      ttsApi.warmup({}).then(function () {
        console.log('[ebook-standalone] TTS warmup complete.');
      }).catch(function () {});
    }
  } catch (e) {}

  console.log('[ebook-standalone] Boot complete. Waiting for __ebookOpenBook(path)...');
})();
