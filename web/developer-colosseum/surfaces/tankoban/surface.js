// surfaces/tankoban/surface.js — Tankoban browsing world.
// QML parity: Featured -> Next Up -> Continue Reading -> tabs -> requested tab.
// All catalogue/filter/library decisions are native-issued. This file owns presentation only.
(function (CW) {
  'use strict';
  const { h } = CW;

  const TABS = CW.contract.TABS.Tankoban;
  const TOP_PREFIX = 'tankoban.chrome.';
  const NAV_PREFIX = 'tankoban.nav.';

  const isTop = s => String(s.id || '').startsWith(TOP_PREFIX);
  const isNav = s => String(s.id || '').startsWith(NAV_PREFIX);
  const isDiscoverChrome = s => /^tankoban\.discover\.(types|catalogues|filters|notice)$/.test(String(s.id || ''));
  const isCollection = s => /\.collection$/.test(String(s.id || ''));
  const isGenre = s => /\.genres$/.test(String(s.id || ''));
  const isRanked = s => {
    const id = String(s.id || '');
    return id === 'tankoban.manga.top'
      || id === 'tankoban.comics.top'
      || id.startsWith('tankoban.comics.catalogue.')
      || id.startsWith('tankoban.comics.shelf.');
  };

  function choiceTail(choice) {
    const label = String((choice && choice.label) || '');
    const at = label.indexOf(' · ');
    return at < 0 ? label : label.slice(at + 3);
  }

  function choiceGroup(choice) {
    const label = String((choice && choice.label) || '');
    const at = label.indexOf(' · ');
    return at < 0 ? '' : label.slice(0, at);
  }

  function disposeInside(node) {
    if (!node) return;
    const all = [node, ...node.querySelectorAll('*')];
    all.forEach(el => { if (typeof el.__dispose === 'function') el.__dispose(); });
  }

  function replacePreserving(host, ...nodes) {
    CW.focus.preserve(host, () => {
      disposeInside(host);
      host.replaceChildren(...nodes);
    });
  }

  function image(src, cls, title) {
    const fallback = h('span.tk-art-fallback', {}, title || '');
    const host = h('span.' + cls, {}, fallback);
    if (!src) return host;
    const img = h('img', { alt: '', decoding: 'async', loading: 'lazy', src });
    img.addEventListener('load', () => { img.classList.add('on'); fallback.remove(); });
    img.addEventListener('error', () => img.remove());
    host.appendChild(img);
    return host;
  }

  function header(section, onExplore) {
    return h('div.tk-widget-header', {},
      h('h2', {}, section.title || ''),
      onExplore
        ? h('button.tk-explore', {
            type: 'button', 'data-focus': true,
            'data-key': section.id + '#explore', onclick: onExplore
          }, h('span', {}, 'Explore'), h('span.tk-chevron', { 'aria-hidden': true }, '›'))
        : null);
  }

  function mount(el, route, env) {
    const top = h('div.world-pane.tk-top');
    const continuing = h('div.world-pane.tk-continue');
    const tabsHost = h('div.tk-tabs');
    const discoverHead = h('div.tk-discover-head', { hidden: true });
    const pane = h('div.world-pane.tk-pane');
    const root = h('div.tk-surface', {}, top, continuing, tabsHost, discoverHead, pane);
    el.appendChild(root);

    let tabSub = null;
    let bar = null;
    let queryTimer = 0;
    let openMenu = '';
    let worldSections = [];
    let currentDiscoverType = 'manga';
    const views = { discover: {}, manga: {}, comics: {}, library: {} };
    const discoverTypeViews = {};
    const scrollByTab = {};
    let restoreTab = '';

    function section(id) {
      return worldSections.find(s => s.id === id) || null;
    }

    function navSection(sourceId) {
      return section(NAV_PREFIX + sourceId);
    }

    function navChoice(sourceId) {
      const s = navSection(sourceId);
      return s && Array.isArray(s.choices) && s.choices.length ? s.choices[0] : null;
    }

    function viewFor(tab) {
      return views[tab] || (views[tab] = {});
    }

    function board() {
      return document.getElementById('board');
    }

    function saveScroll(tab) {
      const b = board();
      if (b && tab) scrollByTab[tab] = b.scrollTop;
    }

    function restoreScroll() {
      if (!restoreTab) return;
      const tab = restoreTab;
      restoreTab = '';
      requestAnimationFrame(() => {
        const b = board();
        if (b && scrollByTab[tab] != null) b.scrollTop = scrollByTab[tab];
      });
    }

    function subscribeTab() {
      if (tabSub) tabSub.close();
      const view = viewFor(route.tab);
      const params = Object.keys(view).length
        ? { world: 'Tankoban', tab: route.tab, view }
        : { world: 'Tankoban', tab: route.tab };
      tabSub = env.port.subscribe('world', params, onWorld);
    }

    function applyCurrentView(patch) {
      views[route.tab] = { ...viewFor(route.tab), ...patch };
      subscribeTab();
    }

    function applyDiscoverType(patch) {
      const old = viewFor('discover');
      const oldType = old.type || currentDiscoverType || 'manga';
      if (Object.keys(old).length) discoverTypeViews[oldType] = { ...old };
      const nextType = patch.type || oldType;
      const saved = discoverTypeViews[nextType];
      views.discover = saved ? { ...patch, ...saved, type: nextType }
                             : { ...old, ...patch };
      currentDiscoverType = nextType;
      subscribeTab();
    }

    function applyDiscoverPin(patch) {
      views.discover = { ...viewFor('discover'), ...patch };
      if (patch.type) currentDiscoverType = patch.type;
      if (route.tab === 'discover') subscribeTab();
      else env.router.go({ name: 'world', world: 'Tankoban', tab: 'discover' }, { replace: true });
    }

    function chooseCurrent(choice, owningSection) {
      return env.choose(choice, owningSection, null, patch => applyCurrentView(patch));
    }

    function chooseDiscoverPin(choice, owningSection) {
      return env.choose(choice, owningSection, null, patch => applyDiscoverPin(patch));
    }

    const commonCtx = {
      open: env.open,
      seeAll: env.seeAll,
      act: env.act,
      more: s => tabSub ? env.more(tabSub, s) : Promise.resolve({ ok: false })
    };

    const topCtx = {
      ...commonCtx,
      forget: env.forget,
      choose: (c, s) => chooseCurrent(c, s)
    };

    const paneCtx = {
      ...commonCtx,
      forget: item => env.act('world.tankoban.collection.remove', { item }),
      choose: (c, s) => {
        if (s && s.id === 'tankoban.comics.genres')
          return chooseDiscoverPin(c, s);
        return chooseCurrent(c, s);
      }
    };

    const nextCtx = { ...topCtx, forget: () => {} };

    function renderContinueSection(s, ctx, remove) {
      if (s.state === 'loading' || s.state === 'error')
        return CW.section.render(s, ctx);
      if (!s.items.length) return null;

      const row = h('div.rail', {}, s.items.map(it => {
        const card = CW.cards.continueTile(it, ctx);
        return h('span.tk-continue-card', {},
          card,
          remove ? h('button.tk-continue-remove', {
            type: 'button', tabindex: '-1',
            'aria-label': 'Remove ' + it.title + ' from Continue',
            onclick: e => { e.stopPropagation(); remove(it); }
          }, '✕') : null);
      }));
      return h('section.widget.tk-continue-section', { 'data-section': s.id },
        h('div.wh', {},
          h('h2', {}, s.title),
          s.seeAll ? h('button.more', {
            type: 'button', 'data-focus': true, 'data-key': s.id + '#all',
            onclick: () => env.seeAll(s)
          }, 'See all', h('span.ch', { 'aria-hidden': true }, '›')) : null),
        h('div.rail-wrap', {}, row));
    }

    function renderHero(s) {
      if (s.state !== 'ready' || !s.items.length) {
        const clone = { ...s, title: '' };
        return CW.section.render(clone, topCtx);
      }

      const scroller = h('div.tk-hero-scroller', { 'data-key': s.id + '#scroller' });
      const dots = h('div.tk-hero-dots', {});
      let dragging = null;

      function at() {
        return scroller.clientWidth ? Math.round(scroller.scrollLeft / scroller.clientWidth) : 0;
      }

      function syncDots() {
        const current = Math.max(0, Math.min(s.items.length - 1, at()));
        [...dots.children].forEach((dot, i) => dot.classList.toggle('on', i === current));
      }

      function show(index, smooth) {
        const left = Math.max(0, Math.min(s.items.length - 1, index)) * scroller.clientWidth;
        scroller.scrollTo({ left, behavior: smooth ? 'smooth' : 'auto' });
      }

      s.items.forEach((it, index) => {
        const slide = h('article.tk-hero-slide', {
          'data-key': it.key,
          style: {
            '--tk-hero-c1': it.c1 || 'var(--washTop)',
            '--tk-hero-c2': it.c2 || 'var(--washBottom)'
          }
        });
        const poster = it.artKind === 'poster';
        const src = poster ? it.cover : (it.backdrop || it.cover);
        if (src) {
          const art = h('img.' + (poster ? 'tk-hero-poster' : 'tk-hero-art'), {
            alt: '', decoding: 'async', src
          });
          art.addEventListener('load', () => art.classList.add('on'));
          art.addEventListener('error', () => art.remove());
          slide.appendChild(art);
        }
        slide.append(
          h('span.tk-hero-wash'),
          h('span.tk-hero-ghost', { 'aria-hidden': true }, it.ghost || ''),
          h('div.tk-hero-copy', {},
            h('span.tk-hero-kicker', {}, 'FEATURED IN TANKOBAN'),
            h('h2', {}, it.title),
            it.subtitle ? h('p', {}, it.subtitle) : null,
            h('div.tk-hero-actions', {},
              h('button.tk-hero-primary', {
                type: 'button', 'data-focus': true, 'data-key': it.key + '#read',
                onclick: () => env.open(it, 'details')
              }, 'Read'),
              h('button.tk-hero-secondary', {
                type: 'button', 'data-focus': true, 'data-key': it.key + '#details',
                onclick: () => env.open(it, 'details')
              }, 'Details')
            )
          )
        );
        scroller.appendChild(slide);

        dots.appendChild(h('button.tk-hero-dot' + (index === 0 ? '.on' : ''), {
          type: 'button', tabindex: '-1', 'aria-label': 'Show ' + it.title,
          onclick: () => show(index, true)
        }));
      });

      let frame = 0;
      scroller.addEventListener('scroll', () => {
        cancelAnimationFrame(frame);
        frame = requestAnimationFrame(syncDots);
      }, { passive: true });

      scroller.addEventListener('focusin', e => {
        const slide = e.target.closest('.tk-hero-slide');
        if (!slide) return;
        const index = [...scroller.children].indexOf(slide);
        if (index >= 0) show(index, true);
      });

      scroller.addEventListener('pointerdown', e => {
        if (e.target.closest('button')) return;
        dragging = { x: e.clientX, scroll: scroller.scrollLeft, id: e.pointerId };
        scroller.setPointerCapture(e.pointerId);
        scroller.classList.add('dragging');
      });
      scroller.addEventListener('pointermove', e => {
        if (!dragging || dragging.id !== e.pointerId) return;
        scroller.scrollLeft = dragging.scroll - (e.clientX - dragging.x);
      });
      const endDrag = e => {
        if (!dragging || dragging.id !== e.pointerId) return;
        const target = at();
        dragging = null;
        scroller.classList.remove('dragging');
        show(target, true);
      };
      scroller.addEventListener('pointerup', endDrag);
      scroller.addEventListener('pointercancel', endDrag);

      return h('section.widget.tk-hero-widget', { 'data-section': s.id },
        scroller, s.items.length > 1 ? dots : null);
    }

    function renderTop() {
      const hero = section('tankoban.chrome.featured');
      const next = section('tankoban.chrome.nextUp');
      const nodes = [];
      if (hero) nodes.push(renderHero(hero));
      if (next && (next.state === 'loading' || next.items.length)) {
        const nextNode = renderContinueSection(next, nextCtx, null);
        if (nextNode) nodes.push(nextNode);
      }
      replacePreserving(top, ...nodes);

      const cont = section('tankoban.chrome.continue');
      const contNodes = [];
      if (cont && (cont.state === 'loading' || cont.items.length)) {
        const contNode = renderContinueSection(cont, topCtx, env.forget);
        if (contNode) contNodes.push(contNode);
      }
      replacePreserving(continuing, ...contNodes);
    }

    function rankedCaption(s, it) {
      const detailShelf = s.id.startsWith('tankoban.comics.catalogue.')
        || s.id.startsWith('tankoban.comics.shelf.');
      return detailShelf && it.year ? it.title + ' (' + it.year + ')' : it.title;
    }

    function animateScroll(scroller, target) {
      const style = getComputedStyle(root);
      const duration = Number(style.getPropertyValue('--tk-rank-slide-ms')) || 0;
      if (!duration) { scroller.scrollLeft = target; return; }
      const start = scroller.scrollLeft;
      const delta = target - start;
      const begun = performance.now();
      function tick(now) {
        const p = Math.min(1, (now - begun) / duration);
        const eased = 1 - Math.pow(1 - p, 3);
        scroller.scrollLeft = start + delta * eased;
        if (p < 1) requestAnimationFrame(tick);
      }
      requestAnimationFrame(tick);
    }

    function renderRanked(s) {
      if (s.state === 'loading' || s.state === 'error')
        return CW.section.render(s, paneCtx);

      const nav = navSection(s.id);
      const pick = navChoice(s.id);
      const scroller = h('div.tk-rank-scroller');
      const row = h('div.tk-rank-row');
      const left = h('button.tk-rank-chevron.left', {
        type: 'button', 'data-focus': true, 'data-key': s.id + '#earlier',
        'aria-label': 'Show earlier items'
      }, '‹');
      const right = h('button.tk-rank-chevron.right', {
        type: 'button', 'data-focus': true, 'data-key': s.id + '#later',
        'aria-label': 'Show later items'
      }, '›');

      s.items.forEach((it, index) => {
        const caption = rankedCaption(s, it);
        const cover = h('span.tk-rank-cover', {},
          it.cover ? (() => {
            const art = h('img', { alt: '', decoding: 'async', loading: 'lazy', src: it.cover });
            art.addEventListener('load', () => art.classList.add('on'));
            art.addEventListener('error', () => art.remove());
            return art;
          })() : null,
          h('span.tk-rank-caption', {}, caption));
        const item = h('button.tk-rank-item', {
          type: 'button', 'data-focus': true, 'data-key': it.key,
          onclick: () => env.open(it, it.primary || 'details')
        },
        h('span.tk-rank-number', { 'aria-hidden': true }, String(index + 1)),
        cover);
        item.setAttribute('aria-label', rankedCaption(s, it));
        row.appendChild(item);
      });
      scroller.appendChild(row);

      function sync() {
        const overflow = scroller.scrollWidth > scroller.clientWidth;
        left.hidden = !overflow || scroller.scrollLeft <= 1;
        right.hidden = !overflow
          || scroller.scrollLeft >= scroller.scrollWidth - scroller.clientWidth - 1;
      }

      function page(dir) {
        const max = Math.max(0, scroller.scrollWidth - scroller.clientWidth);
        const target = Math.max(0, Math.min(max, scroller.scrollLeft + dir * scroller.clientWidth * 0.8));
        animateScroll(scroller, target);
      }

      left.addEventListener('click', () => page(-1));
      right.addEventListener('click', () => page(1));
      scroller.addEventListener('scroll', sync, { passive: true });
      scroller.addEventListener('focusin', e => {
        const item = e.target.closest('.tk-rank-item');
        if (item) item.scrollIntoView({ inline: 'nearest', block: 'nearest', behavior: 'smooth' });
      });
      requestAnimationFrame(sync);

      const explore = pick && nav
        ? () => chooseDiscoverPin(pick, nav)
        : s.seeAll ? () => env.seeAll(s) : null;

      return h('section.widget.tk-ranked', { 'data-section': s.id },
        header(s, explore),
        h('div.tk-rank-strip', {}, scroller, left, right));
    }

    function renderGenres(s) {
      if (s.state === 'loading' || s.state === 'error')
        return CW.section.render(s, paneCtx);

      const grid = h('div.tk-genre-grid', {}, (s.choices || []).map(c =>
        h('button.tk-genre-tile', {
          type: 'button', 'data-focus': true, 'data-key': c.key,
          onclick: () => paneCtx.choose(c, s)
        },
        c.art ? image(c.art, 'tk-genre-art', '') : null,
        h('span.tk-genre-shade'),
        h('span.tk-genre-name', {}, c.label),
        c.sublabel ? h('span.tk-genre-count', {}, c.sublabel) : null)
      ));
      return h('section.widget.tk-genres', { 'data-section': s.id },
        header(s, s.seeAll ? () => env.seeAll(s) : null), grid);
    }

    function menuRows(kind, choices, returnKey) {
      if (openMenu !== kind) return null;
      const nodes = [];
      if (kind === 'catalogue') {
        nodes.push(h('div.tk-menu-header', {}, 'TANKOBAN'));
        choices.forEach(c => nodes.push(h('button.tk-menu-row' + (c.selected ? '.on' : ''), {
          type: 'button', role: 'menuitem', 'data-focus': true, 'data-key': c.key,
          onclick: () => {
            openMenu = '';
            chooseCurrent(c, section('tankoban.discover.catalogues'));
          }
        }, h('span', {}, c.label), c.sublabel ? h('small', {}, c.sublabel) : null)));
      } else {
        let lastGroup = '';
        choices.forEach(c => {
          const group = choiceGroup(c);
          if (group && group !== lastGroup) {
            nodes.push(h('div.tk-menu-header', {}, group.toUpperCase()));
            lastGroup = group;
          }
          nodes.push(h('button.tk-menu-row' + (c.selected ? '.on' : ''), {
            type: 'button', role: 'menuitem', 'data-focus': true, 'data-key': c.key,
            onclick: () => {
              openMenu = '';
              chooseCurrent(c, section('tankoban.discover.filters'));
            }
          }, h('span', {}, choiceTail(c))));
        });
      }
      const box = h('div.tk-menu', {
        role: 'menu', 'data-focus-scope': true, 'data-key': 'tankoban:menu:' + kind
      }, nodes);
      box.__close = () => closeDiscoverMenu(returnKey);
      return box;
    }

    function closeDiscoverMenu(returnKey) {
      openMenu = '';
      renderDiscoverControls();
      if (!returnKey) return;
      const button = discoverHead.querySelector('[data-key="' + CSS.escape(returnKey) + '"]');
      if (button) button.focus({ preventScroll: true });
    }

    function openDiscoverMenu(kind, returnKey) {
      openMenu = openMenu === kind ? '' : kind;
      renderDiscoverControls();
      if (!openMenu) return;
      const first = discoverHead.querySelector('.tk-menu [data-focus]');
      if (first) first.focus({ preventScroll: true });
    }

    function renderDiscoverControls() {
      if (route.tab !== 'discover') {
        discoverHead.hidden = true;
        replacePreserving(discoverHead);
        return;
      }
      discoverHead.hidden = false;
      const types = section('tankoban.discover.types');
      const catalogues = section('tankoban.discover.catalogues');
      const filters = section('tankoban.discover.filters');
      const notice = section('tankoban.discover.notice');
      if (!types || !catalogues || !filters) {
        replacePreserving(discoverHead,
          CW.section.note('Loading Tankoban', 'Preparing the catalogue controls.'));
        return;
      }

      const selectedType = types.choices.find(c => c.selected);
      if (selectedType) currentDiscoverType = selectedType.label.toLowerCase();
      const selectedCatalogue = catalogues.choices.find(c => c.selected) || catalogues.choices[0];
      const selectedFilter = filters.choices.find(c => c.selected) || filters.choices[0];
      const allFilter = filters.choices.find(c => c.label === 'All') || filters.choices[0];
      const hasFilter = selectedFilter && selectedFilter.label !== 'All';

      const typeLens = h('div.tk-type-lens', {}, types.choices.map(c =>
        h('button.tk-type' + (c.selected ? '.on' : ''), {
          type: 'button', 'data-focus': true, 'data-key': c.key,
          'aria-pressed': c.selected ? 'true' : 'false',
          onclick: () => env.choose(c, types, null, patch => applyDiscoverType(patch))
        }, c.label)
      ));

      const catalogueKey = 'tankoban:discover:catalogue-menu';
      const catalogueButton = h('button.tk-catalogue', {
        type: 'button', 'data-focus': true, 'data-key': catalogueKey,
        'aria-haspopup': 'menu', 'aria-expanded': openMenu === 'catalogue' ? 'true' : 'false',
        onclick: () => openDiscoverMenu('catalogue', catalogueKey)
      },
      h('span.tk-kicker', {}, 'NOW BROWSING'),
      h('span.tk-catalogue-name', {},
        h('strong', {}, selectedCatalogue ? selectedCatalogue.label : '—'),
        h('span.tk-catalogue-caret', { 'aria-hidden': true }, '▾')),
      h('small', {}, (selectedCatalogue && selectedCatalogue.sublabel)
        ? selectedCatalogue.sublabel + (hasFilter ? '   ·   ' + choiceTail(selectedFilter) : '')
        : (hasFilter ? choiceTail(selectedFilter) : '')));

      const filterKey = 'tankoban:discover:filter-menu';
      const filterMain = h('button.tk-filter-main', {
        type: 'button', 'data-focus': true, 'data-key': filterKey,
        'aria-haspopup': 'menu', 'aria-expanded': openMenu === 'filter' ? 'true' : 'false',
        onclick: () => openDiscoverMenu('filter', filterKey)
      },
      h('span', {}, hasFilter ? choiceTail(selectedFilter) : 'Filter'),
      !hasFilter ? h('span.tk-filter-caret', { 'aria-hidden': true }, '▾') : null);
      const filterWrap = h('div.tk-filter-wrap' + (hasFilter ? '.has-value' : ''), {},
        filterMain,
        hasFilter ? h('button.tk-filter-clear', {
          type: 'button', 'data-focus': true, 'data-key': filterKey + '#clear',
          'aria-label': 'Clear filter',
          onclick: e => { e.stopPropagation(); chooseCurrent(allFilter, filters); }
        }, '✕') : null,
        menuRows('filter', filters.choices, filterKey));

      const nodes = [
        h('div.tk-mast', {},
          typeLens,
          h('div.tk-shelf', {}, catalogueButton,
            menuRows('catalogue', catalogues.choices, catalogueKey))),
        h('div.tk-filterbar', {},
          h('span.tk-filter-label', {}, 'FILTER'),
          filterWrap)
      ];
      if (notice && notice.data && notice.data.text) {
        nodes.push(h('div.tk-discover-notice', {}, notice.data.text));
      }
      replacePreserving(discoverHead, ...nodes);
    }

    function renderLibrary() {
      const filters = section('tankoban.library.filters');
      const sorts = section('tankoban.library.sort');
      const saved = section('tankoban.library.saved');
      if (!filters || !sorts || !saved) {
        replacePreserving(pane, CW.section.note('Loading Tankoban', 'Preparing your library.'));
        return;
      }

      const query = viewFor('library').query || '';
      const input = h('input.tk-library-search', {
        type: 'search', autocomplete: 'off', value: query,
        placeholder: 'Search your library', 'aria-label': 'Search your library',
        'data-focus': true, 'data-key': 'tankoban:library:query'
      });
      input.addEventListener('input', () => {
        clearTimeout(queryTimer);
        queryTimer = setTimeout(() => {
          views.library = { ...viewFor('library'), query: input.value };
          subscribeTab();
        }, 0);
      });
      input.addEventListener('keydown', e => {
        if (e.key === 'Escape') {
          input.value = '';
          views.library = { ...viewFor('library'), query: '' };
          subscribeTab();
        }
      }, true);

      const pills = (s, cls) => h('div.' + cls, {}, s.choices.map(c =>
        h('button.tk-library-pill' + (c.selected ? '.on' : ''), {
          type: 'button', 'data-focus': true, 'data-key': c.key,
          'aria-pressed': c.selected ? 'true' : 'false',
          onclick: () => chooseCurrent(c, s)
        }, c.label)
      ));

      const toolbar = h('div.tk-library-toolbar', {},
        input,
        pills(filters, 'tk-library-filters'),
        pills(sorts, 'tk-library-sorts'));

      let body;
      if (saved.state === 'loading' || saved.state === 'error') {
        body = CW.section.render({ ...saved, title: '' }, paneCtx);
      } else if (!saved.items.length) {
        body = CW.section.note(saved.emptyTitle || 'No matches',
          saved.emptyText || null);
      } else {
        const grid = h('div.tk-library-grid', { 'data-tk-disposable': true },
          saved.items.map(it => h('div.tk-library-cell', {}, CW.cards.poster(it, paneCtx))));
        const ro = new ResizeObserver(() => {
          const basis = Number(getComputedStyle(root).getPropertyValue('--tk-library-column-basis'));
          if (basis > 0)
            grid.style.setProperty('--tk-library-cols', String(Math.max(2, Math.floor(grid.clientWidth / basis))));
        });
        ro.observe(grid);
        grid.__dispose = () => ro.disconnect();
        body = grid;
      }
      replacePreserving(pane, toolbar, body);
      restoreScroll();
    }

    function renderPane() {
      pane.dataset.tab = route.tab;

      if (route.tab === 'library') {
        renderLibrary();
        return;
      }

      const bodySections = worldSections.filter(s =>
        !isTop(s) && !isNav(s) && !isDiscoverChrome(s));

      if (route.tab === 'discover') {
        const wall = bodySections.find(s => s.id === 'tankoban.discover.wall')
          || bodySections.find(s => s.state === 'error');
        if (!wall) {
          replacePreserving(pane, CW.section.note('Loading Tankoban', 'Preparing the catalogue.'));
          return;
        }
        const node = CW.section.render({ ...wall, title: '' }, paneCtx);
        node.classList.add('tk-discover-wall');
        const grid = node.querySelector('.grid');
        if (grid) {
          grid.setAttribute('data-tk-disposable', '');
          const ro = new ResizeObserver(() => {
            const basis = Number(getComputedStyle(root).getPropertyValue('--tk-discover-column-basis'));
            if (basis > 0)
              grid.style.setProperty('--tk-discover-cols', String(Math.max(3, Math.floor(grid.clientWidth / basis))));
          });
          ro.observe(grid);
          grid.__dispose = () => ro.disconnect();
        }
        replacePreserving(pane, node);
        restoreScroll();
        return;
      }

      const nodes = [];
      bodySections.forEach(s => {
        if (isCollection(s)) {
          if (s.state === 'loading' || s.items.length) {
            const collection = renderContinueSection(
              { ...s, seeAll: null }, paneCtx, paneCtx.forget);
            if (collection) nodes.push(collection);
          }
          return;
        }
        if (isRanked(s)) {
          nodes.push(renderRanked(s));
          return;
        }
        if (isGenre(s)) {
          nodes.push(renderGenres(s));
          return;
        }
        nodes.push(CW.section.render(s, paneCtx));
      });
      replacePreserving(pane, ...nodes);
      restoreScroll();
    }

    function onWorld(ev) {
      worldSections = ev.sections || [];

      if (!ev.changed || String(ev.changed).startsWith(TOP_PREFIX))
        renderTop();

      if (!ev.changed
          || isDiscoverChrome({ id: ev.changed })
          || String(ev.changed).startsWith(NAV_PREFIX)) {
        renderDiscoverControls();
      }

      if (!ev.changed
          || !String(ev.changed).startsWith(TOP_PREFIX)) {
        renderPane();
      }
    }

    function makeBar(tab) {
      return CW.tabBar(TABS, tab,
        next => env.router.go(
          { name: 'world', world: 'Tankoban', tab: next },
          { replace: true }));
    }

    function showTab(next) {
      if (route && route.tab) saveScroll(route.tab);
      route = { ...next, tab: next.tab || 'discover' };
      restoreTab = route.tab;
      openMenu = '';
      discoverHead.hidden = route.tab !== 'discover';
      replacePreserving(pane);
      subscribeTab();
    }

    root.addEventListener('pointerdown', e => {
      if (!openMenu) return;
      if (e.target.closest('.tk-menu')
          || e.target.closest('.tk-catalogue')
          || e.target.closest('.tk-filter-wrap')) return;
      openMenu = '';
      renderDiscoverControls();
    }, true);

    bar = makeBar(route.tab || 'discover');
    tabsHost.replaceChildren(bar);
    showTab({ ...route, tab: route.tab || 'discover' });

    return {
      update(next) {
        const tab = next.tab || 'discover';
        if (tab === route.tab) return;
        const hadFocus = bar.contains(document.activeElement);
        const fresh = makeBar(tab);
        bar.dispose();
        bar.replaceWith(fresh);
        bar = fresh;
        if (hadFocus) {
          const active = bar.querySelector('.tab.on');
          if (active) active.focus({ preventScroll: true });
        }
        showTab({ ...next, tab });
      },
      unmount() {
        clearTimeout(queryTimer);
        disposeInside(root);
        if (tabSub) tabSub.close();
        if (bar) bar.dispose();
      }
    };
  }

  CW.router.register('tankoban', { mount });
})(window.CW = window.CW || {});
