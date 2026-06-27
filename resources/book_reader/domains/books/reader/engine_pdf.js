// Books reader engine: PDF (pdfjs-dist)
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

  async function ensurePdfJs() {
    if (window.pdfjsLib) return window.pdfjsLib;
    try {
      await loadScriptOnce(assetUrl('../node_modules/pdfjs-dist/build/pdf.min.js'));
    } catch {
      await loadScriptOnce(assetUrl('../node_modules/pdfjs-dist/build/pdf.js'));
    }
    if (!window.pdfjsLib) throw new Error('pdfjs_not_available');
    try {
      window.pdfjsLib.GlobalWorkerOptions.workerSrc = assetUrl('../node_modules/pdfjs-dist/build/pdf.worker.min.js');
    } catch {}
    return window.pdfjsLib;
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
    const state = {
      host: null,
      stage: null,
      canvas: null,
      status: null,
      pdf: null,
      pageNum: 1,
      pageCount: 0,
      zoom: 1,
      fitMode: 'page',
      renderToken: 0,
      theme: 'light',
    };

    function resetHost() {
      if (state.host) state.host.innerHTML = '';
    }

    function createStage(host) {
      const wrap = document.createElement('div');
      wrap.className = 'booksReaderPdfWrap';

      const status = document.createElement('div');
      status.className = 'booksReaderPdfStatus';

      const canvas = document.createElement('canvas');
      canvas.className = 'booksReaderPdfCanvas';

      wrap.appendChild(status);
      wrap.appendChild(canvas);
      host.appendChild(wrap);

      state.stage = wrap;
      state.status = status;
      state.canvas = canvas;
    }

    function applySettings(settings) {
      const s = settings || {};
      const theme = String(s.theme || 'light');
      state.theme = theme;
      if (state.host) state.host.dataset.readerTheme = theme;
    }

    function computeScale(page, fitMode, zoom) {
      const vp1 = page.getViewport({ scale: 1 });
      const sw = Math.max(200, state.stage ? state.stage.clientWidth : 1200);
      const sh = Math.max(200, state.stage ? (state.stage.clientHeight - 36) : 900);

      let base = 1;
      if (fitMode === 'width') base = sw / Math.max(1, vp1.width);
      else base = Math.min(sw / Math.max(1, vp1.width), sh / Math.max(1, vp1.height));

      return Math.max(0.1, base * Math.max(0.4, Math.min(4, Number(zoom || 1))));
    }

    async function renderPage() {
      if (!state.pdf || !state.canvas) return;
      const token = ++state.renderToken;

      const pageNum = Math.max(1, Math.min(state.pageCount || 1, Number(state.pageNum || 1)));
      state.pageNum = pageNum;

      const page = await state.pdf.getPage(pageNum);
      if (token !== state.renderToken) return;

      const scale = computeScale(page, state.fitMode, state.zoom);
      const viewport = page.getViewport({ scale });
      const canvas = state.canvas;
      const ctx = canvas.getContext('2d', { alpha: false });

      canvas.width = Math.max(1, Math.floor(viewport.width));
      canvas.height = Math.max(1, Math.floor(viewport.height));
      canvas.style.width = `${Math.floor(viewport.width)}px`;
      canvas.style.height = `${Math.floor(viewport.height)}px`;

      await page.render({ canvasContext: ctx, viewport }).promise;
      if (token !== state.renderToken) return;

      if (state.status) {
        state.status.textContent = `Page ${state.pageNum}/${state.pageCount} - Zoom ${Math.round(scale * 100)}%`;
      }
    }

    async function open(opts) {
      const o = (opts && typeof opts === 'object') ? opts : {};
      state.host = o.host;
      resetHost();
      createStage(state.host);

      const pdfjsLib = await ensurePdfJs();

      let doc = null;
      let lastErr = null;

      try {
        const raw = await api.filesRead(o.book.path);
        const data = toArrayBuffer(raw);
        if (data) {
          const bytes = new Uint8Array(data);
          try {
            doc = await pdfjsLib.getDocument({ data: bytes }).promise;
          } catch (err) {
            lastErr = err;
            doc = await pdfjsLib.getDocument({ data: bytes, disableWorker: true }).promise;
          }
        }
      } catch (err) {
        lastErr = err;
      }

      if (!doc) {
        const fileUrl = toFileUrl(o.book.path);
        try {
          doc = await pdfjsLib.getDocument(fileUrl).promise;
        } catch (err) {
          lastErr = err;
          doc = await pdfjsLib.getDocument({ url: fileUrl, disableWorker: true }).promise;
        }
      }

      if (!doc) throw (lastErr || new Error('pdf_open_failed'));
      state.pdf = doc;
      state.pageCount = Number(state.pdf.numPages || 0);

      const loc = (o.locator && typeof o.locator === 'object') ? o.locator : {};
      state.pageNum = Math.max(1, Math.min(state.pageCount || 1, Number(loc.page || 1)));
      state.zoom = Number.isFinite(Number(loc.zoom)) ? Number(loc.zoom) : 1;
      state.fitMode = (String(loc.fitMode || '') === 'width') ? 'width' : 'page';

      applySettings(o.settings);
      await renderPage();
    }

    async function destroy() {
      state.renderToken += 1;
      try { if (state.pdf && typeof state.pdf.destroy === 'function') state.pdf.destroy(); } catch {}
      state.pdf = null;
      state.pageCount = 0;
      state.pageNum = 1;
      resetHost();
      state.host = null;
      state.stage = null;
      state.canvas = null;
      state.status = null;
    }

    async function next() {
      if (!state.pdf) return;
      if (state.pageNum < state.pageCount) {
        state.pageNum += 1;
        await renderPage();
      }
    }

    async function prev() {
      if (!state.pdf) return;
      if (state.pageNum > 1) {
        state.pageNum -= 1;
        await renderPage();
      }
    }

    async function getLocator() {
      return {
        page: Number(state.pageNum || 1),
        zoom: Number(state.zoom || 1),
        fitMode: state.fitMode,
        scrollTop: 0,
        scrollLeft: 0,
      };
    }

    async function getToc() {
      if (!state.pdf || typeof state.pdf.getOutline !== 'function') return [];
      try {
        const outline = await state.pdf.getOutline();
        const out = [];
        for (const item of (Array.isArray(outline) ? outline : [])) {
          const title = String(item && item.title || '').trim();
          if (!title) continue;
          out.push({ label: title, dest: item.dest || null });
        }
        return out;
      } catch {
        return [];
      }
    }

    async function goTo(target) {
      if (!state.pdf || !target) return;

      if (typeof target === 'object' && Number.isFinite(Number(target.page))) {
        state.pageNum = Math.max(1, Math.min(state.pageCount, Number(target.page)));
        await renderPage();
        return;
      }

      const dest = (target && typeof target === 'object') ? target.dest : null;
      if (!dest || typeof state.pdf.getDestination !== 'function') return;

      try {
        const resolved = Array.isArray(dest) ? dest : await state.pdf.getDestination(dest);
        if (!resolved || !resolved[0]) return;
        const pageIndex = await state.pdf.getPageIndex(resolved[0]);
        if (Number.isFinite(pageIndex)) {
          state.pageNum = Math.max(1, Math.min(state.pageCount, Number(pageIndex) + 1));
          await renderPage();
        }
      } catch {}
    }

    async function search(query) {
      if (!state.pdf) return { ok: false, count: 0 };
      const q = String(query || '').trim().toLowerCase();
      if (!q) return { ok: false, count: 0 };

      for (let i = 1; i <= state.pageCount; i += 1) {
        try {
          const page = await state.pdf.getPage(i);
          const txt = await page.getTextContent();
          const str = (txt.items || []).map((x) => String(x && x.str || '')).join(' ').toLowerCase();
          if (str.includes(q)) {
            state.pageNum = i;
            await renderPage();
            return { ok: true, count: 1 };
          }
        } catch {}
      }

      return { ok: true, count: 0 };
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
      setFitMode: async (fitMode) => {
        state.fitMode = (fitMode === 'width') ? 'width' : 'page';
        await renderPage();
      },
      setZoom: async (zoom) => {
        state.zoom = Math.max(0.4, Math.min(4, Number(zoom || 1)));
        await renderPage();
      },
      getPageHint: () => ({ page: state.pageNum, pageCount: state.pageCount }),
    };
  }

  // FIX-R07: keep legacy engine available as compatibility fallback.
  window.booksReaderEngines.pdf_legacy = { create };
})();
