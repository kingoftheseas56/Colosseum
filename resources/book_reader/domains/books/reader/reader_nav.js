// BUILD_OVERHAUL: Navigation module — page nav, scrub bar, chapter markers, goto
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  // ── Page navigation ──────────────────────────────────────────

  async function stepNext() {
    var state = RS.state;
    if (!state.engine || typeof state.engine.next !== 'function') return;
    await state.engine.next();
    await RS.saveProgress();
    await syncProgressUI();
  }

  async function stepPrev() {
    var state = RS.state;
    if (!state.engine || typeof state.engine.prev !== 'function') return;
    await state.engine.prev();
    await RS.saveProgress();
    await syncProgressUI();
  }

  function isScrolledTextFlow() {
    return RS.isEpubOrTxtOpen() && String(RS.state.settings.flowMode || 'paginated') === 'scrolled';
  }

  async function stepNextSmart() {
    // FIX-NAV01: In scrolled mode arrows jump chapters (no discrete pages); in paged mode they step pages.
    if (isScrolledTextFlow()) return nextChapter();
    return stepNext();
  }

  async function stepPrevSmart() {
    // FIX-NAV01: In scrolled mode arrows jump chapters (no discrete pages); in paged mode they step pages.
    if (isScrolledTextFlow()) return prevChapter();
    return stepPrev();
  }

  // ── Progress rendering ───────────────────────────────────────

  function renderProgressFraction(fraction) {
    // Footer scrub bar removed — corner overlays handle display now
  }

  function renderProgressPct(fraction) {
    var els = RS.ensureEls();
    if (els.cornerRight) els.cornerRight.textContent = Math.round(RS.clamp01(fraction) * 100) + '%';
  }

  function renderPageText(text) {
    var els = RS.ensureEls();
    if (els.cornerLeft) els.cornerLeft.textContent = String(text || '');
  }

  async function syncProgressUI(detailMaybe) {
    var state = RS.state;
    if (!state.open || !state.engine) return;

    var locator = null;
    if (detailMaybe && typeof detailMaybe === 'object') {
      var frac = Number(detailMaybe.fraction);
      var index = Number(detailMaybe.index);
      locator = {
        fraction: Number.isFinite(frac) ? frac : null,
        tocHref: detailMaybe.tocItem && detailMaybe.tocItem.href ? String(detailMaybe.tocItem.href) : null,
        pageLabel: detailMaybe.pageItem && detailMaybe.pageItem.label != null ? String(detailMaybe.pageItem.label) : null,
        pageIndex: Number.isFinite(index) ? (index + 1) : null,
      };
    } else if (typeof state.engine.getLocator === 'function') {
      try { locator = await state.engine.getLocator(); } catch (e) { locator = null; }
    }
    var pageHint = (typeof state.engine.getPageHint === 'function') ? state.engine.getPageHint() : null;

    var fraction = 0;
    var label = '-';

    if (RS.isPdfOpen()) {
      // PDF: scrub bar shows whole-document progress (no chapters)
      var page = Number((pageHint && pageHint.page) || (locator && locator.page) || (locator && locator.pageIndex) || 0);
      var pageCount = Number((pageHint && pageHint.pageCount) || 0);
      if (pageCount > 1) {
        fraction = RS.clamp01((Math.max(1, page) - 1) / (pageCount - 1));
      } else {
        var fr = Number(locator && locator.fraction);
        fraction = Number.isFinite(fr) ? RS.clamp01(fr) : 0;
      }
      if (page > 0 && pageCount > 0) label = page + '/' + pageCount;
      else if (page > 0) label = 'p.' + page;
      else label = 'PDF';

      state.progressFraction = fraction;
      renderProgressFraction(fraction);
      renderProgressPct(fraction);
      renderPageText(label);
    } else {
      // FIX_AUDIT: EPUB/TXT scrub now reflects whole-book progress for stable seeking.
      var bookFrac = 0;
      var fr2 = Number(locator && locator.fraction);
      bookFrac = Number.isFinite(fr2) ? RS.clamp01(fr2) : 0;

      fraction = bookFrac;
      state.progressFraction = fraction;

      // Corner overlays: chapter name in left, percentage in right
      // FIX-TTS12: only update chapter label when a new one is available — never clear
      // it to empty. This prevents blinking when relocate events lack tocItem data.
      if (detailMaybe && detailMaybe.tocItem && detailMaybe.tocItem.label) {
        label = String(detailMaybe.tocItem.label);
        state._lastChapterLabel = label;
      }

      // Add a small "where am I" hint (Readest vibe): chapter + page label when available.
      var chapterLabel = String(state._lastChapterLabel || label || '');
      var pageLabel = (detailMaybe && detailMaybe.pageItem && detailMaybe.pageItem.label != null)
        ? String(detailMaybe.pageItem.label)
        : (locator && locator.pageLabel != null ? String(locator.pageLabel) : '');

      // Page indicator: use real layout page count (adjusts with font/spacing)
      var sectionPages = (state.engine && typeof state.engine.getSectionPageInfo === 'function')
        ? state.engine.getSectionPageInfo() : null;

      var leftText = chapterLabel;
      if (sectionPages && sectionPages.total > 0) {
        var pageTag = sectionPages.current + '/' + sectionPages.total;
        leftText = chapterLabel ? (chapterLabel + '  \u00b7  ' + pageTag) : pageTag;
      } else if (pageLabel) {
        // Fallback: embedded page label from EPUB metadata
        var looksLikeFraction = /\d+\s*\/\s*\d+/.test(pageLabel);
        leftText = chapterLabel ? (chapterLabel + '  ·  ' + (looksLikeFraction ? pageLabel : ('p.' + pageLabel))) : (looksLikeFraction ? pageLabel : ('p.' + pageLabel));
      }
      if (leftText) renderPageText(leftText);

      renderProgressFraction(fraction);
      renderProgressPct(fraction);

      // Track per-chapter read state (drives TOC sidebar progress indicators)
      if (detailMaybe && detailMaybe.section && typeof detailMaybe.section.current === 'number') {
        var secIdx = detailMaybe.section.current;
        var secTotal = detailMaybe.section.total || 1;
        if (!state.chapterReadState) state.chapterReadState = {};
        var chapterFrac = 0;
        var fracs = null;
        if (state.engine && typeof state.engine.getSectionFractions === 'function') {
          try { fracs = state.engine.getSectionFractions(); } catch (e) {}
        }
        if (fracs && fracs.length > secIdx + 1) {
          var start = fracs[secIdx] || 0;
          var end = fracs[secIdx + 1] || 1;
          var span2 = end - start;
          if (span2 > 0) chapterFrac = RS.clamp01((bookFrac - start) / span2);
        }
        var prev = state.chapterReadState[secIdx] || 0;
        if (chapterFrac > prev) state.chapterReadState[secIdx] = chapterFrac;
        bus.emit('chapter:progress', secIdx, chapterFrac, secTotal);
      }
    }

    // Debounced auto-save on every relocate (catches TTS advances, scroll, etc.)
    if (state.relocateSaveTimer) { clearTimeout(state.relocateSaveTimer); state.relocateSaveTimer = null; }
    if (!state.suspendProgressSave) {
      state.relocateSaveTimer = setTimeout(function () {
        state.relocateSaveTimer = null;
        if (state.open && !state.suspendProgressSave) {
          try { RS.saveProgress(); } catch (e) {}
        }
      }, 2000);
    }

  }

  // ── Chapter transition ─────────────────────────────────────

  function findTocLabelForSpine(spineIndex) {
    var items = RS.state.tocItems || [];
    var label = '';
    for (var i = items.length - 1; i >= 0; i--) {
      if (items[i].spineIndex >= 0 && items[i].spineIndex <= spineIndex) {
        label = items[i].label || '';
        break;
      }
    }
    return label;
  }

  function showChapterTransition(detail) {
    var els = RS.ensureEls();
    var state = RS.state;
    if (!els.chapterTransition) return;
    if (isScrolledTextFlow()) return;

    // If transition is already showing, treat as "continue" (user pressed next again)
    if (isChapterTransitionOpen()) {
      dismissChapterTransition(true);
      return;
    }

    var currentLabel = findTocLabelForSpine(detail.currentIndex);
    var nextLabel = findTocLabelForSpine(detail.nextIndex);

    if (els.chapterTransCurrent) {
      els.chapterTransCurrent.textContent = currentLabel || ('Section ' + (detail.currentIndex + 1));
    }
    if (els.chapterTransNext) {
      els.chapterTransNext.textContent = nextLabel || ('Section ' + (detail.nextIndex + 1));
    }

    // Store direction for advance
    state.chapterTransDir = detail.direction;

    els.chapterTransition.classList.remove('hidden');

    // Auto-advance countdown (3 seconds)
    var remaining = 3;
    if (els.chapterTransCountdown) els.chapterTransCountdown.textContent = remaining + 's';
    if (state.chapterTransTimer) clearTimeout(state.chapterTransTimer);
    if (state.chapterTransInterval) clearInterval(state.chapterTransInterval);

    state.chapterTransInterval = setInterval(function () {
      remaining--;
      if (els.chapterTransCountdown) {
        els.chapterTransCountdown.textContent = remaining > 0 ? (remaining + 's') : '';
      }
    }, 1000);

    state.chapterTransTimer = setTimeout(function () {
      dismissChapterTransition(true);
    }, 3000);
  }

  function dismissChapterTransition(advance) {
    var els = RS.ensureEls();
    var state = RS.state;
    if (state.chapterTransTimer) { clearTimeout(state.chapterTransTimer); state.chapterTransTimer = null; }
    if (state.chapterTransInterval) { clearInterval(state.chapterTransInterval); state.chapterTransInterval = null; }
    if (els.chapterTransition) els.chapterTransition.classList.add('hidden');
    if (advance && state.engine && typeof state.engine.advanceSection === 'function') {
      state.engine.advanceSection(state.chapterTransDir || 1);
    }
  }

  function isChapterTransitionOpen() {
    var els = RS.ensureEls();
    return !!(els.chapterTransition && !els.chapterTransition.classList.contains('hidden'));
  }

  function progressFractionFromEvent(ev) {
    var els = RS.ensureEls();
    if (!els.progress) return 0;
    var rect = els.progress.getBoundingClientRect();
    if (!rect || !rect.width) return 0;
    return RS.clamp01((Number(ev.clientX) - rect.left) / rect.width);
  }

  async function seekToProgressFraction(fraction) {
    var state = RS.state;
    if (!state.open || !state.engine || typeof state.engine.goTo !== 'function') return;
    var f = RS.clamp01(fraction);
    if (RS.isPdfOpen()) {
      var hint = (typeof state.engine.getPageHint === 'function') ? state.engine.getPageHint() : null;
      var pageCount = Number(hint && hint.pageCount || 0);
      if (pageCount > 0) {
        var page = Math.max(1, Math.min(pageCount, Math.round(f * Math.max(1, pageCount - 1)) + 1));
        await state.engine.goTo({ page: page });
      } else {
        await state.engine.goTo({ fraction: f });
      }
    } else {
      // FIX_AUDIT: EPUB/TXT scrub seeking uses whole-book fractions to avoid glitchy jumps.
      await state.engine.goTo({ fraction: f });
    }
    await RS.saveProgress();
    await syncProgressUI();
  }

  async function seekToBookFraction(fraction) {
    var state = RS.state;
    if (!state.open || !state.engine || typeof state.engine.goTo !== 'function') return;
    var f = RS.clamp01(fraction);
    if (RS.isPdfOpen()) {
      var hint = (typeof state.engine.getPageHint === 'function') ? state.engine.getPageHint() : null;
      var pageCount = Number(hint && hint.pageCount || 0);
      if (pageCount > 0) {
        var page = Math.max(1, Math.min(pageCount, Math.round(f * Math.max(1, pageCount - 1)) + 1));
        await state.engine.goTo({ page: page });
      } else {
        await state.engine.goTo({ fraction: f });
      }
    } else {
      // FIX_AUDIT: Home/End must target whole-book bounds regardless of chapter-local scrub behavior.
      await state.engine.goTo({ fraction: f });
    }
    await RS.saveProgress();
    await syncProgressUI();
  }

  // ── Chapter markers ──────────────────────────────────────────

  function renderChapterMarkers() {
    var els = RS.ensureEls();
    if (!els.progress) return;
    var old = els.progress.querySelector('.scrubChapters');
    if (old) old.remove();
    // FIX-NAV01: render section markers for all formats (EPUB uses whole-book scrub)
    var state = RS.state;
    if (!state.engine || typeof state.engine.getSectionFractions !== 'function') return;
    var fracs;
    try { fracs = state.engine.getSectionFractions(); } catch (e) { return; }
    if (!fracs || fracs.length < 2) return;
    var container = document.createElement('div');
    container.className = 'scrubChapters';
    for (var i = 0; i < fracs.length; i++) {
      if (fracs[i] <= 0.005 || fracs[i] >= 0.995) continue;
      var mark = document.createElement('div');
      mark.className = 'scrubChapterMark';
      mark.style.left = (fracs[i] * 100).toFixed(4) + '%';
      container.appendChild(mark);
    }
    els.progress.appendChild(container);
  }

  // ── History buttons ──────────────────────────────────────────

  function applyPauseBoundaryByMode() {
    var state = RS.state;
    if (!state.engine || typeof state.engine.setPauseBoundary !== 'function') return;
    state.engine.setPauseBoundary(false);
  }

  // BOOK_FIX 5.2: syncProgressOnActivity is intentionally a no-op. In scrolled
  // flow mode, Foliate's paginator emits `relocate` as the user scrolls, and
  // that event already drives the progress scrub sync via syncProgressUI.
  // Calling it again on every mousemove/scroll activity was double-work —
  // removed the redundancy here. In paginated mode there's nothing to sync
  // between page turns. Kept as an exported no-op so existing callers don't
  // break; future work can inline the call sites and delete this shim.
  function syncProgressOnActivity() {
    return;
  }

  // ── Goto dialog ──────────────────────────────────────────────

  function isGotoOpen() {
    var els = RS.ensureEls();
    return !!(els.gotoOverlay && !els.gotoOverlay.classList.contains('hidden'));
  }

  function openGotoDialog() {
    var els = RS.ensureEls();
    if (!els.gotoOverlay) return;
    els.gotoOverlay.classList.remove('hidden');
    if (els.gotoHint) {
      if (RS.isPdfOpen()) {
        var state = RS.state;
        var hint = state.engine && typeof state.engine.getPageHint === 'function' ? state.engine.getPageHint() : null;
        els.gotoHint.textContent = hint && hint.pageCount ? ('Pages 1 - ' + hint.pageCount) : 'Enter page number';
      } else {
        els.gotoHint.textContent = 'Enter percentage (0\u2013100)';
      }
    }
    if (els.gotoInput) { els.gotoInput.value = ''; setTimeout(function () { els.gotoInput.focus(); }, 50); }
  }

  function closeGotoDialog() {
    var els = RS.ensureEls();
    if (els.gotoOverlay) els.gotoOverlay.classList.add('hidden');
  }

  async function submitGoto() {
    var els = RS.ensureEls();
    var raw = String((els.gotoInput && els.gotoInput.value) || '').trim();
    if (!raw) return;
    closeGotoDialog();

    if (raw.endsWith('%')) {
      var pct = parseFloat(raw.replace('%', ''));
      if (Number.isFinite(pct)) {
        await seekToProgressFraction(pct / 100);
        return;
      }
    }

    var num = parseFloat(raw);
    if (!Number.isFinite(num)) return;

    if (RS.isPdfOpen()) {
      var state = RS.state;
      if (state.engine && typeof state.engine.goTo === 'function') {
        await state.engine.goTo({ page: Math.round(num) });
      }
    } else {
      await seekToProgressFraction(num > 1 ? num / 100 : num);
    }
    await RS.saveProgress();
    await syncProgressUI();
  }

  // ── Chapter-level navigation ─────────────────────────────────
  // FIX_CHAP_NAV: explicit next/prev chapter (bypasses boundary pause)

  async function nextChapter() {
    var state = RS.state;
    if (!state.engine) return;
    // Dismiss any open transition card first
    if (isChapterTransitionOpen()) dismissChapterTransition(false);
    if (typeof state.engine.nextSection === 'function') {
      await state.engine.nextSection();
    } else if (typeof state.engine.advanceSection === 'function') {
      await state.engine.advanceSection(1);
    }
    await RS.saveProgress();
    await syncProgressUI();
  }

  async function prevChapter() {
    var state = RS.state;
    if (!state.engine) return;
    if (isChapterTransitionOpen()) dismissChapterTransition(false);
    if (typeof state.engine.prevSection === 'function') {
      await state.engine.prevSection();
    } else if (typeof state.engine.advanceSection === 'function') {
      await state.engine.advanceSection(-1);
    }
    await RS.saveProgress();
    await syncProgressUI();
  }

  // ── Relocate handler ─────────────────────────────────────────

  function handleRelocate(detail) {
    bus.emit('reader:relocated', detail);
    syncProgressUI(detail).catch(function () {});
    bus.emit('tts:show-return');

    // Debounced progress save on relocate
    var state = RS.state;
    if (state.relocateSaveTimer) clearTimeout(state.relocateSaveTimer);
    state.relocateSaveTimer = setTimeout(function () {
      RS.saveProgress().catch(function () {});
    }, 800);
  }

  // ── Auto save ────────────────────────────────────────────────

  function clearAutoSave() {
    var state = RS.state;
    if (!state.saveTimer) return;
    clearInterval(state.saveTimer);
    state.saveTimer = null;
  }

  function startAutoSave() {
    clearAutoSave();
    RS.state.saveTimer = setInterval(function () {
      RS.saveProgress().catch(function () {});
    }, 3000);
  }

  // ── Bind ─────────────────────────────────────────────────────

  function bind() {
    var els = RS.ensureEls();
    var state = RS.state;

    // Nav arrow buttons
    els.prevBtn && els.prevBtn.addEventListener('click', function () { stepPrevSmart().catch(function () {}); });
    els.nextBtn && els.nextBtn.addEventListener('click', function () { stepNextSmart().catch(function () {}); });

    // FIX-READER-GAPS: history back/forward buttons
    els.histBackBtn && els.histBackBtn.addEventListener('click', function () {
      try {
        if (!state.engine || typeof state.engine.historyBack !== 'function') return;
        if (typeof state.engine.historyCanGoBack === 'function' && !state.engine.historyCanGoBack()) return;
        state.engine.historyBack();
      } catch (e) {}
    });
    els.histFwdBtn && els.histFwdBtn.addEventListener('click', function () {
      try {
        if (!state.engine || typeof state.engine.historyForward !== 'function') return;
        if (typeof state.engine.historyCanGoForward === 'function' && !state.engine.historyCanGoForward()) return;
        state.engine.historyForward();
      } catch (e) {}
    });

    // Chapter transition Continue button
    els.chapterTransContinue && els.chapterTransContinue.addEventListener('click', function () {
      dismissChapterTransition(true);
    });

    // Goto dialog buttons
    els.gotoSubmit && els.gotoSubmit.addEventListener('click', function () { submitGoto().catch(function () {}); });
    els.gotoCancel && els.gotoCancel.addEventListener('click', function () { closeGotoDialog(); });
    els.gotoOverlay && els.gotoOverlay.addEventListener('click', function (e) {
      if (e.target === els.gotoOverlay) closeGotoDialog();
    });
    els.gotoInput && els.gotoInput.addEventListener('keydown', function (e) {
      if (e.key === 'Enter') { e.preventDefault(); submitGoto().catch(function () {}); }
      if (e.key === 'Escape') { e.preventDefault(); closeGotoDialog(); }
    });

    // Scrub bar interaction
    if (els.progress) {
      if (!els.progress.hasAttribute('tabindex')) els.progress.tabIndex = 0;
      var dragging = false;
      var activePointerId = null;
      var suppressClickUntil = 0;
      var onPointerUp = function (ev) { endDrag(ev, true); };
      var onPointerCancel = function (ev) { endDrag(ev, false); };

      var onMove = function (ev) {
        if (!dragging) return;
        if (activePointerId != null && ev && ev.pointerId != null && ev.pointerId !== activePointerId) return;
        state.progressDragFraction = progressFractionFromEvent(ev);
        renderProgressFraction(state.progressDragFraction);
      };

      var endDrag = function (ev, commit) {
        if (!dragging) return;
        if (activePointerId != null && ev && ev.pointerId != null && ev.pointerId !== activePointerId) return;
        dragging = false;
        activePointerId = null;
        state.hudDragProgress = false;
        if (els.progress) els.progress.classList.remove('dragging');
        if (typeof window !== 'undefined' && 'PointerEvent' in window) {
          document.removeEventListener('pointermove', onMove);
          document.removeEventListener('pointerup', onPointerUp);
          document.removeEventListener('pointercancel', onPointerCancel);
          if (els.progress && typeof els.progress.releasePointerCapture === 'function') {
            try { if (ev && ev.pointerId != null) els.progress.releasePointerCapture(ev.pointerId); } catch (e) {}
          }
        } else {
          document.removeEventListener('mousemove', onMove);
        }
        if (commit) {
          suppressClickUntil = Date.now() + 220;
          seekToProgressFraction(state.progressDragFraction).catch(function () {});
        }
      };

      if (typeof window !== 'undefined' && 'PointerEvent' in window) {
        els.progress.addEventListener('pointerdown', function (e) {
          if (e.button !== 0) return;
          e.preventDefault();
          dragging = true;
          activePointerId = (e.pointerId != null) ? e.pointerId : null;
          state.hudDragProgress = true;
          els.progress.classList.add('dragging');
          if (typeof els.progress.setPointerCapture === 'function' && activePointerId != null) {
            try { els.progress.setPointerCapture(activePointerId); } catch (err) {}
          }
          state.progressDragFraction = progressFractionFromEvent(e);
          renderProgressFraction(state.progressDragFraction);
          document.addEventListener('pointermove', onMove, { passive: true });
          document.addEventListener('pointerup', onPointerUp);
          document.addEventListener('pointercancel', onPointerCancel);
        });
      } else {
        var onUp = function () { endDrag(null, true); };
        els.progress.addEventListener('mousedown', function (e) {
          if (e.button !== 0) return;
          e.preventDefault();
          dragging = true;
          state.hudDragProgress = true;
          els.progress.classList.add('dragging');
          state.progressDragFraction = progressFractionFromEvent(e);
          renderProgressFraction(state.progressDragFraction);
          document.addEventListener('mousemove', onMove, { passive: true });
          document.addEventListener('mouseup', onUp, { once: true });
        });
      }

      els.progress.addEventListener('click', function (e) {
        if (dragging) return;
        if (Date.now() < suppressClickUntil) return;
        seekToProgressFraction(progressFractionFromEvent(e)).catch(function () {});
      });

      els.progress.addEventListener('keydown', function (e) {
        if (!state.open) return;
        var step = 0;
        if (e.key === 'ArrowRight' || e.key === 'ArrowUp') step = 0.02;
        else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') step = -0.02;
        else if (e.key === 'Home') step = -1;
        else if (e.key === 'End') step = 1;
        else return;
        e.preventDefault();
        var base = (step === -1) ? 0 : (step === 1) ? 1 : RS.clamp01(state.progressFraction + step);
        seekToProgressFraction(base).catch(function () {});
      });
    }

    // Bus events
    bus.on('nav:prev', function () { stepPrev().catch(function () {}); });
    bus.on('nav:next', function () { stepNext().catch(function () {}); });
    bus.on('nav:seek', function (fraction) { seekToProgressFraction(fraction).catch(function () {}); });
    // FIX_AUDIT: dedicated whole-book seek channel for Home/End semantics.
    bus.on('nav:seek-book', function (fraction) { seekToBookFraction(fraction).catch(function () {}); });
    bus.on('nav:goto-open', function () { openGotoDialog(); });
    bus.on('nav:goto-close', function () { closeGotoDialog(); });
    bus.on('nav:progress-sync', function (detail) { syncProgressUI(detail).catch(function () {}); });
    bus.on('nav:next-chapter', function () { nextChapter().catch(function () {}); });
    bus.on('nav:prev-chapter', function () { prevChapter().catch(function () {}); });
    bus.on('toc:updated', function () { renderChapterMarkers(); });
    bus.on('reader:user-activity', function () { syncProgressOnActivity(); });
    bus.on('appearance:flow-mode-changed', function () { applyPauseBoundaryByMode(); });
  }

  // ── Lifecycle ────────────────────────────────────────────────

  function onOpen() {
    renderChapterMarkers();
    syncProgressUI().catch(function () {});
    startAutoSave();

    var state = RS.state;
    // Wire engine relocate callback
    if (state.engine && typeof state.engine.onRelocateEvent === 'function') {
      state.engine.onRelocateEvent(handleRelocate);
    }
    // FIX-READER-GAPS: wire history button enabled/disabled state
    if (state.engine && typeof state.engine.onHistoryChange === 'function') {
      state.engine.onHistoryChange(function (info) {
        var els = RS.ensureEls();
        if (els.histBackBtn) els.histBackBtn.disabled = !info.canGoBack;
        if (els.histFwdBtn) els.histFwdBtn.disabled = !info.canGoForward;
      });
    }
    // BUILD_CHAP_TRANS: wire section boundary callback + enable pause (EPUB only)
    if (!RS.isPdfOpen()) {
      if (state.engine && typeof state.engine.onSectionBoundary === 'function') {
        state.engine.onSectionBoundary(showChapterTransition);
      }
      applyPauseBoundaryByMode();
    }
  }

  function onClose() {
    clearAutoSave();
    var state = RS.state;
    if (state.relocateSaveTimer) { clearTimeout(state.relocateSaveTimer); state.relocateSaveTimer = null; }
    renderProgressFraction(0);
    renderPageText('');
    renderProgressPct(0);
    // BUILD_CHAP_TRANS: clean up transition state
    dismissChapterTransition(false);
    state.chapterReadState = {};
    // FIX-READER-GAPS: reset history buttons
    var els = RS.ensureEls();
    if (els.histBackBtn) els.histBackBtn.disabled = true;
    if (els.histFwdBtn) els.histFwdBtn.disabled = true;
  }

  // ── Export ────────────────────────────────────────────────────

  window.booksReaderNav = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    stepNext: stepNext,
    stepPrev: stepPrev,
    syncProgressUI: syncProgressUI,
    renderProgressFraction: renderProgressFraction,
    renderProgressPct: renderProgressPct,
    renderChapterMarkers: renderChapterMarkers,
    seekToProgressFraction: seekToProgressFraction,
    seekToBookFraction: seekToBookFraction,
    isGotoOpen: isGotoOpen,
    openGotoDialog: openGotoDialog,
    closeGotoDialog: closeGotoDialog,
    handleRelocate: handleRelocate,
    clearAutoSave: clearAutoSave,
    startAutoSave: startAutoSave,
    // BUILD_CHAP_TRANS
    showChapterTransition: showChapterTransition,
    dismissChapterTransition: dismissChapterTransition,
    isChapterTransitionOpen: isChapterTransitionOpen,
    nextChapter: nextChapter,
    prevChapter: prevChapter,
  };
})();
