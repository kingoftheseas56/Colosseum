// READEST_PORT: Reading ruler overlay (Readest-inspired) for books reader
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  var STORE_KEY = 'brReadingRuler:v1';
  var DEFAULTS = {
    enabled: false,
    yPct: 40,
    heightPx: 92,
    dimPct: 42,
    tintPct: 12,
    color: 'warm'
  };

  var COLOR_MAP = {
    warm: '255,236,170',
    green: '188,247,195',
    blue: '182,224,255',
    gray: '255,255,255'
  };

  var runtime = {
    open: false,
    bound: false,
    uiBound: false,
    dragging: false,
    dragPointerId: null,
    resizeObserver: null,
    dom: null,
    uiRoot: null
  };

  function clamp(n, min, max) {
    n = Number(n);
    if (!Number.isFinite(n)) n = min;
    if (n < min) return min;
    if (n > max) return max;
    return n;
  }

  function getSettings() {
    var state = RS.state;
    if (!state.readingRuler || typeof state.readingRuler !== 'object') {
      state.readingRuler = loadSettings();
    }
    return state.readingRuler;
  }

  function loadSettings() {
    var out = {
      enabled: DEFAULTS.enabled,
      yPct: DEFAULTS.yPct,
      heightPx: DEFAULTS.heightPx,
      dimPct: DEFAULTS.dimPct,
      tintPct: DEFAULTS.tintPct,
      color: DEFAULTS.color
    };
    try {
      var raw = localStorage.getItem(STORE_KEY);
      var parsed = raw ? JSON.parse(raw) : null;
      if (parsed && typeof parsed === 'object') {
        out.enabled = !!parsed.enabled;
        out.yPct = clamp(parsed.yPct, 8, 92);
        out.heightPx = clamp(parsed.heightPx, 36, 260);
        out.dimPct = clamp(parsed.dimPct, 0, 85);
        out.tintPct = clamp(parsed.tintPct, 0, 60);
        out.color = COLOR_MAP[String(parsed.color || '')] ? String(parsed.color) : DEFAULTS.color;
      }
    } catch (e) {}
    return out;
  }

  function saveSettings() {
    try { localStorage.setItem(STORE_KEY, JSON.stringify(getSettings())); } catch (e) {}
  }

  function getReadingArea() {
    try {
      var root = document.getElementById('booksReaderView');
      return root ? root.querySelector('.br-reading-area') : null;
    } catch (e) { return null; }
  }

  function ensureDom() {
    var area = getReadingArea();
    if (!area) return null;

    if (runtime.dom && runtime.dom.root && runtime.dom.root.parentElement === area) {
      return runtime.dom;
    }

    var existing = area.querySelector('.br-reading-ruler');
    if (existing) {
      runtime.dom = {
        area: area,
        root: existing,
        topPane: existing.querySelector('.br-reading-ruler-pane-top'),
        band: existing.querySelector('.br-reading-ruler-band'),
        bottomPane: existing.querySelector('.br-reading-ruler-pane-bottom'),
        handle: existing.querySelector('.br-reading-ruler-handle')
      };
      bindHandleIfNeeded();
      return runtime.dom;
    }

    var root = document.createElement('div');
    root.className = 'br-reading-ruler hidden';
    root.setAttribute('aria-hidden', 'true');
    root.innerHTML = '' +
      '<div class="br-reading-ruler-pane br-reading-ruler-pane-top"></div>' +
      '<div class="br-reading-ruler-band">' +
        '<button type="button" class="br-reading-ruler-handle" title="Drag reading ruler" aria-label="Drag reading ruler"></button>' +
      '</div>' +
      '<div class="br-reading-ruler-pane br-reading-ruler-pane-bottom"></div>';

    area.appendChild(root);

    runtime.dom = {
      area: area,
      root: root,
      topPane: root.querySelector('.br-reading-ruler-pane-top'),
      band: root.querySelector('.br-reading-ruler-band'),
      bottomPane: root.querySelector('.br-reading-ruler-pane-bottom'),
      handle: root.querySelector('.br-reading-ruler-handle')
    };

    bindHandleIfNeeded();
    return runtime.dom;
  }

  function bindHandleIfNeeded() {
    if (!runtime.dom || !runtime.dom.handle || runtime.dom.handle._brRulerBound) return;
    var handle = runtime.dom.handle;
    handle._brRulerBound = true;

    handle.addEventListener('pointerdown', function (e) {
      if (!runtime.open) return;
      var s = getSettings();
      if (!s.enabled) return;
      runtime.dragging = true;
      runtime.dragPointerId = e.pointerId;
      try { handle.setPointerCapture(e.pointerId); } catch (err) {}
      updatePositionFromClientY(e.clientY);
      try { bus.emit('reader:user-activity'); } catch (err) {}
      e.preventDefault();
      e.stopPropagation();
    });

    handle.addEventListener('pointermove', function (e) {
      if (!runtime.dragging) return;
      if (runtime.dragPointerId != null && e.pointerId !== runtime.dragPointerId) return;
      updatePositionFromClientY(e.clientY);
      e.preventDefault();
      e.stopPropagation();
    });

    function stopDrag(e) {
      if (!runtime.dragging) return;
      if (runtime.dragPointerId != null && e && e.pointerId != null && e.pointerId !== runtime.dragPointerId) return;
      runtime.dragging = false;
      try { handle.releasePointerCapture(runtime.dragPointerId); } catch (err) {}
      runtime.dragPointerId = null;
      saveSettings();
      try { bus.emit('reader:user-activity'); } catch (err) {}
      if (e) {
        e.preventDefault();
        e.stopPropagation();
      }
    }

    handle.addEventListener('pointerup', stopDrag);
    handle.addEventListener('pointercancel', stopDrag);
    handle.addEventListener('lostpointercapture', function () {
      runtime.dragging = false;
      runtime.dragPointerId = null;
    });
  }

  function updatePositionFromClientY(clientY) {
    var dom = ensureDom();
    if (!dom || !dom.area) return;
    var rect = dom.area.getBoundingClientRect();
    if (!rect || !rect.height) return;
    var pct = ((Number(clientY) - rect.top) / rect.height) * 100;
    var s = getSettings();
    s.yPct = clamp(pct, 8, 92);
    syncControls();
    applyRuler();
  }

  function colorRgb(colorId) {
    var id = String(colorId || '');
    return COLOR_MAP[id] || COLOR_MAP.warm;
  }

  function applyRuler() {
    var dom = ensureDom();
    var s = getSettings();
    syncControls();

    if (!dom || !dom.root || !dom.area) return;

    var shouldShow = !!(runtime.open && s.enabled);
    dom.root.classList.toggle('hidden', !shouldShow);
    dom.root.setAttribute('aria-hidden', shouldShow ? 'false' : 'true');

    if (!shouldShow) return;

    var h = dom.area.clientHeight || 0;
    var yPx = (h * clamp(s.yPct, 0, 100)) / 100;
    var bandH = clamp(s.heightPx, 36, 260);
    var half = bandH / 2;
    var bandTop = Math.max(0, Math.round(yPx - half));
    var bandHeight = Math.round(Math.min(bandH, h));
    if (bandTop + bandHeight > h) bandHeight = Math.max(0, h - bandTop);
    var topH = bandTop;
    var bottomTop = Math.min(h, bandTop + bandHeight);

    dom.root.style.setProperty('--br-ruler-color-rgb', colorRgb(s.color));
    dom.root.style.setProperty('--br-ruler-dim-opacity', String(clamp(s.dimPct, 0, 85) / 100));
    dom.root.style.setProperty('--br-ruler-band-opacity', String(clamp(s.tintPct, 0, 60) / 100));

    if (dom.topPane) {
      dom.topPane.style.top = '0px';
      dom.topPane.style.height = String(topH) + 'px';
    }
    if (dom.band) {
      dom.band.style.top = String(bandTop) + 'px';
      dom.band.style.height = String(bandHeight) + 'px';
    }
    if (dom.bottomPane) {
      dom.bottomPane.style.top = String(bottomTop) + 'px';
      dom.bottomPane.style.bottom = '0px';
    }
  }

  function setLabelText(labelSel, value) {
    var root = runtime.uiRoot;
    if (!root) return;
    var el = root.querySelector(labelSel);
    if (el) el.textContent = String(value);
  }

  function syncControls() {
    if (!runtime.uiRoot) return;
    var s = getSettings();

    var enabled = runtime.uiRoot.querySelector('[data-ruler="enabled"]');
    var y = runtime.uiRoot.querySelector('[data-ruler="yPct"]');
    var height = runtime.uiRoot.querySelector('[data-ruler="heightPx"]');
    var dim = runtime.uiRoot.querySelector('[data-ruler="dimPct"]');
    var tint = runtime.uiRoot.querySelector('[data-ruler="tintPct"]');

    if (enabled) enabled.checked = !!s.enabled;
    if (y) y.value = String(Math.round(clamp(s.yPct, 8, 92)));
    if (height) height.value = String(Math.round(clamp(s.heightPx, 36, 260)));
    if (dim) dim.value = String(Math.round(clamp(s.dimPct, 0, 85)));
    if (tint) tint.value = String(Math.round(clamp(s.tintPct, 0, 60)));

    setLabelText('[data-ruler-label="yPct"]', String(Math.round(clamp(s.yPct, 8, 92))) + '%');
    setLabelText('[data-ruler-label="heightPx"]', String(Math.round(clamp(s.heightPx, 36, 260))) + 'px');
    setLabelText('[data-ruler-label="dimPct"]', String(Math.round(clamp(s.dimPct, 0, 85))) + '%');
    setLabelText('[data-ruler-label="tintPct"]', String(Math.round(clamp(s.tintPct, 0, 60))) + '%');

    var chips = runtime.uiRoot.querySelectorAll('.br-ruler-swatch');
    for (var i = 0; i < chips.length; i++) {
      chips[i].classList.toggle('active', String(chips[i].dataset.color || '') === String(s.color || ''));
    }
  }

  function chipHtml(id, label, color) {
    return '' +
      '<button type="button" class="br-ruler-swatch" data-color="' + String(id) + '">' +
        '<span class="br-ruler-swatch-dot" style="background:' + String(color) + ';"></span>' +
        '<span>' + String(label) + '</span>' +
      '</button>';
  }

  function buildUiHtml() {
    return '' +
      '<div class="br-settings-section br-ruler-settings" data-br-ruler-ui="1">' +
        '<div class="br-settings-toggle-row">' +
          '<div class="br-settings-label">Reading ruler</div>' +
          '<input type="checkbox" data-ruler="enabled" />' +
        '</div>' +
        '<div class="br-settings-label" style="margin-top:8px;">Position</div>' +
        '<div class="br-settings-slider-row">' +
          '<input class="br-settings-slider" type="range" min="8" max="92" step="1" data-ruler="yPct" />' +
          '<div class="br-settings-value" data-ruler-label="yPct">40%</div>' +
        '</div>' +
        '<div class="br-settings-label" style="margin-top:8px;">Height</div>' +
        '<div class="br-settings-slider-row">' +
          '<input class="br-settings-slider" type="range" min="36" max="260" step="2" data-ruler="heightPx" />' +
          '<div class="br-settings-value" data-ruler-label="heightPx">92px</div>' +
        '</div>' +
        '<div class="br-settings-label" style="margin-top:8px;">Dim background</div>' +
        '<div class="br-settings-slider-row">' +
          '<input class="br-settings-slider" type="range" min="0" max="85" step="1" data-ruler="dimPct" />' +
          '<div class="br-settings-value" data-ruler-label="dimPct">42%</div>' +
        '</div>' +
        '<div class="br-settings-label" style="margin-top:8px;">Band tint</div>' +
        '<div class="br-settings-slider-row">' +
          '<input class="br-settings-slider" type="range" min="0" max="60" step="1" data-ruler="tintPct" />' +
          '<div class="br-settings-value" data-ruler-label="tintPct">12%</div>' +
        '</div>' +
        '<div class="br-settings-label" style="margin-top:8px;">Color</div>' +
        '<div class="br-ruler-swatch-row">' +
          chipHtml('warm', 'Warm', '#ffe08a') +
          chipHtml('green', 'Green', '#b7f3bd') +
          chipHtml('blue', 'Blue', '#b5dcff') +
          chipHtml('gray', 'Gray', '#ffffff') +
        '</div>' +
        '<div class="br-settings-actions">' +
          '<button type="button" class="br-settings-mini-btn" data-ruler-action="center">Center</button>' +
          '<button type="button" class="br-settings-mini-btn" data-ruler-action="reset">Reset</button>' +
        '</div>' +
        '<div class="br-settings-value" style="margin-top:8px; text-align:left; min-width:0;">Drag the pill handle on the page to move the ruler.</div>' +
      '</div>';
  }

  function ensureUi() {
    var els = RS.ensureEls();
    var overlayFont = els.overlayFont;
    if (!overlayFont) return null;
    var body = overlayFont.querySelector('.br-overlay-body');
    if (!body) return null;

    var root = body.querySelector('[data-br-ruler-ui="1"]');
    if (!root) {
      var wrap = document.createElement('div');
      wrap.innerHTML = buildUiHtml();
      root = wrap.firstElementChild;
      body.appendChild(root);
    }
    runtime.uiRoot = root;

    if (!runtime.uiBound) {
      runtime.uiBound = true;

      var checkbox = root.querySelector('[data-ruler="enabled"]');
      if (checkbox) {
        checkbox.addEventListener('change', function () {
          var s = getSettings();
          s.enabled = !!checkbox.checked;
          saveSettings();
          applyRuler();
          try { RS.showToast(s.enabled ? 'Reading ruler on' : 'Reading ruler off', 1300); } catch (e) {}
        });
      }

      var sliderKeys = ['yPct', 'heightPx', 'dimPct', 'tintPct'];
      for (var i = 0; i < sliderKeys.length; i++) {
        (function (key) {
          var el = root.querySelector('[data-ruler="' + key + '"]');
          if (!el) return;
          el.addEventListener('input', function () {
            var s = getSettings();
            s[key] = Number(el.value);
            applyRuler();
          });
          el.addEventListener('change', function () {
            var s = getSettings();
            s[key] = Number(el.value);
            saveSettings();
            applyRuler();
          });
        })(sliderKeys[i]);
      }

      var chips = root.querySelectorAll('.br-ruler-swatch');
      for (var c = 0; c < chips.length; c++) {
        (function (chip) {
          chip.addEventListener('click', function () {
            var s = getSettings();
            var next = String(chip.dataset.color || '');
            if (!COLOR_MAP[next]) return;
            s.color = next;
            saveSettings();
            applyRuler();
          });
        })(chips[c]);
      }

      root.addEventListener('click', function (e) {
        var btn = e.target && e.target.closest ? e.target.closest('[data-ruler-action]') : null;
        if (!btn) return;
        var action = String(btn.dataset.rulerAction || '');
        var s = getSettings();
        if (action === 'center') {
          s.yPct = 50;
          saveSettings();
          applyRuler();
          return;
        }
        if (action === 'reset') {
          s.enabled = DEFAULTS.enabled;
          s.yPct = DEFAULTS.yPct;
          s.heightPx = DEFAULTS.heightPx;
          s.dimPct = DEFAULTS.dimPct;
          s.tintPct = DEFAULTS.tintPct;
          s.color = DEFAULTS.color;
          saveSettings();
          applyRuler();
        }
      });
    }

    syncControls();
    return root;
  }

  function bindResizeTracking() {
    if (runtime.resizeObserver) return;
    if (typeof ResizeObserver !== 'function') return;
    try {
      runtime.resizeObserver = new ResizeObserver(function () {
        applyRuler();
      });
      var dom = ensureDom();
      if (dom && dom.area) runtime.resizeObserver.observe(dom.area);
    } catch (e) {
      runtime.resizeObserver = null;
    }
  }

  function unbindResizeTracking() {
    if (runtime.resizeObserver) {
      try { runtime.resizeObserver.disconnect(); } catch (e) {}
      runtime.resizeObserver = null;
    }
  }

  function onWindowResize() {
    if (!runtime.open) return;
    applyRuler();
  }

  function onReaderRelocated() {
    if (!runtime.open || runtime.dragging) return;
    applyRuler();
  }

  function bind() {
    if (runtime.bound) return;
    runtime.bound = true;

    getSettings();
    ensureUi();

    window.addEventListener('resize', onWindowResize);
    if (bus && typeof bus.on === 'function') {
      bus.on('reader:relocated', onReaderRelocated);
      bus.on('reader:user-activity', function () {
        if (!runtime.open) return;
        if (runtime.dom && (!runtime.dom.root || !runtime.dom.root.isConnected)) {
          runtime.dom = null;
          applyRuler();
        }
      });
    }
  }

  function onOpen() {
    runtime.open = true;
    runtime.dom = null;
    ensureUi();
    ensureDom();
    bindResizeTracking();
    applyRuler();
  }

  function onClose() {
    runtime.open = false;
    runtime.dragging = false;
    runtime.dragPointerId = null;
    if (runtime.dom && runtime.dom.root) {
      runtime.dom.root.classList.add('hidden');
      runtime.dom.root.setAttribute('aria-hidden', 'true');
    }
    unbindResizeTracking();
  }

  window.booksReaderRuler = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    applyRuler: applyRuler
  };
})();
