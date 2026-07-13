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
      applyNow();
      persist();
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

  function render(container) {
    container.innerHTML = '';
    gridEl = null; customRowEl = null; pageInputEl = null; inkInputEl = null;

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
  }

  function onClose() {
    // Flush, don't drop — a color change mid-debounce must still land, or the
    // custom pick silently reverts to whatever was last saved on next boot.
    flushCustomPersist();
    gridEl = null; customRowEl = null; pageInputEl = null; inkInputEl = null;
  }

  window.__roomRegisterPanel && window.__roomRegisterPanel('aa', {
    render: render,
    onClose: onClose,
  });
})();
