// Books reader engine: TXT
// WAVE2: Font family support
(function () {
  'use strict';

  window.booksReaderEngines = window.booksReaderEngines || {};

  function toFileUrl(filePath) {
    const raw = String(filePath || '');
    if (!raw) return '';
    const normalized = raw.replace(/\\/g, '/');
    if (/^[A-Za-z]:\//.test(normalized)) return `file:///${encodeURI(normalized)}`;
    if (normalized.startsWith('/')) return `file://${encodeURI(normalized)}`;
    return normalized;
  }

  function toUint8(data) {
    if (!data) return new Uint8Array(0);
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    return new Uint8Array(0);
  }

  function ensureHost(host) {
    host.innerHTML = '';
    const wrap = document.createElement('div');
    wrap.className = 'booksReaderTextWrap';
    const content = document.createElement('div');
    content.className = 'booksReaderTextDoc';
    wrap.appendChild(content);
    host.appendChild(wrap);
    return { wrap, content };
  }

  function decodeUtf8(buffer) {
    try {
      const view = toUint8(buffer);
      return new TextDecoder('utf-8').decode(view);
    } catch {
      return '';
    }
  }

  function create(api) {
    const state = {
      host: null,
      wrap: null,
      content: null,
      text: '',
      searchMarks: [],
      activeSearchIndex: -1,
    };

    function clearSearchMarks() {
      try {
        for (const mark of state.searchMarks) {
          if (!mark || !mark.parentNode) continue;
          const p = mark.parentNode;
          p.replaceChild(document.createTextNode(mark.textContent || ''), mark);
          p.normalize();
        }
      } catch {}
      state.searchMarks = [];
      state.activeSearchIndex = -1;
    }

    function setActiveSearchIndex(index) {
      if (!state.searchMarks.length) {
        state.activeSearchIndex = -1;
        return;
      }
      const prev = state.searchMarks[state.activeSearchIndex];
      if (prev) prev.classList.remove('booksSearchHitActive');

      const i = Math.max(0, Math.min(state.searchMarks.length - 1, Number(index) || 0));
      state.activeSearchIndex = i;
      const next = state.searchMarks[i];
      if (!next) return;
      next.classList.add('booksSearchHitActive');
      try { next.scrollIntoView({ block: 'center', behavior: 'smooth' }); } catch {}
    }

    function applySettings(settings) {
      if (!state.content) return;
      const s = settings || {};
      // FIX-READER-GAPS: convert percentage/factor settings to pixels
      const rawSize = Number(s.fontSize || 100);
      const fontSize = (rawSize / 100) * 18; // base 18px at 100%
      const lineHeight = Number(s.lineHeight || 1.6);
      const rawMargin = Number(s.margin || 1);
      const margin = rawMargin * 24; // base 24px at 1.0x
      const theme = String(s.theme || 'light');

      state.content.style.fontSize = `${Math.max(8, Math.min(45, fontSize))}px`;
      state.content.style.lineHeight = String(Math.max(1.0, Math.min(2.4, lineHeight)));
      state.content.style.padding = `${Math.max(0, Math.min(160, margin))}px`;

      // WAVE2: font family
      const fontFamily = String(s.fontFamily || 'serif');
      if (fontFamily === 'sans-serif') state.content.style.fontFamily = 'system-ui, -apple-system, sans-serif';
      else if (fontFamily === 'monospace') state.content.style.fontFamily = 'ui-monospace, "Cascadia Code", "Consolas", monospace';
      else state.content.style.fontFamily = 'Georgia, "Times New Roman", serif';

      const root = state.host;
      if (root) {
        root.dataset.readerTheme = theme;
      }
    }

    async function open(opts) {
      const o = (opts && typeof opts === 'object') ? opts : {};
      state.host = o.host;
      if (!state.host) throw new Error('reader_host_missing');
      const ui = ensureHost(state.host);
      state.wrap = ui.wrap;
      state.content = ui.content;

      let buffer = null;
      try {
        buffer = await api.filesRead(o.book.path);
      } catch {}
      if (!buffer) {
        const res = await fetch(toFileUrl(o.book.path));
        if (!res || !res.ok) throw new Error(`file_not_accessible: HTTP ${res ? res.status : 'unknown'}`);
        buffer = await res.arrayBuffer();
      }

      state.text = decodeUtf8(buffer);
      const blocks = String(state.text || '').split(/\r?\n\s*\r?\n/g).map((x) => x.trim()).filter(Boolean);
      if (!blocks.length) {
        state.content.textContent = '(Empty text file)';
      } else {
        const frag = document.createDocumentFragment();
        for (const block of blocks) {
          const p = document.createElement('p');
          p.textContent = block;
          frag.appendChild(p);
        }
        state.content.appendChild(frag);
      }

      applySettings(o.settings);

      const loc = (o.locator && typeof o.locator === 'object') ? o.locator : null;
      if (loc && Number.isFinite(Number(loc.scrollTop))) {
        state.wrap.scrollTop = Number(loc.scrollTop);
      }
    }

    async function destroy() {
      clearSearchMarks();
      if (state.host) state.host.innerHTML = '';
      state.host = null;
      state.wrap = null;
      state.content = null;
      state.text = '';
    }

    async function next() {
      if (!state.wrap) return;
      const step = Math.max(240, Math.floor((state.wrap.clientHeight || 700) * 0.9));
      state.wrap.scrollTop += step;
    }

    async function prev() {
      if (!state.wrap) return;
      const step = Math.max(240, Math.floor((state.wrap.clientHeight || 700) * 0.9));
      state.wrap.scrollTop = Math.max(0, state.wrap.scrollTop - step);
    }

    async function getLocator() {
      const scrollTop = state.wrap ? Math.max(0, Math.floor(state.wrap.scrollTop || 0)) : 0;
      const maxScroll = state.wrap ? Math.max(0, Math.floor((state.wrap.scrollHeight || 0) - (state.wrap.clientHeight || 0))) : 0;
      const fraction = maxScroll > 0 ? Math.max(0, Math.min(1, scrollTop / maxScroll)) : 0;
      return {
        section: 0,
        offset: 0,
        scrollTop,
        fraction,
      };
    }

    async function getToc() {
      return [];
    }

    async function goTo(target) {
      if (!state.wrap) return;
      const t = (target && typeof target === 'object') ? target : {};
      const fraction = Number(t.fraction);
      if (Number.isFinite(fraction)) {
        const f = Math.max(0, Math.min(1, fraction));
        const maxScroll = Math.max(0, (state.wrap.scrollHeight || 0) - (state.wrap.clientHeight || 0));
        state.wrap.scrollTop = Math.round(maxScroll * f);
        return;
      }
      const top = Number(t.scrollTop);
      if (Number.isFinite(top)) state.wrap.scrollTop = Math.max(0, top);
    }

    async function search(query) {
      if (!state.content) return { ok: false, count: 0, hits: [] };
      const q = String(query || '').trim();
      if (!q) {
        clearSearchMarks();
        return { ok: false, count: 0, hits: [] };
      }

      clearSearchMarks();

      const qLower = q.toLowerCase();
      const walker = document.createTreeWalker(state.content, NodeFilter.SHOW_TEXT);
      const textNodes = [];
      let node = walker.nextNode();
      while (node) {
        textNodes.push(node);
        node = walker.nextNode();
      }

      for (const textNode of textNodes) {
        const text = String(textNode.nodeValue || '');
        if (!text) continue;
        const lower = text.toLowerCase();
        if (lower.indexOf(qLower) < 0) continue;

        const frag = document.createDocumentFragment();
        let cursor = 0;
        let idx = lower.indexOf(qLower, cursor);
        while (idx >= 0) {
          if (idx > cursor) frag.appendChild(document.createTextNode(text.slice(cursor, idx)));
          const mark = document.createElement('mark');
          mark.className = 'booksSearchHit';
          mark.textContent = text.slice(idx, idx + q.length);
          frag.appendChild(mark);
          state.searchMarks.push(mark);
          cursor = idx + q.length;
          idx = lower.indexOf(qLower, cursor);
        }
        if (cursor < text.length) frag.appendChild(document.createTextNode(text.slice(cursor)));

        const parent = textNode.parentNode;
        if (parent) parent.replaceChild(frag, textNode);
      }

      const count = state.searchMarks.length;
      if (count > 0) {
        setActiveSearchIndex(0);
      }
      return {
        ok: true,
        count,
        hits: Array.from({ length: count }, (_x, i) => i),
      };
    }

    async function searchGoTo(index) {
      setActiveSearchIndex(index);
    }

    function clearSearch() {
      clearSearchMarks();
    }

    // WAVE3: get selected text from content area
    function getSelectedText() {
      const sel = window.getSelection();
      return (sel && sel.toString().trim()) || '';
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
      searchGoTo,
      clearSearch,
      applySettings,
      getSelectedText,   // WAVE3
    };
  }

  window.booksReaderEngines.txt = { create };
})();
