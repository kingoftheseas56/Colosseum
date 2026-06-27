// Books reader engine: EPUB (epub.js)
(function () {
  'use strict';

  window.booksReaderEngines = window.booksReaderEngines || {};

  function assetUrl(rel) {
    try { return new URL(rel, window.location.href).toString(); } catch { return rel; }
  }

  function loadScriptOnce(src) {
    return new Promise((resolve, reject) => {
      const existing = Array.from(document.querySelectorAll('script')).find((s) => s.src && (s.src === src || s.src.endsWith(src)));
      if (existing) {
        if (existing.dataset.loaded === '1') return resolve();
        existing.addEventListener('load', () => resolve(), { once: true });
        existing.addEventListener('error', () => reject(new Error(`Failed to load ${src}`)), { once: true });
        return;
      }

      const s = document.createElement('script');
      s.src = src;
      s.async = false;
      s.onload = () => {
        s.dataset.loaded = '1';
        resolve();
      };
      s.onerror = () => reject(new Error(`Failed to load ${src}`));
      document.body.appendChild(s);
    });
  }

  async function ensureEpubJs() {
    if (typeof window.ePub === 'function') return;
    try {
      await loadScriptOnce(assetUrl('../node_modules/epubjs/dist/epub.min.js'));
    } catch {
      await loadScriptOnce(assetUrl('../node_modules/epubjs/dist/epub.js'));
    }
    if (typeof window.ePub !== 'function') throw new Error('epubjs_not_available');
  }

  function toArrayBuffer(data) {
    if (!data) return null;
    if (data instanceof ArrayBuffer) return data;
    if (ArrayBuffer.isView(data)) {
      return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    }
    return null;
  }

  function toFileUrl(filePath) {
    const raw = String(filePath || '');
    if (!raw) return '';
    const normalized = raw.replace(/\\/g, '/');
    if (/^[A-Za-z]:\//.test(normalized)) return `file:///${encodeURI(normalized)}`;
    if (normalized.startsWith('/')) return `file://${encodeURI(normalized)}`;
    return normalized;
  }

  function create(api) {
    const MAX_STALE_SELECTION_MS = 7000;
    const state = {
      host: null,
      stage: null,
      book: null,
      rendition: null,
      lastCfi: null,
      lastHref: null,
      lastFraction: null,
      theme: 'light',
      onUserActivity: (api && typeof api.onUserActivity === 'function') ? api.onUserActivity : null,
      lastActivityAt: 0,
      onDblClick: null,
      onContextMenu: null,
      onDictLookup: (api && typeof api.onDictLookup === 'function') ? api.onDictLookup : null,
      lastSelectionText: '',
      lastSelectionAt: 0,
    };

    function clearHost() {
      if (state.host) state.host.innerHTML = '';
    }

    function createStage(host) {
      const stage = document.createElement('div');
      stage.className = 'booksReaderEpubStage';
      host.appendChild(stage);
      return stage;
    }

    function buildThemeRules(settings) {
      const s = settings || {};
      const fontSize = Math.max(12, Math.min(42, Number(s.fontSize || 18)));
      const lineHeight = Math.max(1.1, Math.min(2.4, Number(s.lineHeight || 1.6)));
      const margin = Math.max(0, Math.min(80, Number(s.margin || 24)));
      const theme = String(s.theme || 'light');
      state.theme = theme;

      const dark = theme === 'dark';
      const sepia = theme === 'sepia';

      const bg = dark ? '#101216' : (sepia ? '#f5ecd8' : '#ffffff');
      const fg = dark ? '#e4e8ef' : (sepia ? '#4d3d2c' : '#1a1a1a');

      return {
        body: {
          'font-size': `${fontSize}px`,
          'line-height': String(lineHeight),
          margin: `${margin}px`,
          color: fg,
          background: bg,
        },
      };
    }

    function applySettings(settings) {
      if (!state.rendition) return;
      const rules = buildThemeRules(settings);
      try {
        state.rendition.themes.default(rules);
      } catch {}
      if (state.host) state.host.dataset.readerTheme = state.theme;
    }

    function emitUserActivity(ev) {
      if (typeof state.onUserActivity !== 'function') return;
      const now = Date.now();
      if ((now - Number(state.lastActivityAt || 0)) < 120) return;
      state.lastActivityAt = now;
      try { state.onUserActivity(ev || null); } catch {}
    }

    function normalizeLookupWord(raw) {
      let s = String(raw || '').trim();
      if (!s) return '';
      s = s.replace(/\s+/g, ' ').split(' ')[0];
      s = s.replace(/^[\s"'`([{<]+/, '').replace(/[\s"'`)\]}>.,;:!?]+$/, '');
      if (s.length > 120) s = s.slice(0, 120);
      return s;
    }

    function isWordChar(ch) {
      return /[A-Za-z0-9'-]/.test(String(ch || ''));
    }

    function extractWordFromPointPosition(node, offset) {
      if (!node) return '';
      let textNode = node;
      let idx = Number(offset || 0);
      if (textNode.nodeType !== Node.TEXT_NODE) {
        const childNodes = textNode.childNodes || [];
        if (!childNodes.length) return '';
        let childIndex = Math.max(0, Math.min(childNodes.length - 1, idx - 1));
        if (idx <= 0) childIndex = 0;
        textNode = childNodes[childIndex];
        if (!textNode || textNode.nodeType !== Node.TEXT_NODE) return '';
        idx = (idx <= 0) ? 0 : String(textNode.textContent || '').length;
      }

      const text = String(textNode.textContent || '');
      if (!text) return '';
      let at = Math.max(0, Math.min(text.length - 1, idx));
      if (!isWordChar(text.charAt(at)) && at > 0 && isWordChar(text.charAt(at - 1))) at -= 1;
      if (!isWordChar(text.charAt(at))) return '';

      let start = at;
      let end = at + 1;
      while (start > 0 && isWordChar(text.charAt(start - 1))) start -= 1;
      while (end < text.length && isWordChar(text.charAt(end))) end += 1;
      return normalizeLookupWord(text.slice(start, end));
    }

    function wordAtPoint(doc, ev) {
      if (!doc || !ev) return '';
      const x = Number(ev.clientX);
      const y = Number(ev.clientY);
      if (!Number.isFinite(x) || !Number.isFinite(y)) return '';
      try {
        if (typeof doc.caretRangeFromPoint === 'function') {
          const range = doc.caretRangeFromPoint(x, y);
          if (range) return extractWordFromPointPosition(range.startContainer, range.startOffset);
        }
      } catch {}
      try {
        if (typeof doc.caretPositionFromPoint === 'function') {
          const pos = doc.caretPositionFromPoint(x, y);
          if (pos) return extractWordFromPointPosition(pos.offsetNode, pos.offset);
        }
      } catch {}
      return '';
    }

    function dispatchDictLookup(doc, ev) {
      let text = normalizeLookupWord(rememberSelectionText(doc, false) || recentSelectionText());
      if (!text && ev && ev.type === 'contextmenu') {
        text = wordAtPoint(doc, ev);
      }
      if (typeof state.onDictLookup === 'function') {
        try {
          state.onDictLookup(text, ev || null);
          return true;
        } catch {}
      }
      return false;
    }

    function isPlainDKey(ev) {
      if (!ev) return false;
      if (ev.ctrlKey || ev.metaKey || ev.altKey) return false;
      const key = String(ev.key || '').toLowerCase();
      return key === 'd';
    }

    function selectionTextFromDoc(doc) {
      try {
        if (!doc || typeof doc.getSelection !== 'function') return '';
        const sel = doc.getSelection();
        return (sel && sel.toString().trim()) || '';
      } catch {
        return '';
      }
    }

    function rememberSelectionText(doc, allowClear) {
      const text = selectionTextFromDoc(doc);
      if (text) {
        state.lastSelectionText = text;
        state.lastSelectionAt = Date.now();
        return text;
      }
      if (allowClear) {
        state.lastSelectionText = '';
        state.lastSelectionAt = 0;
      }
      return '';
    }

    function recentSelectionText() {
      if (!state.lastSelectionText) return '';
      if ((Date.now() - Number(state.lastSelectionAt || 0)) > MAX_STALE_SELECTION_MS) return '';
      return state.lastSelectionText;
    }

    function bindSelectionEventsToDoc(doc) {
      try {
        if (!doc || doc._tankoDictBound) return;
        doc._tankoDictBound = true;

        const remember = () => { rememberSelectionText(doc, false); };
        const rememberBeforeContext = (ev) => {
          try { if (ev && ev.button === 2) rememberSelectionText(doc, false); } catch {}
        };

        doc.addEventListener('mousemove', emitUserActivity, { passive: true });
        doc.addEventListener('pointermove', emitUserActivity, { passive: true });
        doc.addEventListener('pointerdown', emitUserActivity, { passive: true });
        doc.addEventListener('touchstart', emitUserActivity, { passive: true });
        doc.addEventListener('click', emitUserActivity, { passive: true });
        doc.addEventListener('selectionchange', remember);
        doc.addEventListener('mouseup', remember, { passive: true });
        doc.addEventListener('keyup', remember);
        doc.addEventListener('touchend', remember, { passive: true });
        doc.addEventListener('pointerdown', rememberBeforeContext, true);
        doc.addEventListener('dblclick', (ev) => {
          emitUserActivity(ev);
          const text = rememberSelectionText(doc, false) || recentSelectionText();
          if (!dispatchDictLookup(doc, ev) && typeof state.onDblClick === 'function') {
            state.onDblClick(text, ev);
          }
        });
        doc.addEventListener('contextmenu', (ev) => {
          emitUserActivity(ev);
          const text = rememberSelectionText(doc, false) || recentSelectionText();
          if (typeof state.onDictLookup === 'function' && ev && typeof ev.preventDefault === 'function') ev.preventDefault();
          if (!dispatchDictLookup(doc, ev) && typeof state.onContextMenu === 'function') {
            state.onContextMenu(ev, text);
          }
        }, true);
        doc.addEventListener('keydown', (ev) => {
          emitUserActivity(ev);
          if (!isPlainDKey(ev)) return;
          if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();
          if (ev && typeof ev.stopPropagation === 'function') ev.stopPropagation();
          if (!dispatchDictLookup(doc, ev)) {
            const text = rememberSelectionText(doc, false) || recentSelectionText();
            if (typeof state.onDblClick === 'function') state.onDblClick(text, ev);
          }
        }, true);
      } catch {}
    }

    function bindSelectionEvents() {
      try {
        if (!state.rendition) return;
        const contents = (typeof state.rendition.getContents === 'function') ? state.rendition.getContents() : [];
        for (const c of contents) {
          const doc = c && (c.document || c.doc);
          if (doc) bindSelectionEventsToDoc(doc);
        }
      } catch {}
    }

    async function open(opts) {
      const o = (opts && typeof opts === 'object') ? opts : {};
      state.host = o.host;
      clearHost();
      state.stage = createStage(state.host);

      await ensureEpubJs();
      let opened = false;
      let lastErr = null;

      try {
        const raw = await api.filesRead(o.book.path);
        const data = toArrayBuffer(raw) || raw;
        state.book = window.ePub(data);
        opened = true;
      } catch (err) {
        lastErr = err;
      }

      if (!opened) {
        try {
          state.book = window.ePub(toFileUrl(o.book.path));
          opened = true;
        } catch (err) {
          lastErr = err;
        }
      }

      if (!opened || !state.book) {
        throw (lastErr || new Error('epub_open_failed'));
      }

            try { if (state.book && state.book.ready) await state.book.ready; } catch {}

state.rendition = state.book.renderTo(state.stage, {
        width: '100%',
        height: '100%',
        flow: 'paginated',
        manager: 'default',
      });

      state.rendition.on('relocated', (loc) => {
        try {
          state.lastCfi = loc && loc.start ? (loc.start.cfi || null) : null;
          state.lastHref = loc && loc.start ? (loc.start.href || null) : null;
          // FIX-READER-GAPS: compute approximate book-level fraction from spine position
          state.lastFraction = null;
          if (loc && loc.start && state.book && state.book.spine) {
            const spineLen = state.book.spine.length;
            if (spineLen > 0) {
              const idx = Number(loc.start.index) || 0;
              const d = loc.start.displayed;
              const pageFrac = (d && d.total > 0) ? ((d.page || 1) - 1) / d.total : 0;
              state.lastFraction = (idx + pageFrac) / spineLen;
            }
          }
        } catch {}
      });
      state.rendition.on('rendered', (_section, view) => {
        try {
          const doc = view && view.document;
          if (doc) bindSelectionEventsToDoc(doc);
          bindSelectionEvents();
        } catch {}
      });

      applySettings(o.settings);

      async function safeDisplayFromLocator(locator) {
        const loc = (locator && typeof locator === 'object') ? locator : null;
        if (!state.rendition) return;
        // 1) Preferred: CFI or href
        if (loc && (loc.cfi || loc.href)) {
          try {
            await state.rendition.display(loc.cfi || loc.href);
            return true;
          } catch (e) {
            // fall through to percent
          }
        }
        // 2) Fallback: percent → CFI via locations
        if (loc && typeof loc.percent === 'number' && isFinite(loc.percent) && state.book && state.book.locations) {
          try {
            if (!state.book.locations.length || state.book.locations.length < 2) {
              // Generate locations only when needed (can be slow)
              await state.book.locations.generate(1600);
            }
            if (typeof state.book.locations.cfiFromPercentage === 'function') {
              const cfi = state.book.locations.cfiFromPercentage(loc.percent);
              if (cfi) {
                await state.rendition.display(cfi);
                return true;
              }
            }
          } catch (e2) {
            // ignore and fall through
          }
        }
        // 3) Default: start
        try { await state.rendition.display(); } catch {}
        return false;
      }

      await safeDisplayFromLocator(o.locator);
      bindSelectionEvents();
    }

    async function destroy() {
      try { if (state.rendition) state.rendition.destroy(); } catch {}
      try { if (state.book) state.book.destroy(); } catch {}
      state.book = null;
      state.rendition = null;
      state.lastCfi = null;
      state.lastHref = null;
      state.lastFraction = null;
      state.onUserActivity = null;
      state.lastActivityAt = 0;
      state.onDblClick = null;
      state.onContextMenu = null;
      state.onDictLookup = null;
      state.lastSelectionText = '';
      state.lastSelectionAt = 0;
      clearHost();
      state.host = null;
      state.stage = null;
    }

    async function next() {
      if (state.rendition) await state.rendition.next();
    }

    async function prev() {
      if (state.rendition) await state.rendition.prev();
    }

    async function getLocator() {
      const frac = state.lastFraction;
      return {
        cfi: state.lastCfi || null,
        href: state.lastHref || null,
        fraction: (typeof frac === 'number' && isFinite(frac)) ? frac : null,
        percent: (typeof frac === 'number' && isFinite(frac)) ? frac : null,
      };
    }

    async function getToc() {
      try {
        const nav = await state.book.loaded.navigation;
        const toc = Array.isArray(nav && nav.toc) ? nav.toc : [];
        return toc.map((item) => ({
          label: String(item && item.label || ''),
          href: String(item && item.href || ''),
        })).filter((x) => x.href);
      } catch {
        return [];
      }
    }

    async function goTo(target) {
      if (!state.rendition) return;
      if (!target) return;
      if (typeof target === 'string') {
        await state.rendition.display(target);
        return;
      }
      if (target.cfi || target.href) {
        await state.rendition.display(target.cfi || target.href);
      }
    }

    async function search(query) {
      const q = String(query || '').trim().toLowerCase();
      if (!q || !state.book) return { ok: false, count: 0 };

      try {
        const sections = state.book.spine && Array.isArray(state.book.spine.spineItems)
          ? state.book.spine.spineItems
          : [];

        for (const section of sections) {
          if (!section || !section.href || typeof section.load !== 'function') continue;
          let doc = null;
          try {
            doc = await section.load(state.book.load.bind(state.book));
            const bodyText = String(doc && doc.body ? doc.body.textContent || '' : '').toLowerCase();
            if (bodyText.includes(q)) {
              await state.rendition.display(section.href);
              return { ok: true, count: 1 };
            }
          } catch {}
          try { if (section.unload) section.unload(); } catch {}
        }

        return { ok: true, count: 0 };
      } catch {
        return { ok: false, count: 0 };
      }
    }

    function getSelectedText() {
      try {
        if (state.rendition && typeof state.rendition.getContents === 'function') {
          const contents = state.rendition.getContents();
          for (const c of contents) {
            const doc = c && (c.document || c.doc);
            const text = selectionTextFromDoc(doc);
            if (text) return text;
          }
        }
      } catch {}
      const sel = window.getSelection();
      const text = (sel && sel.toString().trim()) || '';
      if (text) return text;
      return recentSelectionText();
    }

    return {
      open,
      destroy,
      next,
      prev,
      getLocator,
      getToc,
      goTo,
      search,
      applySettings,
      getSelectedText,
      set onDblClick(fn) { state.onDblClick = typeof fn === 'function' ? fn : null; },
      set onContextMenu(fn) { state.onContextMenu = typeof fn === 'function' ? fn : null; },
      set onDictLookup(fn) { state.onDictLookup = typeof fn === 'function' ? fn : null; },
    };
  }

  // FIX-R07: keep legacy engine available as compatibility fallback.
  window.booksReaderEngines.epub_legacy = { create };
})();
