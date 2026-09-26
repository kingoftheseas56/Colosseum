// surfaces/tankoban/surface.js — Tankoban browsing world.
// QML parity: Featured → Next Up → Continue Reading → tabs → requested tab.
// Native owns every catalogue/filter/library decision; this surface only presents
// Sections/Choices and resubscribes when a native-issued view patch is chosen.
(function (CW) {
  'use strict';
  const { h } = CW;

  const TABS = CW.contract.TABS.Tankoban;
  const isTop = s => s.layout === 'hero' || /(^|\.)nextUp$/.test(s.id);
  const isDiscoverControl = s => /^tankoban\.discover\.(types|catalogues|filters)$/.test(s.id);

  function scoped(ev, keep) {
    const sections = ev.sections.filter(keep);
    if (ev.changed) {
      const changed = ev.sections.find(s => s.id === ev.changed);
      if (changed && !keep(changed)) return null;
    }
    return { type: ev.type, changed: ev.changed, sections };
  }

  CW.router.register('tankoban', {
    mount(el, route, env) {
      const top = h('div.world-pane.tk-top');
      const continuing = h('div.world-pane.tk-continue');
      const tabsHost = h('div.tk-tabs');
      const discoverHead = h('div.tk-discover-head', { hidden: true });
      const libraryQuery = h('input.tk-query', {
        type: 'search', autocomplete: 'off', placeholder: 'Search your library',
        'aria-label': 'Search your library', 'data-focus': true,
        'data-key': 'tankoban:library:query'
      });
      const queryBox = h('label.sfield.tk-querybox', { hidden: true }, libraryQuery);
      const pane = h('div.world-pane.tk-pane');
      const root = h('div.tk-surface', {}, top, continuing, tabsHost,
        discoverHead, queryBox, pane);
      el.appendChild(root);

      let tabSub = null;
      let bar = null;
      let queryTimer = 0;
      let openMenu = '';
      let lastControls = [];
      const views = {
        discover: {},
        manga: {},
        comics: {},
        library: {}
      };

      function viewFor(tab) {
        return views[tab] || (views[tab] = {});
      }

      function subscribeTab() {
        if (tabSub) tabSub.close();
        const view = viewFor(route.tab);
        const params = Object.keys(view).length
          ? { world: 'Tankoban', tab: route.tab, view }
          : { world: 'Tankoban', tab: route.tab };
        tabSub = env.port.subscribe('world', params, onWorld);
      }

      function applyView(patch) {
        views[route.tab] = { ...viewFor(route.tab), ...patch };
        subscribeTab();
      }

      function pick(choice, section) {
        return env.choose(choice, section, null, patch => applyView(patch));
      }

      const topCtx = {
        open: env.open,
        forget: env.forget,
        seeAll: env.seeAll,
        act: env.act,
        choose: (c, s) => pick(c, s),
        more: section => env.more(tabSub, section)
      };

      const paneCtx = {
        open: env.open,
        // Manga/Comics "Your Collection" is a Continue-shaped visual, but
        // its context action removes the Collection entry, not Progress.
        forget: item => env.act('world.tankoban.collection.remove', { item }),
        seeAll: env.seeAll,
        act: env.act,
        choose: (c, s) => pick(c, s),
        more: section => env.more(tabSub, section)
      };

      const continueSub = env.port.subscribe('continue', { scope: 'Tankoban' }, ev => {
        const renamed = {
          ...ev,
          sections: ev.sections.map(s => ({ ...s, title: 'Continue Reading' }))
        };
        CW.section.sync(continuing, renamed, topCtx);
      });

      function section(id) {
        return lastControls.find(s => s.id === id) || null;
      }

      function closeDiscoverMenu(returnKey) {
        openMenu = '';
        renderDiscoverControls();
        if (!returnKey) return;
        const button = discoverHead.querySelector('[data-key="' + CSS.escape(returnKey) + '"]');
        if (button) button.focus({ preventScroll: true });
      }

      function menu(name, choices, returnKey) {
        if (openMenu !== name) return null;
        const box = h('div.tk-menu', {
          role: 'menu', 'data-focus-scope': true, 'data-key': 'tankoban:menu:' + name
        }, choices.map(c =>
          h('button.tk-menu-row' + (c.selected ? '.on' : ''), {
            type: 'button', role: 'menuitem', 'data-focus': true,
            'data-key': c.key,
            onclick: () => {
              openMenu = '';
              pick(c, section(name === 'catalogue'
                ? 'tankoban.discover.catalogues'
                : 'tankoban.discover.filters'));
            }
          },
          h('span', {}, c.label),
          c.sublabel ? h('small', {}, c.sublabel) : null)
        ));
        box.__close = () => closeDiscoverMenu(returnKey);
        return box;
      }

      function replaceDiscover(...nodes) {
        CW.focus.preserve(discoverHead, () => discoverHead.replaceChildren(...nodes));
      }

      function renderDiscoverControls() {
        if (route.tab !== 'discover') {
          discoverHead.hidden = true;
          replaceDiscover();
          return;
        }
        discoverHead.hidden = false;
        const types = section('tankoban.discover.types');
        const catalogues = section('tankoban.discover.catalogues');
        const filters = section('tankoban.discover.filters');
        if (!types || !catalogues || !filters) {
          discoverHead.replaceChildren(CW.section.note(
            'Loading Tankoban', 'Preparing the catalogue controls.'));
          return;
        }

        const selectedCatalogue = catalogues.choices.find(c => c.selected) || catalogues.choices[0];
        const selectedFilter = filters.choices.find(c => c.selected) || filters.choices[0];
        const typeLens = h('div.tk-type-lens', {}, types.choices.map(c =>
          h('button.tk-type' + (c.selected ? '.on' : ''), {
            type: 'button', 'data-focus': true, 'data-key': c.key,
            'aria-pressed': c.selected ? 'true' : 'false',
            onclick: () => pick(c, types)
          }, c.label)
        ));

        const catalogueKey = 'tankoban:discover:catalogue-menu';
        const catalogueButton = h('button.tk-catalogue', {
          type: 'button', 'data-focus': true, 'data-key': catalogueKey,
          'aria-haspopup': 'menu', 'aria-expanded': openMenu === 'catalogue' ? 'true' : 'false',
          onclick: () => {
            openMenu = openMenu === 'catalogue' ? '' : 'catalogue';
            renderDiscoverControls();
            if (openMenu === 'catalogue') {
              const first = discoverHead.querySelector('.tk-menu [data-focus]');
              if (first) first.focus({ preventScroll: true });
            }
          }
        },
        h('span.tk-kicker', {}, 'NOW BROWSING'),
        h('strong', {}, selectedCatalogue ? selectedCatalogue.label : '—'),
        h('small', {}, selectedCatalogue && selectedCatalogue.sublabel
          ? selectedCatalogue.sublabel : 'Tankoban built-in catalogue'));

        const filterKey = 'tankoban:discover:filter-menu';
        const filterButton = h('button.tk-filter', {
          type: 'button', 'data-focus': true, 'data-key': filterKey,
          'aria-haspopup': 'menu', 'aria-expanded': openMenu === 'filter' ? 'true' : 'false',
          onclick: () => {
            openMenu = openMenu === 'filter' ? '' : 'filter';
            renderDiscoverControls();
            if (openMenu === 'filter') {
              const first = discoverHead.querySelector('.tk-menu [data-focus]');
              if (first) first.focus({ preventScroll: true });
            }
          }
        }, selectedFilter && selectedFilter.label !== 'All'
          ? selectedFilter.label : 'Filter');

        replaceDiscover(
          h('div.tk-mast', {},
            typeLens,
            h('div.tk-shelf', {}, catalogueButton,
              menu('catalogue', catalogues.choices, catalogueKey))),
          h('div.tk-filterbar', {},
            h('span.tk-filter-label', {}, 'FILTER'),
            filterButton,
            menu('filter', filters.choices, filterKey))
        );
      }

      function onWorld(ev) {
        const controls = ev.sections.filter(isDiscoverControl);
        if (controls.length || route.tab === 'discover') {
          lastControls = controls;
          renderDiscoverControls();
        }

        const topEvent = scoped(ev, isTop);
        if (topEvent) CW.section.sync(top, topEvent, topCtx);

        const bodyEvent = scoped(ev, s => !isTop(s) && !isDiscoverControl(s));
        if (bodyEvent) CW.section.sync(pane, bodyEvent, paneCtx);
      }

      function makeBar(tab) {
        return CW.tabBar(TABS, tab,
          next => env.router.go(
            { name: 'world', world: 'Tankoban', tab: next },
            { replace: true }));
      }

      function showTab(next) {
        route = next;
        openMenu = '';
        lastControls = [];
        discoverHead.hidden = route.tab !== 'discover';
        queryBox.hidden = route.tab !== 'library';
        pane.dataset.tab = route.tab;
        const query = viewFor('library').query || '';
        if (libraryQuery.value !== query) libraryQuery.value = query;
        pane.replaceChildren();
        subscribeTab();
      }

      libraryQuery.addEventListener('input', () => {
        clearTimeout(queryTimer);
        queryTimer = setTimeout(() => {
          views.library = { ...viewFor('library'), query: libraryQuery.value };
          if (route.tab === 'library') subscribeTab();
        }, 250);
      });
      libraryQuery.addEventListener('keydown', e => {
        if (e.key !== 'Escape' || !libraryQuery.value) return;
        e.stopPropagation();
        libraryQuery.value = '';
        views.library = { ...viewFor('library'), query: '' };
        subscribeTab();
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
          continueSub.close();
          if (tabSub) tabSub.close();
          if (bar) bar.dispose();
        }
      };
    }
  });
})(window.CW = window.CW || {});
