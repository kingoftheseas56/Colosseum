// Books reader engine: Foliate bridge (EPUB + PDF)
// WAVE1: Stability gate, flow mode, search navigation, relocate callback
// WAVE2: Font family, column mode, font size range 8-30
// RCSS_INTEGRATION: ReadiumCSS content styling integration
// ReadiumCSS (BSD-3-Clause, Readium Foundation). See THIRD_PARTY_NOTICES.md
(function () {
  'use strict';

  window.booksReaderEngines = window.booksReaderEngines || {};

  let foliateModulePromise = null;

  function ensureFoliateModule() {
    if (foliateModulePromise) return foliateModulePromise;
    const url = new URL('./vendor/foliate/view.js', window.location.href).toString();
    foliateModulePromise = import(url);
    return foliateModulePromise;
  }

  // RCSS_INTEGRATION: pre-load ReadiumCSS files as text for injection via setStyles
  let _rcssCache = { before: null, dflt: null, after: null, loaded: false };

  async function loadReadiumCSS() {
    if (_rcssCache.loaded) return _rcssCache;
    const base = new URL('./vendor/readiumcss/', window.location.href).toString();
    try {
      const [b, d, a] = await Promise.all([
        fetch(base + 'ReadiumCSS-before.css').then(r => r.text()),
        fetch(base + 'ReadiumCSS-default.css').then(r => r.text()),
        fetch(base + 'ReadiumCSS-after.css').then(r => r.text()),
      ]);
      // Fix font url() paths from relative to absolute
      const fontsUrl = base + 'fonts/';
      const fixPaths = (css) => css.replace(/url\(["']?fonts\//g, 'url("' + fontsUrl);
      _rcssCache = { before: fixPaths(b), dflt: fixPaths(d), after: fixPaths(a), loaded: true };
    } catch (e) {
      // Fallback: leave unloaded, will use legacy buildEpubStyles
      _rcssCache.loaded = false;
    }
    return _rcssCache;
  }

  function toArrayBuffer(data) {
    if (!data) return null;
    if (data instanceof ArrayBuffer) return data;
    if (ArrayBuffer.isView(data)) {
      return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    }
    return null;
  }

  function baseName(filePath) {
    const p = String(filePath || '').replace(/\\/g, '/');
    const i = p.lastIndexOf('/');
    return i >= 0 ? p.slice(i + 1) : p;
  }

  function extName(filePath) {
    const p = String(filePath || '').toLowerCase();
    const i = p.lastIndexOf('.');
    return i >= 0 ? p.slice(i + 1) : '';
  }

  function mimeForFormat(format, filePath) {
    const f = String(format || '').toLowerCase() || extName(filePath);
    if (f === 'epub') return 'application/epub+zip';
    if (f === 'pdf') return 'application/pdf';
    if (f === 'txt') return 'text/plain';
    if (f === 'mobi') return 'application/x-mobipocket-ebook';
    if (f === 'fb2') return 'application/x-fictionbook+xml';
    return 'application/octet-stream';
  }

  function toFileUrl(filePath) {
    const raw = String(filePath || '');
    if (!raw) return '';
    const normalized = raw.replace(/\\/g, '/');
    if (/^[A-Za-z]:\//.test(normalized)) return `file:///${encodeURI(normalized)}`;
    if (normalized.startsWith('/')) return `file://${encodeURI(normalized)}`;
    return normalized;
  }

  function clamp(n, min, max) {
    const v = Number(n);
    if (!Number.isFinite(v)) return min;
    return Math.max(min, Math.min(max, v));
  }

  // UI-F03: resolve TOC href to spine section index
  function resolveSpineIndex(href, sections) {
    if (!href || !Array.isArray(sections) || !sections.length) return -1;
    const base = href.split('#')[0];
    if (!base) return -1;
    for (let i = 0; i < sections.length; i++) {
      const sec = sections[i];
      const secId = String(sec && (sec.id || sec.href || sec.path || '') || '');
      if (!secId) continue;
      if (secId === base || secId === './' + base) return i;
      // Compare only file names when full path doesn't match
      const secFile = secId.split('/').pop();
      const baseFile = base.split('/').pop();
      if (secFile && baseFile && secFile === baseFile) return i;
    }
    return -1;
  }

  function flattenToc(items, out, sections, depth) {
    depth = depth || 0;
    for (const item of (Array.isArray(items) ? items : [])) {
      const label = String(item && item.label || '').trim();
      const href = String(item && item.href || '').trim();
      if (label && href) {
        const spineIndex = resolveSpineIndex(href, sections);
        out.push({ label, href, spineIndex, depth });
      }
      flattenToc(item && item.subitems, out, sections, depth + 1);
    }
  }

  function cloneLocation(loc) {
    const x = (loc && typeof loc === 'object') ? loc : null;
    if (!x) return null;
    const out = {};
    for (const k of Object.keys(x)) out[k] = x[k];
    return out;
  }

  function makeLocatorFromRelocate(detail, state) {
    const d = (detail && typeof detail === 'object') ? detail : {};

    const fraction = Number(d.fraction);
    const hasFraction = Number.isFinite(fraction);

    const pageLabelRaw = d.pageItem && d.pageItem.label;
    const pageLabel = (pageLabelRaw == null) ? null : String(pageLabelRaw);

    const indexNum = Number(d.index);
    const pageIndex = Number.isFinite(indexNum) ? (indexNum + 1) : null;

    return {
      cfi: d.cfi ? String(d.cfi) : null,
      fraction: hasFraction ? fraction : null,
      location: cloneLocation(d.location),
      tocHref: d.tocItem && d.tocItem.href ? String(d.tocItem.href) : null,
      href: d.tocItem && d.tocItem.href ? String(d.tocItem.href) : null,
      pageLabel,
      pageIndex,
      zoom: Number(state.zoom || 1),
      fitMode: String(state.fitMode || 'page'),
      format: String(state.format || ''),
      updatedAt: Date.now(),
    };
  }

  function create(api) {
    const MAX_STALE_SELECTION_MS = 7000;

    const state = {
      host: null,
      view: null,
      bookObj: null,
      format: 'epub',
      lastRelocate: null,
      zoom: 1,
      fitMode: 'page',
      settings: null,
      onRelocate: null,
      relocateCallback: null, // WAVE1: external relocate callback
      searchHits: [],          // WAVE1: all search hit CFIs
      onDblClick: null,        // FIX-R04: iframe dblclick callback
      onContextMenu: null,     // FIX-R04: iframe contextmenu callback
      onDictLookup: (api && typeof api.onDictLookup === 'function') ? api.onDictLookup : null, // FIX_DICT_R2
      onUserActivity: (api && typeof api.onUserActivity === 'function') ? api.onUserActivity : null, // FIX-R07
      lastActivityAt: 0,       // FIX-R07
      onReadingAction: (api && typeof api.onReadingAction === 'function') ? api.onReadingAction : null, // TASK2
      annotationMeta: {},      // BUILD_ANNOT: {cfi: {color, style}}
      Overlayer: null,         // BUILD_ANNOT: Overlayer class reference
      onShowAnnotationCb: null, // BUILD_ANNOT
      onCreateOverlayCb: null, // BUILD_ANNOT
      _historyChangeCb: null,  // BUILD_HIST
      lastSelectionText: '',   // FIX_DICT: cache selection captured inside iframe
      lastSelectionAt: 0,
    };

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

    // FIX_DICT_RCLICK: use Selection API word granularity to select word under cursor
    function selectWordAtPoint(doc, x, y) {
      try {
        if (!doc || typeof doc.caretRangeFromPoint !== 'function') return '';
        const range = doc.caretRangeFromPoint(x, y);
        if (!range) return '';
        const sel = doc.getSelection();
        if (!sel) return '';
        sel.removeAllRanges();
        sel.addRange(range);
        sel.modify('move', 'backward', 'word');
        sel.modify('extend', 'forward', 'word');
        const text = (sel.toString() || '').trim();
        return text;
      } catch { return ''; }
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

    // TASK2: emit reading action (scroll/page-turn) to hide HUD
    function emitReadingAction() {
      if (typeof state.onReadingAction !== 'function') return;
      try { state.onReadingAction(); } catch {}
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

    function clearHost() {
      if (state.host) state.host.innerHTML = '';
    }

    function applyPdfZoomMode() {
      if (!state.view || state.format !== 'pdf') return;
      const renderer = state.view.renderer;
      if (!renderer || typeof renderer.setAttribute !== 'function') return;

      if (state.fitMode === 'width') {
        renderer.setAttribute('zoom', 'fit-width');
        return;
      }
      if (state.fitMode === 'page') {
        renderer.setAttribute('zoom', 'fit-page');
        return;
      }
      renderer.setAttribute('zoom', String(clamp(state.zoom, 0.4, 4)));
    }

    // RCSS_INTEGRATION: build content CSS using ReadiumCSS (or legacy fallback)
    function buildEpubStyles(settings) {
      const rcss = _rcssCache;
      if (!rcss.loaded) return buildLegacyStyles(settings);
      const s = (settings && typeof settings === 'object') ? settings : {};
      // Font size applied directly on body — NOT via --USER__fontSize which triggers
      // ReadiumCSS zoom on body and breaks foliate-js paginator column calculations.
      const fontSize = clamp(Number(s.fontSize || 100), 75, 250);
      const userOverrides = fontSize !== 100
        ? ('body { font-size: ' + fontSize + '% !important; }')
        : '';
      // BUILD_JUSTIFY: prevent last-line stretch when text-align is justify
      const justifyFix = (s.textAlign === 'justify')
        ? '\np::after{content:"";display:inline-block;width:100%;visibility:hidden;}'
        : '';
      // FIX_EPUB_MARGIN: ReadiumCSS scroll-on defaults clamp the body to a max line length
      // with auto margins, which forces text into a centered column.
      // In Tankoban, the margin slider should control side padding (and allow full-width).
      const marginFactor = clamp(Number((s.margin === 0) ? 0 : (s.margin || 1)), 0, 4);
      const sidePad = Math.round(marginFactor * 24);
      const maxLineWidth = clamp(Number(s.maxLineWidth || 960), 400, 1600);
      const layoutOverrides = '\n/* TANKOBAN_MAX_LINE_WIDTH */\n' +
        'html,body{width:auto !important;}\n' +
        'body{margin:0 !important;margin-left:auto !important;margin-right:auto !important;padding-left:' + sidePad + 'px !important;padding-right:' + sidePad + 'px !important;max-width:' + maxLineWidth + 'px !important;box-sizing:border-box !important;}\n';
      return rcss.before + '\n' + rcss.dflt + '\n' + rcss.after + '\n' + userOverrides + layoutOverrides + justifyFix;
    }

    // RCSS_INTEGRATION: legacy fallback (original buildEpubStyles for when ReadiumCSS fails to load)
    function buildLegacyStyles(settings) {
      const s = (settings && typeof settings === 'object') ? settings : {};
      const fontSize = clamp(Number(s.fontSize || 100), 75, 250);
      const lineHeight = clamp(Number(s.lineHeight || 1.5), 1.0, 2.0);
      const fontWeight = clamp(Number(s.fontWeight || 400), 100, 900);
      const margin = clamp(Number((s.margin === 0) ? 0 : (s.margin || 1)), 0, 4);
      const theme = String(s.theme || 'light');
      const fontFamily = String(s.fontFamily || 'publisher');
      const isDark = theme === 'dark' || theme === 'contrast1' || theme === 'contrast2' || theme === 'contrast4';
      const isSepia = theme === 'sepia';
      const bg = isDark ? '#101216' : (isSepia ? '#f5ecd8' : '#ffffff');
      const fg = isDark ? '#e4e8ef' : (isSepia ? '#4d3d2c' : '#1a1a1a');
      let ff;
      if (fontFamily === 'sansTf' || fontFamily === 'humanistTf') ff = 'system-ui, -apple-system, sans-serif';
      else if (fontFamily === 'monospaceTf') ff = 'ui-monospace, "Cascadia Code", "Consolas", monospace';
      else ff = 'Georgia, "Times New Roman", serif';
      return 'body { font-size: ' + fontSize + '% !important; line-height: ' + lineHeight + ' !important; font-weight: ' + fontWeight + ' !important; '
        + 'padding: 0 ' + (margin * 24) + 'px !important; color: ' + fg + ' !important; background: ' + bg + ' !important; '
        + 'font-family: ' + ff + ' !important; }';
    }

    // RCSS_INTEGRATION: apply ReadiumCSS flag variables on iframe :root style attribute
    // ReadiumCSS uses [style*="readium-..."] attribute selectors, so variables must be on documentElement.style
    function applyReadiumCSSFlags(settings) {
      if (!state.view || !state.view.renderer) return;
      const s = (settings && typeof settings === 'object') ? settings : {};
      let contents;
      try {
        contents = (typeof state.view.renderer.getContents === 'function')
          ? state.view.renderer.getContents() : [];
      } catch { return; }

      for (const c of contents) {
        if (!c || !c.doc || !c.doc.documentElement) continue;
        const docEl = c.doc.documentElement;

        // Theme / appearance
        const theme = String(s.theme || 'light');
        let appearance = 'readium-default-on';
        // BUILD_THEMES: nord and gruvboxDark are dark themes
        if (theme === 'dark' || theme === 'contrast1' || theme === 'contrast2' || theme === 'contrast4' || theme === 'nord' || theme === 'gruvboxDark') appearance = 'readium-night-on';
        else if (theme === 'sepia') appearance = 'readium-sepia-on';
        docEl.style.setProperty('--USER__appearance', appearance);

        // Advanced settings always on
        docEl.style.setProperty('--USER__advancedSettings', 'readium-advanced-on');

        // View mode — always scroll-on so ReadiumCSS resets its own column layout;
        // the foliate-js paginator handles pagination, not ReadiumCSS.
        docEl.style.setProperty('--USER__view', 'readium-scroll-on');

        // Font family
        const fontFamily = String(s.fontFamily || 'publisher');
        const isPublisher = fontFamily === 'publisher' || fontFamily === '';
        docEl.style.setProperty('--USER__fontOverride', isPublisher ? 'readium-font-off' : 'readium-font-on');
        if (!isPublisher) {
          const fontMap = {
            oldStyleTf: 'var(--RS__oldStyleTf)', modernTf: 'var(--RS__modernTf)',
            sansTf: 'var(--RS__sansTf)', humanistTf: 'var(--RS__humanistTf)',
            monospaceTf: 'var(--RS__monospaceTf)',
            AccessibleDfA: 'AccessibleDfA', IAWriterDuospace: '"IA Writer Duospace"',
          };
          docEl.style.setProperty('--USER__fontFamily', fontMap[fontFamily] || fontFamily);
        } else {
          docEl.style.removeProperty('--USER__fontFamily');
        }

        // Font size — handled via stylesheet body rule (buildEpubStyles), NOT via
        // --USER__fontSize which triggers ReadiumCSS zoom on body and breaks paginator columns.
        docEl.style.removeProperty('--USER__fontSize');

        // Line height
        const lineHeight = Number(s.lineHeight || 0);
        if (lineHeight && lineHeight !== 1.5) docEl.style.setProperty('--USER__lineHeight', String(lineHeight));
        else docEl.style.removeProperty('--USER__lineHeight');

        // Font weight
        const fontWeight = clamp(Number(s.fontWeight || 400), 100, 900);
        if (fontWeight !== 400) docEl.style.setProperty('--USER__fontWeight', String(fontWeight));
        else docEl.style.removeProperty('--USER__fontWeight');

        // Max line length (ReadiumCSS)
        const maxLineWidth = clamp(Number(s.maxLineWidth || 960), 400, 1600);
        docEl.style.setProperty('--USER__maxLineLength', maxLineWidth + 'px');

        // Page margins — handled via paginator 'gap' attribute (applySettings), NOT via
        // --USER__pageMargins which triggers ReadiumCSS body padding and conflicts with paginator.
        docEl.style.removeProperty('--USER__pageMargins');

        // Column count — handled via paginator 'max-column-count' attribute (setColumnMode), NOT via
        // --USER__colCount which triggers ReadiumCSS column-count and conflicts with paginator.
        docEl.style.removeProperty('--USER__colCount');

        // Text align
        const textAlign = String(s.textAlign || '');
        if (textAlign) docEl.style.setProperty('--USER__textAlign', textAlign);
        else docEl.style.removeProperty('--USER__textAlign');

        // Letter spacing
        const letterSpacing = Number(s.letterSpacing || 0);
        if (letterSpacing) docEl.style.setProperty('--USER__letterSpacing', letterSpacing + 'rem');
        else docEl.style.removeProperty('--USER__letterSpacing');

        // Word spacing
        const wordSpacing = Number(s.wordSpacing || 0);
        if (wordSpacing) docEl.style.setProperty('--USER__wordSpacing', wordSpacing + 'rem');
        else docEl.style.removeProperty('--USER__wordSpacing');

        // Paragraph spacing
        const paraSpacing = Number(s.paraSpacing || 0);
        if (paraSpacing) docEl.style.setProperty('--USER__paraSpacing', paraSpacing + 'rem');
        else docEl.style.removeProperty('--USER__paraSpacing');

        // Paragraph indent
        const paraIndent = String(s.paraIndent || '');
        if (paraIndent) docEl.style.setProperty('--USER__paraIndent', paraIndent);
        else docEl.style.removeProperty('--USER__paraIndent');

        // Hyphens
        const bodyHyphens = String(s.bodyHyphens || '');
        if (bodyHyphens) docEl.style.setProperty('--USER__bodyHyphens', bodyHyphens);
        else docEl.style.removeProperty('--USER__bodyHyphens');

        // Extended theme colors (paper, contrast1-4)
        applyExtendedThemeColors(docEl, theme);
      }
    }

    // RCSS_INTEGRATION: custom bg/fg for extended themes not built into ReadiumCSS
    function applyExtendedThemeColors(docEl, theme) {
      const custom = {
        paper:      { bg: '#f8f4ec', fg: '#2c2c2c' },
        contrast1:  { bg: '#000000', fg: '#ffff00' },
        contrast2:  { bg: '#0a0a3e', fg: '#ffd700' },
        contrast3:  { bg: '#f5f5dc', fg: '#1a1a00' },
        contrast4:  { bg: '#1a1a2e', fg: '#e0e0e0' },
        // BUILD_THEMES: named literary themes
        nord:        { bg: '#2e3440', fg: '#d8dee9' },
        gruvbox:     { bg: '#fbf1c7', fg: '#3c3836' },
        gruvboxDark: { bg: '#282828', fg: '#ebdbb2' },
        solarized:   { bg: '#fdf6e3', fg: '#657b83' },
      };
      const ct = custom[theme];
      if (ct) {
        docEl.style.setProperty('--RS__backgroundColor', ct.bg);
        docEl.style.setProperty('--RS__textColor', ct.fg);
      } else {
        docEl.style.removeProperty('--RS__backgroundColor');
        docEl.style.removeProperty('--RS__textColor');
      }
    }

    function applySettings(settings) {
      const s = (settings && typeof settings === 'object') ? settings : {};
      state.settings = s;

      const theme = String(s.theme || 'light');
      if (state.host) state.host.dataset.readerTheme = theme;

      if (!state.view) return;

      if (state.format === 'epub' || state.format === 'mobi' || state.format === 'fb2') {
        // BOOK_FIX 2.1: single canonical style-application path. Pre-2.1 this
        // function ran FIVE overlapping layers against every setting change:
        //   A. setStyles(buildEpubStyles) — stylesheet with body !important rules
        //   B. applyReadiumCSSFlags — --USER__ CSS custom properties on :root
        //   C. Direct doc.body.style.setProperty via renderer.getContents()
        //   D. Direct doc.body.style.setProperty via renderer.element document
        //   E. Paginator attributes (gap / max-inline-size / margin)
        //   F. A delayed 120ms re-setStyles to compensate for QWebEngine layout
        //      timing (expand() running before first setStyles CSS commit).
        //
        // C + D wrote the same body styles Layer A already covers with
        // !important stylesheet rules — redundant three-way overlap that caused
        // 2-3 reflows per setting change and body-style leakage across reopen.
        // F was semantically correct but magic-number-based.
        //
        // Post-2.1: A + B + E only (each addresses a non-overlapping concern —
        // A for body typography/layout, B for per-document CSS vars consumed
        // by ReadiumCSS, E for paginator config foliate-js reads as element
        // attributes, not CSS). F is replaced with requestAnimationFrame×2 so
        // the re-apply runs exactly when layout has committed instead of on a
        // magic 120ms timer.
        try {
          const renderer = state.view.renderer;
          if (renderer && typeof renderer.setStyles === 'function') {
            renderer.setStyles(buildEpubStyles(s));
          }
          // Layer B — ReadiumCSS flag vars cover theme / fontFamily / lineHeight
          // / fontWeight / textAlign / letterSpacing / wordSpacing / paraSpacing
          // / paraIndent / bodyHyphens / extended-theme colors.
          applyReadiumCSSFlags(s);

          // Layer E — paginator config. These are foliate-js element attributes
          // (not CSS), so they can't live inside setStyles. Guard redundant
          // writes: only re-set if the value changed since last applySettings.
          if (renderer && typeof renderer.setAttribute === 'function') {
            const margin  = clamp(Number((s.margin === 0) ? 0 : (s.margin || 1)), 0, 4);
            const gapPct  = Math.max(2, Math.round(margin * 7));
            const gapAttr = gapPct + '%';
            if (renderer.getAttribute('gap') !== gapAttr) {
              renderer.setAttribute('gap', gapAttr);
            }
            if (renderer.getAttribute('max-inline-size') !== '9999px') {
              renderer.setAttribute('max-inline-size', '9999px');
            }
            if (renderer.getAttribute('margin') !== '64px') {
              renderer.setAttribute('margin', '64px');
            }
          }
        } catch {}

        // QWebEngine layout-commit fix (supersedes the old 120ms setTimeout).
        // The first setStyles above triggers expand() inside foliate-js to
        // recompute column widths, but QWebEngine's CSS layout commit is async
        // — expand() measures against stale layout. Two animation frames later
        // layout has committed; re-apply so the paginator measures correctly.
        // Cheaper than setTimeout(..., 120) and semantically what we want.
        try {
          const raf = window.requestAnimationFrame || function (cb) { return setTimeout(cb, 16); };
          raf(function () {
            raf(function () {
              try {
                if (state.view && state.view.renderer && typeof state.view.renderer.setStyles === 'function') {
                  state.view.renderer.setStyles(buildEpubStyles(s));
                }
              } catch (e) {}
            });
          });
        } catch {}
      }

      if (state.format === 'pdf') applyPdfZoomMode();
    }

    // WAVE1: stability gate — typed-array first, fetch fallback, error classification
    async function makeFileForBook(book) {
      let ab = null;
      try {
        const raw = await api.filesRead(book.path);
        ab = toArrayBuffer(raw);
      } catch (readErr) {
        // IPC read failed; will try fetch fallback
      }

      if (ab) {
        if (ab.byteLength === 0) {
          throw new Error(`file_empty: "${baseName(book.path)}" is empty (0 bytes).`);
        }
        return new File([ab], baseName(book.path), {
          type: mimeForFormat(book.format, book.path),
        });
      }

      let res;
      try {
        res = await fetch(toFileUrl(book.path));
      } catch (fetchErr) {
        throw new Error(`file_not_accessible: Cannot read "${baseName(book.path)}". Check the file exists and is readable.`);
      }
      if (!res.ok) {
        throw new Error(`file_not_accessible: HTTP ${res.status} for "${baseName(book.path)}".`);
      }
      const blob = await res.blob();
      if (!blob || blob.size === 0) {
        throw new Error(`file_empty: "${baseName(book.path)}" is empty.`);
      }
      return new File([blob], baseName(book.path), {
        type: blob.type || mimeForFormat(book.format, book.path),
      });
    }

    function locatorToNavigationTarget(locator) {
      const loc = (locator && typeof locator === 'object') ? locator : null;
      if (!loc) return null;

      if (loc.cfi) return String(loc.cfi);

      const frac = Number(loc.fraction);
      if (Number.isFinite(frac) && frac >= 0 && frac <= 1) {
        return { fraction: frac };
      }

      if (loc.tocHref) return String(loc.tocHref);
      if (loc.href) return String(loc.href);

      const pageIndex = Number(loc.pageIndex);
      if (Number.isFinite(pageIndex) && pageIndex > 0) return pageIndex - 1;

      const page = Number(loc.page);
      if (Number.isFinite(page) && page > 0) return page - 1;

      const pageLabel = Number(loc.pageLabel);
      if (Number.isFinite(pageLabel) && pageLabel > 0) return pageLabel - 1;

      return null;
    }

    // FIX_DICT_LOAD: bind all events on a single iframe document
    function bindDocEvents(doc) {
      if (!doc || doc._tankoEvBound) return;
      doc._tankoEvBound = true;

      // BUILD_COVER: inject full-viewport layout for epub:type="cover" sections
      var epubType = '';
      if (doc.body) {
        epubType = doc.body.getAttribute('epub:type') ||
                   doc.body.getAttributeNS('http://www.idpf.org/2007/ops', 'type') || '';
      }
      if (epubType.split(/\s+/).indexOf('cover') >= 0) {
        var coverStyle = doc.createElement('style');
        coverStyle.textContent =
          'html,body{margin:0!important;padding:0!important;height:100%!important;}' +
          'body{display:flex!important;align-items:center!important;justify-content:center!important;background:#000!important;}' +
          'body img,body svg{max-width:100%!important;max-height:100vh!important;width:auto!important;height:auto!important;object-fit:contain!important;}';
        doc.head.appendChild(coverStyle);
      }

      const rememberFromDoc = () => { rememberSelectionText(doc, false); };
      const rememberBeforeContext = (ev) => {
        try { if (ev && ev.button === 2) rememberSelectionText(doc, false); } catch {}
      };
      doc.addEventListener('mousemove', emitUserActivity, { passive: true });
      doc.addEventListener('pointermove', emitUserActivity, { passive: true });
      doc.addEventListener('pointerdown', emitUserActivity, { passive: true });
      doc.addEventListener('pointerdown', rememberBeforeContext, true);
      doc.addEventListener('touchstart', emitUserActivity, { passive: true });
      doc.addEventListener('click', (ev) => {
        emitUserActivity(ev);
        // Single left click: hide dictionary popup and selection menu
        try {
          var Dict = window.booksReaderDict;
          if (Dict && typeof Dict.hideDictPopup === 'function') Dict.hideDictPopup();
          if (Dict && typeof Dict.hideSelMenu === 'function') Dict.hideSelMenu();
        } catch (e) {}
      });
      doc.addEventListener('selectionchange', rememberFromDoc);
      doc.addEventListener('mouseup', rememberFromDoc, { passive: true });
      doc.addEventListener('mouseup', (ev) => {
        // Show selection context menu only when user drag-selects text
        setTimeout(() => {
          try {
            var sel = doc.getSelection();
            if (!sel || sel.isCollapsed || !sel.toString().trim()) return;
            var Dict = window.booksReaderDict;
            if (Dict && typeof Dict.showSelMenu === 'function') Dict.showSelMenu(ev);
          } catch (e) {}
        }, 80);
      }, { passive: true });
      doc.addEventListener('keyup', rememberFromDoc);
      doc.addEventListener('touchend', rememberFromDoc, { passive: true });
      doc.addEventListener('dblclick', (ev) => {
        emitUserActivity(ev);
        // Double-click selects a word (browser default) — show dictionary menu
        setTimeout(() => {
          try {
            var sel = doc.getSelection();
            if (!sel || sel.isCollapsed || !sel.toString().trim()) return;
            var Dict = window.booksReaderDict;
            if (Dict && typeof Dict.showSelMenu === 'function') Dict.showSelMenu(ev);
          } catch (e) {}
        }, 50);
      });
      doc.addEventListener('contextmenu', (ev) => {
        emitUserActivity(ev);
        // Right-click away from text: hide dictionary
        try {
          var Dict0 = window.booksReaderDict;
          if (Dict0 && typeof Dict0.hideDictPopup === 'function') Dict0.hideDictPopup();
        } catch (e) {}
        // FIX_DICT_RCLICK: if no text selected, programmatically select word under cursor
        if (!selectionTextFromDoc(doc)) {
          selectWordAtPoint(doc, ev.clientX, ev.clientY);
        }
        const text = rememberSelectionText(doc, false) || recentSelectionText();
        const word = normalizeLookupWord(text);
        if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();
        if (word) {
          try {
            const Dict = window.booksReaderDict;
            if (Dict && typeof Dict.triggerDictLookupFromText === 'function') {
              Dict.triggerDictLookupFromText(word, ev);
            }
          } catch (e) { /* dict lookup failed silently */ }
          return;
        }
        if (!dispatchDictLookup(doc, ev) && typeof state.onContextMenu === 'function') {
          state.onContextMenu(ev, text);
        }
      }, true);
      doc.addEventListener('keydown', (ev) => {
        emitUserActivity(ev);
        if (isPlainDKey(ev)) {
          if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();
          if (ev && typeof ev.stopPropagation === 'function') ev.stopPropagation();
          if (!dispatchDictLookup(doc, ev)) {
            const text = rememberSelectionText(doc, false) || recentSelectionText();
            if (typeof state.onDblClick === 'function') state.onDblClick(text, ev);
          }
          return;
        }
        // BOOK_FIX 3.1: route directly into the central dispatcher instead of
        // dispatching a synthetic parent-document KeyboardEvent. The synthetic
        // path loses the link between the parent's preventDefault and the
        // iframe's native default (Space scrolling, arrow caret moves), so the
        // user could see a page flip AND iframe scroll from the same keypress.
        // Direct call: both paths (parent listener + this iframe listener)
        // share one decision tree, and `handled`→preventDefault on the iframe
        // event stops iframe native behavior cleanly.
        try {
          const Kb = window.booksReaderKeyboard;
          if (Kb && typeof Kb.handleKeyEvent === 'function') {
            const handled = Kb.handleKeyEvent(ev);
            if (handled && typeof ev.preventDefault === 'function') ev.preventDefault();
            if (handled && typeof ev.stopPropagation === 'function') ev.stopPropagation();
          }
        } catch (_e) {}
      }, true);
      // TASK2: scroll inside content = reading action, hides HUD
      if (doc.defaultView && !doc.defaultView._tankoScrollBound) {
        doc.defaultView._tankoScrollBound = true;
        doc.defaultView.addEventListener('scroll', emitReadingAction, { passive: true });
      }
    }

    // FIX-R04: register dblclick/contextmenu handlers inside EPUB iframe
    function bindIframeEvents() {
      if (!state.view || !state.view.renderer) return;
      try {
        const renderer = state.view.renderer;
        const contents = (typeof renderer.getContents === 'function') ? renderer.getContents() : [];
        for (const c of contents) {
          if (c && c.doc) bindDocEvents(c.doc);
        }
        const el = renderer.element;
        if (el && el.contentDocument) bindDocEvents(el.contentDocument);
      } catch {}
    }

    async function open(opts) {
      const o = (opts && typeof opts === 'object') ? opts : {};
      const book = (o.book && typeof o.book === 'object') ? o.book : null;
      if (!book || !book.path) throw new Error('invalid_book');

      state.host = o.host;
      if (!state.host) throw new Error('reader_host_missing');

      state.format = String(book.format || extName(book.path) || '').toLowerCase();
      state.zoom = clamp(Number(o.locator && o.locator.zoom || 1), 0.4, 4);
      state.fitMode = String(o.locator && o.locator.fitMode || 'page');
      if (!['page', 'width', 'manual'].includes(state.fitMode)) state.fitMode = 'page';
      state.searchHits = [];

      clearHost();

      await ensureFoliateModule();
      // RCSS_INTEGRATION: load ReadiumCSS files before rendering
      await loadReadiumCSS();

      const file = await makeFileForBook(book);

      const view = document.createElement('foliate-view');
      view.className = 'booksFoliateView';
      state.host.appendChild(view);
      state.view = view;

      view.addEventListener('mousemove', emitUserActivity, { passive: true });
      view.addEventListener('pointermove', emitUserActivity, { passive: true });
      view.addEventListener('pointerdown', emitUserActivity, { passive: true });
      view.addEventListener('touchstart', emitUserActivity, { passive: true });
      view.addEventListener('click', emitUserActivity, { passive: true });
      view.addEventListener('scroll', emitReadingAction, { passive: true }); // TASK2

      // WAVE1: relocate handler calls external callback for TOC + progress
      state.onRelocate = (ev) => {
        state.lastRelocate = ev && ev.detail ? ev.detail : null;
        bindIframeEvents(); // FIX-R04: re-register iframe events after section change
        // RCSS_INTEGRATION: re-apply ReadiumCSS flags after section change (fresh iframe)
        if (state.settings && (state.format === 'epub' || state.format === 'mobi' || state.format === 'fb2')) {
          try { applyReadiumCSSFlags(state.settings); } catch {}
        }
        emitReadingAction(); // TASK2: page turn = reading action, hides HUD
        if (typeof state.relocateCallback === 'function') {
          try { state.relocateCallback(state.lastRelocate); } catch {}
        }
      };
      view.addEventListener('relocate', state.onRelocate);

      // FIX_DICT_LOAD: bind iframe events when section loads (proven reliable via foliate-js load event)
      view.addEventListener('load', (e) => {
        const detail = e && e.detail;
        if (!detail || !detail.doc) return;
        bindDocEvents(detail.doc);
      });

      // BUILD_ANNOT: import Overlayer for annotation draw functions
      try {
        const overlayerUrl = new URL('./vendor/foliate/overlayer.js', window.location.href).toString();
        const { Overlayer: OverlayerClass } = await import(overlayerUrl);
        state.Overlayer = OverlayerClass;
      } catch {}

      // BUILD_ANNOT: wire draw-annotation event (applies color + style to overlays)
      view.addEventListener('draw-annotation', (ev) => {
        const { draw, annotation } = ev.detail || {};
        if (!draw || !annotation) return;
        const meta = state.annotationMeta[annotation.value];
        const color = (meta && meta.color) || '#FEF3BD';
        const style = (meta && meta.style) || 'highlight';
        const O = state.Overlayer;
        if (!O) return;
        const fn = style === 'underline' ? O.underline
          : style === 'strikethrough' ? O.strikethrough
          : style === 'outline' ? O.outline
          : O.highlight;
        draw(fn, { color });
      });

      // BUILD_ANNOT: wire show-annotation click + create-overlay section-change events
      view.addEventListener('show-annotation', (ev) => {
        if (typeof state.onShowAnnotationCb === 'function') {
          try { state.onShowAnnotationCb(ev.detail); } catch {}
        }
      });
      view.addEventListener('create-overlay', (ev) => {
        if (typeof state.onCreateOverlayCb === 'function') {
          try { state.onCreateOverlayCb(ev.detail); } catch {}
        }
      });

      // BUILD_HIST: wire history change listener
      if (view.history) {
        view.history.addEventListener('index-change', () => {
          if (typeof state._historyChangeCb === 'function') {
            try {
              state._historyChangeCb({
                canGoBack: view.history.canGoBack,
                canGoForward: view.history.canGoForward,
              });
            } catch {}
          }
        });
      }

      await view.open(file);
      state.bookObj = view.book || null;

      applySettings(o.settings || {});

      // Respect the user's saved flow mode (paginated or scrolled). The toggle
      // button persists `settings.flowMode` per-book; honor it on reopen so
      // scroll-mode readers don't snap back to paginated at chapter start.
      if (state.format === 'epub' || state.format === 'mobi' || state.format === 'fb2') {
        try {
          if (state.view.renderer && typeof state.view.renderer.setAttribute === 'function') {
            var savedFlow = String((o.settings && o.settings.flowMode) || 'paginated');
            state.view.renderer.setAttribute('flow', savedFlow === 'scrolled' ? 'scrolled' : 'paginated');
          }
        } catch {}
      }

      // BUILD_CHAP_TRANS: wire section-boundary pause event
      state.view.renderer.addEventListener('section-boundary', function(ev) {
        if (typeof state._sectionBoundaryCb === 'function') {
          try { state._sectionBoundaryCb(ev.detail); } catch {}
        }
      });

      const target = locatorToNavigationTarget(o.locator);

      // BOOK_FIX 1.3: subscribe BEFORE init() so we don't miss a fast stabilized
      // emission on small books. Foliate's renderer fires `stabilized` every
      // time pagination settles — initial layout + any reflow after a setStyles
      // change. applySettings (post-2.1) re-applies via requestAnimationFrame×2
      // to work around QWebEngine's async CSS-layout-commit, which triggers a
      // SECOND stabilized shortly after init. We must catch the last one, not
      // the first, or the reader reveals during the RAF-deferred reflow and
      // the user sees the shimmer the whole gate is meant to prevent.
      //
      // Debounce: on each stabilized, arm a 220ms timer; reset it on any new
      // stabilized within the window. Fire the bridge once the bus goes quiet.
      // 220ms is comfortably larger than the 2-RAF gap (~32ms at 60fps) so the
      // second stabilized always lands inside the window.
      let readyFired = false;
      let settleTimer = null;
      const fireReady = function () {
        if (readyFired) return;
        readyFired = true;
        try {
          if (window.__ebookNav && typeof window.__ebookNav.markReaderReady === 'function') {
            window.__ebookNav.markReaderReady();
          }
        } catch (e) { /* swallow */ }
      };
      const onStabilized = function () {
        if (readyFired) return;
        if (settleTimer) clearTimeout(settleTimer);
        settleTimer = setTimeout(fireReady, 220);
      };
      try {
        if (state.view.renderer && typeof state.view.renderer.addEventListener === 'function') {
          state.view.renderer.addEventListener('stabilized', onStabilized);
        }
      } catch {}

      await view.init({
        lastLocation: target,
        showTextStart: true,
      });

      // FIX_DICT: ensure iframe handlers are attached even if relocate is delayed.
      bindIframeEvents();

      if (!state.lastRelocate) {
        try { state.lastRelocate = view.lastLocation || null; } catch {}
      }

      if (state.format === 'pdf') applyPdfZoomMode();

      // JS-side fallback — some renderer configurations (PDF, older foliate-js)
      // never emit `stabilized`. The C++ 5s watchdog is the hard backstop, but
      // 5s of black overlay is jarring; fire readiness 700ms after init()
      // resolves if no stabilized has been observed at all. If stabilized is
      // armed, the debounce wins and this no-op's.
      try {
        setTimeout(function () {
          if (!readyFired && !settleTimer) fireReady();
        }, 700);
      } catch {}

      // BOOK_FIX 2.2: expose a relayout hook the C++ resize-debounce (200ms
      // after last resizeEvent) can call. Forces Foliate's paginator to
      // recompute columns/margins against the current container size — the
      // embedded ResizeObserver theoretically handles this, but QWebEngine's
      // iframe→viewport propagation is lossy enough that explicit render()
      // is more reliable. Flash protection: drop renderer opacity to 0 before
      // render(), restore on the next stabilized (via a reused debounce).
      window.__ebookRelayout = function () {
        try {
          if (!state.view || !state.view.renderer) return;
          const r = state.view.renderer;

          // Hide pre-reflow to mask intermediate columns.
          try {
            if (r.style) r.style.opacity = '0';
            else if (typeof r.setAttribute === 'function') r.setAttribute('style', 'opacity:0');
          } catch {}

          // Listen for the next stabilized-quiet and restore visibility. One-
          // shot; reinstalled on every relayout call. Uses the same 220ms
          // settle window as the open-time gate for consistent UX.
          let relayoutSettle = null;
          let relayoutDone = false;
          const finish = function () {
            if (relayoutDone) return;
            relayoutDone = true;
            try { r.removeEventListener('stabilized', onRelayoutStable); } catch {}
            try {
              if (r.style) r.style.opacity = '';
              else if (typeof r.removeAttribute === 'function') r.removeAttribute('style');
            } catch {}
          };
          const onRelayoutStable = function () {
            if (relayoutDone) return;
            if (relayoutSettle) clearTimeout(relayoutSettle);
            relayoutSettle = setTimeout(finish, 220);
          };
          try { r.addEventListener('stabilized', onRelayoutStable); } catch {}

          // Backstop — if stabilized never fires post-render (some PDF paths),
          // restore opacity after 800ms anyway.
          setTimeout(finish, 800);

          // Trigger the re-layout.
          try {
            if (typeof r.render === 'function') r.render();
            else if (typeof r.size === 'function') r.size();
          } catch {}
        } catch {}
      };
    }

    async function destroy() {
      if (state.view && state.onRelocate) {
        try { state.view.removeEventListener('relocate', state.onRelocate); } catch {}
      }

      // BOOK_FIX 2.2: clear the relayout hook so a late C++ resize-debounce
      // firing after destroy() doesn't call into a torn-down renderer.
      try { if (window.__ebookRelayout) delete window.__ebookRelayout; } catch {}

      try { if (state.view && typeof state.view.close === 'function') state.view.close(); } catch {}
      try { if (state.bookObj && typeof state.bookObj.destroy === 'function') state.bookObj.destroy(); } catch {}
      try { if (state.view) state.view.remove(); } catch {}

      clearHost();

      state.host = null;
      state.view = null;
      state.bookObj = null;
      state.lastRelocate = null;
      state.onRelocate = null;
      state.relocateCallback = null;
      state.lastActivityAt = 0;
      state.zoom = 1;
      state.fitMode = 'page';
      state.settings = null;
      state.searchHits = [];
      state.onDblClick = null;     // FIX-R04
      state.onContextMenu = null;  // FIX-R04
      state.onDictLookup = null;   // FIX_DICT_R2
      state.onUserActivity = null; // FIX-R07
      state._sectionBoundaryCb = null; // BUILD_CHAP_TRANS
      state.lastSelectionText = '';
      state.lastSelectionAt = 0;
    }

    async function next() {
      if (!state.view || typeof state.view.next !== 'function') return;
      await state.view.next();
    }

    async function prev() {
      if (!state.view || typeof state.view.prev !== 'function') return;
      await state.view.prev();
    }

    async function getLocator() {
      const detail = state.lastRelocate || (state.view && state.view.lastLocation) || null;
      return makeLocatorFromRelocate(detail, state);
    }

    async function getToc() {
      const out = [];
      const sections = (state.bookObj && Array.isArray(state.bookObj.sections)) ? state.bookObj.sections : [];
      flattenToc(state.bookObj && state.bookObj.toc, out, sections);
      return out;
    }

    async function goTo(target) {
      if (!state.view || typeof state.view.goTo !== 'function') return;
      if (!target) return;

      if (typeof target === 'string') {
        await state.view.goTo(target);
        return;
      }

      if (Number.isFinite(Number(target.page)) && Number(target.page) > 0) {
        await state.view.goTo(Number(target.page) - 1);
        return;
      }

      if (target.cfi) {
        await state.view.goTo(String(target.cfi));
        return;
      }

      if (target.href) {
        await state.view.goTo(String(target.href));
        return;
      }

      if (Number.isFinite(Number(target.fraction))) {
        const fr = Number(target.fraction);
        if (typeof state.view.goToFraction === 'function') {
          await state.view.goToFraction(fr);
        } else if (typeof state.view.goTo === 'function') {
          await state.view.goTo({ fraction: fr });
        }
      }
    }

    // WAVE1: search collects hits + excerpts for results list
    async function search(query, options) {
      state.searchHits = [];
      state.searchGroups = [];
      if (!state.view || typeof state.view.search !== 'function') return { ok: false, count: 0, hits: [], groups: [] };

      const q = String(query || '').trim();
      if (!q) return { ok: false, count: 0, hits: [], groups: [] };
      const searchOpts = (options && typeof options === 'object') ? options : {};

      const flatHits = []; // [{cfi, excerpt, label}]
      const groups = [];   // [{label, subitems:[{cfi, excerpt}]}]

      const pushMatch = (label, it) => {
        const cfi = String(it && it.cfi || '');
        if (!cfi) return;
        const excerpt = (it && it.excerpt && typeof it.excerpt === 'object') ? it.excerpt : null;
        flatHits.push({ cfi, excerpt, label: String(label || '') });
      };

      try {
        for await (const res of state.view.search({ query: q, matchCase: !!searchOpts.matchCase, wholeWords: !!searchOpts.wholeWords })) {
          if (res === 'done') break;

          // Grouped results (Foliate returns sections with subitems)
          if (res && Array.isArray(res.subitems)) {
            const label = String(res.label || '');
            const subitems = [];
            for (const it of res.subitems) {
              const cfi = String(it && it.cfi || '');
              if (!cfi) continue;
              const excerpt = (it && it.excerpt && typeof it.excerpt === 'object') ? it.excerpt : null;
              subitems.push({ cfi, excerpt });
              pushMatch(label, it);
            }
            if (subitems.length) groups.push({ label, subitems });
            continue;
          }

          // Flat result (older/alternate implementations)
          const cfi = String(res && res.cfi || '');
          if (cfi) {
            flatHits.push({ cfi, excerpt: null, label: '' });
          }
        }
      } catch {
        return { ok: false, count: 0, hits: [], groups: [] };
      }

      // Store flattened CFIs for prev/next navigation
      state.searchHits = flatHits.map(x => x.cfi);
      state.searchGroups = groups;

      if (state.searchHits.length > 0) {
        try { await state.view.goTo(state.searchHits[0]); } catch {}
      }

      return { ok: true, count: state.searchHits.length, hits: state.searchHits, groups, flat: flatHits };
    }

    // WAVE1: navigate to specific search hit by index
    async function searchGoTo(index) {
      if (!state.view || !state.searchHits.length) return;
      const i = Math.max(0, Math.min(state.searchHits.length - 1, index));
      const cfi = state.searchHits[i];
      if (cfi) {
        try { await state.view.goTo(cfi); } catch {}
      }
    }

    // UI-F04: clear search state and highlights
    function clearSearch() {
      state.searchHits = [];
      // Foliate view may expose clearSearch to remove annotation overlays
      try {
        if (state.view && typeof state.view.clearSearch === 'function') {
          state.view.clearSearch();
        }
      } catch {}
    }

    // Set flow mode for EPUB: 'paginated' or 'scrolled'
    function setFlowMode(mode) {
      if (state.format === 'pdf') return;
      if (!state.view || !state.view.renderer) return;
      var val = (mode === 'scrolled') ? 'scrolled' : 'paginated';
      try {
        state.view.renderer.setAttribute('flow', val);
      } catch {}
    }

    // WAVE2: set column mode (single or spread/auto)
    function setColumnMode(mode) {
      if (state.format === 'pdf') return;
      if (!state.view || !state.view.renderer) return;
      try {
        const val = (mode === 'single') ? '1' : '2';
        state.view.renderer.setAttribute('max-column-count', val);
      } catch {}
    }

    // WAVE1: register relocate event callback
    function onRelocateEvent(cb) {
      state.relocateCallback = (typeof cb === 'function') ? cb : null;
    }

    async function setFitMode(fitMode) {
      if (state.format !== 'pdf') return;
      state.fitMode = (String(fitMode || '').toLowerCase() === 'width') ? 'width' : 'page';
      applyPdfZoomMode();
    }

    async function setZoom(zoom) {
      if (state.format !== 'pdf') return;
      state.zoom = clamp(Number(zoom || 1), 0.4, 4);
      state.fitMode = 'manual';
      applyPdfZoomMode();
    }

    // WAVE3: get selected text from renderer iframe or outer window
    function getSelectedText() {
      try {
        const renderer = state.view && state.view.renderer;
        if (renderer) {
          // Try accessing the content iframe document
          const el = renderer.element;
          if (el && el.contentDocument) {
            const sel = el.contentDocument.getSelection();
            if (sel && sel.toString().trim()) return sel.toString().trim();
          }
          // Try getContents() array (some Foliate versions)
          if (typeof renderer.getContents === 'function') {
            for (const c of renderer.getContents()) {
              if (c && c.doc) {
                const s = c.doc.getSelection();
                if (s && s.toString().trim()) return s.toString().trim();
              }
            }
          }
        }
      } catch {}
      const sel = window.getSelection();
      const text = (sel && sel.toString().trim()) || '';
      if (text) return text;
      return recentSelectionText();
    }

    // BUILD_CHAP: Get section fraction positions for chapter markers on scrubber
    function getSectionFractions() {
      if (!state.view || typeof state.view.getSectionFractions !== 'function') return [];
      try { return state.view.getSectionFractions(); } catch { return []; }
    }

    // BUILD_HIST: Navigation history — expose Foliate's built-in History class
    function historyBack() {
      if (!state.view || !state.view.history) return;
      try { state.view.history.back(); } catch {}
    }

    function historyForward() {
      if (!state.view || !state.view.history) return;
      try { state.view.history.forward(); } catch {}
    }

    function historyCanGoBack() {
      return !!(state.view && state.view.history && state.view.history.canGoBack);
    }

    function historyCanGoForward() {
      return !!(state.view && state.view.history && state.view.history.canGoForward);
    }

    function onHistoryChange(cb) {
      state._historyChangeCb = typeof cb === 'function' ? cb : null;
      // Listener bound in open() after view creation
    }

    // BUILD_ANNOT: Get CFI + text for current text selection in iframe
    function getSelectionCFI() {
      try {
        const renderer = state.view && state.view.renderer;
        if (!renderer || typeof renderer.getContents !== 'function') return null;
        const contents = renderer.getContents();
        for (const c of contents) {
          if (!c || !c.doc) continue;
          const sel = c.doc.getSelection();
          if (!sel || !sel.rangeCount || sel.isCollapsed) continue;
          const range = sel.getRangeAt(0);
          const text = sel.toString().trim();
          if (!text) continue;
          const cfi = state.view.getCFI(c.index, range);
          return { cfi, text, index: c.index };
        }
      } catch {}
      return null;
    }

    // BUILD_ANNOT: Annotation overlay management
    function addAnnotation(annotation) {
      if (!state.view || typeof state.view.addAnnotation !== 'function') return;
      try { state.view.addAnnotation(annotation); } catch {}
    }

    function deleteAnnotation(annotation) {
      if (!state.view || typeof state.view.deleteAnnotation !== 'function') return;
      try { state.view.deleteAnnotation(annotation); } catch {}
    }

    function showAnnotation(annotation) {
      if (!state.view || typeof state.view.showAnnotation !== 'function') return;
      try { state.view.showAnnotation(annotation); } catch {}
    }

    function setAnnotationMeta(cfi, meta) {
      state.annotationMeta[String(cfi)] = meta;
    }

    function removeAnnotationMeta(cfi) {
      delete state.annotationMeta[String(cfi)];
    }

    function getPageHint() {
      const loc = state.lastRelocate || (state.view && state.view.lastLocation) || null;
      const page = Number(loc && loc.pageItem && loc.pageItem.label);
      const fallback = Number(loc && loc.index);
      const pageCount = Array.isArray(state.bookObj && state.bookObj.sections)
        ? state.bookObj.sections.length
        : 0;

      return {
        page: Number.isFinite(page) ? page : (Number.isFinite(fallback) ? (fallback + 1) : 0),
        pageCount,
      };
    }

    // Per-section page info (layout-dependent — changes with font size / line gap)
    function getSectionPageInfo() {
      if (!state.view || !state.view.renderer) return null;
      const r = state.view.renderer;
      if (r.scrolled) return null; // scroll mode has no discrete pages
      const totalSlots = r.pages;
      const currentSlot = r.page;
      if (typeof totalSlots !== 'number' || typeof currentSlot !== 'number') return null;
      if (totalSlots < 3) return null; // need at least 1 content page + 2 boundary slots
      const contentPages = totalSlots - 2;
      const currentPage = Math.max(1, Math.min(currentSlot, contentPages));
      return { current: currentPage, total: contentPages };
    }

    // BUILD_CHAP_TRANS: pause-at-boundary and advance APIs
    function setPauseBoundary(enabled) {
      if (!state.view || !state.view.renderer) return;
      state.view.renderer.pauseAtBoundary = !!enabled;
    }
    function advanceSection(dir) {
      if (!state.view || !state.view.renderer) return;
      return state.view.renderer.advanceToSection(dir);
    }
    function onSectionBoundary(cb) {
      state._sectionBoundaryCb = typeof cb === 'function' ? cb : null;
    }
    // FIX_CHAP_NAV: explicit chapter-level navigation (skips boundary pause)
    function nextSection() {
      if (!state.view || !state.view.renderer) return;
      return state.view.renderer.nextSection();
    }
    function prevSection() {
      if (!state.view || !state.view.renderer) return;
      return state.view.renderer.prevSection();
    }

    // BOOK_FIX 5.2: the old `scrollContent(delta)` helper was removed as an
    // unused API, NOT because scrolled flow mode was dropped. Scrolled flow is
    // still fully supported — user toggles via the flow button (toolbar) or
    // settings.flowMode='scrolled'; foliate-js's paginator switches layout
    // mode via renderer.setAttribute('flow', 'scrolled'). Native iframe scroll
    // handles content motion; Foliate's `relocate` event fires on scroll and
    // drives progress updates.

    return {
      open,
      destroy,
      next,
      prev,
      getLocator,
      getToc,
      goTo,
      search,
      searchGoTo,       // WAVE1
      clearSearch,      // UI-F04
      applySettings,
      setFitMode,
      setZoom,
      getPageHint,
      getSectionPageInfo,
      setFlowMode,       // WAVE1
      setColumnMode,     // WAVE2
      onRelocateEvent,   // WAVE1
      getSelectedText,   // WAVE3
      // BUILD_CHAP
      getSectionFractions,
      // BUILD_CHAP_TRANS
      setPauseBoundary,
      advanceSection,
      onSectionBoundary,
      nextSection,
      prevSection,
      // BUILD_HIST
      historyBack,
      historyForward,
      historyCanGoBack,
      historyCanGoForward,
      onHistoryChange,
      // BUILD_ANNOT
      getSelectionCFI,
      addAnnotation,
      deleteAnnotation,
      showAnnotation,
      setAnnotationMeta,
      removeAnnotationMeta,
      onShowAnnotation: (cb) => { state.onShowAnnotationCb = typeof cb === 'function' ? cb : null; },
      onCreateOverlay: (cb) => { state.onCreateOverlayCb = typeof cb === 'function' ? cb : null; },
      // FIX-R04: iframe event callback setters
      set onDblClick(fn) { state.onDblClick = typeof fn === 'function' ? fn : null; },
      set onContextMenu(fn) { state.onContextMenu = typeof fn === 'function' ? fn : null; },
      set onDictLookup(fn) { state.onDictLookup = typeof fn === 'function' ? fn : null; },
      // TTS_REWRITE: expose foliate view/renderer for tts_core.js integration
      getFoliateView() { return state.view || null; },
      getFoliateRenderer() { return (state.view && state.view.renderer) || null; },
    };
  }

  window.booksReaderEngines.epub = { create };
  window.booksReaderEngines.pdf = { create };
  window.booksReaderEngines.mobi = { create };
  window.booksReaderEngines.fb2 = { create };
})();
