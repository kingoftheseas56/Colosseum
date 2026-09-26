// surfaces/theatre/surface.js — Theatre world (pilot, Claude). Layout per SCHEMA.md and the Portico halfway mock:
// top region (featured + next up) · Continue Watching · tab bar · tab pane. Only shared components; no arrow keys.
// All browsing logic is native: Library filters/sort arrive as view Choices (CONTRACT §13.1), never computed here.
(function (CW) {
  'use strict';
  const { h } = CW;

  // Top region = the hero and Next Up, recognised by role rather than one exact id, so a native id change
  // (theatre.nextUp vs theatre.discover.nextUp) can never push the hero under the tab bar again.
  const isTop = s => s.layout === 'hero' || /(^|\.)nextUp$/.test(s.id);
  const isNextUp = s => /(^|\.)nextUp$/.test(s.id);

  // Feed only the sections a box owns into CW.section.sync; a change to another box's section is ignored.
  function scoped(ev, keep) {
    const sections = ev.sections.filter(keep);
    if (ev.changed) {
      const changed = ev.sections.find(s => s.id === ev.changed);
      if (changed && !keep(changed)) return null;   // a removed id (not in the list) goes to whichever box has it
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
      const pane = h('div.world-pane.th-pane');
      let bar = null, tabSub = null, view = null;
      const nextUpKeys = new Set();

      function subscribeTab() {
        if (tabSub) tabSub.close();
        const params = view ? { world: 'Theatre', tab: route.tab, view } : { world: 'Theatre', tab: route.tab };
        tabSub = env.port.subscribe('world', params, onTabEvent);
      }

      const ctx = {
        // Next Up cards open the next EPISODE (intent nextUp), everything else opens details (SCHEMA.md)
        open: (it, intent) => env.open(it, nextUpKeys.has(it.key) ? 'nextUp' : intent),
        forget: env.forget, seeAll: env.seeAll, act: env.act,
        // §13.1: a view Choice (library filter/sort, catalogue facet) merges its native patch and resubscribes
        choose: (c, s) => env.choose(c, s, null, patch => { view = { ...(view || {}), ...patch }; subscribeTab(); }),
        more: section => env.more(tabSub, section)
      };

      const contSub = env.port.subscribe('continue', { scope: 'Theatre' }, ev => {
        const next = { ...ev, sections: ev.sections.map(s => ({ ...s, title: 'Continue Watching' })) };
        CW.section.sync(contBox, next, ctx);   // its See all uses the native-issued route
      });

      function onTabEvent(ev) {
        nextUpKeys.clear();
        ev.sections.filter(isNextUp).forEach(s => s.items.forEach(it => nextUpKeys.add(it.key)));
        const top = scoped(ev, isTop);
        if (top && (top.sections.length || top.changed)) CW.section.sync(topBox, top, ctx);
        const rest = scoped(ev, x => !isTop(x));
        if (rest) { CW.section.sync(pane, rest, ctx); mark(pane); }
      }

      function showTab(r) {
        route = r;
        view = null;                 // each tab starts from native defaults
        pane.replaceChildren();
        subscribeTab();
      }

      const makeBar = tab => CW.tabBar(CW.contract.TABS.Theatre, tab,
        t => env.router.go({ name: 'world', world: 'Theatre', tab: t }, { replace: true }));
      bar = makeBar(route.tab);
      el.append(topBox, contBox, bar, pane);
      showTab(route);

      return {
        update(r) {
          // tab change: the top region and Continue stay put; only the bar state and the pane change
          const hadFocus = bar.contains(document.activeElement);
          const fresh = makeBar(r.tab);
          bar.dispose(); bar.replaceWith(fresh); bar = fresh;
          if (hadFocus) { const on = bar.querySelector('.tab.on'); if (on) on.focus({ preventScroll: true }); }
          showTab(r);
        },
        unmount() { contSub.close(); if (tabSub) tabSub.close(); if (bar) bar.dispose(); }
      };
    }
  });
})(window.CW = window.CW || {});
