// surfaces/tankoban/surface.js — Tankoban browsing world.
// QML parity: Featured → Next Up → Continue Reading → tabs → requested tab.
// Native owns catalogue/filter/library decisions; this surface owns presentation only.
(function (CW) {
  'use strict';
  const { h } = CW;

  const TABS = CW.contract.TABS.Tankoban;
  const isHero = s => s.id === 'tankoban.chrome.featured';
  const isPersonal = s => s.id === 'tankoban.chrome.nextUp' || s.id === 'tankoban.chrome.continue';
  const isDiscoverControl = s => /^tankoban\.discover\.(types|catalogues|filters)$/.test(s.id);
  const isLibraryControl = s => /^tankoban\.library\.(filters|sort)$/.test(s.id);
  const isRanked = s => s.id === 'tankoban.manga.top' || s.id === 'tankoban.comics.top';

  function scoped(ev, keep) {
    const sections = ev.sections.filter(keep);
    if (ev.changed) {
      const changed = ev.sections.find(s => s.id === ev.changed);
      if (changed && !keep(changed)) return null;
    }
    return { type: ev.type, changed: ev.changed, sections };
  }

  function image(url, cls) {
    if (!url) return null;
    const img = h('img' + (cls ? '.' + cls : ''), { alt: '', decoding: 'async', src: url });
    img.addEventListener('error', () => img.remove());
    return img;
  }

  CW.router.register('tankoban', {
    mount(el, route, env) {
      const heroBox = h('div.tk-hero-box');
      const personalBox = h('div.world-pane.tk-personal');
      const tabsHost = h('div.tk-tabs');
      const discoverHead = h('div.tk-discover-head', { hidden: true });
      const libraryHead = h('div.tk-library-head', { hidden: true });
      const pane = h('div.world-pane.tk-pane');
      const root = h('div.tk-surface', {}, heroBox, personalBox, tabsHost,
        discoverHead, libraryHead, pane);
      el.appendChild(root);

      const libraryQuery = h('input.tk-library-query', {
        type: 'search', autocomplete: 'off', placeholder: 'Search your library',
        'aria-label': 'Search your library', 'data-focus': true,
        'data-key': 'tankoban:library:query'
      });

      let tabSub = null;
      let bar = null;
      let queryTimer = 0;
      let discoverMenu = '';
      let discoverControls = [];
      let libraryControls = [];
      const views = { discover: {}, manga: {}, comics: {}, library: {} };

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

      const ctx = {
        open: env.open,
        forget: item => env.act('world.tankoban.collection.remove', { item }),
        seeAll: env.seeAll,
        act: env.act,
        choose: (c, s) => pick(c, s),
        more: section => env.more(tabSub, section)
      };

      function renderHero(section) {
        CW.focus.preserve(heroBox, () => {
          if (!section || section.state === 'loading') {
            heroBox.replaceChildren(CW.section.note('Loading Tankoban', 'Preparing Featured in Tankoban.'));
            return;
          }
          if (section.state === 'error') {
            heroBox.replaceChildren(CW.section.note('Couldn’t load this', section.error || 'Colosseum could not load this section.', 'err'));
            return;
          }
          if (!section.items.length) {
            heroBox.replaceChildren();
            return;
          }

          let at = 0;
          const slides = section.items.map((it, index) => {
            const poster = it.artKind === 'poster';
            const media = poster ? image(it.cover, 'tk-feature-poster') : image(it.backdrop || it.cover, 'tk-feature-art');
            const slide = h('div.tk-feature-slide', {
              'data-key': it.key,
              style: { '--tk-c1': it.c1 || 'transparent', '--tk-c2': it.c2 || 'transparent' }
            },
              h('div.tk-feature-gradient'),
              media,
              poster ? h('div.tk-feature-vignette') : null,
              h('div.tk-feature-wash'),
              it.ghost ? h('span.tk-feature-ghost', {}, it.ghost) : null,
              h('div.tk-feature-copy', {},
                h('span.tk-feature-kicker', {}, 'FEATURED IN TANKOBAN'),
                h('h2', {}, it.title),
                it.subtitle ? h('p', {}, it.subtitle) : null,
                h('div.tk-feature-actions', {},
                  h('button.tk-feature-read', {
                    type: 'button', 'data-focus': true, 'data-key': it.key + '#read',
                    onclick: () => env.open(it, 'details')
                  }, 'Read'),
                  h('button.tk-feature-details', {
                    type: 'button', 'data-focus': true, 'data-key': it.key + '#details',
                    onclick: () => env.open(it, 'details')
                  }, 'Details'))));
            return slide;
          });

          const track = h('div.tk-feature-track', {}, slides);
          const dots = h('div.tk-feature-dots', {}, section.items.map((it, index) =>
            h('button.tk-feature-dot', {
              type: 'button', tabindex: '-1', 'aria-label': 'Show ' + it.title,
              onclick: () => show(index)
            })));
          const carousel = h('section.widget.tk-feature', {
            'data-section': section.id, 'data-layout': 'hero', 'data-state': section.state,
            'data-arrows': true
          }, track, dots);

          function show(index) {
            at = (index + slides.length) % slides.length;
            track.style.transform = 'translateX(' + (-100 * at) + '%)';
            slides.forEach((slide, i) => { slide.inert = i !== at; });
            [...dots.children].forEach((dot, i) => dot.classList.toggle('on', i === at));
          }

          carousel.addEventListener('cw-arrow', e => {
            const step = e.detail === 'right' ? 1 : e.detail === 'left' ? -1 : 0;
            if (!step || at + step < 0 || at + step >= slides.length) return;
            e.preventDefault();
            show(at + step);
            const target = slides[at].querySelector('[data-focus]');
            if (target) target.focus({ preventScroll: true });
          });

          let downX = null;
          carousel.addEventListener('pointerdown', e => {
            downX = e.clientX;
            carousel.setPointerCapture && carousel.setPointerCapture(e.pointerId);
          });
          carousel.addEventListener('pointerup', e => {
            if (downX == null) return;
            const dx = e.clientX - downX;
            downX = null;
            if (Math.abs(dx) < 36) return;
            show(Math.max(0, Math.min(slides.length - 1, at + (dx < 0 ? 1 : -1))));
          });
          show(0);
          heroBox.replaceChildren(carousel);
        });
      }

      function rankedSection(section) {
        const rail = h('div.tk-ranked-rail');
        const strip = h('div.tk-ranked-strip', { 'data-arrows': true }, rail);
        const prev = h('button.tk-ranked-chevron.left', {
          type: 'button', 'data-focus': true, 'data-key': section.id + '#prev',
          'aria-label': 'Show earlier items', hidden: true
        }, '‹');
        const next = h('button.tk-ranked-chevron.right', {
          type: 'button', 'data-focus': true, 'data-key': section.id + '#next',
          'aria-label': 'Show later items', hidden: true
        }, '›');

        section.items.forEach((it, index) => {
          const card = h('button.tk-ranked-card', {
            type: 'button', 'data-focus': true, 'data-key': it.key,
            'aria-label': it.title, onclick: () => env.open(it, it.primary || 'details')
          },
            h('span.tk-ranked-num', {}, String(index + 1)),
            h('span.tk-ranked-cover', {},
              image(it.cover, 'tk-ranked-image'),
              h('span.tk-ranked-title', {}, it.title)));
          rail.appendChild(card);
        });

        const updateChevrons = () => {
          prev.hidden = strip.scrollLeft <= 1;
          next.hidden = strip.scrollLeft >= strip.scrollWidth - strip.clientWidth - 1;
        };
        const page = dir => strip.scrollBy({ left: dir * strip.clientWidth * 0.8, behavior: 'smooth' });
        prev.addEventListener('click', () => page(-1));
        next.addEventListener('click', () => page(1));
        strip.addEventListener('scroll', updateChevrons, { passive: true });

        strip.addEventListener('cw-arrow', e => {
          if (e.detail !== 'left' && e.detail !== 'right') return;
          const active = document.activeElement;
          if (active === prev || active === next) {
            e.preventDefault();
            page(e.detail === 'right' ? 1 : -1);
          }
        });

        const header = h('div.wh', {},
          h('h2', {}, section.title),
          section.seeAll ? h('button.more', {
            type: 'button', 'data-focus': true, 'data-key': section.id + '#all',
            onclick: () => env.seeAll(section)
          }, 'Explore', h('span.ch', { 'aria-hidden': true }, '›')) : null);
        const widget = h('section.widget.tk-ranked', {
          'data-section': section.id, 'data-layout': 'rail', 'data-state': section.state
        }, header, h('div.tk-ranked-wrap', {}, strip, prev, next));
        queueMicrotask(updateChevrons);
        return widget;
      }

      function replaceRanked(ev) {
        if (ev.type === 'reset') pane.querySelectorAll('.tk-ranked').forEach(n => n.remove());
        for (const section of ev.sections.filter(isRanked)) {
          const old = pane.querySelector('[data-section="' + CSS.escape(section.id) + '"]');
          const fresh = rankedSection(section);
          if (old) old.replaceWith(fresh);
          else pane.appendChild(fresh);
        }
      }

      function discoverSection(id) {
        return discoverControls.find(s => s.id === id) || null;
      }

      function closeDiscoverMenu(returnKey) {
        discoverMenu = '';
        renderDiscoverControls();
        if (!returnKey) return;
        const button = discoverHead.querySelector('[data-key="' + CSS.escape(returnKey) + '"]');
        if (button) button.focus({ preventScroll: true });
      }

      function discoverMenuBox(name, choices, returnKey) {
        if (discoverMenu !== name) return null;
        const box = h('div.tk-menu', {
          role: 'menu', 'data-focus-scope': true, 'data-key': 'tankoban:menu:' + name
        }, choices.map(c =>
          h('button.tk-menu-row' + (c.selected ? '.on' : ''), {
            type: 'button', role: 'menuitem', 'data-focus': true, 'data-key': c.key,
            onclick: () => {
              discoverMenu = '';
              pick(c, discoverSection(name === 'catalogue'
                ? 'tankoban.discover.catalogues'
                : 'tankoban.discover.filters'));
            }
          }, h('span', {}, c.label), c.sublabel ? h('small', {}, c.sublabel) : null)
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
        const types = discoverSection('tankoban.discover.types');
        const catalogues = discoverSection('tankoban.discover.catalogues');
        const filters = discoverSection('tankoban.discover.filters');
        if (!types || !catalogues || !filters) {
          replaceDiscover(CW.section.note('Loading Tankoban', 'Preparing the catalogue controls.'));
          return;
        }

        const selectedCatalogue = catalogues.choices.find(c => c.selected) || catalogues.choices[0];
        const selectedFilter = filters.choices.find(c => c.selected) || filters.choices[0];
        const typeLens = h('div.tk-type-lens', {}, types.choices.map(c =>
          h('button.tk-type' + (c.selected ? '.on' : ''), {
            type: 'button', 'data-focus': true, 'data-key': c.key,
            'aria-pressed': c.selected ? 'true' : 'false', onclick: () => pick(c, types)
          }, c.label)));

        const catalogueKey = 'tankoban:discover:catalogue-menu';
        const catalogueButton = h('button.tk-catalogue', {
          type: 'button', 'data-focus': true, 'data-key': catalogueKey,
          'aria-haspopup': 'menu', 'aria-expanded': discoverMenu === 'catalogue' ? 'true' : 'false',
          onclick: () => {
            discoverMenu = discoverMenu === 'catalogue' ? '' : 'catalogue';
            renderDiscoverControls();
            if (discoverMenu === 'catalogue') {
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
          'aria-haspopup': 'menu', 'aria-expanded': discoverMenu === 'filter' ? 'true' : 'false',
          onclick: () => {
            discoverMenu = discoverMenu === 'filter' ? '' : 'filter';
            renderDiscoverControls();
            if (discoverMenu === 'filter') {
              const first = discoverHead.querySelector('.tk-menu [data-focus]');
              if (first) first.focus({ preventScroll: true });
            }
          }
        }, selectedFilter && selectedFilter.label !== 'All' ? selectedFilter.label : 'Filter');

        replaceDiscover(
          h('div.tk-mast', {},
            typeLens,
            h('div.tk-shelf', {}, catalogueButton,
              discoverMenuBox('catalogue', catalogues.choices, catalogueKey))),
          h('div.tk-filterbar', {},
            h('span.tk-filter-label', {}, 'FILTER'),
            filterButton,
            discoverMenuBox('filter', filters.choices, filterKey)));
      }

      function librarySection(id) {
        return libraryControls.find(s => s.id === id) || null;
      }

      function renderLibraryControls() {
        if (route.tab !== 'library') {
          libraryHead.hidden = true;
          libraryHead.replaceChildren();
          return;
        }
        libraryHead.hidden = false;
        const filters = librarySection('tankoban.library.filters');
        const sorts = librarySection('tankoban.library.sort');
        if (!filters || !sorts) {
          libraryHead.replaceChildren(CW.section.note('Loading Tankoban', 'Preparing your library.'));
          return;
        }

        const pills = (section, cls) => h('div.' + cls, {}, section.choices.map(c =>
          h('button.tk-library-pill' + (c.selected ? '.on' : ''), {
            type: 'button', 'data-focus': true, 'data-key': c.key,
            'aria-pressed': c.selected ? 'true' : 'false', onclick: () => pick(c, section)
          }, c.label)));

        CW.focus.preserve(libraryHead, () => libraryHead.replaceChildren(
          h('div.tk-library-toolbar', {},
            libraryQuery,
            pills(filters, 'tk-library-filters'),
            pills(sorts, 'tk-library-sorts'))));
      }

      function bodyEvent(ev) {
        const next = scoped(ev, s => !isHero(s) && !isPersonal(s)
          && !isDiscoverControl(s) && !isLibraryControl(s) && !isRanked(s));
        if (!next) return null;
        return {
          ...next,
          sections: next.sections.map(s => {
            if (s.id !== 'tankoban.library.saved') return s;
            return { ...s, items: s.items.map(it => {
              const clean = { ...it };
              delete clean.progress;
              delete clean.badge;
              delete clean.subtitle;
              return clean;
            }) };
          })
        };
      }

      function polishBody() {
        const mangaGenres = pane.querySelector('[data-section="tankoban.manga.genres"] .more');
        if (mangaGenres && mangaGenres.firstChild) mangaGenres.firstChild.nodeValue = 'Explore';
      }

      function onWorld(ev) {
        const hero = ev.sections.find(isHero);
        if (hero && (ev.type === 'reset' || ev.changed === hero.id)) renderHero(hero);

        const personal = scoped(ev, isPersonal);
        if (personal) CW.section.sync(personalBox, personal, ctx);

        const dControls = ev.sections.filter(isDiscoverControl);
        if (dControls.length || route.tab === 'discover') {
          discoverControls = dControls;
          renderDiscoverControls();
        }

        const lControls = ev.sections.filter(isLibraryControl);
        if (lControls.length || route.tab === 'library') {
          libraryControls = lControls;
          renderLibraryControls();
        }

        const body = bodyEvent(ev);
        if (body) CW.section.sync(pane, body, ctx);
        replaceRanked(ev);
        polishBody();
      }

      function makeBar(tab) {
        return CW.tabBar(TABS, tab,
          next => env.router.go({ name: 'world', world: 'Tankoban', tab: next }, { replace: true }));
      }

      function showTab(next) {
        route = next;
        discoverMenu = '';
        discoverControls = [];
        libraryControls = [];
        discoverHead.hidden = route.tab !== 'discover';
        libraryHead.hidden = route.tab !== 'library';
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
          if (tabSub) tabSub.close();
          if (bar) bar.dispose();
        }
      };
    }
  });
})(window.CW = window.CW || {});
