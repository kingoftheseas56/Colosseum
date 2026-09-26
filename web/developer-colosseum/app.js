(function () {
  'use strict';

  const $ = function (id) { return document.getElementById(id); };
  const WORLD_ORDER = ['Tankoban', 'Biblio', 'Theatre'];
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
    surface: 'Home',
    snapshot: {},
    activeTabs: { Tankoban: 'discover', Biblio: 'discover', Theatre: 'discover' },
    heroIndex: 0,
    heroTimer: 0,
    searchOpen: false,
    searchTimer: 0,
    lastFocus: null
  };

  function isObject(value) {
    return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
  }

  function merge(target, source) {
    if (!isObject(source))
      return source;
    const out = isObject(target) ? Object.assign({}, target) : {};
    Object.keys(source).forEach(function (key) {
      const value = source[key];
      out[key] = isObject(value) ? merge(out[key], value) : value;
    });
    return out;
  }

  function node(tag, className, text) {
    const el = document.createElement(tag);
    if (className) el.className = className;
    if (text !== undefined && text !== null) el.textContent = String(text);
    return el;
  }
  function send(type, payload) {
    const body = Object.assign({ surface: state.surface }, payload || {});
    return window.ColosseumWeb.action(type, body);
  }

  function safeUrl(value) {
    if (!value) return '';
    try {
      const url = new URL(String(value), location.href);
      const allowed = ['https:', 'http:', 'file:', 'data:', 'blob:', 'colosseum:'];
      return allowed.includes(url.protocol) ? url.href : '';
    } catch (_) {
      return '';
    }
  }

  function titleOf(item) {
    if (!item || typeof item !== 'object') return String(item || '');
    return String(item.title || item.caption || item.name || item.label || '');
  }

  function artOf(item) {
    if (!item || typeof item !== 'object') return '';
    return item.cover || item.coverUrl || item.art || item.image ||
           item.poster || item.posterUrl || item.banner || item.backdrop || '';
  }

  function idOf(item) {
    if (!item || typeof item !== 'object') return '';
    return String(item.id || item.seriesId || item.malId || item.gcdId ||
                  item.canonicalId || item.identityId || '');
  }
  function kindOf(item) {
    if (!item || typeof item !== 'object') return '';
    return String(item.kind || item.type || item.mediaType || item.format || '').toLowerCase();
  }

  function progressOf(item) {
    if (!item || typeof item !== 'object') return -1;
    let value = item.progress;
    if (value === undefined || value === null) value = item.percent;
    value = Number(value);
    if (!Number.isFinite(value) || value < 0) return -1;
    if (value <= 1) value *= 100;
    return Math.max(0, Math.min(100, value));
  }

  function subtitleOf(item) {
    if (!item || typeof item !== 'object') return '';
    if (item.episode) return String(item.episode);
    if (item.author) return String(item.author);
    if (item.subtitle) return String(item.subtitle);
    if (item.year) return String(item.year);
    if (item.lane) return String(item.lane);
    if (item.source) return String(item.source);
    return '';
  }

  function normalized(item) {
    const raw = isObject(item) ? item : { title: String(item || '') };
    return {
      raw: raw,
      id: idOf(raw),
      title: titleOf(raw) || 'Untitled',
      art: safeUrl(artOf(raw)),
      kind: kindOf(raw),
      subtitle: subtitleOf(raw),
      progress: progressOf(raw)
    };
  }
  function fallbackHue(seed) {
    let hash = 0;
    const text = String(seed || 'Colosseum');
    for (let i = 0; i < text.length; i++)
      hash = ((hash << 5) - hash + text.charCodeAt(i)) | 0;
    return Math.abs(hash) % 360;
  }

  function artFrame(item, className) {
    const data = normalized(item);
    const frame = node('div', className || 'art-frame');
    const hue = fallbackHue(data.title);
    frame.style.setProperty('--fallback-a', 'hsl(' + hue + ' 27% 29%)');
    frame.style.setProperty('--fallback-b', 'hsl(' + ((hue + 32) % 360) + ' 24% 11%)');
    if (data.art) {
      const image = node('img', 'art-image');
      image.alt = '';
      image.loading = 'lazy';
      image.decoding = 'async';
      image.src = data.art;
      image.addEventListener('error', function () { image.remove(); });
      frame.append(image);
    }
    return frame;
  }

  function identityPayload(item) {
    const data = normalized(item);
    return {
      id: data.id,
      title: data.title,
      kind: data.kind,
      item: data.raw
    };
  }
  function openItem(world, item, intent) {
    send('open-item', Object.assign({
      world: world,
      intent: intent || 'details'
    }, identityPayload(item)));
  }

  function activateContinue(world, item, intent) {
    send(intent === 'details' ? 'continue-details' : 'resume',
         Object.assign({ world: world }, identityPayload(item)));
  }

  function activateNextUp(world, item) {
    send('next-up', Object.assign({ world: world }, identityPayload(item)));
  }

  function sectionHeading(title, moreLabel, onMore) {
    const head = node('div', 'section-heading');
    const h = node('h2', '', title);
    head.append(h);
    if (moreLabel && onMore) {
      const more = node('button', 'text-button', moreLabel + ' ›');
      more.type = 'button';
      more.addEventListener('click', onMore);
      head.append(more);
    }
    return head;
  }

  function emptyState(title, body) {
    const box = node('div', 'empty-state');
    box.append(node('h3', '', title), node('p', '', body));
    return box;
  }
  function posterCard(world, item, options) {
    const opts = options || {};
    const data = normalized(item);
    const card = node('button', 'poster-card');
    card.type = 'button';
    card.dataset.nav = 'true';
    card.append(artFrame(data.raw, 'poster-art'));

    const copy = node('span', 'poster-copy');
    copy.append(node('strong', '', data.title));
    if (data.subtitle) copy.append(node('small', '', data.subtitle));
    card.append(copy);

    if (opts.badge) card.append(node('span', 'poster-badge', opts.badge));
    if (data.progress >= 0) {
      const track = node('span', 'card-progress');
      const fill = node('i');
      fill.style.width = data.progress + '%';
      track.append(fill);
      card.append(track);
    }

    card.addEventListener('click', function () {
      if (opts.onClick) opts.onClick(data.raw);
      else openItem(world, data.raw, 'details');
    });
    return card;
  }
  function rail(world, title, items, options) {
    const list = Array.isArray(items) ? items : [];
    const opts = options || {};
    const section = node('section', 'content-section');
    section.append(sectionHeading(title, opts.moreLabel, opts.onMore));

    if (!list.length) {
      section.append(emptyState('Nothing here yet', opts.empty || 'Colosseum has not supplied rows for this section.'));
      return section;
    }

    const row = node('div', 'poster-rail');
    list.forEach(function (item) {
      row.append(posterCard(world, item, {
        badge: opts.badge,
        onClick: opts.onClick
      }));
    });
    section.append(row);
    return section;
  }

  function continueRail(world, title, items, nextUp) {
    return rail(world, title, items, {
      moreLabel: 'See all',
      onMore: function () { send('continue-see-all', { world: world }); },
      onClick: function (item) {
        if (nextUp) activateNextUp(world, item);
        else activateContinue(world, item, 'resume');
      },
      empty: nextUp ? 'No verified next item is available.' : 'No unfinished progress is available.'
    });
  }
  function hero(world, items) {
    const list = Array.isArray(items) ? items : [];
    const wrap = node('section', 'world-hero');
    if (!list.length) {
      wrap.append(emptyState('Featured is waiting', 'The existing Colosseum catalogue has not supplied featured items yet.'));
      return wrap;
    }

    const item = list[Math.min(state.heroIndex, list.length - 1)] || list[0];
    const data = normalized(item);
    const visual = artFrame(item, 'hero-art');
    const shade = node('div', 'hero-shade');
    const copy = node('div', 'hero-copy');
    copy.append(node('p', 'eyebrow', 'Featured in ' + world));
    copy.append(node('h1', '', data.title));
    if (data.subtitle) copy.append(node('p', 'hero-subtitle', data.subtitle));

    const actions = node('div', 'hero-actions');
    const primary = node('button', 'primary-button', world === 'Theatre' ? 'Watch' : 'Read');
    primary.type = 'button';
    primary.dataset.nav = 'true';
    primary.addEventListener('click', function () { openItem(world, item, 'primary'); });
    const details = node('button', 'outline-button', 'Details');
    details.type = 'button';
    details.dataset.nav = 'true';
    details.addEventListener('click', function () { openItem(world, item, 'details'); });
    actions.append(primary, details);
    copy.append(actions);

    wrap.append(visual, shade, copy);

    if (list.length > 1) {
      const count = node('span', 'hero-count', (state.heroIndex + 1) + ' / ' + list.length);
      wrap.append(count);
      wrap.addEventListener('pointerenter', stopHeroTimer);
      wrap.addEventListener('pointerleave', startHeroTimer);
      wrap.addEventListener('focusin', stopHeroTimer);
      wrap.addEventListener('focusout', startHeroTimer);
    }
    return wrap;
  }

  function clearHeroTimer() {
    if (state.heroTimer) window.clearInterval(state.heroTimer);
    state.heroTimer = 0;
  }

  function stopHeroTimer() { clearHeroTimer(); }

  function refreshHeroOnly() {
    const selector = state.surface === 'Home' ? '.universe-hero' : '.world-hero';
    const current = $('surface').querySelector(selector);
    if (!current) return;
    const replacement = state.surface === 'Home'
      ? homeUniverseHero(homeData())
      : hero(state.surface, listFrom(worldData(state.surface).featured));
    current.replaceWith(replacement);
  }

  function startHeroTimer() {
    clearHeroTimer();
    const list = listFrom(worldData(state.surface).featured);
    if (state.surface === 'Home' || list.length < 2) return;
    state.heroTimer = window.setInterval(function () {
      state.heroIndex = (state.heroIndex + 1) % list.length;
      refreshHeroOnly();
    }, 6500);
  }

  function homeData() {
    return isObject(state.snapshot.home) ? state.snapshot.home : {};
  }

  function worldData(name) {
    if (isObject(state.snapshot.worlds) && isObject(state.snapshot.worlds[name]))
      return state.snapshot.worlds[name];
    const lower = name.toLowerCase();
    if (isObject(state.snapshot[lower])) return state.snapshot[lower];
    return {};
  }

  function listFrom(value) {
    if (Array.isArray(value)) return value;
    if (isObject(value) && Array.isArray(value.items)) return value.items;
    if (isObject(value) && Array.isArray(value.rows)) return value.rows;
    return [];
  }

  function firstSectionItems(payload) {
    if (!isObject(payload) || !Array.isArray(payload.sections)) return [];
    for (const section of payload.sections) {
      const items = listFrom(section);
      if (items.length) return items;
    }
    return [];
  }

  function deriveHomeWorldPreview(world) {
    const data = worldData(world);
    if (world === 'Tankoban') {
      const tabs = isObject(data.tabs) ? data.tabs : {};
      return {
        manga: listFrom(tabs.manga || data.manga).length ? listFrom(tabs.manga || data.manga)
              : firstSectionItems(tabs.manga || data.manga),
        comics: listFrom(tabs.comics || data.comics).length ? listFrom(tabs.comics || data.comics)
                : firstSectionItems(tabs.comics || data.comics)
      };
    }

    if (world === 'Theatre') {
      return { items: listFrom(data.featured).concat(listFrom(data.movies)).slice(0, 9) };
    }
    if (world === 'Biblio') {
      return { chart: listFrom(data.featured).slice(0, 10), genres: data.genres || [] };
    }
    return {};
  }

  function homeUniverseHero(data) {
    const list = Array.isArray(data.universes) ? data.universes :
                 (Array.isArray(state.snapshot.universes) ? state.snapshot.universes : []);
    const box = node('section', 'universe-hero');
    if (!list.length) {
      box.append(emptyState('Universes are waiting', 'Installed Universe data will appear here when Colosseum mounts the Home snapshot.'));
      return box;
    }

    const index = Math.min(state.heroIndex, list.length - 1);
    const item = list[index] || list[0];
    box.append(artFrame(item, 'universe-art'), node('div', 'universe-shade'));

    const copy = node('div', 'universe-copy');
    copy.append(node('p', 'eyebrow', 'UNIVERSE'));
    copy.append(node('h1', '', titleOf(item)));
    const open = node('button', 'glass-button', 'Explore the universe →');
    open.type = 'button';
    open.dataset.nav = 'true';
    open.addEventListener('click', function () {
      send('open-universe', { extensionId: item.extensionId || item.id || '', name: titleOf(item), item: item });
    });
    copy.append(open);
    box.append(copy);

    const hall = node('button', 'universe-hall', list.length + ' worlds ›');
    hall.type = 'button';
    hall.dataset.nav = 'true';
    hall.addEventListener('click', function () { send('open-universe-hall'); });
    box.append(hall);

    if (list.length > 1) {
      box.addEventListener('pointerenter', stopHeroTimer);
      box.addEventListener('pointerleave', startHomeHeroTimer);
      box.addEventListener('focusin', stopHeroTimer);
      box.addEventListener('focusout', startHomeHeroTimer);
    }
    return box;
  }

  function startHomeHeroTimer() {
    clearHeroTimer();
    const data = homeData();
    const list = Array.isArray(data.universes) ? data.universes :
                 (Array.isArray(state.snapshot.universes) ? state.snapshot.universes : []);
    if (state.surface !== 'Home' || list.length < 2) return;
    state.heroTimer = window.setInterval(function () {
      state.heroIndex = (state.heroIndex + 1) % list.length;
      refreshHeroOnly();
    }, 6500);
  }

  function fan(items, side) {
    const list = Array.isArray(items) ? items.slice(0, 5) : [];
    const wrap = node('div', 'cover-fan ' + side);
    if (!list.length) {
      wrap.append(node('span', 'fan-empty', 'Waiting for catalogue'));
      return wrap;
    }

    list.forEach(function (item, index) {
      const card = posterCard('Tankoban', item);
      const slot = index - (list.length - 1) / 2;
      card.classList.add('fan-card');
      card.style.setProperty('--fan-slot', slot);
      card.style.setProperty('--fan-y', Math.abs(slot) * 8 + 'px');
      card.style.setProperty('--fan-angle', slot * 7 + 'deg');
      wrap.append(card);
    });
    return wrap;
  }

  function tankobanIntro(data) {
    const box = node('section', 'world-intro tankoban-intro');
    const heading = node('button', 'intro-title', 'Tankoban');
    heading.type = 'button';
    heading.dataset.nav = 'true';
    heading.addEventListener('click', function () { navigate('Tankoban'); });

    box.append(node('span', 'intro-corner left', 'Manga'),
               node('span', 'intro-corner right', 'Comics'),
               heading);
    const fans = node('div', 'fans');
    fans.append(fan(data.manga, 'left'), fan(data.comics, 'right'));
    box.append(fans);
    return box;
  }

  function theatreIntro(data) {
    const items = Array.isArray(data.items) ? data.items.slice(0, 9) : [];
    const box = node('section', 'world-intro theatre-intro');
    const heading = node('button', 'intro-title', 'Theatre');
    heading.type = 'button';
    heading.dataset.nav = 'true';
    heading.addEventListener('click', function () { navigate('Theatre'); });
    box.append(node('span', 'intro-corner left', 'Trending'), heading, node('i', 'gold-rule'));

    const band = node('div', 'film-band');
    const reel = node('div', 'film-reel');
    const doubled = items.concat(items);
    if (!doubled.length) {
      band.append(node('span', 'film-empty', 'Waiting for Theatre catalogue'));
    } else {
      doubled.forEach(function (item) {
        const dataItem = normalized(item);
        const frame = node('button', 'film-frame');
        frame.type = 'button';
        frame.dataset.nav = 'true';
        frame.append(artFrame(item, 'film-art'));
        frame.append(node('span', 'film-lane', dataItem.kind || 'THEATRE'));
        frame.append(node('strong', '', dataItem.title));
        frame.addEventListener('click', function () { navigate('Theatre'); });
        reel.append(frame);
      });
      band.append(reel);
    }
    box.append(band);
    return box;
  }

  function biblioIntro(data) {
    const chart = Array.isArray(data.chart) ? data.chart : [];
    const genres = Array.isArray(data.genres) ? data.genres.slice(0, 4) : [];
    const box = node('section', 'world-intro biblio-intro');
    const heading = node('button', 'intro-title', 'Biblio');
    heading.type = 'button';
    heading.dataset.nav = 'true';
    heading.addEventListener('click', function () { navigate('Biblio'); });
    box.append(node('span', 'intro-corner left', 'Top charts'), heading);

    const desk = node('div', 'reading-desk');
    const pile = node('button', 'book-pile');
    pile.type = 'button';
    pile.dataset.nav = 'true';
    pile.append(node('i', 'book-slab slab-three'), node('i', 'book-slab slab-two'), node('i', 'book-slab slab-one'));
    if (chart[0]) pile.append(artFrame(chart[0], 'top-book'));
    pile.addEventListener('click', function () { navigate('Biblio'); });

    const stats = node('div', 'chart-copy');
    stats.append(node('strong', '', 'Top 10'), node('p', '', 'Current Biblio chart'));
    const chips = node('div', 'genre-chips');
    genres.forEach(function (genre) {
      const name = typeof genre === 'string' ? genre : titleOf(genre);
      const chip = node('button', 'genre-chip', name);
      chip.type = 'button';
      chip.dataset.nav = 'true';
      chip.addEventListener('click', function () {
        navigate('Biblio');
        send('open-genre', { world: 'Biblio', genre: name });
      });
      chips.append(chip);
    });

    stats.append(chips);
    const runner = node('button', 'runner-book');
    runner.type = 'button';
    runner.dataset.nav = 'true';
    if (chart[1]) runner.append(artFrame(chart[1], 'runner-art'));
    runner.append(node('span', '', 'No. 2'));
    runner.addEventListener('click', function () { navigate('Biblio'); });

    desk.append(pile, stats, runner);
    box.append(desk);
    return box;
  }

  function vaultLauncher() {
    const box = node('section', 'vault-launcher');
    const copy = node('div');
    copy.append(node('p', 'eyebrow', 'ON THIS MACHINE'));
    copy.append(node('h2', '', 'Vault'));
    copy.append(node('p', '', 'Keep the native Vault surface. Web Colosseum only hands off to it.'));
    const open = node('button', 'outline-button', 'Open Vault →');
    open.type = 'button';
    open.dataset.nav = 'true';
    open.addEventListener('click', function () { send('open-vault'); });
    box.append(copy, open);
    return box;
  }

  function renderHome() {
    const data = homeData();
    const root = node('div', 'home-surface');
    root.append(homeUniverseHero(data));

    const resumes = Array.isArray(data.continue) ? data.continue :
                    (Array.isArray(data.progress) ? data.progress : []);
    if (resumes.length)
      root.append(continueRail('Home', 'Continue', resumes, false));

    const tank = isObject(data.tankoban) ? data.tankoban : deriveHomeWorldPreview('Tankoban');
    const theatre = isObject(data.theatre) ? data.theatre : deriveHomeWorldPreview('Theatre');
    const biblio = isObject(data.biblio) ? data.biblio : deriveHomeWorldPreview('Biblio');

    root.append(tankobanIntro(tank));
    root.append(theatreIntro(theatre));
    root.append(biblioIntro(biblio));
    root.append(vaultLauncher());
    return root;
  }

  function tabPayload(world, key) {
    const data = worldData(world);
    if (isObject(data.tabs) && data.tabs[key] !== undefined)
      return data.tabs[key];
    return data[key];
  }

  function renderPayload(world, key, payload) {
    const wrap = node('div', 'tab-content');
    if (Array.isArray(payload)) {
      wrap.append(gridSection(world, '', payload));
      return wrap;
    }

    if (!isObject(payload)) {
      wrap.append(emptyState('Waiting for ' + key, 'Colosseum has not mounted this tab snapshot yet.'));
      window.ColosseumWeb.requestSnapshot(world, key);
      return wrap;
    }

    if (Array.isArray(payload.sections)) {
      payload.sections.forEach(function (section, index) {
        const title = section.title || section.label || '';
        const items = listFrom(section);
        const layout = section.layout || (section.grid ? 'grid' : 'rail');
        wrap.append(layout === 'grid'
          ? gridSection(world, title, items, section)
          : rail(world, title || ('Section ' + (index + 1)), items, {
              moreLabel: section.moreLabel || (section.seeAll ? 'See all' : ''),
              onMore: section.seeAll ? function () {
                send('see-all', { world: world, tab: key, pin: section.pin || section });
              } : null
            }));
      });
      return wrap;
    }

    const items = listFrom(payload);
    wrap.append(gridSection(world, payload.title || '', items, payload));
    return wrap;
  }

  function gridSection(world, title, items, options) {
    const opts = options || {};
    const section = node('section', 'content-section grid-section');
    if (title) section.append(sectionHeading(title,
      opts.seeAll ? (opts.moreLabel || 'See all') : '',
      opts.seeAll ? function () { send('see-all', { world: world, pin: opts.pin || opts }); } : null));

    const list = Array.isArray(items) ? items : [];
    if (!list.length) {
      section.append(emptyState('No items supplied', 'This frontend does not invent catalogue entries.'));
      return section;
    }

    const grid = node('div', 'poster-grid');
    list.forEach(function (item) { grid.append(posterCard(world, item)); });
    section.append(grid);
    return section;
  }

  function tabsFor(world, data) {
    if (Array.isArray(data.tabModel) && data.tabModel.length)
      return data.tabModel;
    return WORLD_TABS[world] || [];
  }

  function renderWorld(world) {
    const data = worldData(world);
    const root = node('div', 'world-surface ' + world.toLowerCase() + '-surface');
    root.append(hero(world, listFrom(data.featured)));

    const next = listFrom(data.nextUp);
    if (next.length) root.append(continueRail(world, 'Next Up', next, true));

    const resumes = listFrom(data.continue);
    if (resumes.length)
      root.append(continueRail(world, world === 'Theatre' ? 'Continue Watching' : 'Continue Reading', resumes, false));

    const tabs = tabsFor(world, data);
    const active = state.activeTabs[world] || (tabs[0] && tabs[0].key) || 'discover';
    const tabBar = node('nav', 'world-tabs');

    tabBar.setAttribute('aria-label', world + ' sections');

    tabs.forEach(function (tab) {
      const button = node('button', '', tab.label || tab.key);
      button.type = 'button';
      button.id = 'world-tab-' + world.toLowerCase() + '-' + tab.key;
      button.dataset.nav = 'true';
      button.dataset.active = String(tab.key === active);
      button.setAttribute('aria-current', tab.key === active ? 'page' : 'false');
      button.addEventListener('click', function () {
        state.lastFocus = button.id;
        state.activeTabs[world] = tab.key;
        send('world-tab', { world: world, tab: tab.key });
        state.heroIndex = 0;
        render();
      });
      tabBar.append(button);
    });
    root.append(tabBar);

    const payload = tabPayload(world, active);
    root.append(renderPayload(world, active, payload));
    return root;
  }

  function setWallpaper() {
    const value = safeUrl(state.snapshot.wallpaper || homeData().wallpaper);
    const wall = $('wallpaper');
    wall.style.backgroundImage = value ? 'url("' + value.replace(/"/g, '%22') + '")' : '';
    wall.dataset.hasImage = value ? 'true' : 'false';
  }

  function updateTopbar() {
    const onHome = state.surface === 'Home';
    $('home-button').hidden = onHome;
    $('brand-button').classList.toggle('compact', !onHome);
    document.querySelectorAll('#world-nav [data-world]').forEach(function (button) {
      const active = button.dataset.world === state.surface;
      button.dataset.active = String(active);
      button.setAttribute('aria-current', active ? 'page' : 'false');
    });

    const initial = String(state.snapshot.accountInitial || '?').trim().slice(0, 1).toUpperCase() || '?';
    $('account-button').firstElementChild.textContent = initial;
    $('backend-status').textContent = state.mounted ? 'Colosseum backend mounted' : 'Waiting for Colosseum backend';
    $('backend-status').dataset.ready = String(state.mounted);
    $('app').dataset.surface = state.surface;
  }

  function render() {
    clearHeroTimer();
    updateTopbar();
    setWallpaper();
    const surface = $('surface');
    surface.replaceChildren(state.surface === 'Home' ? renderHome() : renderWorld(state.surface));
    restoreFocus();
    if (state.surface === 'Home') startHomeHeroTimer();
    else startHeroTimer();
  }

  function navigate(surface) {
    const next = WORLD_ORDER.includes(surface) ? surface : 'Home';
    if (state.surface === next) return;
    rememberFocus();
    state.surface = next;

    state.heroIndex = 0;
    if (next === 'Home') send('home');
    else send('open-world', { world: next });
    render();
    if (next !== 'Home' && !Object.keys(worldData(next)).length)
      window.ColosseumWeb.requestSnapshot(next, state.activeTabs[next]);
  }

  function rememberFocus() {
    const active = document.activeElement;
    state.lastFocus = active && active.id ? active.id : null;
  }

  function restoreFocus() {
    window.requestAnimationFrame(function () {
      if (state.lastFocus) {
        const target = document.getElementById(state.lastFocus);
        if (target) { target.focus(); state.lastFocus = null; return; }
      }
      const active = document.activeElement;
      if (active && active !== document.body && !$('surface').contains(active))
        return;
      const target = document.querySelector('#surface [data-nav="true"]');
      if (target && state.mounted) target.focus({ preventScroll: true });
    });
  }

  function patchSnapshot(data) {
    state.snapshot = merge(state.snapshot, data);
    if (data.surface) {
      const name = String(data.surface);
      if (name === 'Home' || WORLD_ORDER.includes(name)) state.surface = name;
    }
    if (isObject(data.activeTabs))
      state.activeTabs = Object.assign({}, state.activeTabs, data.activeTabs);
    state.mounted = true;
    render();
    if (state.searchOpen) renderSearchResults();
  }

  function mountSnapshot(data) {
    state.snapshot = data || {};
    state.surface = data && (data.surface === 'Home' || WORLD_ORDER.includes(data.surface))
      ? data.surface : 'Home';
    state.activeTabs = Object.assign(
      { Tankoban: 'discover', Biblio: 'discover', Theatre: 'discover' },
      isObject(data.activeTabs) ? data.activeTabs : {}
    );
    WORLD_ORDER.forEach(function (world) {
      const active = worldData(world).activeTab;
      if (active) state.activeTabs[world] = String(active);
    });
    state.mounted = true;
    state.heroIndex = 0;
    render();
  }

  function openSearch() {
    state.searchOpen = true;
    state.lastFocus = document.activeElement && document.activeElement.id;
    $('search-overlay').hidden = false;
    $('search-input').value = '';
    $('search-results').replaceChildren(
      emptyState('Search Colosseum', 'Type a query. The existing backend owns the search.')
    );
    $('search-input').focus();
    send('search-open');
  }

  function closeSearch() {
    state.searchOpen = false;
    $('search-overlay').hidden = true;
    send('search-close');
    const target = state.lastFocus ? document.getElementById(state.lastFocus) : $('search-button');
    if (target) target.focus();
    state.lastFocus = null;
  }

  function renderSearchResults() {
    const root = $('search-results');
    root.replaceChildren();
    const search = isObject(state.snapshot.search) ? state.snapshot.search : {};
    const results = Array.isArray(search.results) ? search.results : [];
    const query = $('search-input').value.trim();

    if (search.loading) {
      root.append(emptyState('Searching…', 'Colosseum is resolving "' + query + '".'));
      return;
    }
    if (!query) {
      root.append(emptyState('Search Colosseum', 'Type a query. The existing backend owns the search.'));
      return;
    }
    if (!results.length) {
      root.append(emptyState('No results yet', 'No backend results are mounted for this query.'));
      return;
    }

    results.forEach(function (item) {
      const data = normalized(item);
      const button = node('button', 'search-result');
      button.type = 'button';
      button.append(artFrame(item, 'search-result-art'));
      const copy = node('span');
      copy.append(node('strong', '', data.title));
      if (data.subtitle) copy.append(node('small', '', data.subtitle));
      button.append(copy, node('span', 'search-arrow', '→'));
      button.addEventListener('click', function () {
        closeSearch();
        openItem(item.world || state.surface, item, 'details');
      });
      root.append(button);
    });
  }

  function searchChanged() {
    window.clearTimeout(state.searchTimer);
    state.searchTimer = window.setTimeout(function () {
      const query = $('search-input').value.trim();
      send('search-query', { query: query });
      renderSearchResults();
    }, 120);
  }

  function focusables() {
    return Array.from(document.querySelectorAll(
      'button:not([disabled]):not([hidden]), input:not([disabled]), [tabindex]:not([tabindex="-1"])'
    )).filter(function (el) {
      if (el.closest('[hidden]')) return false;
      const rect = el.getBoundingClientRect();
      return rect.width > 0 && rect.height > 0;
    });
  }

  function spatialMove(key) {
    const current = document.activeElement;
    if (!current || current === document.body) return false;
    const from = current.getBoundingClientRect();
    const fx = from.left + from.width / 2;
    const fy = from.top + from.height / 2;
    let best = null;
    let bestScore = Infinity;

    focusables().forEach(function (candidate) {
      if (candidate === current) return;
      const rect = candidate.getBoundingClientRect();
      const x = rect.left + rect.width / 2;
      const y = rect.top + rect.height / 2;

      const dx = x - fx;
      const dy = y - fy;
      if (key === 'ArrowLeft' && dx >= -2) return;
      if (key === 'ArrowRight' && dx <= 2) return;
      if (key === 'ArrowUp' && dy >= -2) return;
      if (key === 'ArrowDown' && dy <= 2) return;

      const horizontal = key === 'ArrowLeft' || key === 'ArrowRight';
      const primary = horizontal ? Math.abs(dx) : Math.abs(dy);
      const secondary = horizontal ? Math.abs(dy) : Math.abs(dx);
      const score = primary + secondary * 2.4;
      if (score < bestScore) { best = candidate; bestScore = score; }
    });

    if (best) {
      best.focus({ preventScroll: true });
      best.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'smooth' });
      return true;
    }

    if (key === 'ArrowDown')
      window.scrollBy({ top: Math.round(innerHeight * 0.68), behavior: 'smooth' });
    else if (key === 'ArrowUp')
      window.scrollBy({ top: -Math.round(innerHeight * 0.68), behavior: 'smooth' });
    return key === 'ArrowDown' || key === 'ArrowUp';
  }

  function bindStaticControls() {
    $('brand-button').addEventListener('click', function () { navigate('Home'); });
    $('home-button').addEventListener('click', function () { navigate('Home'); });

    document.querySelectorAll('#world-nav [data-world]').forEach(function (button) {
      button.addEventListener('click', function () { navigate(button.dataset.world); });
    });

    $('search-button').addEventListener('click', openSearch);
    $('search-close').addEventListener('click', closeSearch);
    $('search-input').addEventListener('input', searchChanged);
    $('trackers-button').addEventListener('click', function () { send('trackers'); });
    $('wallpaper-button').addEventListener('click', function () { send('wallpaper'); });
    $('account-button').addEventListener('click', function () { send('account'); });
    $('minimize-button').addEventListener('click', function () { send('window-minimize'); });
    $('fullscreen-button').addEventListener('click', function () { send('window-toggle-fullscreen'); });
    $('close-button').addEventListener('click', function () { send('window-close'); });

    document.addEventListener('keydown', function (event) {
      if (event.key === 'Escape') {
        if (state.searchOpen) { event.preventDefault(); closeSearch(); return; }
        if (state.surface !== 'Home') { event.preventDefault(); navigate('Home'); return; }
      }

      if (state.searchOpen) return;
      if (['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) {
        if (spatialMove(event.key)) event.preventDefault();
      }
    });
  }

  window.addEventListener('colosseum:webui-mount', function (event) {
    mountSnapshot(event.detail || {});
  });

  window.addEventListener('colosseum:webui-patch', function (event) {
    patchSnapshot(event.detail || {});
  });

  bindStaticControls();
  render();
  window.ColosseumWeb.requestSnapshot('Home', '');
}());
