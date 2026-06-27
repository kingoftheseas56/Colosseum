// BUILD_OVERHAUL: Generic overlay controller — toggle/open/close, one-at-a-time, backdrop dismiss
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  // FIX_AUDIT: split single settings overlay into dedicated font/theme/tts overlays.
  var NAMES = ['search', 'font', 'theme', 'tts', 'keys'];
  var current = ''; // name of currently open overlay, or ''
  var lastOpenerBtn = null;

  function capitalize(s) { return s.charAt(0).toUpperCase() + s.slice(1); }

  function getOverlayEl(name) {
    return document.getElementById('brOverlay' + capitalize(name));
  }

  function getToolbarBtn(name) {
    var map = {
      search: 'booksReaderSearchBtn',
      font: 'booksReaderFontBtn',
      theme: 'booksReaderThemeBtn',
      tts: 'booksReaderTtsBtn',
      keys: 'booksReaderKeysBtn',
    };
    return document.getElementById(map[name] || '');
  }

  function getBackdrop() {
    return document.getElementById('brOverlayBackdrop');
  }

  function getReadingArea() {
    return document.querySelector('.br-reading-area');
  }

  // Position overlay panel below its toolbar button
  function positionOverlay(name) {
    var el = getOverlayEl(name);
    if (!el) return;

    // FIX_AUDIT: keep settings-family overlays centered as modals.
    if (name === 'font' || name === 'theme' || name === 'tts' || name === 'keys') {
      el.style.right = '';
      el.style.left = '50%';
      el.style.transform = 'translateX(-50%)';
      return;
    }

    // Others: anchor below toolbar button
    var btn = getToolbarBtn(name);
    var area = getReadingArea();
    if (!btn || !area) {
      el.style.right = '8px';
      return;
    }

    var btnRect = btn.getBoundingClientRect();
    var areaRect = area.getBoundingClientRect();
    var rightOffset = areaRect.right - btnRect.right;
    el.style.left = '';
    el.style.transform = '';
    el.style.right = Math.max(8, rightOffset - 12) + 'px';
  }


  function repositionCurrent() {
    if (!current) return;
    positionOverlay(current);
  }

  function open(name) {
    if (NAMES.indexOf(name) === -1) return;
    if (current === name) return;
    closeAll();

    var el = getOverlayEl(name);
    var backdrop = getBackdrop();
    var btn = getToolbarBtn(name);
    if (!el) return;

    positionOverlay(name);
    el.classList.remove('hidden');
    if (backdrop) backdrop.classList.remove('hidden');
    if (btn) btn.classList.add('active');
    lastOpenerBtn = btn || null;
    current = name;

    bus.emit('overlay:opened', name);

    // Auto-focus first input
    var inp = el.querySelector('input, textarea');
    if (inp) setTimeout(function () { try { inp.focus(); } catch (e) {} }, 50);
  }

  function closeAll() {
    for (var i = 0; i < NAMES.length; i++) {
      var el = getOverlayEl(NAMES[i]);
      var btn = getToolbarBtn(NAMES[i]);
      if (el) el.classList.add('hidden');
      if (btn) btn.classList.remove('active');
    }
    var prev = current;
    var backdrop = getBackdrop();
    if (backdrop) backdrop.classList.add('hidden');
    current = '';
    if (prev) bus.emit('overlay:closed', prev);
    var restoreBtn = lastOpenerBtn;
    lastOpenerBtn = null;
    if (restoreBtn && typeof restoreBtn.focus === 'function') {
      setTimeout(function () { try { restoreBtn.focus(); } catch (e) {} }, 0);
    }
  }

  function toggle(name) {
    if (current === name) {
      closeAll();
    } else {
      open(name);
    }
  }

  function isOpen(name) {
    if (name) return current === name;
    return current !== '';
  }

  // ── Bind ──────────────────────────────────────────────────────

  function bind() {
    // Close buttons inside overlays
    var closeBtns = document.querySelectorAll('.br-overlay-close');
    for (var i = 0; i < closeBtns.length; i++) {
      closeBtns[i].addEventListener('click', function () { closeAll(); });
    }

    // Backdrop click
    var backdrop = getBackdrop();
    if (backdrop) {
      backdrop.addEventListener('click', function () { closeAll(); });
    }

    // Toolbar buttons with data-overlay attribute
    var overlayBtns = document.querySelectorAll('.br-toolbar-overlay-btn[data-overlay]');
    for (var j = 0; j < overlayBtns.length; j++) {
      (function (btn) {
        btn.addEventListener('click', function () {
          toggle(btn.getAttribute('data-overlay'));
        });
      })(overlayBtns[j]);
    }

    // Keep panel alignment stable if layout changes while an overlay is open
    window.addEventListener('resize', repositionCurrent, { passive: true });
    bus.on('sidebar:toggled', function () { repositionCurrent(); });
    bus.on('reader:fullscreen-changed', function () { repositionCurrent(); });

    // Bus events
    bus.on('overlay:toggle', function (name) { toggle(name); });
    bus.on('overlay:open', function (name) { open(name); });
    bus.on('overlay:close', function () { closeAll(); });
  }

  // ── Lifecycle ─────────────────────────────────────────────────

  function onOpen() {
    // Overlays always start closed when a book opens
    closeAll();
  }

  function onClose() {
    closeAll();
  }

  // ── Export ─────────────────────────────────────────────────────

  window.booksReaderOverlays = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    toggle: toggle,
    open: open,
    closeAll: closeAll,
    isOpen: isOpen,
  };
})();
