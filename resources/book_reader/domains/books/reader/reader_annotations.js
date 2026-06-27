// BUILD_OVERHAUL: Annotation functionality extracted from monolithic controller.js
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  // PATCH6: annotation filters + export
  function getAnnotFilter() {
    var s = RS.state;
    if (!s.annotFilter) {
      try { s.annotFilter = localStorage.getItem('brAnnotFilter:v1') || 'all'; } catch (e) { s.annotFilter = 'all'; }
    }
    var f = String(s.annotFilter || 'all');
    return (f === 'highlights' || f === 'notes') ? f : 'all';
  }
  function setAnnotFilter(f) {
    var s = RS.state;
    s.annotFilter = (f === 'highlights' || f === 'notes') ? f : 'all';
    try { localStorage.setItem('brAnnotFilter:v1', s.annotFilter); } catch (e) {}
  }
  function buildAnnotExportMarkdown(list) {
    var b = RS.state.book;
    var title = (b && (b.title || b.name)) ? String(b.title || b.name) : 'Annotations';
    var out = [];
    out.push('# ' + title);
    out.push('');
    for (var i = 0; i < list.length; i++) {
      var a = list[i];
      var t = String(a.text || '').trim();
      var n = String(a.note || '').trim();
      var lab = String(a.chapterLabel || a.label || '').trim();
      var when = a.ts ? new Date(Number(a.ts)).toISOString().replace('T', ' ').slice(0, 16) : '';
      out.push('## ' + (lab || ('Highlight ' + (i + 1))));
      if (when) out.push('*' + when + '*');
      out.push('');
      if (t) out.push('> ' + t.replace(/\n/g, '\n> '));
      if (n) { out.push(''); out.push(n); }
      out.push('');
    }
    return out.join('\n');
  }


  // BUILD_OVERHAUL: annotations require EPUB-compatible engine APIs
  function annotationsSupported() {
    var state = RS.state;
    if (RS.isPdfOpen()) return false;
    if (!state.engine) return false;
    return typeof state.engine.getSelectionCFI === 'function'
      && typeof state.engine.addAnnotation === 'function';
  }

  // ── loadAnnotations ─────────────────────────────────────────
  async function loadAnnotations() {
    var state = RS.state;
    if (!state.book || !state.book.id) { state.annotations = []; return; }
    try {
      var result = await Tanko.api.booksAnnotations.get(state.book.id);
      state.annotations = Array.isArray(result) ? result : [];
    } catch (e) {
      state.annotations = [];
    }
  }

  // ── applyAnnotationsToView ──────────────────────────────────
  function applyAnnotationsToView() {
    var state = RS.state;
    if (!annotationsSupported()) return;
    if (!state.engine || !Array.isArray(state.annotations)) return;
    for (var i = 0; i < state.annotations.length; i++) {
      var ann = state.annotations[i];
      if (!ann || !ann.cfi) continue;
      try {
        if (typeof state.engine.setAnnotationMeta === 'function') {
          state.engine.setAnnotationMeta(ann.cfi, { color: ann.color, style: ann.style });
        }
        if (typeof state.engine.addAnnotation === 'function') {
          state.engine.addAnnotation({ value: ann.cfi });
        }
      } catch (e) { /* swallow */ }
    }
  }

  // ── saveAnnotation ──────────────────────────────────────────
  async function saveAnnotation() {
    var state = RS.state;
    var els = RS.ensureEls();
    if (!annotationsSupported()) return;
    if (!state.book || !state.book.id) return;

    var cfi = state.annotPendingCfi;
    if (cfi && typeof cfi === 'object') cfi = cfi.cfi || cfi.value || '';
    cfi = String(cfi || '').trim();
    if (!cfi) return;
    var text = state.annotPendingText || '';
    var editId = state.annotEditId;

    // Read color from active swatch
    var color = state.annotColor || '#FEF3BD';
    if (els.annotColorPicker) {
      var activeSwatch = els.annotColorPicker.querySelector('.booksAnnotColorSwatch.active');
      if (activeSwatch && activeSwatch.dataset.color) color = activeSwatch.dataset.color;
    }

    // Read style from active button
    var style = state.annotStyle || 'highlight';
    if (els.annotStylePicker) {
      var activeBtn = els.annotStylePicker.querySelector('.booksAnnotStyleBtn.active');
      if (activeBtn && activeBtn.dataset.style) style = activeBtn.dataset.style;
    }

    // Read note
    var note = '';
    if (els.annotNote) note = els.annotNote.value || '';

    // Capture chapter label from subtitle element
    var chapterLabel = '';
    var _els = RS.ensureEls();
    if (_els.subtitle && _els.subtitle.textContent && _els.subtitle.textContent !== '\u2014') {
      chapterLabel = _els.subtitle.textContent.trim();
    }

    var ann = {
      cfi: String(cfi),
      text: text,
      color: color,
      style: style,
      note: note,
      chapterLabel: chapterLabel,
      ts: Date.now(),
    };
    if (editId) ann.id = editId;

    try {
      await Tanko.api.booksAnnotations.save(state.book.id, ann);
    } catch (e) { /* swallow */ }

    await loadAnnotations();
    applyAnnotationsToView();
    renderAnnotationList();
    hideAnnotPopup();
    RS.showToast(editId ? 'Annotation updated' : 'Annotation saved');
  }

  // ── deleteAnnotationById ────────────────────────────────────
  async function deleteAnnotationById(annId) {
    var state = RS.state;
    if (!annotationsSupported()) return;
    if (!annId || !state.book || !state.book.id) return;

    var ann = null;
    for (var i = 0; i < state.annotations.length; i++) {
      if (state.annotations[i].id === annId) { ann = state.annotations[i]; break; }
    }

    if (ann && ann.cfi && state.engine) {
      try {
        if (typeof state.engine.deleteAnnotation === 'function') {
          state.engine.deleteAnnotation({ value: ann.cfi });
        }
        if (typeof state.engine.removeAnnotationMeta === 'function') {
          state.engine.removeAnnotationMeta(ann.cfi);
        }
      } catch (e) { /* swallow */ }
    }

    try {
      await Tanko.api.booksAnnotations.delete(state.book.id, annId);
    } catch (e) { /* swallow */ }

    await loadAnnotations();
    applyAnnotationsToView();
    renderAnnotationList();
    hideAnnotPopup();
    RS.showToast('Annotation deleted');
  }

  // ── isAnnotPopupOpen ────────────────────────────────────────
  function isAnnotPopupOpen() {
    var els = RS.ensureEls();
    return !!(els.annotPopup && !els.annotPopup.classList.contains('hidden'));
  }

  // ── hideAnnotPopup ──────────────────────────────────────────
  function hideAnnotPopup() {
    var els = RS.ensureEls();
    var state = RS.state;
    if (els.annotPopup) els.annotPopup.classList.add('hidden');
    state.annotEditId = null;
    state.annotPendingCfi = null;
    state.annotPendingText = '';
  }

  // ── Color swatches ──────────────────────────────────────────
  function renderColorSwatches(selectedHex) {
    var els = RS.ensureEls();
    if (!els.annotColorPicker) return;
    els.annotColorPicker.innerHTML = '';
    var colors = RS.ANNOT_COLORS;
    for (var i = 0; i < colors.length; i++) {
      var c = colors[i];
      var swatch = document.createElement('div');
      swatch.className = 'booksAnnotColorSwatch' + (c.hex === selectedHex ? ' active' : '');
      swatch.style.background = c.hex;
      swatch.dataset.color = c.hex;
      swatch.title = c.id;
      swatch.addEventListener('click', (function (hex) {
        return function () { selectColor(hex); };
      })(c.hex));
      els.annotColorPicker.appendChild(swatch);
    }
  }

  function selectColor(hex) {
    RS.state.annotColor = hex;
    var els = RS.ensureEls();
    if (!els.annotColorPicker) return;
    var swatches = els.annotColorPicker.querySelectorAll('.booksAnnotColorSwatch');
    for (var i = 0; i < swatches.length; i++) {
      swatches[i].classList.toggle('active', swatches[i].dataset.color === hex);
    }
  }

  // ── Style buttons ───────────────────────────────────────────
  function renderStyleButtons(selectedStyle) {
    var els = RS.ensureEls();
    if (!els.annotStylePicker) return;
    els.annotStylePicker.innerHTML = '';
    var styles = RS.ANNOT_STYLES;
    for (var i = 0; i < styles.length; i++) {
      var s = styles[i];
      var btn = document.createElement('button');
      btn.className = 'booksAnnotStyleBtn' + (s === selectedStyle ? ' active' : '');
      btn.textContent = s.charAt(0).toUpperCase() + s.slice(1);
      btn.dataset.style = s;
      btn.addEventListener('click', (function (style) {
        return function () { selectStyle(style); };
      })(s));
      els.annotStylePicker.appendChild(btn);
    }
  }

  function selectStyle(style) {
    RS.state.annotStyle = style;
    var els = RS.ensureEls();
    if (!els.annotStylePicker) return;
    var btns = els.annotStylePicker.querySelectorAll('.booksAnnotStyleBtn');
    for (var i = 0; i < btns.length; i++) {
      btns[i].classList.toggle('active', btns[i].dataset.style === style);
    }
  }

  // ── showAnnotPopup ──────────────────────────────────────────
  // FIX-ANN01: anchorRange is an optional DOM Range from inside the EPUB iframe,
  // used for iframe-aware popup positioning instead of window.getSelection().
  function showAnnotPopup(existingAnn, anchorRange) {
    var els = RS.ensureEls();
    var state = RS.state;
    if (!annotationsSupported()) return;
    if (!els.annotPopup) return;

    if (existingAnn && existingAnn.id) {
      // Editing existing annotation
      state.annotEditId = existingAnn.id;
      state.annotPendingCfi = existingAnn.cfi || null;
      state.annotPendingText = existingAnn.text || '';
      renderColorSwatches(existingAnn.color || '#FEF3BD');
      renderStyleButtons(existingAnn.style || 'highlight');
      if (els.annotNote) els.annotNote.value = existingAnn.note || '';
    } else {
      // New annotation — capture selection CFI and text, use last-used color/style
      state.annotEditId = null;
      if (existingAnn && existingAnn.cfi) {
        state.annotPendingCfi = existingAnn.cfi;
        state.annotPendingText = existingAnn.text || '';
      } else if (state.engine && typeof state.engine.getSelectionCFI === 'function') {
        var selData = state.engine.getSelectionCFI();
        if (selData) {
          state.annotPendingCfi = selData.cfi || selData.value || selData;
          state.annotPendingText = selData.text || '';
        }
      }
      renderColorSwatches(state.annotColor || '#FEF3BD');
      renderStyleButtons(state.annotStyle || 'highlight');
      if (els.annotNote) els.annotNote.value = '';
    }

    // Show/hide delete button
    if (els.annotDelete) {
      els.annotDelete.classList.toggle('hidden', !state.annotEditId);
    }

    // FIX-ANN01: Position popup using iframe-aware coordinates.
    // Priority: (1) anchorRange from engine show-annotation event (has iframe Range),
    // (2) Dict.getSelectionRect() which iterates iframe docs, (3) center of host.
    var POPUP_HEIGHT = 240;
    var positioned = false;
    if (els.host) {
      // (1) Engine-supplied Range from inside the EPUB iframe
      if (!positioned && anchorRange && typeof anchorRange.getBoundingClientRect === 'function') {
        try {
          var aRect = anchorRange.getBoundingClientRect();
          if (aRect && aRect.width > 0) {
            var docView = anchorRange.startContainer &&
              anchorRange.startContainer.ownerDocument &&
              anchorRange.startContainer.ownerDocument.defaultView;
            var frame = docView && docView.frameElement;
            var absBottom, absTop, absLeft;
            if (frame && typeof frame.getBoundingClientRect === 'function') {
              var fr = frame.getBoundingClientRect();
              absBottom = fr.top + aRect.bottom;
              absTop = fr.top + aRect.top;
              absLeft = fr.left + aRect.left;
            } else {
              absBottom = aRect.bottom;
              absTop = aRect.top;
              absLeft = aRect.left;
            }
            var top = absBottom + 8;
            if (top + POPUP_HEIGHT > window.innerHeight - 8) top = Math.max(8, absTop - POPUP_HEIGHT - 8);
            els.annotPopup.style.top = Math.max(8, top) + 'px';
            els.annotPopup.style.left = Math.max(8, Math.min(absLeft, window.innerWidth - 340)) + 'px';
            positioned = true;
          }
        } catch (e) { /* swallow */ }
      }

      // (2) Dict module's iframe-aware selection rect (handles renderer.getContents())
      if (!positioned) {
        try {
          var Dict = window.booksReaderDict;
          if (Dict && typeof Dict.getSelectionRect === 'function') {
            var selRect = Dict.getSelectionRect();
            if (selRect && selRect.width > 0) {
              var top2 = selRect.bottom + 8;
              if (top2 + POPUP_HEIGHT > window.innerHeight - 8) top2 = Math.max(8, selRect.top - POPUP_HEIGHT - 8);
              els.annotPopup.style.top = Math.max(8, top2) + 'px';
              els.annotPopup.style.left = Math.max(8, Math.min(selRect.left, window.innerWidth - 340)) + 'px';
              positioned = true;
            }
          }
        } catch (e) { /* swallow */ }
      }

      // (3) Fallback: center on reading host
      if (!positioned) {
        var hostRect = els.host.getBoundingClientRect();
        els.annotPopup.style.top = Math.max(8, hostRect.top + hostRect.height / 2 - 100) + 'px';
        els.annotPopup.style.left = Math.max(8, hostRect.left + hostRect.width / 2 - 160) + 'px';
      }
    }

    els.annotPopup.classList.remove('hidden');
  }

  // ── renderAnnotationList ────────────────────────────────────
  function renderAnnotationList() {
    var els = RS.ensureEls();
    var state = RS.state;
    if (!els.annotList) return;

    if (!annotationsSupported()) {
      els.annotList.innerHTML = '<div class="booksAnnotEmpty">Annotations are unavailable for this format</div>';
      return;
    }

    if (!state.annotations || state.annotations.length === 0) {
      els.annotList.innerHTML = '<div class="booksAnnotEmpty">No annotations yet</div>';
      return;
    }

    // PATCH6: toolbar (filters + export)
    var filter = getAnnotFilter();
    var toolbar = document.createElement('div');
    toolbar.className = 'br-annot-toolbar';
    toolbar.innerHTML = '' +
      '<div class="br-annot-filters">' +
        '<button type="button" class="br-annot-filter" data-filter="all">All</button>' +
        '<button type="button" class="br-annot-filter" data-filter="highlights">Highlights</button>' +
        '<button type="button" class="br-annot-filter" data-filter="notes">Notes</button>' +
      '</div>' +
      '<button type="button" class="br-annot-export">Export</button>';
    // Build filtered list
    var anns = Array.isArray(state.annotations) ? state.annotations.slice() : [];
    if (filter === 'notes') {
      anns = anns.filter(function (a) { return a && String(a.note || '').trim().length > 0; });
    } else if (filter === 'highlights') {
      anns = anns.filter(function (a) { return a && String(a.note || '').trim().length === 0; });
    }
    // Render
    var html = '';
    html += toolbar.outerHTML;
    for (var i = 0; i < anns.length; i++) {
      var ann = anns[i];
      var textPreview = RS.escHtml(String(ann.text || '').substring(0, 80));
      if (String(ann.text || '').length > 80) textPreview += '\u2026';
      var notePreview = ann.note ? '<div class="booksAnnotItemNote">' + RS.escHtml(String(ann.note).substring(0, 60)) + '</div>' : '';
      var dotColor = RS.escHtml(ann.color || '#FEF3BD');

      html += '<div class="booksAnnotItem" data-annot-id="' + RS.escHtml(ann.id) + '">'
        + '<span class="booksAnnotDot" style="background:' + dotColor + '"></span>'
        + '<div class="booksAnnotItemBody">'
        + '<div class="booksAnnotItemText">' + textPreview + '</div>'
        + notePreview
        + '</div>'
        + '<button class="booksAnnotItemDelete" data-annot-delete="' + RS.escHtml(ann.id) + '" title="Delete">\u00D7</button>'
        + '</div>';
    }
    els.annotList.innerHTML = html;

    // PATCH6: wire toolbar
    var tbar = els.annotList.querySelector('.br-annot-toolbar');
    if (tbar) {
      var fbtns = tbar.querySelectorAll('.br-annot-filter');
      for (var fb = 0; fb < fbtns.length; fb++) {
        (function (btn) {
          btn.classList.toggle('is-active', btn.dataset.filter === filter);
          btn.addEventListener('click', function () {
            setAnnotFilter(btn.dataset.filter);
            renderAnnotationList();
          });
        })(fbtns[fb]);
      }
      var exp = tbar.querySelector('.br-annot-export');
      if (exp) {
        exp.addEventListener('click', function () {
          try {
            var md = buildAnnotExportMarkdown(anns);
            if (Tanko.api && Tanko.api.clipboard && Tanko.api.clipboard.copyText) {
              Tanko.api.clipboard.copyText(md).then(function () { RS.showToast('Copied'); }).catch(function () { RS.showToast('Copy failed'); });
            } else {
              // Fallback: prompt
              window.prompt('Copy annotations', md);
            }
          } catch (e) { RS.showToast('Export failed'); }
        });
      }
      if (!document.getElementById('brAnnotToolbarStyle')) {
        var st = document.createElement('style');
        st.id = 'brAnnotToolbarStyle';
        st.textContent = [
          '.br-annot-toolbar{display:flex;align-items:center;gap:10px;margin:6px 0 10px 0;}',
          '.br-annot-filters{display:flex;gap:6px;}',
          '.br-annot-filter{padding:6px 10px;border-radius:10px;border:1px solid rgba(255,255,255,0.10);background:rgba(0,0,0,0.18);cursor:pointer;font-size:12px;}',
          '.br-annot-filter.is-active{outline:2px solid rgba(255,255,255,0.20);}',
          '.br-annot-export{margin-left:auto;padding:6px 10px;border-radius:10px;border:1px solid rgba(255,255,255,0.10);background:rgba(0,0,0,0.18);cursor:pointer;font-size:12px;}',
        ].join('\n');
        document.head.appendChild(st);
      }
    }

    // Wire click-to-navigate and edit
    var items = els.annotList.querySelectorAll('.booksAnnotItem');
    for (var j = 0; j < items.length; j++) {
      (function (item) {
        item.addEventListener('click', function (e) {
          if (e.target.classList.contains('booksAnnotItemDelete')) return;
          var id = item.getAttribute('data-annot-id');
          var target = null;
          for (var k = 0; k < state.annotations.length; k++) {
            if (state.annotations[k].id === id) { target = state.annotations[k]; break; }
          }
          if (target && target.cfi && state.engine && typeof state.engine.showAnnotation === 'function') {
            try { state.engine.showAnnotation({ value: target.cfi }); } catch (e2) { /* swallow */ }
          }
          // Open edit popup directly for the clicked annotation
          if (target) showAnnotPopup(target);
        });
      })(items[j]);
    }

    // Wire delete buttons
    var delBtns = els.annotList.querySelectorAll('.booksAnnotItemDelete');
    for (var d = 0; d < delBtns.length; d++) {
      (function (btn) {
        btn.addEventListener('click', function (e) {
          e.stopPropagation();
          var id = btn.getAttribute('data-annot-delete');
          if (id) deleteAnnotationById(id).catch(function () {});
        });
      })(delBtns[d]);
    }
  }

  // ── bind ────────────────────────────────────────────────────
  function bind() {
    var els = RS.ensureEls();
    var state = RS.state;

    // Save button
    if (els.annotSave) {
      els.annotSave.addEventListener('click', function () { saveAnnotation().catch(function () {}); });
    }

    // Close button
    if (els.annotClose) {
      els.annotClose.addEventListener('click', function () { hideAnnotPopup(); });
    }

    // Delete button (in popup)
    if (els.annotDelete) {
      els.annotDelete.addEventListener('click', function () {
        if (state.annotEditId) deleteAnnotationById(state.annotEditId).catch(function () {});
      });
    }
    // BUILD_OVERHAUL: contextmenu bridge is wired by reader_core iframe callbacks

    // Bus events
    bus.on('annot:show-popup', function (ann) { showAnnotPopup(ann); });
    bus.on('annot:hide-popup', function () { hideAnnotPopup(); });
    bus.on('annot:save', function () { saveAnnotation().catch(function () {}); });
    bus.on('annot:delete', function (id) { deleteAnnotationById(id).catch(function () {}); });

    bus.on('annotations:render', function () { renderAnnotationList(); });

    bus.on('sidebar:tab-changed', function (tab) {
      if (tab === 'annotations') renderAnnotationList();
    });
  }

  // ── onOpen ──────────────────────────────────────────────────
  async function onOpen() {
    var state = RS.state;
    if (!annotationsSupported()) {
      state.annotations = [];
      hideAnnotPopup();
      renderAnnotationList();
      return;
    }

    await loadAnnotations();
    applyAnnotationsToView();
    renderAnnotationList();

    // BUILD_OVERHAUL: wire engine callbacks with engine API methods
    if (state.engine) {
      if (typeof state.engine.onShowAnnotation === 'function') {
        state.engine.onShowAnnotation(function (data) {
          var cfi = data && (data.cfi || data.value) ? String(data.cfi || data.value) : '';
          if (!cfi) return;
          var found = null;
          for (var i = 0; i < state.annotations.length; i++) {
            if (state.annotations[i].cfi === cfi) { found = state.annotations[i]; break; }
          }
          // FIX-ANN01: pass range from engine event for iframe-aware popup positioning
          if (found) showAnnotPopup(found, data && data.range);
        });
      }

      if (typeof state.engine.onCreateOverlay === 'function') {
        state.engine.onCreateOverlay(function () {
          applyAnnotationsToView();
        });
      }
    }
  }

  // ── onClose ─────────────────────────────────────────────────
  function onClose() {
    var state = RS.state;
    if (state.engine) {
      try {
        if (typeof state.engine.onShowAnnotation === 'function') state.engine.onShowAnnotation(null);
      } catch (e) { /* swallow */ }
      try {
        if (typeof state.engine.onCreateOverlay === 'function') state.engine.onCreateOverlay(null);
      } catch (e2) { /* swallow */ }
    }
    RS.state.annotations = [];
    hideAnnotPopup();
  }

  // ── Export ──────────────────────────────────────────────────
  window.booksReaderAnnotations = {
    bind: bind,
    loadAnnotations: loadAnnotations,
    applyAnnotationsToView: applyAnnotationsToView,
    showAnnotPopup: showAnnotPopup,
    hideAnnotPopup: hideAnnotPopup,
    isAnnotPopupOpen: isAnnotPopupOpen,
    renderAnnotationList: renderAnnotationList,
    onOpen: onOpen,
    onClose: onClose,
  };
})();
