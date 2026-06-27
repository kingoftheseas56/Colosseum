// BUILD_OVERHAUL: Sidebar shell — tabbed sidebar with TOC, Bookmarks, Annotations
(function () {
  'use strict';

  var RS = window.booksReaderState;
  var bus = window.booksReaderBus;

  // ── Switch active tab ──────────────────────────────────────

  function switchTab(tabName) {
    var els = RS.ensureEls();
    if (!els.sidebar) return;
    var state = RS.state;

    // Update tab buttons
    var tabBtns = document.querySelectorAll('.br-sidebar-tab[data-sidebar-tab]');
    for (var i = 0; i < tabBtns.length; i++) {
      tabBtns[i].classList.toggle('active', tabBtns[i].getAttribute('data-sidebar-tab') === tabName);
    }

    // Update panes
    var panes = document.querySelectorAll('.br-sidebar-pane[data-pane]');
    for (var j = 0; j < panes.length; j++) {
      panes[j].classList.toggle('hidden', panes[j].getAttribute('data-pane') !== tabName);
    }

    // If sidebar is collapsed, open it
    if (!state.sidebarOpen) {
      state.sidebarOpen = true;
      els.sidebar.classList.remove('collapsed');
      // FIX_HUD: hide toolbar when sidebar opens
      bus.emit('sidebar:toggled', true);
    }

    state.sidebarTab = tabName;

    // Highlight toolbar buttons
    if (els.bookmarksBtn) els.bookmarksBtn.classList.toggle('active', tabName === 'bookmarks');
    if (els.annotBtn) els.annotBtn.classList.toggle('active', tabName === 'annotations');

    bus.emit('sidebar:tab-changed', tabName);

    // Persist sidebar state
    try { Tanko.api.booksUi.save({ sidebarOpen: true, sidebarTab: tabName }); } catch (e) {}
  }

  // ── Toggle sidebar visibility ────────────────────────────────

  function toggleSidebar(forceOpen) {
    var els = RS.ensureEls();
    if (!els.sidebar) return;
    var state = RS.state;
    var nextOpen = (typeof forceOpen === 'boolean') ? forceOpen : !state.sidebarOpen;
    state.sidebarOpen = nextOpen;
    els.sidebar.classList.toggle('collapsed', !nextOpen);

    // FIX_HUD: notify core to hide/show toolbar so it doesn't overlap sidebar tabs
    bus.emit('sidebar:toggled', nextOpen);

    // Persist sidebar state
    try { Tanko.api.booksUi.save({ sidebarOpen: nextOpen }); } catch (e) {}
  }

  function closeSidebar() {
    toggleSidebar(false);
  }


  function shouldIgnoreReadingAreaClickForSidebarClose(ev) {
    var t = ev && ev.target;
    if (!t || !t.closest) return false;

    // Ignore clicks on reader chrome / floating controls / overlays.
    if (t.closest('.br-toolbar')) return true;
    if (t.closest('.br-footer')) return true;
    if (t.closest('.br-overlay')) return true;
    if (t.closest('.br-overlay-backdrop')) return true;
    if (t.closest('.booksGotoOverlay')) return true;
    if (t.closest('.booksAnnotPopup')) return true;
    if (t.closest('.booksReaderDictPopup')) return true;
    if (t.closest('.booksReaderTtsBar')) return true;
    if (t.closest('.booksReaderTtsMega')) return true;
    if (t.closest('.booksReaderTtsDiag')) return true;
    if (t.closest('.booksReaderReturnTts')) return true;
    if (t.closest('.br-reading-ruler')) return true;
    if (t.closest('.br-nav-arrow')) return true;

    // Don't collapse sidebar while the user is selecting text.
    try {
      var sel = window.getSelection && window.getSelection();
      if (sel && String(sel).trim()) return true;
    } catch (e) {}

    return false;
  }

  // ── Bind ─────────────────────────────────────────────────────

  function bind() {
    var els = RS.ensureEls();

    // Sidebar toggle button
    els.sidebarToggle && els.sidebarToggle.addEventListener('click', function () {
      toggleSidebar();
    });

    // Close button at bottom of tab strip
    var closeBtn = document.getElementById('brSidebarCloseBtn');
    if (closeBtn) closeBtn.addEventListener('click', function () { closeSidebar(); });

    // Tab buttons
    var tabBtns = document.querySelectorAll('.br-sidebar-tab[data-sidebar-tab]');
    for (var i = 0; i < tabBtns.length; i++) {
      (function (btn) {
        btn.addEventListener('click', function () {
          switchTab(btn.getAttribute('data-sidebar-tab'));
        });
      })(tabBtns[i]);
    }

    // Wire toolbar buttons to sidebar tabs
    els.bookmarksBtn && els.bookmarksBtn.addEventListener('click', function () {
      switchTab('bookmarks');
    });
    els.annotBtn && els.annotBtn.addEventListener('click', function () {
      switchTab('annotations');
    });

    // FIX_HUD: click on reading area closes sidebar
    var readingArea = document.querySelector('.br-reading-area');
    if (readingArea) {
      readingArea.addEventListener('click', function (ev) {
        if (!RS.state.sidebarOpen) return;
        if (shouldIgnoreReadingAreaClickForSidebarClose(ev)) return;
        closeSidebar();
      });
    }

    // Bus events
    bus.on('sidebar:toggle', function (force) { toggleSidebar(force); });
    bus.on('sidebar:close', function () { closeSidebar(); });
    bus.on('sidebar:switch-tab', function (tab) { switchTab(tab); });
  }

  // ── Lifecycle ────────────────────────────────────────────────

  function onOpen() {
    var state = RS.state;
    var els = RS.ensureEls();

    // FIX_HUD: sidebar always starts collapsed; restore active tab preference WITHOUT opening
    if (els.sidebar) els.sidebar.classList.add('collapsed');
    state.sidebarOpen = false;
    try {
      Tanko.api.booksUi.get().then(function (res) {
        if (res && res.ui && res.ui.sidebarTab) {
          // Only set the active tab, do NOT open the sidebar
          var tabName = res.ui.sidebarTab;
          state.sidebarTab = tabName;
          var tabBtns = document.querySelectorAll('.br-sidebar-tab[data-sidebar-tab]');
          for (var i = 0; i < tabBtns.length; i++) {
            tabBtns[i].classList.toggle('active', tabBtns[i].getAttribute('data-sidebar-tab') === tabName);
          }
          var panes = document.querySelectorAll('.br-sidebar-pane[data-pane]');
          for (var j = 0; j < panes.length; j++) {
            panes[j].classList.toggle('hidden', panes[j].getAttribute('data-pane') !== tabName);
          }
        }
      }).catch(function () {});
    } catch (e) {}
  }

  function onClose() {
    // Nothing to reset; sidebar state persists
  }

  // ── Export ────────────────────────────────────────────────────

  window.booksReaderSidebar = {
    bind: bind,
    onOpen: onOpen,
    onClose: onClose,
    toggleSidebar: toggleSidebar,
    switchTab: switchTab,
    openTab: switchTab,
  };
})();
