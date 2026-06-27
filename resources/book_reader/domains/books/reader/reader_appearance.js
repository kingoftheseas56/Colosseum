// BUILD_OVERHAUL: Appearance/settings module extracted from monolithic controller.js
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;
  var FONT_WEIGHT_STORAGE_KEY = 'books_fontWeight';
  var INVERT_DARK_IMAGES_KEY = 'books_invertDarkImages';
  var DARK_READER_THEMES = ['dark', 'contrast1', 'contrast2', 'contrast3', 'contrast4', 'nord', 'gruvbox', 'gruvboxDark', 'solarized'];
  var CUSTOM_CSS_STYLE_ID = 'books-reader-user-custom-css';

  function injectCustomCssIntoIframes() {
    var css = String((RS.state.settings && RS.state.settings.customCss) || '').trim();
    var host = document.getElementById('booksReaderHost');
    if (!host) return;
    var iframes = host.querySelectorAll('iframe');
    for (var i = 0; i < iframes.length; i++) {
      try {
        var doc = iframes[i].contentDocument;
        if (!doc) continue;
        var existing = doc.getElementById(CUSTOM_CSS_STYLE_ID);
        if (css) {
          if (!existing) {
            existing = doc.createElement('style');
            existing.id = CUSTOM_CSS_STYLE_ID;
            (doc.head || doc.documentElement).appendChild(existing);
          }
          existing.textContent = css;
        } else if (existing) {
          existing.remove();
        }
      } catch (e) { /* cross-origin iframe */ }
    }
  }

  function isDarkReaderTheme(theme) {
    return DARK_READER_THEMES.indexOf(String(theme || 'light')) !== -1;
  }
  var INVERT_DARK_IMAGES_STYLE_ID = 'books-reader-invert-dark-images-style';

  function clampFontWeight(v) {
    var n = Number(v);
    if (!isFinite(n)) n = 400;
    n = Math.round(n / 100) * 100;
    if (n < 100) n = 100;
    if (n > 900) n = 900;
    return n;
  }

  function loadFontWeightSetting() {
    var state = RS.state;
    var fromStorage = null;
    try {
      var raw = localStorage.getItem(FONT_WEIGHT_STORAGE_KEY);
      if (raw !== null && raw !== '') fromStorage = clampFontWeight(raw);
    } catch (e) {}
    var next = fromStorage != null ? fromStorage : clampFontWeight(state.settings && state.settings.fontWeight);
    if (!state.settings) state.settings = {};
    state.settings.fontWeight = next;
    return next;
  }

  function persistFontWeightSetting(v) {
    try { localStorage.setItem(FONT_WEIGHT_STORAGE_KEY, String(clampFontWeight(v))); } catch (e) {}
  }

  function ensureFontWeightControl() {
    var els = RS.ensureEls();
    var existingSlider = document.getElementById('booksReaderFontWeightSlider');
    var existingValue = document.getElementById('booksReaderFontWeightValue');

    if (!existingSlider || !existingValue) {
      var fontSizeSlider = els.fontSizeSlider || document.getElementById('booksReaderFontSizeSlider');
      var panelBody = fontSizeSlider && fontSizeSlider.closest
        ? fontSizeSlider.closest('.br-overlay-body')
        : null;
      var fontSizeSection = fontSizeSlider && fontSizeSlider.closest
        ? fontSizeSlider.closest('.br-settings-section')
        : null;

      if (panelBody && fontSizeSection && !document.getElementById('brSettingsFontWeightSection')) {
        var section = document.createElement('div');
        section.className = 'br-settings-section';
        section.id = 'brSettingsFontWeightSection';
        section.innerHTML =
          '<div class="br-settings-label">Font weight</div>'
          + '<div class="br-settings-slider-row">'
          + '  <input id="booksReaderFontWeightSlider" class="br-settings-slider" type="range" min="100" max="900" step="100" value="400" />'
          + '  <span id="booksReaderFontWeightValue" class="br-settings-value">400</span>'
          + '</div>';

        if (fontSizeSection.nextSibling) panelBody.insertBefore(section, fontSizeSection.nextSibling);
        else panelBody.appendChild(section);
      }

      existingSlider = document.getElementById('booksReaderFontWeightSlider');
      existingValue = document.getElementById('booksReaderFontWeightValue');
    }

    if (els) {
      els.fontWeightSlider = existingSlider || null;
      els.fontWeightValue = existingValue || null;
    }
    return { slider: existingSlider, value: existingValue };
  }

  // ── Core appearance functions ────────────────────────────────────

  function applyThemeAttribute(theme) {
    var els = RS.ensureEls();
    var nextTheme = String(theme || 'light');
    if (els.readerView) els.readerView.setAttribute('data-reader-theme', nextTheme);
    if (els.host) els.host.setAttribute('data-reader-theme', nextTheme);
    try {
      var readingArea = (els.readerView && typeof els.readerView.querySelector === 'function')
        ? els.readerView.querySelector('.br-reading-area')
        : null;
      if (readingArea) readingArea.setAttribute('data-reader-theme', nextTheme);
    } catch (e) {}
    // LISTEN_THEME: apply same theme to Listening player overlay
    var lp = document.getElementById('booksListenPlayerOverlay');
    if (lp) lp.setAttribute('data-reader-theme', nextTheme);
    // FIX-TTS03: set blend mode for TTS highlight overlayer (lighten for dark themes)
    // BUILD_THEMES: nord and gruvboxDark are dark themes
    var isDark = isDarkReaderTheme(nextTheme);
    document.documentElement.style.setProperty('--overlayer-highlight-blend-mode', isDark ? 'lighten' : 'normal');
  }

  function isInvertDarkImagesEnabled() {
    try {
      var raw = localStorage.getItem(INVERT_DARK_IMAGES_KEY);
      if (raw == null) return true; // default: on
      raw = String(raw).toLowerCase();
      return !(raw === '0' || raw === 'false' || raw === 'off' || raw === 'no');
    } catch (e) {
      return true;
    }
  }

  function setInvertDarkImagesEnabled(enabled) {
    try { localStorage.setItem(INVERT_DARK_IMAGES_KEY, enabled ? '1' : '0'); } catch (e) {}
  }

  function shouldInvertImagesForTheme(theme) {
    var t = String(theme || 'light');
    return isInvertDarkImagesEnabled() && isDarkReaderTheme(t);
  }

  function collectReaderIframeDocs() {
    var docs = [];
    var seen = [];
    function pushDoc(doc) {
      if (!doc || !doc.documentElement) return;
      if (seen.indexOf(doc) !== -1) return;
      seen.push(doc);
      docs.push(doc);
    }
    function pushIframeDocsFrom(root) {
      if (!root || typeof root.querySelectorAll !== 'function') return;
      var iframes = root.querySelectorAll('iframe');
      for (var i = 0; i < iframes.length; i++) {
        try {
          var d = iframes[i].contentDocument || (iframes[i].contentWindow && iframes[i].contentWindow.document) || null;
          pushDoc(d);
        } catch (e) {}
      }
    }

    var state = RS.state || {};
    var renderer = null;
    try {
      if (state.engine && typeof state.engine.getFoliateRenderer === 'function') {
        renderer = state.engine.getFoliateRenderer();
      } else if (state.engine && state.engine.renderer) {
        renderer = state.engine.renderer;
      }
    } catch (e) {}

    try {
      if (renderer && typeof renderer.getContents === 'function') {
        var contents = renderer.getContents() || [];
        for (var c = 0; c < contents.length; c++) {
          var item = contents[c] || {};
          pushDoc(item.document || item.doc || null);
        }
      }
    } catch (e) {}

    var els = state.els || {};
    pushIframeDocsFrom(els.host || null);
    pushIframeDocsFrom(els.readerView || null);
    return docs;
  }

  function injectDarkImageInvertStyleIntoIframes(theme) {
    var shouldInvert = shouldInvertImagesForTheme(theme);
    var css = shouldInvert
      ? 'img, svg, image { filter: invert(1) hue-rotate(180deg) !important; }'
      : '';

    var docs = collectReaderIframeDocs();
    for (var i = 0; i < docs.length; i++) {
      var doc = docs[i];
      try {
        var styleEl = doc.getElementById(INVERT_DARK_IMAGES_STYLE_ID);
        if (!styleEl) {
          styleEl = doc.createElement('style');
          styleEl.id = INVERT_DARK_IMAGES_STYLE_ID;
          styleEl.type = 'text/css';
          var parent = doc.head || doc.documentElement || doc.body;
          if (!parent) continue;
          parent.appendChild(styleEl);
        }
        if (styleEl.textContent !== css) styleEl.textContent = css;
      } catch (e) {}
    }
  }

  function applySettings() {
    var state = RS.state;
    state.settings.fontWeight = loadFontWeightSetting();
    var theme = state && state.settings ? state.settings.theme : 'light';
    applyThemeAttribute(theme);
    if (state.engine && typeof state.engine.applySettings === 'function') {
      try { state.engine.applySettings(Object.assign({}, state.settings)); } catch (e) { /* swallow */ }
    }
    injectDarkImageInvertStyleIntoIframes(theme);
    injectCustomCssIntoIframes();
  }

  function normalizeMaxLineWidth(v) {
    var n = Number(v);
    if (!Number.isFinite(n)) n = 960;
    n = Math.round(n / 50) * 50;
    return Math.max(400, Math.min(1600, n));
  }

  function syncMaxLineWidthFromLocalStorage() {
    try {
      var raw = localStorage.getItem('books_maxLineWidth');
      if (raw == null || raw === '') return;
      RS.state.settings.maxLineWidth = normalizeMaxLineWidth(raw);
    } catch (e) { /* swallow */ }
  }

  function syncAaPanelUI() {
    var els = RS.ensureEls();
    var s = RS.state.settings;
    ensureFontWeightControl();
    if (els.fontSizeSlider) { els.fontSizeSlider.value = String(s.fontSize || 100); }
    if (els.fontSizeValue) { els.fontSizeValue.textContent = String(s.fontSize || 100) + '%'; }
    if (els.fontWeightSlider) { els.fontWeightSlider.value = String(clampFontWeight(s.fontWeight || 400)); }
    if (els.fontWeightValue) { els.fontWeightValue.textContent = String(clampFontWeight(s.fontWeight || 400)); }
    if (els.fontFamily) { els.fontFamily.value = String(s.fontFamily || 'publisher'); }
    if (els.lineHeightSlider) { els.lineHeightSlider.value = String(s.lineHeight || 1.5); }
    if (els.lineHeightValue) { els.lineHeightValue.textContent = String(Number(s.lineHeight || 1.5).toFixed(1)); }
    if (els.marginSlider) { els.marginSlider.value = String(s.margin || 1); }
    if (els.marginValue) { els.marginValue.textContent = Number(s.margin || 1).toFixed(2); }
    var maxLineWidth = normalizeMaxLineWidth(s.maxLineWidth);
    if (els.maxLineWidthSlider) { els.maxLineWidthSlider.value = String(maxLineWidth); }
    if (els.maxLineWidthValue) { els.maxLineWidthValue.textContent = String(maxLineWidth) + 'px'; }
    if (els.columnToggle) { els.columnToggle.checked = (s.columnMode !== 'single'); }
    if (els.flowToggle) { els.flowToggle.checked = String(s.flowMode || 'paginated') === 'scrolled'; }
    if (els.invertDarkImagesToggle) { els.invertDarkImagesToggle.checked = isInvertDarkImagesEnabled(); }
    // BUILD_READIUMCSS: new typography controls
    syncTextAlignChips(s.textAlign || '');
    if (els.letterSpacingSlider) { els.letterSpacingSlider.value = String(s.letterSpacing || 0); }
    if (els.letterSpacingValue) { els.letterSpacingValue.textContent = Number(s.letterSpacing || 0).toFixed(2) + ' rem'; }
    if (els.wordSpacingSlider) { els.wordSpacingSlider.value = String(s.wordSpacing || 0); }
    if (els.wordSpacingValue) { els.wordSpacingValue.textContent = Number(s.wordSpacing || 0).toFixed(2) + ' rem'; }
    if (els.paraSpacingSlider) { els.paraSpacingSlider.value = String(s.paraSpacing || 0); }
    if (els.paraSpacingValue) { els.paraSpacingValue.textContent = Number(s.paraSpacing || 0).toFixed(1) + ' rem'; }
    if (els.paraIndent) { els.paraIndent.value = String(s.paraIndent || ''); }
    if (els.hyphens) { els.hyphens.value = String(s.bodyHyphens || ''); }
    // Custom CSS
    var customCssEl = document.getElementById('booksReaderCustomCss');
    if (customCssEl) { customCssEl.value = String(s.customCss || ''); }
    // TTS
    if (els.settingsTtsRate) { els.settingsTtsRate.value = String(Number(s.ttsRate || 1.0).toFixed(1)); }
    if (els.settingsTtsRateValue) { els.settingsTtsRateValue.textContent = Number(s.ttsRate || 1.0).toFixed(1) + 'x'; }
    if (els.settingsTtsVoice && s.ttsVoice) { els.settingsTtsVoice.value = String(s.ttsVoice); }
  }

  function syncThemeFontUI() {
    var state = RS.state;
    // Theme chips
    var chips = document.querySelectorAll('.br-settings-chip[data-theme]');
    var curTheme = String(state.settings.theme || 'light');
    for (var i = 0; i < chips.length; i++) {
      chips[i].classList.toggle('active', chips[i].getAttribute('data-theme') === curTheme);
    }
    applyThemeAttribute(curTheme);
  }

  function syncControlAvailability() {
    var els = RS.ensureEls();
    var state = RS.state;
    var active = !!state.open;
    var isPdf = active && RS.isPdfOpen();
    var isTextFlow = active && RS.isEpubOrTxtOpen();

    if (els.fitPageBtn) els.fitPageBtn.disabled = !isPdf;
    if (els.fitWidthBtn) els.fitWidthBtn.disabled = !isPdf;
    if (els.zoomDown) els.zoomDown.disabled = !isPdf;
    if (els.zoomUp) els.zoomUp.disabled = !isPdf;
    if (els.pdfGroup) els.pdfGroup.classList.toggle('hidden', !isPdf);
    if (els.fsBtn) els.fsBtn.disabled = !active;
    if (els.progress) els.progress.setAttribute('aria-disabled', active ? 'false' : 'true');

    // Flow toggle only for EPUB/TXT
    if (els.flowBtn) els.flowBtn.disabled = !isTextFlow;
    if (els.flowToggle) els.flowToggle.disabled = !isTextFlow;
    // TTS only for EPUB/TXT
    var ttsEnabled = isTextFlow && RS.ttsSupported();
    if (els.playBtn) els.playBtn.disabled = !ttsEnabled;
    // BUILD_TTS_FIX4: disable TTS launch button for non-supported formats
    if (els.ttsLaunch) {
      els.ttsLaunch.disabled = !ttsEnabled;
      els.ttsLaunch.title = ttsEnabled ? 'Read aloud (T)' : (isPdf ? 'TTS not available for PDF' : 'Read aloud (T)');
    }
    if (els.settingsTtsVoiceSection) els.settingsTtsVoiceSection.classList.toggle('hidden', !ttsEnabled);
    if (els.settingsTtsRateSection) els.settingsTtsRateSection.classList.toggle('hidden', !ttsEnabled);
    if (els.settingsSleepSection) els.settingsSleepSection.classList.toggle('hidden', !ttsEnabled);
    if (els.ttsSettingsBtn) els.ttsSettingsBtn.disabled = !ttsEnabled;
    updateFlowBtnLabel();
  }

  // FIX_HUD: SVG icons for flow mode toggle
  var PAGE_ICON_SVG = '<svg viewBox="0 0 22 19" xmlns="http://www.w3.org/2000/svg"><path d="M20 0.5H14C13.4178 0.5 12.8437 0.635544 12.3229 0.895898C11.8022 1.15625 11.3493 1.53426 11 2C10.6507 1.53426 10.1978 1.15625 9.67705 0.895898C9.15634 0.635544 8.58217 0.5 8 0.5H2C1.60218 0.5 1.22064 0.658035 0.93934 0.93934C0.658035 1.22064 0.5 1.60218 0.5 2V14C0.5 14.3978 0.658035 14.7794 0.93934 15.0607C1.22064 15.342 1.60218 15.5 2 15.5H8C8.59674 15.5 9.16903 15.7371 9.59099 16.159C10.0129 16.581 10.25 17.1533 10.25 17.75C10.25 17.9489 10.329 18.1397 10.4697 18.2803C10.6103 18.421 10.8011 18.5 11 18.5C11.1989 18.5 11.3897 18.421 11.5303 18.2803C11.671 18.1397 11.75 17.9489 11.75 17.75C11.75 17.1533 11.9871 16.581 12.409 16.159C12.831 15.7371 13.4033 15.5 14 15.5H20C20.3978 15.5 20.7794 15.342 21.0607 15.0607C21.342 14.7794 21.5 14.3978 21.5 14V2C21.5 1.60218 21.342 1.22064 21.0607 0.93934C20.7794 0.658035 20.3978 0.5 20 0.5ZM8 14H2V2H8C8.59674 2 9.16903 2.23705 9.59099 2.65901C10.0129 3.08097 10.25 3.65326 10.25 4.25V14.75C9.6015 14.262 8.8116 13.9987 8 14ZM20 14H14C13.1884 13.9987 12.3985 14.262 11.75 14.75V4.25C11.75 3.65326 11.9871 3.08097 12.409 2.65901C12.831 2.23705 13.4033 2 14 2H20V14ZM14 4.25H17.75C17.9489 4.25 18.1397 4.32902 18.2803 4.46967C18.421 4.61032 18.5 4.80109 18.5 5C18.5 5.19891 18.421 5.38968 18.2803 5.53033C18.1397 5.67098 17.9489 5.75 17.75 5.75H14C13.8011 5.75 13.6103 5.67098 13.4697 5.53033C13.329 5.38968 13.25 5.19891 13.25 5C13.25 4.80109 13.329 4.61032 13.4697 4.46967C13.6103 4.32902 13.8011 4.25 14 4.25ZM18.5 8C18.5 8.19891 18.421 8.38968 18.2803 8.53033C18.1397 8.67098 17.9489 8.75 17.75 8.75H14C13.8011 8.75 13.6103 8.67098 13.4697 8.53033C13.329 8.38968 13.25 8.19891 13.25 8C13.25 7.80109 13.329 7.61032 13.4697 7.46967C13.6103 7.32902 13.8011 7.25 14 7.25H17.75C17.9489 7.25 18.1397 7.32902 18.2803 7.46967C18.421 7.61032 18.5 7.80109 18.5 8ZM18.5 11C18.5 11.1989 18.421 11.3897 18.2803 11.5303C18.1397 11.671 17.9489 11.75 17.75 11.75H14C13.8011 11.75 13.6103 11.671 13.4697 11.5303C13.329 11.3897 13.25 11.1989 13.25 11C13.25 10.8011 13.329 10.6103 13.4697 10.4697C13.6103 10.329 13.8011 10.25 14 10.25H17.75C17.9489 10.25 18.1397 10.329 18.2803 10.4697C18.421 10.6103 18.5 10.8011 18.5 11Z" fill="currentColor"/></svg>';
  var SCROLL_ICON_SVG = '<svg viewBox="0 0 20 18" xmlns="http://www.w3.org/2000/svg"><path d="M18.25 0.75H1.75C1.35218 0.75 0.970644 0.908035 0.68934 1.18934C0.408035 1.47064 0.25 1.85218 0.25 2.25V15.75C0.25 16.1478 0.408035 16.5294 0.68934 16.8107C0.970644 17.092 1.35218 17.25 1.75 17.25H18.25C18.6478 17.25 19.0294 17.092 19.3107 16.8107C19.592 16.5294 19.75 16.1478 19.75 15.75V2.25C19.75 1.85218 19.592 1.47064 19.3107 1.18934C19.0294 0.908035 18.6478 0.75 18.25 0.75ZM18.25 15.75H1.75V2.25H18.25V15.75ZM15.25 6C15.25 6.19891 15.171 6.38968 15.0303 6.53033C14.8897 6.67098 14.6989 6.75 14.5 6.75H5.5C5.30109 6.75 5.11032 6.67098 4.96967 6.53033C4.82902 6.38968 4.75 6.19891 4.75 6C4.75 5.80109 4.82902 5.61032 4.96967 5.46967C5.11032 5.32902 5.30109 5.25 5.5 5.25H14.5C14.6989 5.25 14.8897 5.32902 15.0303 5.46967C15.171 5.61032 15.25 5.80109 15.25 6ZM15.25 9C15.25 9.19891 15.171 9.38968 15.0303 9.53033C14.8897 9.67098 14.6989 9.75 14.5 9.75H5.5C5.30109 9.75 5.11032 9.67098 4.96967 9.53033C4.82902 9.38968 4.75 9.19891 4.75 9C4.75 8.80109 4.82902 8.61032 4.96967 8.46967C5.11032 8.32902 5.30109 8.25 5.5 8.25H14.5C14.6989 8.25 14.8897 8.32902 15.0303 8.46967C15.171 8.61032 15.25 8.80109 15.25 9ZM15.25 12C15.25 12.1989 15.171 12.3897 15.0303 12.5303C14.8897 12.671 14.6989 12.75 14.5 12.75H5.5C5.30109 12.75 5.11032 12.671 4.96967 12.5303C4.82902 12.3897 4.75 12.1989 4.75 12C4.75 11.8011 4.82902 11.6103 4.96967 11.4697C5.11032 11.329 5.30109 11.25 5.5 11.25H14.5C14.6989 11.25 14.8897 11.329 15.0303 11.4697C15.171 11.6103 15.25 11.8011 15.25 12Z" fill="currentColor"/></svg>';

  function updateFlowBtnLabel() {
    var els = RS.ensureEls();
    if (!els.flowBtn) return;
    var mode = String(RS.state.settings.flowMode || 'paginated');
    els.flowBtn.title = mode === 'scrolled' ? 'Scrolled (click to paginate)' : 'Paginated (click to scroll)';
    els.flowBtn.setAttribute('aria-label', els.flowBtn.title);
    els.flowBtn.innerHTML = mode === 'scrolled' ? SCROLL_ICON_SVG : PAGE_ICON_SVG;
    // FIX_HUD: set data-flow attribute for CSS scoping (overflow:hidden in paged mode)
    if (els.host) els.host.setAttribute('data-flow', mode);
  }

  function setTheme(theme) {
    var state = RS.state;
    state.settings.theme = String(theme || 'light');
    applySettings();
    syncThemeFontUI();
    RS.persistSettings().catch(function () {});
  }

  // BUILD_READIUMCSS: text-align chip sync helper
  // BUILD_THEMES: added named literary themes to cycle order
  var THEME_ORDER = ['light', 'sepia', 'dark', 'paper', 'contrast1', 'contrast2', 'contrast3', 'contrast4',
    'nord', 'gruvbox', 'gruvboxDark', 'solarized'];
  var THEME_LABELS = {
    light: 'Light', sepia: 'Sepia', dark: 'Dark', paper: 'Paper',
    contrast1: 'High Contrast 1', contrast2: 'High Contrast 2',
    contrast3: 'High Contrast 3', contrast4: 'High Contrast 4',
    nord: 'Nord', gruvbox: 'Gruvbox', gruvboxDark: 'Gruvbox Dark', solarized: 'Solarized'
  };

  function syncTextAlignChips(align) {
    var chips = document.querySelectorAll('.br-settings-chip[data-align]');
    var cur = String(align || '');
    for (var i = 0; i < chips.length; i++) {
      chips[i].classList.toggle('active', chips[i].getAttribute('data-align') === cur);
    }
  }

  function cycleTheme() {
    var state = RS.state;
    var cur = String(state.settings.theme || 'light');
    var idx = THEME_ORDER.indexOf(cur);
    var next = THEME_ORDER[(idx + 1) % THEME_ORDER.length];
    setTheme(next);
    RS.showToast('Theme: ' + (THEME_LABELS[next] || next));
  }

  // ── PDF helpers ──────────────────────────────────────────────────

  async function applyPdfFit(mode) {
    var state = RS.state;
    if (!state.engine || typeof state.engine.setFitMode !== 'function') return;
    await state.engine.setFitMode(mode);
  }

  async function adjustPdfZoom(delta) {
    var state = RS.state;
    if (!state.engine || typeof state.engine.setZoom !== 'function') return;
    var loc = (typeof state.engine.getLocator === 'function') ? state.engine.getLocator() : {};
    var current = Number(loc.zoom || 1);
    await state.engine.setZoom(current + delta);
  }

  // ── Flow mode toggle ────────────────────────────────────────────

  async function toggleFlowMode() {
    var state = RS.state;
    if (!RS.isEpubOrTxtOpen()) return;
    var current = String(state.settings.flowMode || 'paginated');
    var next = current === 'scrolled' ? 'paginated' : 'scrolled';
    state.settings.flowMode = next;
    if (state.engine && typeof state.engine.setFlowMode === 'function') {
      state.engine.setFlowMode(next);
    }
    // FIX_AUDIT: keep chapter boundary pause aligned with active flow mode.
    bus.emit('appearance:flow-mode-changed', next);
    updateFlowBtnLabel();
    syncAaPanelUI();
    RS.persistSettings().catch(function () {});
  }

  // ── Lifecycle ────────────────────────────────────────────────────

  function onOpen() {
    loadFontWeightSetting();
    syncMaxLineWidthFromLocalStorage();
    applySettings();
    syncAaPanelUI();
    syncThemeFontUI();
    syncControlAvailability();
  }

  function onClose() {
    syncControlAvailability();
  }

  // ── Bind ─────────────────────────────────────────────────────────

  function bind() {
    var els = RS.ensureEls();
    var state = RS.state;
    ensureFontWeightControl();
    state.settings.fontWeight = loadFontWeightSetting();

    // Theme chips
    var themeChips = document.querySelectorAll('.br-settings-chip[data-theme]');
    for (var t = 0; t < themeChips.length; t++) {
      (function (chip) {
        chip.addEventListener('click', function () {
          setTheme(chip.getAttribute('data-theme') || 'light');
        });
      })(themeChips[t]);
    }

    // Font family select
    els.fontFamily && els.fontFamily.addEventListener('change', function () {
      state.settings.fontFamily = String(els.fontFamily.value || 'publisher');
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Font size slider (BUILD_READIUMCSS: percentage 75-250)
    els.fontSizeSlider && els.fontSizeSlider.addEventListener('input', function () {
      var v = Number(els.fontSizeSlider.value || 100);
      state.settings.fontSize = Math.max(75, Math.min(250, v));
      if (els.fontSizeValue) els.fontSizeValue.textContent = String(state.settings.fontSize) + '%';
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Font weight slider (ReadiumCSS --USER__fontWeight)
    els.fontWeightSlider && els.fontWeightSlider.addEventListener('input', function () {
      state.settings.fontWeight = clampFontWeight(els.fontWeightSlider.value || 400);
      if (els.fontWeightValue) els.fontWeightValue.textContent = String(state.settings.fontWeight);
      persistFontWeightSetting(state.settings.fontWeight);
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Line height slider (BUILD_READIUMCSS: 1.0-2.0)
    els.lineHeightSlider && els.lineHeightSlider.addEventListener('input', function () {
      var v = Number(els.lineHeightSlider.value || 1.5);
      state.settings.lineHeight = Math.max(1.0, Math.min(2.0, Math.round(v * 10) / 10));
      if (els.lineHeightValue) els.lineHeightValue.textContent = state.settings.lineHeight.toFixed(1);
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Margin slider (BUILD_READIUMCSS: factor 0-4.0)
    els.marginSlider && els.marginSlider.addEventListener('input', function () {
      var v = Number(els.marginSlider.value || 1);
      state.settings.margin = Math.max(0, Math.min(4.0, Math.round(v * 4) / 4));
      if (els.marginValue) els.marginValue.textContent = state.settings.margin.toFixed(2);
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Max line width slider (400-1600px, step 50)
    els.maxLineWidthSlider && els.maxLineWidthSlider.addEventListener('input', function () {
      var v = normalizeMaxLineWidth(els.maxLineWidthSlider.value || 960);
      state.settings.maxLineWidth = v;
      if (els.maxLineWidthValue) els.maxLineWidthValue.textContent = String(v) + 'px';
      try { localStorage.setItem('books_maxLineWidth', String(v)); } catch (e) { /* swallow */ }
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Column toggle (spread vs single)
    els.columnToggle && els.columnToggle.addEventListener('change', function () {
      state.settings.columnMode = els.columnToggle.checked ? 'auto' : 'single';
      if (state.engine && typeof state.engine.setColumnMode === 'function') {
        state.engine.setColumnMode(state.settings.columnMode);
      }
      RS.persistSettings().catch(function () {});
    });

    // BUILD_READIUMCSS: Text align chips
    var alignChips = document.querySelectorAll('.br-settings-chip[data-align]');
    for (var a = 0; a < alignChips.length; a++) {
      (function (chip) {
        chip.addEventListener('click', function () {
          var align = chip.getAttribute('data-align') || '';
          state.settings.textAlign = align;
          syncTextAlignChips(align);
          applySettings();
          RS.persistSettings().catch(function () {});
        });
      })(alignChips[a]);
    }

    // BUILD_READIUMCSS: Letter spacing slider
    els.letterSpacingSlider && els.letterSpacingSlider.addEventListener('input', function () {
      var v = Number(els.letterSpacingSlider.value || 0);
      state.settings.letterSpacing = Math.max(0, Math.min(0.5, Math.round(v * 100) / 100));
      if (els.letterSpacingValue) els.letterSpacingValue.textContent = state.settings.letterSpacing.toFixed(2) + ' rem';
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // BUILD_READIUMCSS: Word spacing slider
    els.wordSpacingSlider && els.wordSpacingSlider.addEventListener('input', function () {
      var v = Number(els.wordSpacingSlider.value || 0);
      state.settings.wordSpacing = Math.max(0, Math.min(1.0, Math.round(v * 100) / 100));
      if (els.wordSpacingValue) els.wordSpacingValue.textContent = state.settings.wordSpacing.toFixed(2) + ' rem';
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // BUILD_READIUMCSS: Paragraph spacing slider
    els.paraSpacingSlider && els.paraSpacingSlider.addEventListener('input', function () {
      var v = Number(els.paraSpacingSlider.value || 0);
      state.settings.paraSpacing = Math.max(0, Math.min(2.0, Math.round(v * 10) / 10));
      if (els.paraSpacingValue) els.paraSpacingValue.textContent = state.settings.paraSpacing.toFixed(1) + ' rem';
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // BUILD_READIUMCSS: Paragraph indent select
    els.paraIndent && els.paraIndent.addEventListener('change', function () {
      state.settings.paraIndent = String(els.paraIndent.value || '');
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // BUILD_READIUMCSS: Hyphenation select
    els.hyphens && els.hyphens.addEventListener('change', function () {
      state.settings.bodyHyphens = String(els.hyphens.value || '');
      applySettings();
      RS.persistSettings().catch(function () {});
    });

    // Custom CSS textarea
    var customCssEl = document.getElementById('booksReaderCustomCss');
    if (customCssEl) {
      var _customCssTimer = null;
      customCssEl.addEventListener('input', function () {
        if (_customCssTimer) clearTimeout(_customCssTimer);
        _customCssTimer = setTimeout(function () {
          state.settings.customCss = String(customCssEl.value || '');
          applySettings();
          RS.persistSettings().catch(function () {});
        }, 600);
      });
    }

    // PDF controls
    els.fitPageBtn && els.fitPageBtn.addEventListener('click', function () { applyPdfFit('page').catch(function () {}); });
    els.fitWidthBtn && els.fitWidthBtn.addEventListener('click', function () { applyPdfFit('width').catch(function () {}); });
    els.zoomDown && els.zoomDown.addEventListener('click', function () { adjustPdfZoom(-0.1).catch(function () {}); });
    els.zoomUp && els.zoomUp.addEventListener('click', function () { adjustPdfZoom(0.1).catch(function () {}); });

    // Flow mode settings toggle
    els.flowToggle && els.flowToggle.addEventListener('change', function () {
      toggleFlowMode();
    });

    // Image inversion toggle for dark themes (localStorage-only preference)
    if (els.invertDarkImagesToggle) {
      els.invertDarkImagesToggle.addEventListener('change', function () {
        setInvertDarkImagesEnabled(!!els.invertDarkImagesToggle.checked);
        injectDarkImageInvertStyleIntoIframes(state.settings && state.settings.theme);
      });
    }

    // Re-apply iframe image inversion style when EPUB/text iframes reload (page/chapter changes)
    if (els.host && typeof els.host.addEventListener === 'function') {
      els.host.addEventListener('load', function (ev) {
        var t = ev && ev.target;
        if (!t || String(t.tagName || '').toLowerCase() !== 'iframe') return;
        setTimeout(function () {
          injectDarkImageInvertStyleIntoIframes(state.settings && state.settings.theme);
          injectCustomCssIntoIframes();
        }, 0);
      }, true);
    }

    // Flow button in toolbar
    els.flowBtn && els.flowBtn.addEventListener('click', function () {
      toggleFlowMode();
    });

    // Bus events
    bus.on('appearance:apply', function () { applySettings(); });
    bus.on('appearance:cycle-theme', function () { cycleTheme(); });
    bus.on('appearance:sync', function () {
      syncAaPanelUI();
      syncThemeFontUI();
      syncControlAvailability();
      injectDarkImageInvertStyleIntoIframes(state.settings && state.settings.theme);
    });

    // Settings TTS voice select
    if (els.settingsTtsVoice) {
      els.settingsTtsVoice.addEventListener('change', function () {
        var voiceId = els.settingsTtsVoice.value;
        state.settings.ttsVoice = voiceId;
        RS.persistSettings().catch(function () {});
        var tts = window.booksTTS;
        if (tts && typeof tts.setVoice === 'function') {
          tts.setVoice(voiceId);
        }
        bus.emit('tts:voice-changed');
      });
    }

    // Settings TTS rate slider
    if (els.settingsTtsRate) {
      els.settingsTtsRate.addEventListener('input', function () {
        var v = Number(els.settingsTtsRate.value || 1.0);
        state.settings.ttsRate = v;
        if (els.settingsTtsRateValue) els.settingsTtsRateValue.textContent = v.toFixed(1) + 'x';
        RS.persistSettings().catch(function () {});
        var tts = window.booksTTS;
        if (tts && typeof tts.setRate === 'function') {
          tts.setRate(v);
        }
      });
    }

    // Sleep timer chips
    var sleepChips = document.querySelectorAll('.br-settings-chip[data-sleep]');
    for (var s = 0; s < sleepChips.length; s++) {
      (function (chip) {
        chip.addEventListener('click', function () {
          var minutes = Number(chip.getAttribute('data-sleep') || 0);
          bus.emit('tts:sleep', minutes);
        });
      })(sleepChips[s]);
    }
  }

  // ── Export ───────────────────────────────────────────────────────

  window.booksReaderAppearance = {
    bind: bind,
    applySettings: applySettings,
    syncAaPanelUI: syncAaPanelUI,
    syncThemeFontUI: syncThemeFontUI,
    syncControlAvailability: syncControlAvailability,
    updateFlowBtnLabel: updateFlowBtnLabel,
    cycleTheme: cycleTheme,
    onOpen: onOpen,
    onClose: onClose,
  };
})();
