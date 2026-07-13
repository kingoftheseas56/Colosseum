// Custom dropdown widget — replaces ALL native <select> elements to avoid
// QWebEngineView white-blob rendering glitch on Windows.
//
// Provides a full shim: innerHTML, appendChild, options[], remove(), value,
// selectedIndex, and change events so tts_hud.js and reader_appearance.js
// all work unchanged.
//
// Uses MutationObserver to auto-upgrade dynamically-created <select> elements.
(function () {
  'use strict';

  var SKIP_IDS = { booksReaderTheme: true };
  var _upgraded = new WeakSet();

  function upgradeSelect(sel) {
    if (!sel || !sel.parentNode) return null;
    if (SKIP_IDS[sel.id]) return null;
    if (_upgraded.has(sel)) return null;
    _upgraded.add(sel);

    // Snapshot initial options
    var optionData = [];
    for (var i = 0; i < sel.options.length; i++) {
      optionData.push({ value: sel.options[i].value, label: sel.options[i].textContent, disabled: !!sel.options[i].disabled });
    }
    var currentValue = sel.value;
    var id = sel.id;
    var extraClasses = sel.className ? (' ' + sel.className) : '';

    // Create wrapper
    var wrap = document.createElement('div');
    wrap.className = 'br-custom-select' + extraClasses;
    if (sel.title) wrap.title = sel.title;
    if (id) wrap.setAttribute('data-select-id', id);
    // Copy data-* attributes
    if (sel.dataset) {
      for (var dk in sel.dataset) {
        if (sel.dataset.hasOwnProperty(dk)) wrap.dataset[dk] = sel.dataset[dk];
      }
    }

    // Trigger button
    var trigger = document.createElement('div');
    trigger.className = 'br-custom-select__trigger';
    trigger.setAttribute('tabindex', '0');
    trigger.setAttribute('role', 'combobox');
    trigger.setAttribute('aria-expanded', 'false');
    if (sel.getAttribute('aria-label')) trigger.setAttribute('aria-label', sel.getAttribute('aria-label'));
    wrap.appendChild(trigger);

    // Dropdown list
    var dropdown = document.createElement('div');
    dropdown.className = 'br-custom-select__dropdown';
    dropdown.setAttribute('role', 'listbox');
    wrap.appendChild(dropdown);

    // ── Internal option store ──────────────────────────────────
    var _opts = []; // { value, label, disabled, el }

    function _makeOptEl(o) {
      var div = document.createElement('div');
      div.className = 'br-custom-select__option';
      if (o.value === currentValue) div.classList.add('selected');
      if (o.disabled) { div.classList.add('disabled'); div.setAttribute('aria-disabled', 'true'); }
      div.setAttribute('data-value', o.value);
      div.setAttribute('role', 'option');
      div.textContent = o.label;
      return div;
    }

    function rebuildDropdown() {
      dropdown.innerHTML = '';
      for (var j = 0; j < _opts.length; j++) {
        var el = _makeOptEl(_opts[j]);
        _opts[j].el = el;
        dropdown.appendChild(el);
      }
    }

    function syncSelected() {
      for (var j = 0; j < _opts.length; j++) {
        if (_opts[j].el) _opts[j].el.classList.toggle('selected', _opts[j].value === currentValue);
      }
    }

    function updateLabel() {
      var label = '';
      for (var j = 0; j < _opts.length; j++) {
        if (_opts[j].value === currentValue) { label = _opts[j].label; break; }
      }
      trigger.textContent = label || currentValue || '\u00A0';
    }

    function openDropdown() {
      wrap.classList.add('open');
      trigger.setAttribute('aria-expanded', 'true');
    }
    function closeDropdown() {
      wrap.classList.remove('open');
      trigger.setAttribute('aria-expanded', 'false');
    }
    function toggleDropdown() {
      wrap.classList.contains('open') ? closeDropdown() : openDropdown();
    }

    function selectValue(val) {
      if (val === currentValue) { closeDropdown(); return; }
      currentValue = val;
      updateLabel();
      syncSelected();
      closeDropdown();
      wrap.dispatchEvent(new Event('change', { bubbles: true }));
    }

    // ── Events ────────────────────────────────────────────────
    trigger.addEventListener('click', function (e) { e.stopPropagation(); toggleDropdown(); });
    dropdown.addEventListener('click', function (e) {
      var t = e.target;
      while (t && t !== dropdown) {
        if (t.classList.contains('br-custom-select__option') && !t.classList.contains('disabled')) {
          selectValue(t.getAttribute('data-value'));
          return;
        }
        t = t.parentElement;
      }
    });
    trigger.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleDropdown(); }
      if (e.key === 'Escape') closeDropdown();
    });
    document.addEventListener('click', function (e) {
      if (!wrap.contains(e.target)) closeDropdown();
    });

    // ── Shim: .value ──────────────────────────────────────────
    Object.defineProperty(wrap, 'value', {
      get: function () { return currentValue; },
      set: function (v) {
        currentValue = String(v);
        updateLabel();
        syncSelected();
      },
      configurable: true
    });

    // ── Shim: .selectedIndex ──────────────────────────────────
    Object.defineProperty(wrap, 'selectedIndex', {
      get: function () {
        for (var j = 0; j < _opts.length; j++) {
          if (_opts[j].value === currentValue) return j;
        }
        return -1;
      },
      set: function (idx) {
        if (idx >= 0 && idx < _opts.length) {
          currentValue = _opts[idx].value;
          updateLabel(); syncSelected();
        }
      },
      configurable: true
    });

    // ── Shim: .options ────────────────────────────────────────
    var _optionsProxy = (typeof Proxy !== 'undefined')
      ? new Proxy(_opts, {
          get: function (target, prop) {
            if (prop === 'length') return target.length;
            var idx = Number(prop);
            if (!isNaN(idx) && idx >= 0 && idx < target.length) {
              return { value: target[idx].value, textContent: target[idx].label, disabled: target[idx].disabled };
            }
            return undefined;
          }
        })
      : { get length() { return _opts.length; } };

    Object.defineProperty(wrap, 'options', {
      get: function () { return _optionsProxy; },
      configurable: true
    });

    // Helper to rebuild proxy after _opts array reference changes
    function _rebuildProxy() {
      if (typeof Proxy !== 'undefined') {
        _optionsProxy = new Proxy(_opts, {
          get: function (target, prop) {
            if (prop === 'length') return target.length;
            var idx = Number(prop);
            if (!isNaN(idx) && idx >= 0 && idx < target.length) {
              return { value: target[idx].value, textContent: target[idx].label, disabled: target[idx].disabled };
            }
            return undefined;
          }
        });
      }
    }

    // ── Shim: .innerHTML ──────────────────────────────────────
    Object.defineProperty(wrap, 'innerHTML', {
      get: function () { return ''; },
      set: function (html) {
        if (html === '') {
          _opts.length = 0;
          _rebuildProxy();
          rebuildDropdown();
          currentValue = '';
          updateLabel();
        }
      },
      configurable: true
    });

    // ── Shim: .appendChild ────────────────────────────────────
    var _nativeAppend = HTMLElement.prototype.appendChild;
    wrap.appendChild = function (child) {
      if (child === trigger || child === dropdown) {
        return _nativeAppend.call(wrap, child);
      }
      // Treat as <option> being added
      var entry = {
        value: child.value || '',
        label: child.textContent || '',
        disabled: !!child.disabled,
        el: null
      };
      _opts.push(entry);
      var div = _makeOptEl(entry);
      entry.el = div;
      dropdown.appendChild(div);
      if (!currentValue && !entry.disabled && _opts.length === 1) {
        currentValue = entry.value;
      }
      updateLabel();
      return child;
    };

    // ── Shim: .remove(index) ──────────────────────────────────
    wrap.remove = function (idx) {
      if (typeof idx === 'number' && idx >= 0 && idx < _opts.length) {
        if (_opts[idx].el && _opts[idx].el.parentNode) {
          _opts[idx].el.parentNode.removeChild(_opts[idx].el);
        }
        _opts.splice(idx, 1);
        updateLabel();
      } else if (idx === undefined) {
        if (wrap.parentNode) wrap.parentNode.removeChild(wrap);
      }
    };

    // ── Shim: .multiple / .size / .removeAttribute — no-ops ──
    Object.defineProperty(wrap, 'multiple', { get: function () { return false; }, set: function () {}, configurable: true });
    Object.defineProperty(wrap, 'size', { get: function () { return 1; }, set: function () {}, configurable: true });
    var _origRemoveAttr = wrap.removeAttribute.bind(wrap);
    wrap.removeAttribute = function (name) {
      if (name === 'size' || name === 'multiple') return;
      _origRemoveAttr(name);
    };

    // ── Shim: .focus() ───────────────────────────────────────
    wrap.focus = function () { trigger.focus(); };

    // ── Shim: .disabled ───────────────────────────────────────
    var _disabled = sel.disabled || false;
    Object.defineProperty(wrap, 'disabled', {
      get: function () { return _disabled; },
      set: function (v) {
        _disabled = !!v;
        trigger.style.opacity = _disabled ? '0.5' : '';
        trigger.style.pointerEvents = _disabled ? 'none' : '';
      },
      configurable: true
    });

    // ── Build initial options & replace in DOM ────────────────
    for (var k = 0; k < optionData.length; k++) {
      _opts.push({ value: optionData[k].value, label: optionData[k].label, disabled: optionData[k].disabled, el: null });
    }
    rebuildDropdown();
    updateLabel();

    sel.parentNode.insertBefore(wrap, sel);
    sel.style.display = 'none';
    sel.setAttribute('data-upgraded', '1');
    if (id) {
      sel.removeAttribute('id');
      wrap.id = id;
    }
    return wrap;
  }

  // ── Upgrade all existing selects ───────────────────────────
  function upgradeAll() {
    var selects = document.querySelectorAll('select');
    for (var i = 0; i < selects.length; i++) {
      upgradeSelect(selects[i]);
    }
  }

  // ── MutationObserver: auto-upgrade dynamically created selects ──
  function startObserver() {
    if (typeof MutationObserver === 'undefined') return;
    var observer = new MutationObserver(function (mutations) {
      for (var m = 0; m < mutations.length; m++) {
        var added = mutations[m].addedNodes;
        for (var n = 0; n < added.length; n++) {
          var node = added[n];
          if (node.nodeType !== 1) continue;
          // Check if the added node itself is a select
          if (node.tagName === 'SELECT' && !node.getAttribute('data-upgraded') && !SKIP_IDS[node.id]) {
            // Defer to next microtask so the select is fully populated
            (function (s) { Promise.resolve().then(function () { upgradeSelect(s); }); })(node);
          }
          // Check children
          if (node.querySelectorAll) {
            var nested = node.querySelectorAll('select:not([data-upgraded])');
            for (var j = 0; j < nested.length; j++) {
              if (!SKIP_IDS[nested[j].id]) {
                (function (s) { Promise.resolve().then(function () { upgradeSelect(s); }); })(nested[j]);
              }
            }
          }
        }
      }
    });
    observer.observe(document.body || document.documentElement, { childList: true, subtree: true });
  }

  // ── Init ───────────────────────────────────────────────────
  function init() {
    upgradeAll();
    startObserver();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }

  window.brCustomSelect = { upgradeAll: upgradeAll, upgradeSelect: upgradeSelect };
})();
