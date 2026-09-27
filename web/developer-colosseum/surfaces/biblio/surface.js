// surfaces/biblio/surface.js — 1:1 Biblio browsing world.
(function (CW) {
  'use strict';

  const TABS = [
    { key: 'discover', label: 'Discover' },
    { key: 'explore', label: 'Explore' },
    { key: 'library', label: 'Library' }
  ];

  const byId = (sections, id) => sections.find(s => s.id === id);
  const starts = (sections, prefix) => sections.filter(s => String(s.id || '').startsWith(prefix));
  const isReady = s => s && s.state === 'ready';

  function preserveFocus(container, mutate) {
    const active = document.activeElement;
    const key = active && container.contains(active) ? active.dataset.key : '';
    mutate();
    if (!key) return;
    const next = container.querySelector('[data-key="' + CSS.escape(key) + '"]');
    if (next) next.focus({ preventScroll: true });
  }

  function coverFace(url, label) {
    const fallback = CW.h('span.biblio-cover-fallback', {}, (label || 'B').slice(0, 1));
    if (!url) return fallback;
    const image = CW.h('img', { src: url, alt: '', decoding: 'async' });
    image.addEventListener('error', () => image.replaceWith(fallback));
    return image;
  }

  function mount(el, route, env) {
    const chrome = CW.h('div.biblio-chrome');
    const tabsHost = CW.h('div.biblio-tabs');
    const body = CW.h('div.biblio-body');
    const root = CW.h('div.biblio-surface', {}, chrome, tabsHost, body);
    el.appendChild(root);

    const views = { discover: {}, explore: {}, library: { sort: 'added' } };
    const scrollByTab = { discover: 0, explore: 0, library: 0 };
    let activeTab = '';
    let tabs = null;
    let sub = null;
    let latest = [];
    let queryTimer = 0;
    let catalogOpen = false;
    let filterOpen = false;
    let heroIndex = 0;
    let heroTimer = 0;

    function currentView() {
      return views[activeTab] || (views[activeTab] = {});
    }

    function params() {
      const view = { ...currentView() };
      const base = { world: 'Biblio', tab: activeTab };
      return Object.keys(view).length ? { ...base, view } : base;
    }

    function rememberScroll() {
      const scroller = document.scrollingElement;
      if (scroller && activeTab) scrollByTab[activeTab] = scroller.scrollTop;
    }

    function restoreScroll() {
      const y = scrollByTab[activeTab] || 0;
      requestAnimationFrame(() => {
        const scroller = document.scrollingElement;
        if (scroller) scroller.scrollTop = y;
      });
    }

    function resubscribe() {
      if (sub) sub.close();
      sub = env.port.subscribe('world', params(), sync);
    }

    function applyView(patch) {
      const delta = patch || {};
      const queryOnly = activeTab === 'library'
        && Object.keys(delta).length === 1
        && Object.prototype.hasOwnProperty.call(delta, 'query');
      const next = { ...currentView(), ...delta };
      Object.keys(next).forEach(key => {
        if (next[key] === '' || next[key] == null) delete next[key];
      });
      views[activeTab] = next;
      catalogOpen = false;
      filterOpen = false;
      if (!queryOnly) renderBody();
      resubscribe();
    }

    function chooseView(choice, section) {
      return env.choose(choice, section, null, applyView);
    }

    function openItem(item, intent) {
      return env.open(item, intent || item.primary || 'details');
    }

    function note(section, fallbackTitle) {
      if (!section || section.state === 'loading') {
        return CW.section.note('Loading…', null);
      }
      if (section.state === 'error') {
        return CW.section.note('Couldn’t load this', section.error || 'Colosseum could not load this section.', 'err');
      }
      return CW.section.note(section.emptyTitle || fallbackTitle || 'Nothing here yet',
                             section.emptyText || section.error || null);
    }

    function renderHero(section) {
      if (!isReady(section) || !section.items.length) return note(section, 'Nothing here yet');
      const items = section.items;
      heroIndex = Math.min(heroIndex, Math.max(0, items.length - 1));
      clearInterval(heroTimer);

      const slides = items.map((item, index) => {
        const image = item.cover
          ? CW.h('img.biblio-hero-art', { src: item.cover, alt: '', decoding: 'async' })
          : null;
        if (image) image.addEventListener('error', () => image.remove());

        return CW.h('div.biblio-hero-slide' + (index === heroIndex ? '.on' : ''), {
          'data-key': item.key
        },
          image,
          CW.h('span.biblio-hero-wash'),
          CW.h('div.biblio-hero-copy', {},
            CW.h('span.kicker', {}, 'Featured in Biblio'),
            CW.h('h2', {}, item.title),
            item.subtitle ? CW.h('p', {}, item.subtitle) : null,
            CW.h('div.btns', {},
              CW.h('button.b-gold', {
                type: 'button', 'data-focus': true, 'data-key': item.key + '#read',
                onclick: () => openItem(item, 'details')
              }, 'Read'),
              CW.h('button.biblio-hero-secondary', {
                type: 'button', 'data-focus': true, 'data-key': item.key + '#details',
                onclick: () => openItem(item, 'details')
              }, 'Details'))));
      });

      function show(index) {
        heroIndex = (index + slides.length) % slides.length;
        slides.forEach((slide, i) => {
          slide.classList.toggle('on', i === heroIndex);
          slide.inert = i !== heroIndex;
        });
        [...dots.children].forEach((dot, i) => dot.classList.toggle('on', i === heroIndex));
      }

      const dots = CW.h('div.dots', {}, items.map((item, i) =>
        CW.h('button.dot' + (i === heroIndex ? '.on' : ''), {
          type: 'button', tabindex: '-1', 'aria-label': 'Show ' + item.title,
          onclick: () => show(i)
        })));
      const host = CW.h('section.biblio-hero', {
        'data-section': section.id, 'data-state': section.state
      }, ...slides,
        slides.length > 1 ? dots : null);
      show(heroIndex);

      host.addEventListener('cw-arrow', event => {
        const step = event.detail === 'right' ? 1 : event.detail === 'left' ? -1 : 0;
        if (!step || heroIndex + step < 0 || heroIndex + step >= slides.length) return;
        event.preventDefault();
        show(heroIndex + step);
        const focus = slides[heroIndex].querySelector('[data-focus]');
        if (focus) focus.focus({ preventScroll: true });
      });

      if (slides.length > 1) {
        heroTimer = setInterval(() => {
          if (!host.isConnected) return clearInterval(heroTimer);
          if (!host.contains(document.activeElement)) show(heroIndex + 1);
        }, 8000);
      }
      return host;
    }

    const sectionCtx = {
      open: openItem,
      forget: env.forget,
      act: env.act,
      removeChoice: env.removeChoice,
      choose: chooseView,
      seeAll(section) {
        const id = String(section && section.id || '');
        if (id === 'biblio.explore.most-read' || id === 'biblio.explore.classics') {
          const catalogue = id.substring('biblio.explore.'.length);
          return env.act('world.biblio.requestEnrichment', { catalogue })
            .then(() => env.seeAll(section));
        }
        return env.seeAll(section);
      },
      more(section) {
        return sub ? env.more(sub, section) : Promise.resolve({ ok: false });
      }
    };

    function renderChrome() {
      preserveFocus(chrome, () => {
        const featured = byId(latest, 'biblio.chrome.featured');
        const continuing = byId(latest, 'biblio.chrome.continue');
        const nodes = [];
        if (featured) nodes.push(renderHero(featured));
        if (continuing) nodes.push(CW.section.render(continuing, sectionCtx));
        chrome.replaceChildren(...nodes);
      });
    }

    function selectedChoice(section) {
      return section && Array.isArray(section.choices)
        ? section.choices.find(choice => choice.selected) || section.choices[0]
        : null;
    }

    function catalogGroups(section) {
      const all = (section && section.choices) || [];
      return {
        builtins: all.filter(choice => !String(choice.key || '').includes(':ext:')),
        extensions: all.filter(choice => String(choice.key || '').includes(':ext:'))
      };
    }

    function renderCatalogMenu(section) {
      if (!catalogOpen || !section) return null;
      const groups = catalogGroups(section);
      const group = (label, rows) => rows.length ? [
        CW.h('div.biblio-menu-head', {}, label),
        ...rows.map(choice => CW.h('button.biblio-menu-row' + (choice.selected ? '.on' : ''), {
          type: 'button', 'data-focus': true, 'data-key': choice.key,
          onclick: () => chooseView(choice, section)
        }, CW.h('span', {}, choice.label),
           choice.sublabel ? CW.h('small', {}, choice.sublabel) : null))
      ] : [];
      return CW.h('div.biblio-catalog-menu', { role: 'menu', 'data-focus-scope': true },
        ...group('Biblio', groups.builtins),
        ...group('From Your Extensions', groups.extensions));
    }

    function renderDiscoverFilter(sections) {
      const native = byId(sections, 'biblio.discover.filters');
      const extras = starts(sections, 'biblio.discover.extra.');
      const sectionsWithChoices = [native, ...extras].filter(s => s && Array.isArray(s.choices) && s.choices.length);
      if (!sectionsWithChoices.length) return null;

      const active = sectionsWithChoices.flatMap(s => s.choices).find(c => c.selected && c.label !== 'All');
      const label = active ? active.label : 'All';
      const menus = filterOpen
        ? CW.h('div.biblio-filter-menu', { role: 'menu', 'data-focus-scope': true },
            ...sectionsWithChoices.flatMap(section => {
              const rows = [];
              if (sectionsWithChoices.length > 1 || section.title)
                rows.push(CW.h('div.biblio-menu-head', {}, section.title || 'Filter'));
              section.choices.forEach(choice => rows.push(
                CW.h('button.biblio-menu-row' + (choice.selected ? '.on' : ''), {
                  type: 'button', 'data-focus': true, 'data-key': choice.key,
                  onclick: () => chooseView(choice, section)
                }, CW.h('span', {}, choice.label),
                   choice.sublabel ? CW.h('small', {}, choice.sublabel) : null)));
              return rows;
            }))
        : null;

      return CW.h('div.biblio-filter-row', {},
        CW.h('span.biblio-filter-kicker', {}, 'FILTER'),
        CW.h('div.biblio-filter-picker', {},
          CW.h('button.biblio-picker-button', {
            type: 'button', 'data-focus': true, 'data-key': 'biblio:discover:filter',
            'aria-expanded': filterOpen ? 'true' : 'false',
            onclick: () => { filterOpen = !filterOpen; catalogOpen = false; renderBody(); }
          }, label, CW.h('span', { 'aria-hidden': true }, '▾')),
          menus));
    }

    function renderDiscoverResults(section) {
      if (!isReady(section) || !section.items.length) return note(section, 'This catalogue answered with nothing.');
      const grid = CW.h('div.biblio-discover-grid', {}, section.items.map(item => CW.cards.poster(item, sectionCtx)));
      return CW.h('section.biblio-discover-results', {
        'data-section': section.id, 'data-state': section.state
      },
        section.error ? CW.h('div.biblio-notice', {}, section.error) : null,
        grid,
        section.hasMore ? CW.h('button.loadmore', {
          type: 'button', 'data-focus': true, 'data-key': section.id + '#more',
          onclick: () => sectionCtx.more(section)
        }, 'Load more') : null);
    }

    function renderDiscoverMore() {
      const more = byId(latest, 'biblio.discover.more');
      const choice = more && more.choices && more.choices[0];
      if (!choice) return null;
      return CW.h('section.biblio-discover-more', {
        'data-section': more.id, 'data-state': more.state
      }, CW.h('button.loadmore', {
        type: 'button', 'data-focus': true, 'data-key': choice.key,
        onclick: () => chooseView(choice, more)
      }, choice.label));
    }

    function renderDiscover() {
      const catalogues = byId(latest, 'biblio.discover.catalogues');
      const results = byId(latest, 'biblio.discover.results');
      const selected = selectedChoice(catalogues);
      const masthead = CW.h('div.biblio-discover-masthead', {},
        CW.h('div.biblio-type-lens', {},
          CW.h('button.biblio-type-tab.on', {
            type: 'button', 'data-focus': true, 'data-key': 'biblio:type:book',
            'aria-pressed': 'true'
          }, 'Books')),
        CW.h('div.biblio-shelf-picker', {},
          CW.h('span.biblio-now-browsing', {}, 'NOW BROWSING'),
          CW.h('button.biblio-catalog-button', {
            type: 'button', 'data-focus': true, 'data-key': 'biblio:catalogue',
            'aria-label': 'Choose catalogue', 'aria-expanded': catalogOpen ? 'true' : 'false',
            onclick: () => { catalogOpen = !catalogOpen; filterOpen = false; renderBody(); }
          }, CW.h('span', {}, selected ? selected.label : '—'),
             CW.h('span.biblio-catalog-arrow', { 'aria-hidden': true }, '▾')),
          selected && selected.sublabel ? CW.h('span.biblio-byline', {}, selected.sublabel) : null,
          renderCatalogMenu(catalogues)));

      return CW.h('div.biblio-discover-tab', {},
        masthead,
        renderDiscoverFilter(latest),
        renderDiscoverResults(results),
        renderDiscoverMore());
    }

    function rowAction(action, section, extra) {
      return env.act(action, {
        feed: 'world', params: params(), sectionId: section.id, ...(extra || {})
      }).then(answer => {
        if (answer && answer.ok) resubscribe();
        return answer;
      });
    }

    function moveMany(section, steps) {
      const dir = steps < 0 ? -1 : 1;
      let chain = Promise.resolve({ ok: true });
      for (let i = 0; i < Math.abs(steps); i++)
        chain = chain.then(answer => answer && answer.ok === false ? answer : rowAction('rows.move', section, { dir }));
      return chain;
    }

    function renderExploreCard(item, ranked) {
      const card = CW.cards.poster({ ...item, badge: null }, sectionCtx);
      card.classList.add('biblio-explore-card');
      const rev = card.querySelector('.rev');
      if (rev) {
        rev.replaceChildren();
        if (!ranked && item.rating != null) rev.appendChild(CW.h('b', {}, '★ ' + item.rating));
        if (item.source) rev.appendChild(CW.h('span', {}, item.source));
      }
      if (ranked && item.badge)
        card.appendChild(CW.h('span.biblio-rank-number', { 'aria-hidden': true }, item.badge));
      return card;
    }

    function renderExploreRail(section, index, total) {
      if (!section) return null;
      const customize = currentView().customize === true;
      const hidden = !!(section.pref && section.pref.hidden);
      const ranked = section.id === 'biblio.explore.top-10';
      const cards = isReady(section) ? section.items.map(item => renderExploreCard(item, ranked)) : [];
      const bodyNode = section.state === 'loading'
        ? CW.h('div.biblio-explore-rail', {}, Array.from({ length: 6 }, () => CW.h('span.biblio-explore-skeleton')))
        : section.state === 'error' || !cards.length
          ? note(section, 'Nothing here yet')
          : CW.h('div.biblio-explore-rail', {}, ...cards);

      let dragStart = 0;
      let dragging = false;
      const drag = CW.h('button.biblio-drag-handle', {
        type: 'button', 'data-focus': true, 'data-key': section.id + '#drag',
        'aria-label': 'Reorder ' + section.title,
        onpointerdown: event => {
          dragging = true;
          dragStart = event.clientY;
          drag.setPointerCapture && drag.setPointerCapture(event.pointerId);
        },
        onpointerup: event => {
          if (!dragging) return;
          dragging = false;
          const delta = event.clientY - dragStart;
          const steps = Math.max(-index, Math.min(total - index - 1, Math.round(delta / 56)));
          if (steps) moveMany(section, steps);
        }
      }, '⠿');

      const edit = customize && section.pref
        ? CW.h('div.biblio-edit-row', {},
            drag,
            CW.h('button.biblio-edit-button', {
              type: 'button', 'data-focus': index > 0 ? true : null,
              disabled: index <= 0, 'data-key': section.id + '#up',
              'aria-label': 'Move ' + section.title + ' up',
              onclick: () => rowAction('rows.move', section, { dir: -1 })
            }, '▲'),
            CW.h('button.biblio-edit-button', {
              type: 'button', 'data-focus': index < total - 1 ? true : null,
              disabled: index >= total - 1, 'data-key': section.id + '#down',
              'aria-label': 'Move ' + section.title + ' down',
              onclick: () => rowAction('rows.move', section, { dir: 1 })
            }, '▼'),
            CW.h('button.biblio-edit-button.visibility', {
              type: 'button', 'data-focus': true, 'data-key': section.id + '#visibility',
              'aria-label': (hidden ? 'Show ' : 'Hide ') + section.title,
              onclick: () => rowAction('rows.hide', section, { hidden: !hidden })
            }, hidden ? 'Show' : 'Hide'))
        : null;

      return CW.h('section.biblio-explore-row' + (hidden ? '.hidden-row' : ''), {
        'data-section': section.id, 'data-state': section.state
      },
        edit,
        CW.h('div.biblio-explore-row-head', {},
          CW.h('h2', {}, section.title),
          section.seeAll ? CW.h('button.biblio-see-all', {
            type: 'button', 'data-focus': true, 'data-key': section.id + '#all',
            onclick: () => sectionCtx.seeAll(section)
          }, 'See All') : null),
        bodyNode);
    }

    function renderMosaics(section) {
      if (!section || !Array.isArray(section.choices) || !section.choices.length) return null;
      return CW.h('section.biblio-mosaics', {
        'data-section': section.id, 'data-state': section.state
      }, section.choices.map(choice => {
        const arts = Array.isArray(choice.artList) ? choice.artList : (choice.art ? [choice.art] : []);
        return CW.h('button.biblio-mosaic', {
          type: 'button', 'data-focus': true, 'data-key': choice.key,
          onclick: () => env.choose(choice, section)
        },
          CW.h('span.biblio-mosaic-covers', {}, ...arts.slice(0, 7).map(art =>
            CW.h('span.biblio-mosaic-cover', {}, coverFace(art, choice.label)))),
          CW.h('span.biblio-mosaic-title', {}, choice.label));
      }));
    }

    function renderExplore() {
      const control = byId(latest, 'biblio.explore.controls');
      const toggle = control && control.choices && control.choices[0];
      const customize = currentView().customize === true;
      const rows = latest.filter(section =>
        String(section.id || '').startsWith('biblio.explore.')
        && section.id !== 'biblio.explore.controls'
        && section.id !== 'biblio.explore.mosaics'
        && section.id !== 'biblio.explore.unavailable');
      const mosaics = byId(latest, 'biblio.explore.mosaics');

      return CW.h('div.biblio-explore-tab', {},
        CW.h('div.biblio-explore-head', {},
          CW.h('h1', {}, 'Explore'),
          toggle ? CW.h('button.biblio-customize', {
            type: 'button', 'data-focus': true, 'data-key': toggle.key,
            'aria-label': customize ? 'Finish shelf customization' : 'Customize shelves',
            onclick: () => chooseView(toggle, control)
          }, customize ? 'Done' : 'Customize shelves') : null),
        ...rows.map((section, index) => renderExploreRail(section, index, rows.length)),
        renderMosaics(mosaics));
    }

    function renderPillSection(section) {
      if (!section || !Array.isArray(section.choices)) return null;
      return CW.h('div.biblio-library-pill-row', {}, ...section.choices.map(choice =>
        CW.h('button.biblio-library-pill' + (choice.selected ? '.on' : ''), {
          type: 'button', 'data-focus': true, 'data-key': choice.key,
          'aria-pressed': choice.selected ? 'true' : 'false',
          onclick: () => chooseView(choice, section)
        }, choice.label)));
    }

    function renderLibrary() {
      const filters = byId(latest, 'biblio.library.filters');
      const sort = byId(latest, 'biblio.library.sort');
      const saved = byId(latest, 'biblio.library.saved');

      const input = CW.h('input', {
        type: 'search', autocomplete: 'off',
        placeholder: 'Search by title or author',
        'aria-label': 'Search by title or author',
        'data-focus': true, 'data-key': 'biblio:library:query',
        value: currentView().query || ''
      });
      input.addEventListener('input', () => {
        clearTimeout(queryTimer);
        queryTimer = setTimeout(() => applyView({ query: input.value }), 180);
      });
      input.addEventListener('keydown', event => {
        if (event.key === 'Escape') {
          input.value = '';
          applyView({ query: '' });
        }
      });

      const gridBody = isReady(saved) && saved.items.length
        ? CW.h('div.biblio-library-grid', {}, saved.items.map(item => CW.cards.poster(item, sectionCtx)))
        : note(saved, saved && saved.emptyTitle ? saved.emptyTitle : 'Your library is empty');
      const grid = CW.h('section.biblio-library-results', {
        'data-section': saved ? saved.id : 'biblio.library.saved',
        'data-state': saved ? saved.state : 'loading'
      }, gridBody);

      return CW.h('div.biblio-library-tab', {},
        CW.h('div.biblio-library-header', {},
          CW.h('label.biblio-library-search', {}, input)),
        CW.h('div.biblio-library-filters', {},
          renderPillSection(sort),
          CW.h('span.biblio-filter-divider', { 'aria-hidden': true }),
          renderPillSection(filters)),
        grid);
    }

    function renderBody() {
      preserveFocus(body, () => {
        let node;
        if (activeTab === 'discover') node = renderDiscover();
        else if (activeTab === 'explore') node = renderExplore();
        else node = renderLibrary();
        body.replaceChildren(node);
      });
    }

    function sync(ev) {
      latest = ev.sections || [];
      renderChrome();
      renderBody();
    }

    function installTabs(tab) {
      if (tabs && tabs.dispose) tabs.dispose();
      tabs = CW.tabBar(TABS, tab, key => {
        if (key === activeTab) return;
        rememberScroll();
        subscribe(key);
      });
      tabsHost.replaceChildren(tabs);
    }

    function subscribe(tab) {
      if (sub) sub.close();
      activeTab = tab;
      latest = [];
      catalogOpen = false;
      filterOpen = false;
      installTabs(tab);
      body.replaceChildren(CW.section.note('Loading…', null));
      sub = env.port.subscribe('world', params(), sync);
      restoreScroll();
    }

    subscribe(route.tab || 'discover');

    return {
      update(next) {
        const tab = next.tab || 'discover';
        if (tab !== activeTab) subscribe(tab);
      },
      unmount() {
        rememberScroll();
        clearTimeout(queryTimer);
        clearInterval(heroTimer);
        if (sub) sub.close();
        if (tabs && tabs.dispose) tabs.dispose();
      }
    };
  }

  CW.router.register('biblio', { mount });
})(window.CW = window.CW || {});
