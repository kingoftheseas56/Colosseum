// surfaces/theatre/surface.js — Theatre world (pilot, Claude). Layout per SCHEMA.md and the Portico halfway mock:
// top region (featured + next up) · Continue Watching · tab bar · tab pane. Only shared components; no arrow keys.
(function (CW) {
  'use strict';
  const { h } = CW;
  // Top region = the hero and Next Up, recognised by role rather than one exact id, so a native id change
  // (theatre.nextUp vs theatre.discover.nextUp) can never push the hero under the tab bar again.
  const isTop = s => s.layout === 'hero' || /(^|\.)nextUp$/.test(s.id);
  const LIB_FILTERS = [['all', 'All'], ['movies', 'Movies'], ['shows', 'Shows'], ['anime', 'Anime']];
  const LIB_KINDS = { movies: ['movie'], shows: ['series'], anime: ['anime'] };

  // Feed only the sections a box owns into CW.section.sync; a change to another box's section is ignored.
  function scoped(ev, keep) {
    const sections = ev.sections.filter(keep);
    if (ev.changed) {
      const changed = ev.sections.find(s => s.id === ev.changed);
      // a removed section is no longer in the list: let whichever box holds it drop it
      if (changed ? !keep(changed) : false) return null;
    }
    return { type: ev.type, changed: ev.changed, sections };
  }

  // Top 10 gets Portico's rank numerals; the rest of the look is the shared card.
  function mark(box) {
    box.querySelectorAll('[data-section$=".top10"]').forEach(s => s.classList.add('t10mode'));
  }

  CW.router.register('theatre', {
    mount(el, route, env) {
      const topBox = h('div.world-pane.th-top');
      const contBox = h('div.world-pane.th-cont');
      const libBar = h('div.chips.th-libf', { hidden: true });
      const pane = h('div.world-pane.th-pane');
      let bar = null, tabSub = null, libFilter = 'all', lastPaneEv = null;

      const ctx = {
        open: env.open, forget: env.forget, seeAll: env.seeAll, act: env.act,
        choose: (c, s) => env.choose(c, s),
        more: section => env.more(tabSub, section)
      };

      const contSub = env.port.subscribe('continue', { scope: 'Theatre' }, ev => {
        const next = { ...ev, sections: ev.sections.map(s => ({ ...s, title: 'Continue Watching' })) };
        CW.section.sync(contBox, next, ctx);   // its See all uses the native-issued route
      });

      function libraryView(ev) {
        if (!ev || route.tab !== 'library') return ev;
        const kinds = LIB_KINDS[libFilter];
        return { ...ev, changed: null, type: 'reset',
                 sections: ev.sections.map(s => kinds ? { ...s, items: s.items.filter(it => kinds.includes(it.kind)),
                                                          state: s.items.some(it => kinds.includes(it.kind)) ? s.state : 'empty' } : s) };
      }

      function paintPane(ev) {
        const view = libraryView(ev);
        if (!view) return;
        CW.section.sync(pane, view, ctx);
        mark(pane);
      }

      function showTab(r) {
        route = r;
        if (tabSub) tabSub.close();
        pane.replaceChildren();
        lastPaneEv = null;
        libBar.hidden = r.tab !== 'library';
        tabSub = env.port.subscribe('world', { world: 'Theatre', tab: r.tab }, ev => {
          const top = scoped(ev, isTop);
          if (top && (top.sections.length || top.changed)) CW.section.sync(topBox, top, ctx);
          const rest = scoped(ev, x => !isTop(x));
          if (!rest) return;
          lastPaneEv = rest;          // port events always carry the full ordered list
          paintPane(rest);
        });
      }

      function renderLibBar() {
        libBar.replaceChildren(...LIB_FILTERS.map(([k, label]) =>
          h('button.chip' + (k === libFilter ? '.on' : ''), { type: 'button', 'data-focus': true, 'data-key': 'libf:' + k,
            onclick: () => { libFilter = k; renderLibBar(); if (lastPaneEv) paintPane({ ...lastPaneEv, type: 'reset', changed: null }); } }, label)));
      }

      bar = CW.tabBar(CW.contract.TABS.Theatre, route.tab,
        tab => env.router.go({ name: 'world', world: 'Theatre', tab }, { replace: true }));
      renderLibBar();
      el.append(topBox, contBox, bar, libBar, pane);
      showTab(route);

      return {
        update(r) {
          // tab change: rebuild only the tab bar state + pane; the top region and Continue stay put
          const hadFocus = bar.contains(document.activeElement);
          const fresh = CW.tabBar(CW.contract.TABS.Theatre, r.tab,
            tab => env.router.go({ name: 'world', world: 'Theatre', tab }, { replace: true }));
          bar.dispose(); bar.replaceWith(fresh); bar = fresh;
          if (hadFocus) { const on = bar.querySelector('.tab.on'); if (on) on.focus({ preventScroll: true }); }
          showTab(r);
        },
        unmount() { contSub.close(); if (tabSub) tabSub.close(); if (bar) bar.dispose(); }
      };
    }
  });
})(window.CW = window.CW || {});
