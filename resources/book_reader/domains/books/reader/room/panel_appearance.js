// panel_appearance.js — the "aa" popover: type & theme off the pill.
//
// Recon (real names, not the plan's guesses):
//   - window.booksReaderAppearance (reader_appearance.js) already existed —
//     extended here (not replaced) with setTheme, isDarkTheme, and a new
//     `themes: {order, labels, colors}` surface. order/labels are the SAME
//     THEME_ORDER/THEME_LABELS the old #brOverlayTheme chips and the 'm'
//     cycle-theme shortcut already use (12 built-ins). colors is a canonical
//     swatch palette that mirrors the hex values already hardcoded three
//     places (engine_foliate.js applyExtendedThemeColors, books-reader.css
//     .br-host[data-reader-theme] rules, the old chip markup's inline
//     swatch styles) — collected once as the single JS-reachable source
//     instead of a fourth guessed copy.
//   - Current theme + slider values live on window.booksReaderState.state
//     .settings (theme, fontSize 75-250%, lineHeight 1.0-2.0, margin 0-4.0,
//     customPage, customInk) — the exact object the old #brOverlayFont/
//     #brOverlayTheme sliders/chips already read and write; this panel reads/
//     writes the same object so both chrome layers can coexist pre-Phase-5.
//   - theme=custom is a 13th value, deliberately NOT added to THEME_ORDER
//     (kept out of the 'm' cycle-theme shortcut) — its two page/ink colors
//     ride the SAME --RS__backgroundColor/--RS__textColor (iframe content,
//     engine_foliate.js) and --br-reader-bg/--br-reader-text (parent chrome,
//     books-reader.css) paths as the 12 built-ins, fed by two new
//     --reader-custom-page/--reader-custom-ink CSS vars that
//     reader_appearance.js's applyThemeAttribute() sets before the
//     data-reader-theme attribute goes on.
//   - Persistence: RS.persistSettings() -> Tanko.api.booksSettings.save(...)
//     (global) + per-book/global localStorage — same path the old sliders
//     already use. Sliders persist un-debounced per 'input' event, matching
//     the old #brOverlayFont sliders' own discipline exactly. The two native
//     color inputs are debounced ~250ms (like the old custom-CSS textarea's
//     600ms debounce) since a color-picker drag fires far chattier 'input'
//     events than a stepped range slider ticking through discrete values.
//
// Advanced section (spec-review parity ruling): the old chrome's Aa surfaces
// (#brOverlayFont, ebook_reader.html:141-154, + the dynamically-injected
// font-weight section from ensureFontWeightControl() + #brOverlayTheme:166)
// carry 13 controls beyond this panel's primary set, and Phase 5 deletes that
// chrome — none may silently vanish. The collapsed "Advanced" row below the
// sliders mirrors them control-for-control, same keys/ranges/steps/options:
// font family (8 options), font weight (100-900/100), max line width
// (400-1600/50 + books_maxLineWidth localStorage echo), spread/2-col toggle,
// text-align chips (Auto/Left/Justify/Right), letter spacing (0-0.5/0.0625),
// word spacing (0-1/0.125), paragraph spacing (0-2/0.25), paragraph indent
// (5 options), hyphenation (3 options), custom CSS textarea (600ms debounce,
// old discipline), PDF fit/zoom group (hidden unless RS.state.open &&
// RS.isPdfOpen() — the exact syncControlAvailability condition), and
// invert-images-in-dark toggle (localStorage-only preference, deliberately
// NOT persisted via booksSettings — same as old). Handlers that needed
// private seams (font weight, invert, PDF fit/zoom) go through new
// reader_appearance.js exports (setFontWeight / setInvertDarkImages /
// getInvertDarkImages / applyPdfFit / adjustPdfZoom) that mirror the old
// bind() handler bodies; everything else is the same settings-key write +
// applySettings + persistSettings statements the old handlers execute.
// NOT mirrored (not an Aa-surface control): the flow paginated/scrolled
// toggle — brSettingsFlowToggle has no markup anywhere (els.flowToggle is
// null at runtime); flow mode lives on the toolbar flowBtn.
(function () {
  'use strict';

  var RS = window.booksReaderState;

  function escHtml(s) {
    if (RS && typeof RS.escHtml === 'function') return RS.escHtml(s);
    // Fallback must ESCAPE, never hand raw content to innerHTML — same
    // replacement chain as reader_state.js escHtml.
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function appearance() { return window.booksReaderAppearance; }
  function settings() { return (RS && RS.state && RS.state.settings) || {}; }

  function persist() {
    if (RS && typeof RS.persistSettings === 'function') {
      try { RS.persistSettings().catch(function () {}); } catch (e) {}
    }
  }

  function applyNow() {
    var a = appearance();
    if (a && typeof a.applySettings === 'function') { try { a.applySettings(); } catch (e) {} }
  }

  var gridEl = null;
  var customRowEl = null;
  var pageInputEl = null;
  var inkInputEl = null;
  var colorDebounceTimer = null;

  function markActiveTheme(theme) {
    if (gridEl) {
      var chips = gridEl.querySelectorAll('.room-theme-chip');
      for (var i = 0; i < chips.length; i++) {
        chips[i].classList.toggle('is-active', chips[i].getAttribute('data-theme') === theme);
      }
    }
    if (customRowEl) customRowEl.classList.toggle('is-active', theme === 'custom');
  }

  function pickTheme(id) {
    var a = appearance();
    // a.setTheme applies + syncs the old chip UI + persists internally
    // (reader_appearance.js setTheme()) — mirrors the old chip-click handler
    // exactly, no double-apply/double-persist here.
    if (a && typeof a.setTheme === 'function') { try { a.setTheme(id); } catch (e) {} }
    markActiveTheme(id);
  }

  function applyCustomLive() {
    settings().theme = 'custom';
    applyNow();
    // Keep the old #brOverlayTheme chip UI honest (deactivates its chips —
    // none of them are 'custom') while both chrome layers coexist pre-Phase-5.
    var a = appearance();
    if (a && typeof a.syncThemeFontUI === 'function') { try { a.syncThemeFontUI(); } catch (e) {} }
    markActiveTheme('custom');
  }

  function persistCustomDebounced() {
    if (colorDebounceTimer) clearTimeout(colorDebounceTimer);
    colorDebounceTimer = setTimeout(function () { colorDebounceTimer = null; persist(); }, 250);
  }

  function flushCustomPersist() {
    if (!colorDebounceTimer) return;
    clearTimeout(colorDebounceTimer);
    colorDebounceTimer = null;
    persist();
  }

  function renderThemeGrid(container) {
    var a = appearance();
    var model = (a && a.themes) || {};
    var order = Array.isArray(model.order) ? model.order : [];
    var labels = model.labels || {};
    var colors = model.colors || {};
    var cur = String(settings().theme || 'light');

    gridEl = document.createElement('div');
    gridEl.className = 'room-theme-grid';

    order.forEach(function (id) {
      var c = colors[id] || { bg: '#101216', fg: '#e4e8ef' };
      var chip = document.createElement('button');
      chip.type = 'button';
      chip.className = 'room-theme-chip' + (id === cur ? ' is-active' : '');
      chip.setAttribute('data-theme', id);
      chip.innerHTML =
        '<span class="room-theme-swatch" style="background:' + escHtml(c.bg) + ';color:' + escHtml(c.fg) + ';">Aa</span>' +
        '<span class="room-theme-label">' + escHtml(labels[id] || id) + '</span>';
      chip.addEventListener('click', function () { pickTheme(id); });
      gridEl.appendChild(chip);
    });

    container.appendChild(gridEl);
  }

  function renderCustomSection(container) {
    var s = settings();
    var cur = String(s.theme || 'light');

    customRowEl = document.createElement('div');
    customRowEl.className = 'room-custom' + (cur === 'custom' ? ' is-active' : '');

    var title = document.createElement('div');
    title.className = 'room-panel-subtitle';
    title.textContent = 'Custom';
    customRowEl.appendChild(title);

    var inputs = document.createElement('div');
    inputs.className = 'room-custom-inputs';

    var pageWrap = document.createElement('label');
    pageWrap.className = 'room-custom-swatch';
    var pageText = document.createElement('span');
    pageText.textContent = 'Page';
    pageWrap.appendChild(pageText);
    pageInputEl = document.createElement('input');
    pageInputEl.type = 'color';
    pageInputEl.value = String(s.customPage || '#111214');
    pageWrap.appendChild(pageInputEl);
    inputs.appendChild(pageWrap);

    var inkWrap = document.createElement('label');
    inkWrap.className = 'room-custom-swatch';
    var inkText = document.createElement('span');
    inkText.textContent = 'Ink';
    inkWrap.appendChild(inkText);
    inkInputEl = document.createElement('input');
    inkInputEl.type = 'color';
    inkInputEl.value = String(s.customInk || '#c9c5bc');
    inkWrap.appendChild(inkInputEl);
    inputs.appendChild(inkWrap);

    customRowEl.appendChild(inputs);
    container.appendChild(customRowEl);

    pageInputEl.addEventListener('input', function () {
      settings().customPage = String(pageInputEl.value || '#111214');
      applyCustomLive();
      persistCustomDebounced();
    });
    inkInputEl.addEventListener('input', function () {
      settings().customInk = String(inkInputEl.value || '#c9c5bc');
      applyCustomLive();
      persistCustomDebounced();
    });
  }

  function addSlider(container, opts) {
    var section = document.createElement('div');
    section.className = 'room-slider';

    var label = document.createElement('div');
    label.className = 'room-slider-label';
    label.textContent = opts.label;
    section.appendChild(label);

    var row = document.createElement('div');
    row.className = 'room-slider-row';

    var input = document.createElement('input');
    input.type = 'range';
    input.className = 'room-slider-input';
    input.min = String(opts.min);
    input.max = String(opts.max);
    input.step = String(opts.step);
    input.value = String(opts.get());
    row.appendChild(input);

    var value = document.createElement('span');
    value.className = 'room-slider-value';
    value.textContent = opts.fmt(opts.get());
    row.appendChild(value);

    section.appendChild(row);
    container.appendChild(section);

    input.addEventListener('input', function () {
      opts.set(Number(input.value));
      value.textContent = opts.fmt(opts.get());
      // selfApplied: set() already ran the full old-handler body (apply +
      // persist) through an appearance-module seam — don't run it twice.
      if (!opts.selfApplied) {
        applyNow();
        persist();
      }
    });
  }

  function renderSliders(container) {
    addSlider(container, {
      label: 'Text size',
      min: 75, max: 250, step: 12.5,
      get: function () { return Number(settings().fontSize || 100); },
      set: function (v) { settings().fontSize = Math.max(75, Math.min(250, v)); },
      fmt: function (v) { return Math.round(v) + '%'; },
    });
    addSlider(container, {
      label: 'Line spacing',
      min: 1.0, max: 2.0, step: 0.1,
      get: function () { return Number(settings().lineHeight || 1.5); },
      set: function (v) { settings().lineHeight = Math.max(1.0, Math.min(2.0, Math.round(v * 10) / 10)); },
      fmt: function (v) { return Number(v).toFixed(1); },
    });
    addSlider(container, {
      // Margin can legitimately be 0 — reader_appearance.js's own margin math
      // special-cases 0 the same way (Number((s.margin===0)?0:(s.margin||1))).
      label: 'Margins',
      min: 0, max: 4.0, step: 0.25,
      get: function () { var m = settings().margin; return m != null ? Number(m) : 1; },
      set: function (v) { settings().margin = Math.max(0, Math.min(4.0, Math.round(v * 4) / 4)); },
      fmt: function (v) { return Number(v).toFixed(2); },
    });
  }

  // ── Advanced section — old-overlay parity mirror (see header comment) ──

  var advCssEl = null;
  var cssDebounceTimer = null;

  function commitCustomCss() {
    if (!advCssEl) return;
    settings().customCss = String(advCssEl.value || '');
    applyNow();
    persist();
  }

  function flushCssPersist() {
    if (!cssDebounceTimer) return;
    clearTimeout(cssDebounceTimer);
    cssDebounceTimer = null;
    commitCustomCss();
  }

  // Mirrors reader_appearance.js normalizeMaxLineWidth (private there).
  function normalizeMaxLineWidth(v) {
    var n = Number(v);
    if (!isFinite(n)) n = 960;
    n = Math.round(n / 50) * 50;
    return Math.max(400, Math.min(1600, n));
  }

  function addField(container, label) {
    var section = document.createElement('div');
    section.className = 'room-field';
    if (label) {
      var lab = document.createElement('div');
      lab.className = 'room-slider-label';
      lab.textContent = label;
      section.appendChild(lab);
    }
    container.appendChild(section);
    return section;
  }

  function addSelect(container, label, options, current, onChange) {
    var section = addField(container, label);
    var sel = document.createElement('select');
    sel.className = 'room-select';
    options.forEach(function (o) {
      var opt = document.createElement('option');
      opt.value = o.value;
      opt.textContent = o.label;
      sel.appendChild(opt);
    });
    sel.value = String(current);
    section.appendChild(sel);
    // custom_select.js (QWebEngine white-blob workaround) auto-upgrades EVERY
    // native <select> via MutationObserver: the native element is hidden and
    // a .br-custom-select wrapper DIV takes over, dispatching its own bubbling
    // 'change'. A listener on the native select would never fire post-upgrade
    // (found live: the old chrome survives because upgrade moves the id to
    // the wrap and bind() looks up by id). Delegate on the section instead —
    // catches the native select pre-upgrade AND the wrap post-upgrade; both
    // expose .value (the wrap via its property shim).
    section.addEventListener('change', function (e) {
      var t = e.target;
      if (t && t.value !== undefined) onChange(String(t.value));
    });
  }

  function addToggle(container, label, checked, onChange) {
    var row = document.createElement('label');
    row.className = 'room-toggle-row';
    var span = document.createElement('span');
    span.textContent = label;
    row.appendChild(span);
    var input = document.createElement('input');
    input.type = 'checkbox';
    input.checked = !!checked;
    row.appendChild(input);
    container.appendChild(row);
    input.addEventListener('change', function () { onChange(!!input.checked); });
  }

  function addAlignChips(container) {
    var section = addField(container, 'Text align');
    var row = document.createElement('div');
    row.className = 'room-align-chips';
    // Same four values/labels as the old #brSettingsTextAlignSection chips.
    var opts = [
      { v: '', l: 'Auto' }, { v: 'left', l: 'Left' },
      { v: 'justify', l: 'Justify' }, { v: 'right', l: 'Right' },
    ];
    var cur = String(settings().textAlign || '');
    opts.forEach(function (o) {
      var b = document.createElement('button');
      b.type = 'button';
      b.className = 'room-align-chip' + (o.v === cur ? ' is-active' : '');
      b.setAttribute('data-align', o.v);
      b.textContent = o.l;
      b.addEventListener('click', function () {
        // Old handler: settings.textAlign = align; applySettings; persist.
        settings().textAlign = o.v;
        var chips = row.querySelectorAll('.room-align-chip');
        for (var i = 0; i < chips.length; i++) {
          chips[i].classList.toggle('is-active', chips[i].getAttribute('data-align') === o.v);
        }
        applyNow();
        persist();
      });
      row.appendChild(b);
    });
    section.appendChild(row);
  }

  function addPdfGroup(container) {
    var a = appearance();
    // Exact old visibility condition (reader_appearance.js
    // syncControlAvailability): isPdf = state.open && RS.isPdfOpen().
    var isPdf = !!(RS && RS.state && RS.state.open &&
      typeof RS.isPdfOpen === 'function' && RS.isPdfOpen());
    var section = addField(container, 'PDF');
    section.hidden = !isPdf;
    var row = document.createElement('div');
    row.className = 'room-pdf-btns';
    var buttons = [
      { l: 'Fit Page', fn: function () { return a.applyPdfFit('page'); } },
      { l: 'Fit Width', fn: function () { return a.applyPdfFit('width'); } },
      { l: 'Zoom −', fn: function () { return a.adjustPdfZoom(-0.1); } },
      { l: 'Zoom +', fn: function () { return a.adjustPdfZoom(0.1); } },
    ];
    buttons.forEach(function (b) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'room-pdf-btn';
      btn.textContent = b.l;
      btn.disabled = !isPdf;
      btn.addEventListener('click', function () {
        try { var p = b.fn(); if (p && p.catch) p.catch(function () {}); } catch (e) {}
      });
      row.appendChild(btn);
    });
    section.appendChild(row);
  }

  function buildAdvanced(adv) {
    var a = appearance();
    var s = settings();

    // 1. Font family — same 8 options/labels as #booksReaderFontFamily.
    addSelect(adv, 'Font', [
      { value: 'publisher', label: 'Publisher default' },
      { value: 'oldStyleTf', label: 'Serif (Old Style)' },
      { value: 'modernTf', label: 'Serif (Modern)' },
      { value: 'sansTf', label: 'Sans-serif' },
      { value: 'humanistTf', label: 'Sans-serif (Humanist)' },
      { value: 'monospaceTf', label: 'Monospace' },
      { value: 'AccessibleDfA', label: 'Accessible DfA' },
      { value: 'IAWriterDuospace', label: 'iA Writer Duospace' },
    ], String(s.fontFamily || 'publisher'), function (v) {
      settings().fontFamily = String(v || 'publisher');
      applyNow();
      persist();
    });

    // 2. Font weight — 100-900 step 100 like ensureFontWeightControl's slider;
    //    seam handles clamp + books_fontWeight localStorage + apply + persist.
    addSlider(adv, {
      label: 'Font weight',
      min: 100, max: 900, step: 100,
      selfApplied: true,
      get: function () { return Number(settings().fontWeight || 400); },
      set: function (v) {
        if (a && typeof a.setFontWeight === 'function') { try { a.setFontWeight(v); } catch (e) {} }
      },
      fmt: function (v) { return String(Math.round(v)); },
    });

    // 3. Max line width — 400-1600 step 50 + books_maxLineWidth echo,
    //    mirroring the old els.maxLineWidthSlider handler.
    addSlider(adv, {
      label: 'Max line width',
      min: 400, max: 1600, step: 50,
      get: function () { return normalizeMaxLineWidth(settings().maxLineWidth); },
      set: function (v) {
        var n = normalizeMaxLineWidth(v);
        settings().maxLineWidth = n;
        try { localStorage.setItem('books_maxLineWidth', String(n)); } catch (e) {}
      },
      fmt: function (v) { return String(v) + 'px'; },
    });

    // 4. Spread (2-col) — old handler: columnMode + engine.setColumnMode +
    //    persist, NO applySettings.
    addToggle(adv, 'Spread (2-col)', String(s.columnMode || 'auto') !== 'single', function (checked) {
      settings().columnMode = checked ? 'auto' : 'single';
      var engine = RS && RS.state && RS.state.engine;
      if (engine && typeof engine.setColumnMode === 'function') {
        try { engine.setColumnMode(settings().columnMode); } catch (e) {}
      }
      persist();
    });

    // 5. Text align chips.
    addAlignChips(adv);

    // 6-8. Spacing sliders — same ranges/steps/rounding/labels as old markup.
    addSlider(adv, {
      label: 'Letter spacing',
      min: 0, max: 0.5, step: 0.0625,
      get: function () { return Number(settings().letterSpacing || 0); },
      set: function (v) { settings().letterSpacing = Math.max(0, Math.min(0.5, Math.round(v * 100) / 100)); },
      fmt: function (v) { return Number(v).toFixed(2) + ' rem'; },
    });
    addSlider(adv, {
      label: 'Word spacing',
      min: 0, max: 1.0, step: 0.125,
      get: function () { return Number(settings().wordSpacing || 0); },
      set: function (v) { settings().wordSpacing = Math.max(0, Math.min(1.0, Math.round(v * 100) / 100)); },
      fmt: function (v) { return Number(v).toFixed(2) + ' rem'; },
    });
    addSlider(adv, {
      label: 'Paragraph spacing',
      min: 0, max: 2.0, step: 0.25,
      get: function () { return Number(settings().paraSpacing || 0); },
      set: function (v) { settings().paraSpacing = Math.max(0, Math.min(2.0, Math.round(v * 10) / 10)); },
      fmt: function (v) { return Number(v).toFixed(1) + ' rem'; },
    });

    // 9. Paragraph indent — same 5 options as #booksReaderParaIndent.
    addSelect(adv, 'Paragraph indent', [
      { value: '', label: 'Publisher default' },
      { value: '0', label: 'None' },
      { value: '1em', label: '1em' },
      { value: '1.5em', label: '1.5em' },
      { value: '2em', label: '2em' },
    ], String(s.paraIndent || ''), function (v) {
      settings().paraIndent = String(v || '');
      applyNow();
      persist();
    });

    // 10. Hyphenation — same 3 options as #booksReaderHyphens.
    addSelect(adv, 'Hyphenation', [
      { value: '', label: 'Publisher default' },
      { value: 'auto', label: 'Auto' },
      { value: 'none', label: 'Off' },
    ], String(s.bodyHyphens || ''), function (v) {
      settings().bodyHyphens = String(v || '');
      applyNow();
      persist();
    });

    // 11. Custom CSS — 600ms debounce, the old #booksReaderCustomCss discipline.
    var cssSection = addField(adv, 'Custom CSS');
    advCssEl = document.createElement('textarea');
    advCssEl.className = 'room-textarea';
    advCssEl.rows = 4;
    advCssEl.placeholder = '/* Custom CSS rules applied to book content */';
    advCssEl.value = String(s.customCss || '');
    cssSection.appendChild(advCssEl);
    advCssEl.addEventListener('input', function () {
      if (cssDebounceTimer) clearTimeout(cssDebounceTimer);
      cssDebounceTimer = setTimeout(function () {
        cssDebounceTimer = null;
        commitCustomCss();
      }, 600);
    });

    // 12. PDF fit/zoom group (hidden unless a PDF is open).
    addPdfGroup(adv);

    // 13. Invert images in dark mode — localStorage-only, like the old toggle.
    addToggle(adv, 'Invert images in dark mode',
      !!(a && typeof a.getInvertDarkImages === 'function' && a.getInvertDarkImages()),
      function (checked) {
        if (a && typeof a.setInvertDarkImages === 'function') {
          try { a.setInvertDarkImages(checked); } catch (e) {}
        }
      });
  }

  function renderAdvanced(container) {
    var toggle = document.createElement('button');
    toggle.type = 'button';
    toggle.className = 'room-adv-toggle';
    var caret = document.createElement('span');
    caret.className = 'room-adv-caret';
    toggle.appendChild(caret);
    toggle.appendChild(document.createTextNode('Advanced'));
    container.appendChild(toggle);

    var adv = document.createElement('div');
    adv.className = 'room-adv';
    adv.hidden = true; // collapsed by default — the primary panel stays clean
    container.appendChild(adv);

    buildAdvanced(adv);

    toggle.addEventListener('click', function () {
      adv.hidden = !adv.hidden;
      toggle.classList.toggle('is-open', !adv.hidden);
    });
  }

  function render(container) {
    container.innerHTML = '';
    gridEl = null; customRowEl = null; pageInputEl = null; inkInputEl = null;
    advCssEl = null;

    var title = document.createElement('div');
    title.className = 'room-panel-title';
    title.textContent = 'Type & theme';
    container.appendChild(title);

    var a = appearance();
    if (!a || typeof a.applySettings !== 'function' || typeof a.setTheme !== 'function' || !a.themes) {
      var row = document.createElement('div');
      row.className = 'room-search-row room-search-row--empty';
      row.textContent = 'Appearance unavailable';
      container.appendChild(row);
      return;
    }

    renderThemeGrid(container);
    renderCustomSection(container);
    renderSliders(container);
    renderAdvanced(container);
  }

  function onClose() {
    // Flush, don't drop — a color/CSS change mid-debounce must still land, or
    // the edit silently reverts to whatever was last saved on next boot.
    flushCustomPersist();
    flushCssPersist();
    gridEl = null; customRowEl = null; pageInputEl = null; inkInputEl = null;
    advCssEl = null;
  }

  window.__roomRegisterPanel && window.__roomRegisterPanel('aa', {
    render: render,
    onClose: onClose,
  });
})();
