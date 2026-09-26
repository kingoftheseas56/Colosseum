/* Feria's bundled web surface. All catalog rows come from the native runtime. */
(function () {
  'use strict';

  const $ = id => document.getElementById(id);
  const providers = [
    ['netflix', 'Netflix', 'netflix.svg'], ['prime', 'Prime Video', 'prime.png'],
    ['hbomax', 'HBO Max', 'hbomax.svg'], ['disney', 'Disney+', 'disney.png'],
    ['appletv', 'Apple TV+', 'appletv.svg'], ['crunchyroll', 'Crunchyroll', 'crunchyroll.svg'],
    ['youtube', 'YouTube', 'youtube.svg'], ['spotify', 'Spotify', 'spotify.svg'],
    ['ytmusic', 'YouTube Music', 'ytmusic.svg'], ['kindle', 'Kindle', 'kindle.png'],
    ['mangaplus', 'MANGA Plus', 'mangaplus.png'], ['webtoon', 'WEBTOON', 'webtoon.svg'],
    ['dcui', 'DC Universe Infinite', 'dcui.png']
  ];
  const state = { bridge: null, shelves: [], lens: 'all', provider: '', searchOpen: false,
                  detailKey: '', lastFocus: null, renderQueued: false, searchSerial: 0 };

  function imageUrl(value) {
    try {
      const u = new URL(String(value || ''));
      return u.protocol === 'https:' ? u.href : '';
    } catch (_) { return ''; }
  }
  function brandUrl(file) { return '../../qml/feria/brand-logos/' + file; }
  function itemKey(item) { return String(item.canonicalKey || item.id || ''); }
  function itemTitle(item) { return String(item.title || item.t || 'Untitled'); }
  function itemCreator(item) { return String(item.creator || item.by || ''); }
  function itemKind(item) { return String(item.kind || item.k || 'title'); }
  function itemImage(item) { return imageUrl(item.imageUrl); }
  function anyItems() { return state.shelves.some(s => (s.items || []).length); }

  function makePoster(item) {
    const key = itemKey(item);
    const button = document.createElement('button');
    button.type = 'button'; button.className = 'poster';
    button.setAttribute('aria-label', 'Details for ' + itemTitle(item));
    const art = document.createElement('span'); art.className = 'poster-art';
    const url = itemImage(item);
    if (url) {
      const img = document.createElement('img'); img.src = url; img.alt = ''; img.loading = 'lazy';
      img.addEventListener('error', () => img.remove()); art.append(img);
    }
    const name = document.createElement('span'); name.className = 'name'; name.textContent = itemTitle(item);
    const by = document.createElement('span'); by.className = 'by';
    by.textContent = itemCreator(item) || itemKind(item);
    button.append(art, name, by);
    button.addEventListener('click', () => showDetail(key, button));
    return button;
  }

  function renderServices() {
    const rail = $('services'); rail.replaceChildren();
    for (const [id, label, file] of providers) {
      const button = document.createElement('button'); button.type = 'button';
      button.className = 'service' + (state.provider === id ? ' selected' : '');
      button.setAttribute('aria-pressed', String(state.provider === id));
      button.setAttribute('aria-label', (state.provider === id ? 'Show all services' : 'Show ' + label));
      const logo = document.createElement('img'); logo.src = brandUrl(file); logo.alt = '';
      const name = document.createElement('span'); name.textContent = label;
      button.append(logo, name);
      button.addEventListener('click', () => { state.provider = state.provider === id ? '' : id; render(); });
      rail.append(button);
    }
  }

  function visibleShelves() {
    return state.shelves.filter(s => (!state.provider || s.providerId === state.provider)
      && Array.isArray(s.items) && s.items.length);
  }

  function renderHero(shelves) {
    const shelf = shelves[0]; const item = shelf && shelf.items[0];
    const button = $('hero-details');
    if (!item) {
      $('hero-image').style.backgroundImage = '';
      $('hero-kicker').textContent = 'DISCOVER ACROSS YOUR SERVICES';
      $('hero-title').textContent = 'A world of stories.';
      $('hero-summary').textContent = 'Films, shows, music, and books in one place.';
      button.disabled = true; button.onclick = null;
      return;
    }
    const title = itemTitle(item); const url = itemImage(item);
    $('hero-image').style.backgroundImage = url ? 'url(' + JSON.stringify(url) + ')' : '';
    $('hero-kicker').textContent = String(shelf.title || 'FEATURED IN FERIA').toUpperCase();
    $('hero-title').textContent = title;
    $('hero-summary').textContent = String(item.extra && item.extra.description || itemCreator(item) || 'Explore this title in Feria.');
    $('hero-index').textContent = 'FERIA / ' + String(item.rank || 1).padStart(2, '0');
    button.disabled = false;
    button.onclick = () => showDetail(itemKey(item), button);
  }

  function renderShelves(shelves) {
    const root = $('shelves'); root.replaceChildren();
    if (!shelves.length) {
      const status = document.createElement('div'); status.className = 'status';
      status.textContent = anyItems() ? 'No titles in this view. Choose another service or medium.'
        : 'Live catalogs have no titles yet. Use Refresh to try again.';
      root.append(status); return;
    }
    for (const shelf of shelves) {
      const section = document.createElement('section'); section.className = 'shelf';
      const head = document.createElement('div'); head.className = 'shelf-head';
      const title = document.createElement('h2'); title.textContent = shelf.title || 'Discovery';
      const source = document.createElement('span'); source.className = 'shelf-source';
      source.textContent = shelf.sourceLabel || '';
      head.append(title, source);
      const rail = document.createElement('div'); rail.className = 'poster-rail';
      rail.setAttribute('aria-label', title.textContent);
      for (const item of shelf.items) rail.append(makePoster(item));
      section.append(head, rail); root.append(section);
    }
  }

  function render() {
    renderServices();
    document.querySelectorAll('[data-lens]').forEach(button => {
      const selected = button.dataset.lens === state.lens;
      button.classList.toggle('selected', selected);
      button.setAttribute('aria-current', selected ? 'true' : 'false');
    });
    const shelves = visibleShelves();
    renderHero(shelves); renderShelves(shelves);
  }

  function scheduleReload() {
    if (state.renderQueued) return;
    state.renderQueued = true;
    setTimeout(() => {
      state.renderQueued = false;
      state.bridge.shelves(rows => { state.shelves = Array.isArray(rows) ? rows : []; render(); });
    }, 80);
  }

  function search() {
    if (!state.bridge) return;
    const query = $('query').value.trim(); const serial = ++state.searchSerial;
    const root = $('search-results'); root.replaceChildren();
    if (!query) return;
    state.bridge.search(query, rows => {
      if (serial !== state.searchSerial) return;
      root.replaceChildren();
      for (const item of rows || []) root.append(makePoster(item));
      if (!root.childElementCount) {
        const empty = document.createElement('div'); empty.className = 'status';
        empty.textContent = 'No live titles found.'; root.append(empty);
      }
    });
  }

  function showDetail(key, trigger) {
    if (!state.bridge || !key) return;
    state.lastFocus = trigger; state.detailKey = key;
    state.bridge.title(key, item => {
      if (state.detailKey !== key || !item || !item.t) return;
      $('detail-kind').textContent = itemKind(item).toUpperCase();
      $('detail-title').textContent = itemTitle(item);
      $('detail-meta').textContent = [item.y, item.by].filter(Boolean).join('  ·  ');
      $('detail-summary').textContent = item.s || 'No synopsis is available from this catalog.';
      const art = itemImage(item);
      $('detail-art').style.backgroundImage = art ? 'url(' + JSON.stringify(art) + ')' : '';
      $('destinations').replaceChildren();
      $('detail').hidden = false; $('detail-close').focus();
      state.bridge.destinations(key, doors => {
        if (state.detailKey !== key) return;
        const root = $('destinations'); root.replaceChildren();
        if (!doors || !doors.length) {
          const note = document.createElement('p'); note.textContent = 'No provider destination is available for this title.';
          root.append(note); return;
        }
        for (const door of doors) {
          const button = document.createElement('button'); button.type = 'button';
          button.className = 'door' + (door.actionable ? ' enabled' : '');
          button.disabled = !door.actionable;
          const name = document.createElement('strong'); name.textContent = door.label || door.providerId || 'Provider';
          const info = document.createElement('small');
          info.textContent = door.actionable ? (door.mode === 'exact' ? 'Open title in browser ↗' : 'Search provider in browser ↗')
            : 'No web destination available';
          button.append(name, info);
          if (door.actionable) button.addEventListener('click', () => state.bridge.openDestination(key, door.providerId));
          root.append(button);
        }
      });
    });
  }

  function hideDetail() {
    $('detail').hidden = true; state.detailKey = '';
    if (state.lastFocus && state.lastFocus.isConnected) state.lastFocus.focus();
  }
  function toggleSearch() {
    state.searchOpen = !state.searchOpen;
    $('search-view').hidden = !state.searchOpen;
    $('shelves').hidden = state.searchOpen;
    if (state.searchOpen) { $('search-view').scrollIntoView({block: 'start'}); $('query').focus(); }
    else { ++state.searchSerial; $('query').value = ''; $('search-results').replaceChildren(); }
  }
  window.feriaBack = function () {
    if (!$('detail').hidden) { hideDetail(); return; }
    if (state.searchOpen) { toggleSearch(); $('search-toggle').focus(); return; }
    if (state.bridge) state.bridge.close();
  };

  $('close').addEventListener('click', () => state.bridge && state.bridge.close());
  $('search-toggle').addEventListener('click', toggleSearch);
  $('refresh').addEventListener('click', () => state.bridge && state.bridge.refresh());
  $('detail-close').addEventListener('click', hideDetail);
  document.querySelector('[data-close-detail]').addEventListener('click', hideDetail);
  $('query').addEventListener('input', () => { clearTimeout(window.feriaSearchTimer); window.feriaSearchTimer = setTimeout(search, 130); });
  document.querySelectorAll('[data-lens]').forEach(button => button.addEventListener('click', () => {
    state.lens = button.dataset.lens;
    state.provider = '';
    state.bridge.setLens(state.lens);
    render();
  }));
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') { event.preventDefault(); window.feriaBack(); }
    if (event.key === '/' && !state.searchOpen && $('detail').hidden) {
      event.preventDefault(); toggleSearch();
    }
  });

  if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
    $('shelves').textContent = 'Feria must be opened from Colosseum to reach live discovery.';
    return;
  }
  new QWebChannel(qt.webChannelTransport, channel => {
    state.bridge = channel.objects.feria;
    state.bridge.changed.connect(scheduleReload);
    scheduleReload();
    state.bridge.refresh();
  });
})();
