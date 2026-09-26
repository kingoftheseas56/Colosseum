(function () {
  'use strict';

  const $ = id => document.getElementById(id);
  const WORLDS = ['Tankoban', 'Biblio', 'Theatre'];
  const WORLD_TABS = Object.freeze({
    Tankoban: [
      { key: 'discover', label: 'Discover' },
      { key: 'manga', label: 'Manga' },
      { key: 'comics', label: 'Comics' },
      { key: 'library', label: 'Library' }
    ],
    Biblio: [
      { key: 'discover', label: 'Discover' },
      { key: 'explore', label: 'Explore' },
      { key: 'library', label: 'Library' }
    ],
    Theatre: [
      { key: 'discover', label: 'Discover' },
      { key: 'movies', label: 'Movies' },
      { key: 'shows', label: 'Shows' },
      { key: 'anime', label: 'Anime' },
      { key: 'library', label: 'Library' }
    ]
  });

  const state = {
    mounted: false,
    snapshot: {},
    surface: 'Home',
    activeTabs: { Tankoban: 'discover', Biblio: 'discover', Theatre: 'discover' },
    heroIndex: 0,
    heroTimer: 0,
    searchOpen: false,
    searchTimer: 0,
    lastFocus: '',
    mouseMode: false
  };

  function isObject(value) {
    return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
  }

  function merge(target, source) {
    if (!isObject(source)) return source;
    const out = isObject(target) ? Object.assign({}, target) : {};
    Object.keys(source).forEach(key => {
      const value = source[key];
      out[key] = isObject(value) ? merge(out[key], value) : value;
    });
    return out;
  }

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = String(text);
    return node;
  }

  function send(type, payload) {
    return window.ColosseumWeb.action(type, Object.assign({ surface: state.surface }, payload || {}));
  }

  function safeUrl(value) {
    if (!value) return '';
    try {
      const url = new URL(String(value), location.href);
      return ['https:', 'http:', 'file:', 'data:', 'blob:', 'qrc:', 'colosseum:'].includes(url.protocol)
        ? url.href : '';
    } catch (_) {
      return '';
    }
  }

  function rawTitle(item) {
    if (!item || typeof item !== 'object') return String(item || '');
    return String(item.title || item.caption || item.name || item.label || '');
  }

  function rawId(item) {
    if (!item || typeof item !== 'object') return '';
    return String(item.id || item.tt || item.imdbId || item.seriesId || item.malId ||
                  item.mal_id || item.gcdId || item.canonicalId || item.identityId || '');
  }

  function rawKind(item) {
    if (!item || typeof item !== 'object') return '';
    return String(item.kind || item.type || item.mediaType || item.format || '').toLowerCase();
  }

  function posterUrl(item) {
    if (!item || typeof item !== 'object') return '';
    return safeUrl(item.cover || item.coverUrl || item.poster || item.posterUrl ||
                   item.image || item.art || item.banner || '');
  }

  function backdropUrl(item) {
    if (!item || typeof item !== 'object') return '';
    const explicit = safeUrl(item.backdrop || item.background || item.banner ||
                             item.hero || item.wideArt || '');
    if (explicit) return explicit;
    const id = rawId(item);
    if (/^tt\d+$/.test(id))
      return 'https://images.metahub.space/background/medium/' + encodeURIComponent(id) + '/img';
    return posterUrl(item);
  }

  function itemDescription(item) {
    if (!item || typeof item !== 'object') return '';
    return String(item.description || item.overview || item.blurb || item.synopsis || '');
  }

  function itemSubtitle(item) {
    if (!item || typeof item !== 'object') return '';
    if (item.episode) return String(item.episode);
    if (item.author) return String(item.author);
    if (item.subtitle) return String(item.subtitle);
    if (item.year) return String(item.year);
    if (item.releaseYear) return String(item.releaseYear);
    if (item.source) return String(item.source);
    return '';
  }

  function itemProgress(item) {
    if (!item || typeof item !== 'object') return -1;
    let value = item.progress;
    if (value === undefined || value === null) value = item.percent;
    value = Number(value);
    if (!Number.isFinite(value) || value < 0) return -1;
    if (value <= 1) value *= 100;
    return Math.max(0, Math.min(100, value));
  }
  function normalize(item) {
    const raw = isObject(item) ? item : { title: String(item || '') };
    return {
      raw,
      id: rawId(raw),
      title: rawTitle(raw) || 'Untitled',
      kind: rawKind(raw),
      poster: posterUrl(raw),
      backdrop: backdropUrl(raw),
      subtitle: itemSubtitle(raw),
      description: itemDescription(raw),
      progress: itemProgress(raw)
    };
  }

  function identity(item) {
    const data = normalize(item);
    return { id: data.id, title: data.title, kind: data.kind, item: data.raw };
  }

  function worldData(world) {
    if (isObject(state.snapshot.worlds) && isObject(state.snapshot.worlds[world]))
      return state.snapshot.worlds[world];
    const lower = world.toLowerCase();
    return isObject(state.snapshot[lower]) ? state.snapshot[lower] : {};
  }

  function homeData() {
    return isObject(state.snapshot.home) ? state.snapshot.home : {};
  }

  function listFrom(value) {
    if (Array.isArray(value)) return value;
    if (isObject(value) && Array.isArray(value.items)) return value.items;
    if (isObject(value) && Array.isArray(value.rows)) return value.rows;
    return [];
  }

  function tabsFor(world, data) {
    return Array.isArray(data.tabModel) && data.tabModel.length
      ? data.tabModel : (WORLD_TABS[world] || []);
  }

  function tabPayload(world, key) {
    const data = worldData(world);
    if (isObject(data.tabs) && data.tabs[key] !== undefined) return data.tabs[key];
    return data[key];
  }

  function openItem(world, item, intent) {
    send('open-item', Object.assign({ world, intent: intent || 'details' }, identity(item)));
  }

  function resumeItem(world, item) {
    send('resume', Object.assign({ world }, identity(item)));
  }

  function nextUpItem(world, item) {
    send('next-up', Object.assign({ world }, identity(item)));
  }

  function makeImage(url, className) {
    if (!url) return null;
    const image = el('img', className || '');
    image.alt = '';
    image.loading = 'lazy';
    image.decoding = 'async';
    image.src = url;
    image.addEventListener('load', () => image.classList.add('on'));
    image.addEventListener('error', () => image.remove());
    return image;
  }

  function face(item) {
    const data = normalize(item);
    const f = el('div', 'face');
    const fallback = el('div', 'fb', data.title);
    f.append(fallback);
    const image = makeImage(data.poster);
    if (image) f.append(image);
    return f;
  }

  function emptyState(title, text) {
    const box = el('div', 'empty-state');
    const copy = el('div');
    copy.append(el('b', '', title), el('span', '', text));
    box.append(copy);
    return box;
  }

  function sectionHeader(title, more) {
    const head = el('div', 'wh');
    head.append(el('h2', '', title));
    if (more) {
      const button = el('button', 'more fx');
      button.type = 'button';
      button.dataset.nav = 'true';
      button.append(document.createTextNode(more.label || 'See all'), el('span', 'ch', '›'));
      button.addEventListener('click', more.action);
      head.append(button);
    }
    return head;
  }

  function posterCard(world, item, onClick) {
    const data = normalize(item);
    const card = el('button', 'pc fx');
    card.type = 'button';
    card.dataset.nav = 'true';

    const art = el('div', 'art');
    art.append(face(data.raw));
    const rev = el('div', 'rev');
    rev.append(el('b', '', data.title));
    if (data.subtitle) rev.append(el('span', '', data.subtitle));
    art.append(rev);
    card.append(art, el('div', 'cap', data.title));
    if (data.subtitle) card.append(el('div', 'sub', data.subtitle));
    card.addEventListener('click', () => {
      if (onClick) onClick(data.raw);
      else openItem(world, data.raw, 'details');
    });
    return card;
  }

  function continueCard(world, item, isNextUp) {
    const data = normalize(item);
    const card = el('button', 'ct fx');
    card.type = 'button';
    card.dataset.nav = 'true';
    card.append(face(data.raw), el('div', 'shade'));
    card.append(el('div', 'lbl', data.title));
    if (data.subtitle) card.append(el('div', 'sub', data.subtitle));

    const play = el('span', 'play');
    play.innerHTML = '<svg viewBox="0 0 24 24" fill="#f0c44a"><path d="M9 7.5v9l7-4.5z"/></svg>';
    card.append(play, el('span', 'frame'));

    if (data.progress >= 0 && !isNextUp) {
      const bar = el('span', 'bar');
      const fill = el('i');
      fill.style.width = data.progress + '%';
      bar.append(fill);
      card.append(bar);
    }

    card.addEventListener('click', () => isNextUp ? nextUpItem(world, data.raw) : resumeItem(world, data.raw));
    return card;
  }
  function topTenCard(world, item, index) {
    const data = normalize(item);
    const card = el('button', 't10 fx');
    card.type = 'button';
    card.dataset.nav = 'true';
    card.append(el('span', 'num', String(index + 1)));
    const art = el('div', 'art');
    art.append(face(data.raw));
    card.append(art);
    const copy = el('span', 'rank-copy');
    copy.append(el('b', '', data.title));
    if (data.subtitle) copy.append(el('span', '', data.subtitle));
    card.append(copy);
    card.addEventListener('click', () => openItem(world, data.raw, 'details'));
    return card;
  }

  function updateRailEdges(wrap) {
    const rail = wrap && wrap.querySelector('.rail');
    if (!rail) return;
    const prev = wrap.querySelector('.edge.prev');
    const next = wrap.querySelector('.edge.next');
    if (prev) prev.classList.toggle('can', rail.scrollLeft > 5);
    if (next) next.classList.toggle('can', rail.scrollLeft + rail.clientWidth < rail.scrollWidth - 5);
  }

  function railSection(world, title, items, options) {
    const list = Array.isArray(items) ? items : [];
    const opts = options || {};
    if (!list.length && opts.hideWhenEmpty) return null;
    const section = el('section', 'widget');
    section.append(sectionHeader(title, opts.more));

    if (!list.length) {
      section.append(emptyState('Nothing here yet', opts.empty || 'Colosseum has no rows for this section.'));
      return section;
    }

    const wrap = el('div', 'rail-wrap');
    const rail = el('div', 'rail');
    list.forEach((item, index) => {
      if (opts.kind === 'continue') rail.append(continueCard(world, item, false));
      else if (opts.kind === 'next') rail.append(continueCard(world, item, true));
      else if (opts.kind === 'top10') rail.append(topTenCard(world, item, index));
      else rail.append(posterCard(world, item, opts.onClick));
    });

    const prev = el('button', 'edge prev', '‹');
    const next = el('button', 'edge next', '›');
    prev.type = next.type = 'button';
    prev.tabIndex = next.tabIndex = -1;
    prev.addEventListener('click', () => rail.scrollBy({ left: -rail.clientWidth * .78, behavior: 'smooth' }));
    next.addEventListener('click', () => rail.scrollBy({ left: rail.clientWidth * .78, behavior: 'smooth' }));
    rail.addEventListener('scroll', () => updateRailEdges(wrap), { passive: true });
    wrap.append(rail, prev, next);
    section.append(wrap);
    requestAnimationFrame(() => updateRailEdges(wrap));
    return section;
  }

  function heroAction(world, item, primary) {
    if (world === 'Home') {
      send('open-universe', {
        extensionId: item.extensionId || item.id || '',
        name: rawTitle(item),
        item
      });
      return;
    }
    openItem(world, item, primary ? 'primary' : 'details');
  }

  function heroCarousel(world, items, options) {
    const list = Array.isArray(items) ? items : [];
    const opts = options || {};
    if (!list.length) {
      const empty = el('section', 'car empty widget');
      empty.append(emptyState('Featured is waiting', 'Colosseum has not supplied featured items yet.'));
      return empty;
    }

    state.heroIndex = Math.min(state.heroIndex, list.length - 1);
    const car = el('section', 'car widget');
    car.dataset.carousel = 'true';
    const track = el('div', 'track');
    track.style.transform = 'translateX(-' + (state.heroIndex * 100) + '%)';

    list.forEach((item, index) => {
      const data = normalize(item);
      const slide = el('article', 'slide');
      const image = makeImage(data.backdrop || data.poster, 'art');
      if (image) slide.append(image);
      slide.append(el('div', 'wash'));
      slide.append(el('div', 'ghost', (data.kind || world).slice(0, 1).toUpperCase()));

      const copy = el('div', 'copy');
      copy.append(el('div', 'kicker', opts.kicker || ('FEATURED IN ' + world.toUpperCase())));
      copy.append(el('h3', '', data.title));
      if (data.description) copy.append(el('p', '', data.description));

      const buttons = el('div', 'btns');
      const primary = el('button', 'b-gold fx', opts.primary || (world === 'Theatre' ? 'Watch' : 'Read'));
      primary.type = 'button';
      primary.dataset.nav = 'true';
      primary.addEventListener('click', () => heroAction(world, data.raw, true));
      const details = el('button', 'b-glass fx', opts.secondary || 'Details');
      details.type = 'button';
      details.dataset.nav = 'true';
      details.addEventListener('click', () => heroAction(world, data.raw, false));
      buttons.append(primary);
      if (!opts.noSecondary) buttons.append(details);
      copy.append(buttons);
      slide.append(copy);
      track.append(slide);
    });
    car.append(track);

    if (list.length > 1) {
      const dots = el('div', 'dots');
      list.forEach((_, index) => {
        const dot = el('button', 'dot' + (index === state.heroIndex ? ' on' : ''));
        dot.type = 'button';
        dot.tabIndex = -1;
        dot.addEventListener('click', () => setHeroIndex(index));
        dots.append(dot);
      });
      car.append(dots);
    }

    car.addEventListener('pointerenter', stopHeroTimer);
    car.addEventListener('pointerleave', startHeroTimer);
    car.addEventListener('focusin', stopHeroTimer);
    car.addEventListener('focusout', startHeroTimer);
    return car;
  }

  function setHeroIndex(index) {
    const car = document.querySelector('.car[data-carousel="true"]');
    if (!car) return;
    const slides = car.querySelectorAll('.slide');
    if (!slides.length) return;
    state.heroIndex = ((index % slides.length) + slides.length) % slides.length;
    const track = car.querySelector('.track');
    if (track) track.style.transform = 'translateX(-' + (state.heroIndex * 100) + '%)';
    car.querySelectorAll('.dot').forEach((dot, i) => dot.classList.toggle('on', i === state.heroIndex));
  }

  function stopHeroTimer() {
    if (state.heroTimer) clearInterval(state.heroTimer);
    state.heroTimer = 0;
  }

  function startHeroTimer() {
    stopHeroTimer();
    const list = state.surface === 'Home'
      ? (Array.isArray(state.snapshot.universes) ? state.snapshot.universes : [])
      : listFrom(worldData(state.surface).featured);
    if (list.length < 2) return;
    state.heroTimer = setInterval(() => setHeroIndex(state.heroIndex + 1), 6500);
  }

  function tabBar(world, tabs, active, docked) {
    const bar = el('div', 'tabs glass');
    if (docked) bar.dataset.dockedTabs = 'true';
    tabs.forEach(tab => {
      const button = el('button', 'tab fx' + (tab.key === active ? ' on' : ''), tab.label || tab.key);
      button.type = 'button';
      button.id = (docked ? 'dock-' : 'world-') + 'tab-' + world.toLowerCase() + '-' + tab.key;
      button.dataset.nav = 'true';
      button.setAttribute('aria-current', tab.key === active ? 'page' : 'false');
      button.addEventListener('click', () => selectTab(world, tab.key, button.id));
      bar.append(button);
    });
    return bar;
  }

  function selectTab(world, tab, focusId) {
    if (!WORLD_TABS[world] || !WORLD_TABS[world].some(row => row.key === tab)) return;
    state.activeTabs[world] = tab;
    state.lastFocus = focusId || ('world-tab-' + world.toLowerCase() + '-' + tab);
    state.heroIndex = 0;
    send('world-tab', { world, tab });
    render();
  }

  function genreList(items) {
    const counts = new Map();
    (items || []).forEach(item => {
      let genres = item && (item.genres || item.genre);
      if (typeof genres === 'string') genres = genres.split(/[,|]/);
      if (!Array.isArray(genres)) return;
      genres.forEach(name => {
        name = String(name || '').trim();
        if (name) counts.set(name, (counts.get(name) || 0) + 1);
      });
    });
    return [...counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 10);
  }

  function genreMosaic(world, items) {
    const genres = genreList(items);
    if (!genres.length) return null;
    const section = el('section', 'widget');
    section.append(sectionHeader(world === 'Theatre' ? 'Genres' : 'Browse genres'));
    const grid = el('div', 'gm');
    genres.forEach(([name, count]) => {
      const sample = (items || []).find(item => {
        const source = item && (item.genres || item.genre);
        const values = Array.isArray(source) ? source : String(source || '').split(/[,|]/);
        return values.map(x => String(x).trim()).includes(name);
      });
      const tile = el('button', 'gt fx');
      tile.type = 'button';
      tile.dataset.nav = 'true';
      const image = sample ? makeImage(backdropUrl(sample) || posterUrl(sample)) : null;
      if (image) tile.append(image);
      tile.append(el('span', 'n', name), el('span', 'c', String(count)));
      tile.addEventListener('click', () => send('open-genre', { world, genre: name }));
      grid.append(tile);
    });
    section.append(grid);
    return section;
  }

  function renderPayload(world, key, payload) {
    const pane = el('div', 'world-pane');
    if (Array.isArray(payload)) {
      const section = railSection(world, key === 'library' ? 'Your Library' : 'Now Browsing', payload);
      if (section) pane.append(section);
      return pane;
    }

    if (!isObject(payload)) {
      pane.append(emptyState('Waiting for ' + key, 'Colosseum has not mounted this tab snapshot yet.'));
      window.ColosseumWeb.requestSnapshot(world, key);
      return pane;
    }

    const sections = Array.isArray(payload.sections) ? payload.sections : null;
    if (sections) {
      sections.forEach((part, index) => {
        const items = listFrom(part);
        const title = part.title || part.label || ('Section ' + (index + 1));
        const useTop10 = world === 'Theatre' && index === 0 &&
          (key === 'movies' || key === 'shows' || key === 'anime') &&
          /top/i.test(title) && items.length >= 5;
        const section = railSection(world, title, useTop10 ? items.slice(0, 10) : items, {
          kind: useTop10 ? 'top10' : '',
          more: part.seeAll ? {
            label: part.moreLabel || 'See all',
            action: () => send('see-all', { world, tab: key, pin: part.pin || part })
          } : null
        });
        if (section) pane.append(section);
      });
      const all = sections.flatMap(section => listFrom(section));
      const genres = genreMosaic(world, all);
      if (genres && world === 'Theatre' && ['movies', 'shows', 'anime'].includes(key)) pane.append(genres);
      return pane;
    }

    const items = listFrom(payload);
    if (key === 'library') {
      const section = el('section', 'widget');
      section.append(sectionHeader('Your Library'));
      if (!items.length) section.append(emptyState('Nothing saved yet', 'Your existing Colosseum Collection will appear here.'));
      else {
        const grid = el('div', 'grid');
        items.forEach(item => grid.append(posterCard(world, item)));
        section.append(grid);
      }
      pane.append(section);
    } else {
      const section = railSection(world, payload.title || 'Now Browsing', items);
      if (section) pane.append(section);
    }
    return pane;
  }

  function renderWorld(world) {
    const data = worldData(world);
    const pane = el('div', 'world-pane');
    pane.append(heroCarousel(world, listFrom(data.featured)));

    const next = railSection(world, 'Next Up', listFrom(data.nextUp), {
      kind: 'next', hideWhenEmpty: true,
      more: { label: 'See all', action: () => send('continue-see-all', { world }) }
    });
    if (next) pane.append(next);

    const resume = railSection(world, world === 'Theatre' ? 'Continue Watching' : 'Continue Reading',
      listFrom(data.continue), {
        kind: 'continue', hideWhenEmpty: true,
        more: { label: 'See all', action: () => send('continue-see-all', { world }) }
      });
    if (resume) pane.append(resume);

    const tabs = tabsFor(world, data);
    const active = state.activeTabs[world] || (tabs[0] && tabs[0].key) || 'discover';
    const host = el('div', 'tabs-host widget');
    host.id = 'world-tabs-host';
    host.append(tabBar(world, tabs, active, false));
    pane.append(host);

    const payload = tabPayload(world, active);
    pane.append(renderPayload(world, active, payload));
    renderDock(world, tabs, active);
    return pane;
  }

  function deriveHomePreview(world) {
    const data = worldData(world);
    if (world === 'Tankoban') {
      const tabs = isObject(data.tabs) ? data.tabs : {};
      const manga = listFrom(tabs.manga || data.manga);
      const comics = listFrom(tabs.comics || data.comics);
      return manga.concat(comics).slice(0, 16);
    }
    if (world === 'Theatre') {
      return listFrom(data.featured).concat(listFrom(data.movies)).slice(0, 16);
    }
    if (world === 'Biblio') return listFrom(data.featured).slice(0, 16);
    return [];
  }

  function homeSection(world, title, items) {
    const list = Array.isArray(items) ? items : [];
    const section = el('section', 'widget');
    const head = el('div', 'home-world-head');
    head.append(el('h2', '', title));
    const open = el('button', 'fx', 'Open ' + world + ' ›');
    open.type = 'button';
    open.dataset.nav = 'true';
    open.addEventListener('click', () => navigate(world));
    head.append(open);
    section.append(head);

    if (!list.length) {
      section.append(emptyState('Waiting for ' + world, 'The existing catalogue has not supplied this Home preview yet.'));
      return section;
    }
    const wrap = el('div', 'rail-wrap');
    const rail = el('div', 'rail');
    list.forEach(item => rail.append(posterCard(world, item, () => navigate(world))));
    wrap.append(rail);
    section.append(wrap);
    return section;
  }

  function renderHome() {
    const data = homeData();
    const pane = el('div', 'home-pane');
    const universes = Array.isArray(data.universes) ? data.universes :
      (Array.isArray(state.snapshot.universes) ? state.snapshot.universes : []);
    pane.append(heroCarousel('Home', universes, {
      kicker: 'UNIVERSE',
      primary: 'Explore the universe',
      noSecondary: true
    }));

    const resume = railSection('Home', 'Continue', listFrom(data.continue || data.progress), {
      kind: 'continue', hideWhenEmpty: true,
      more: { label: 'See all', action: () => send('continue-see-all', { world: 'Home' }) }
    });
    if (resume) pane.append(resume);

    const tank = isObject(data.tankoban)
      ? listFrom(data.tankoban.manga).concat(listFrom(data.tankoban.comics)).slice(0, 16)
      : deriveHomePreview('Tankoban');
    const theatre = isObject(data.theatre) ? listFrom(data.theatre.items) : deriveHomePreview('Theatre');
    const biblio = isObject(data.biblio) ? listFrom(data.biblio.chart) : deriveHomePreview('Biblio');
    pane.append(homeSection('Tankoban', 'Tankoban', tank));
    pane.append(homeSection('Theatre', 'Theatre', theatre));
    pane.append(homeSection('Biblio', 'Biblio', biblio));

    const vault = el('button', 'vault-door glass fx');
    vault.type = 'button';
    vault.dataset.nav = 'true';
    const copy = el('span', 'copy');
    copy.append(el('b', '', 'Vault'), el('span', '', 'Local media on this machine'));
    vault.append(copy, el('span', 'more', 'Open Vault ›'));
    vault.addEventListener('click', () => send('open-vault'));
    pane.append(vault);
    clearDock();
    return pane;
  }

  function renderDock(world, tabs, active) {
    const dock = $('dock');
    const target = $('dockTabs');
    target.replaceChildren();
    const fresh = tabBar(world, tabs, active, true);
    [...fresh.children].forEach(child => target.append(child));
    dock.setAttribute('aria-hidden', 'true');
    document.body.classList.remove('docked');
  }

  function clearDock() {
    $('dockTabs').replaceChildren();
    $('dock').setAttribute('aria-hidden', 'true');
    document.body.classList.remove('docked');
  }

  function updateDock() {
    const host = $('world-tabs-host');
    const dock = $('dock');
    if (!host || state.surface === 'Home') {
      clearDock();
      return;
    }
    const board = $('board');
    const on = host.getBoundingClientRect().bottom <= board.getBoundingClientRect().top + 4;
    document.body.classList.toggle('docked', on);
    dock.setAttribute('aria-hidden', on ? 'false' : 'true');
  }

  function setWallpaper() {
    const wall = $('wall');
    const value = safeUrl(state.snapshot.wallpaper || homeData().wallpaper);
    wall.style.backgroundImage = value ? 'url("' + value.replace(/"/g, '%22') + '")' : '';
    wall.dataset.hasImage = value ? 'true' : 'false';
  }

  function updateClock() {
    const now = new Date();
    const hour = now.getHours();
    const h12 = hour % 12 || 12;
    $('clkT').textContent = h12 + ':' + String(now.getMinutes()).padStart(2, '0');
    $('clkA').textContent = hour < 12 ? 'AM' : 'PM';
    $('clkD').textContent = now.toLocaleDateString(undefined, {
      weekday: 'long', month: 'long', day: 'numeric'
    });
  }

  function updateTopbar() {
    $('app').dataset.surface = state.surface;
    $('home-button').hidden = state.surface === 'Home';
    document.querySelectorAll('#world-nav [data-world]').forEach(button => {
      const active = button.dataset.world === state.surface;
      button.classList.toggle('active', active);
      button.setAttribute('aria-current', active ? 'page' : 'false');
    });
    const initial = String(state.snapshot.accountInitial || '?').trim().slice(0, 1).toUpperCase() || '?';
    $('account-button').textContent = initial;
    $('backend-status').textContent = state.mounted ? 'Colosseum backend mounted' : 'Waiting for Colosseum backend';
  }

  function updateHints() {
    const hints = $('hints');
    hints.replaceChildren();
    const add = (key, label) => {
      const span = el('span');
      span.innerHTML = '<kbd>' + key + '</kbd>' + label;
      hints.append(span);
    };
    add('↑↓←→', 'Move');
    add('Enter', 'Open');
    add('Esc', state.searchOpen ? 'Back' : (state.surface === 'Home' ? 'Home' : 'Back'));
    if (state.surface !== 'Home') add('[ ]', 'Tabs');
    add('/', 'Search');
  }

  function render() {
    stopHeroTimer();
    updateTopbar();
    setWallpaper();
    const col = $('col');
    col.replaceChildren(state.surface === 'Home' ? renderHome() : renderWorld(state.surface));
    $('board').scrollTop = Math.min($('board').scrollTop, $('board').scrollHeight);
    restoreFocus();
    updateHints();
    startHeroTimer();
    requestAnimationFrame(updateDock);
  }

  function navigate(surface) {
    const next = WORLDS.includes(surface) ? surface : 'Home';
    if (next === state.surface) return;
    rememberFocus();
    state.surface = next;
    state.heroIndex = 0;
    $('board').scrollTop = 0;
    if (next === 'Home') send('home');
    else send('open-world', { world: next });
    render();
    if (next !== 'Home' && !Object.keys(worldData(next)).length)
      window.ColosseumWeb.requestSnapshot(next, state.activeTabs[next]);
  }
  function rememberFocus() {
    const active = document.activeElement;
    state.lastFocus = active && active.id ? active.id : '';
  }

  function restoreFocus() {
    requestAnimationFrame(() => {
      if (state.lastFocus) {
        const target = document.getElementById(state.lastFocus);
        state.lastFocus = '';
        if (target) {
          target.focus({ preventScroll: true });
          revealFocus(target);
          return;
        }
      }
      const active = document.activeElement;
      if (active && active !== document.body && !['board', 'col'].includes(active.id)) return;
      const target = document.querySelector('#col [data-nav="true"]');
      if (target && state.mounted) target.focus({ preventScroll: true });
    });
  }

  function revealFocus(node) {
    if (!node || state.searchOpen) return;
    const rail = node.closest('.rail');
    if (rail) {
      const r = node.getBoundingClientRect();
      const b = rail.getBoundingClientRect();
      if (r.left < b.left) rail.scrollBy({ left: r.left - b.left - 10, behavior: 'smooth' });
      else if (r.right > b.right) rail.scrollBy({ left: r.right - b.right + 10, behavior: 'smooth' });
    }

    const board = $('board');
    const widget = node.closest('.widget');
    if (widget && !node.closest('.car')) {
      const wr = widget.getBoundingClientRect();
      const br = board.getBoundingClientRect();
      if (wr.top < br.top + 8 || wr.top > br.top + 110)
        board.scrollBy({ top: wr.top - br.top - 8, behavior: 'smooth' });
    }
  }
  function focusables() {
    const scope = state.searchOpen ? $('search') : document;
    return [...scope.querySelectorAll('button:not([disabled]):not([hidden]), input:not([disabled])')]
      .filter(node => {
        if (node.closest('[hidden]')) return false;
        const r = node.getBoundingClientRect();
        return r.width > 0 && r.height > 0;
      });
  }

  function spatialMove(key) {
    const current = document.activeElement;
    const list = focusables();
    if (!current || !list.includes(current)) {
      if (list[0]) list[0].focus();
      return true;
    }
    const c = current.getBoundingClientRect();
    const cx = (c.left + c.right) / 2;
    const cy = (c.top + c.bottom) / 2;
    let best = null;
    let bestScore = Infinity;

    list.forEach(candidate => {
      if (candidate === current) return;
      const r = candidate.getBoundingClientRect();
      const x = (r.left + r.right) / 2;
      const y = (r.top + r.bottom) / 2;
      const dx = x - cx;
      const dy = y - cy;
      if (key === 'ArrowLeft' && dx >= -3) return;
      if (key === 'ArrowRight' && dx <= 3) return;
      if (key === 'ArrowUp' && dy >= -3) return;
      if (key === 'ArrowDown' && dy <= 3) return;
      const horizontal = key === 'ArrowLeft' || key === 'ArrowRight';
      const primary = horizontal ? Math.abs(dx) : Math.abs(dy);
      const secondary = horizontal ? Math.abs(dy) : Math.abs(dx);
      const score = primary + secondary * 2.4;
      if (score < bestScore) {
        best = candidate;
        bestScore = score;
      }
    });

    if (!best) return false;
    best.focus({ preventScroll: true });
    revealFocus(best);
    return true;
  }
  function openSearch(initial) {
    state.searchOpen = true;
    state.lastFocus = document.activeElement && document.activeElement.id || '';
    $('search').classList.add('on');
    $('board').inert = true;
    $('topbar').inert = true;
    const input = $('search-input');
    input.value = initial || '';
    renderSearchResults();
    input.focus();
    if (input.value) queueSearch();
    send('search-open');
    updateHints();
  }

  function closeSearch() {
    state.searchOpen = false;
    $('search').classList.remove('on');
    $('board').inert = false;
    $('topbar').inert = false;
    send('search-close');
    const target = state.lastFocus && document.getElementById(state.lastFocus);
    state.lastFocus = '';
    if (target) target.focus();
    else $('search-button').focus();
    updateHints();
  }

  function renderSearchResults() {
    const root = $('search-results');
    root.replaceChildren();
    const query = $('search-input').value.trim();
    const search = isObject(state.snapshot.search) ? state.snapshot.search : {};
    const results = Array.isArray(search.results) ? search.results : [];

    if (!query) {
      root.append(el('div', 'sempty', 'Type to search the current Colosseum backend.'));
      return;
    }
    if (search.loading) {
      root.append(el('div', 'sempty', 'Searching…'));
      return;
    }
    if (!results.length) {
      root.append(el('div', 'sempty', 'No results.'));
      return;
    }
    const grid = el('div', 'search-grid');
    results.forEach(item => {
      const world = item.world || state.surface;
      grid.append(posterCard(world === 'Home' ? 'Theatre' : world, item, raw => {
        closeSearch();
        openItem(world === 'Home' ? 'Theatre' : world, raw, 'details');
      }));
    });
    root.append(grid);
  }

  function queueSearch() {
    clearTimeout(state.searchTimer);
    state.searchTimer = setTimeout(() => {
      send('search-query', { query: $('search-input').value.trim() });
      renderSearchResults();
    }, 120);
  }

  function cycleTab(delta) {
    if (state.surface === 'Home') return;
    const tabs = tabsFor(state.surface, worldData(state.surface));
    if (!tabs.length) return;
    const active = state.activeTabs[state.surface] || tabs[0].key;
    let index = tabs.findIndex(tab => tab.key === active);
    index = (index + delta + tabs.length) % tabs.length;
    selectTab(state.surface, tabs[index].key);
  }

  function patchSnapshot(data) {
    state.snapshot = merge(state.snapshot, data);
    if (data.surface && (data.surface === 'Home' || WORLDS.includes(data.surface)))
      state.surface = data.surface;
    if (isObject(data.activeTabs))
      state.activeTabs = Object.assign({}, state.activeTabs, data.activeTabs);
    state.mounted = true;
    render();
    if (state.searchOpen) renderSearchResults();
  }

  function mountSnapshot(data) {
    state.snapshot = data || {};
    state.surface = data && (data.surface === 'Home' || WORLDS.includes(data.surface))
      ? data.surface : 'Home';
    state.activeTabs = Object.assign(
      { Tankoban: 'discover', Biblio: 'discover', Theatre: 'discover' },
      isObject(data.activeTabs) ? data.activeTabs : {}
    );
    WORLDS.forEach(world => {
      const active = worldData(world).activeTab;
      if (active) state.activeTabs[world] = String(active);
    });
    state.heroIndex = 0;
    state.mounted = true;
    render();
  }

  function bindChrome() {
    $('home-button').addEventListener('click', () => navigate('Home'));
    document.querySelectorAll('#world-nav [data-world]').forEach(button => {
      button.addEventListener('click', () => navigate(button.dataset.world));
    });
    $('search-button').addEventListener('click', () => openSearch(''));
    $('search-close').addEventListener('click', closeSearch);
    $('search-input').addEventListener('input', queueSearch);
    $('trackers-button').addEventListener('click', () => send('trackers'));
    $('account-button').addEventListener('click', () => send('account'));
    $('wallpaper-button').addEventListener('click', () => send('wallpaper'));
    $('minimize-button').addEventListener('click', () => send('window-minimize'));
    $('fullscreen-button').addEventListener('click', () => send('window-toggle-fullscreen'));
    $('close-button').addEventListener('click', () => send('window-close'));
    $('board').addEventListener('scroll', updateDock, { passive: true });

    document.addEventListener('mousemove', () => {
      if (!state.mouseMode) {
        state.mouseMode = true;
        document.body.classList.add('mouse');
        document.body.classList.remove('keys');
      }
    }, { passive: true });
    document.addEventListener('mouseover', event => {
      if (!state.mouseMode) return;
      const target = event.target.closest && event.target.closest('.fx');
      if (!target || target.disabled || target === document.activeElement) return;
      target.focus({ preventScroll: true });
      revealFocus(target);
    });

    document.addEventListener('keydown', event => {
      if (!['Shift', 'Control', 'Alt', 'Meta'].includes(event.key)) {
        state.mouseMode = false;
        document.body.classList.remove('mouse');
        document.body.classList.add('keys');
      }

      if (state.searchOpen) {
        if (event.key === 'Escape') {
          event.preventDefault();
          closeSearch();
        } else if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key)) {
          if (spatialMove(event.key)) event.preventDefault();
        }
        return;
      }

      if (event.key === 'Escape' || event.key === 'Backspace') {
        if (state.surface !== 'Home') {
          event.preventDefault();
          navigate('Home');
        }
        return;
      }

      if (event.key === '[' || event.key === ']') {
        if (state.surface !== 'Home') {
          event.preventDefault();
          cycleTab(event.key === ']' ? 1 : -1);
        }
        return;
      }

      if (event.key === '/') {
        event.preventDefault();
        openSearch('');
        return;
      }
      if (/^\p{L}$/u.test(event.key) && !event.ctrlKey && !event.altKey && !event.metaKey) {
        event.preventDefault();
        openSearch(event.key);
        return;
      }

      if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key)) {
        if (spatialMove(event.key)) event.preventDefault();
      }
    });
  }

  window.addEventListener('colosseum:webui-mount', event => mountSnapshot(event.detail || {}));
  window.addEventListener('colosseum:webui-patch', event => patchSnapshot(event.detail || {}));

  bindChrome();
  updateClock();
  setInterval(updateClock, 1000);
  updateTopbar();
  updateHints();
  $('col').append(emptyState('Waiting for Colosseum', 'The existing backend will mount this frontend.'));
  window.ColosseumWeb.requestSnapshot('Home', '');
}());
