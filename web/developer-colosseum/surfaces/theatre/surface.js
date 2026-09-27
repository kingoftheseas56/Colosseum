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
      let pane = h('div.world-pane.th-pane');
      // Library title search (LibraryPage.qml:190-193): native filters on view.query (§13.1); Escape clears it
      const query = h('input.th-q', { type: 'search', autocomplete: 'off', placeholder: 'Search your library',
                                       'aria-label': 'Search your library', 'data-focus': true });
      const queryBox = h('label.sfield.th-qbox', { hidden: true }, query);
      let queryTimer = 0;
      query.addEventListener('input', () => {
        clearTimeout(queryTimer);
        queryTimer = setTimeout(() => { view = { ...(view || {}), query: query.value }; subscribeTab(); }, 250);
      });
      query.addEventListener('keydown', e => {
        if (e.key === 'Escape' && query.value) { e.stopPropagation(); query.value = ''; view = { ...(view || {}), query: '' }; subscribeTab(); }
      }, true);
      let bar = null, tabSub = null, view = null;
      const nextUpKeys = new Set();
      const top10Cache = new Map();
      const top10Nodes = new Map();
      let firstShelfPaintPending = false, paintEpoch = 0, paintFallback = 0;
      let firstShelfPainted = false;
      let deferredTabEvents = [];
      let retireTimer = 0;
      const retiredPanes = [];

      function clearRetiredPanes() {
        clearTimeout(retireTimer);
        retiredPanes.splice(0).forEach(old => old.replaceChildren());
      }

      function subscribeTab(deferStart = false) {
        if (tabSub) tabSub.close();
        tabSub = null;
        ++paintEpoch;
        const epoch = paintEpoch;
        clearTimeout(paintFallback);
        firstShelfPaintPending = false;
        firstShelfPainted = false;
        deferredTabEvents = [];
        const params = view ? { world: 'Theatre', tab: route.tab, view } : { world: 'Theatre', tab: route.tab };
        const start = () => {
          if (epoch === paintEpoch) tabSub = env.port.subscribe('world', params, onTabEvent);
        };
        if (deferStart) return start;
        start();
      }

      const ctx = {
        // Next Up cards open the next EPISODE (intent nextUp), everything else opens details (SCHEMA.md)
        open: (it, intent) => env.open(it, nextUpKeys.has(it.key) ? 'nextUp' : intent),
        forget: env.forget, seeAll: env.seeAll,
        // card-menu actions (§15.1) resolve after native persists; the store change re-sends the section
        act: (a, p) => env.act(a, p),
        // §13.1: a view Choice (library filter/sort, catalogue facet) merges its native patch and resubscribes
        choose: (c, s) => env.choose(c, s, null, patch => { view = { ...(view || {}), ...patch }; subscribeTab(); }),
        more: section => env.more(tabSub, section)
      };

      const contSub = env.port.subscribe('continue', { scope: 'Theatre' }, ev => {
        const next = { ...ev, sections: ev.sections.map(s => ({ ...s, title: 'Continue Watching' })) };
        CW.section.sync(contBox, next, ctx);   // its See all uses the native-issued route
      });

      function applyTabEvent(ev) {
        if (ev.type === 'reset') top10Nodes.delete(route.tab);
        nextUpKeys.clear();
        ev.sections.filter(isNextUp).forEach(s => s.items.forEach(it => nextUpKeys.add(it.key)));
        const top = scoped(ev, isTop);
        if (top && (top.sections.length || top.changed)) CW.section.sync(topBox, top, ctx);
        const rest = scoped(ev, x => !isTop(x));
        if (rest) { CW.section.sync(pane, rest, ctx); mark(pane); }
        if (ev.changed === `theatre.${route.tab}.top10`)
          top10Nodes.set(route.tab, pane.querySelector(`[data-section="${ev.changed}"][data-state="ready"]`));
      }

      function holdUntilFirstPaint(afterPaint) {
        // A warm feed can deliver every lower shelf in one burst. Let the first shelf paint
        // before building the rest of the DOM; a background WebView still flushes eventually.
        firstShelfPaintPending = true;
        firstShelfPainted = true;
        const epoch = paintEpoch;
        const flush = () => {
          if (epoch !== paintEpoch || !firstShelfPaintPending) return;
          firstShelfPaintPending = false;
          clearTimeout(paintFallback);
          const events = deferredTabEvents;
          deferredTabEvents = [];
          events.forEach(applyTabEvent);
          clearRetiredPanes();
          if (afterPaint) afterPaint();
        };
        requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(flush, 0)));
        paintFallback = setTimeout(flush, 1000);
      }

      function onTabEvent(ev) {
        const top10 = ev.changed === `theatre.${route.tab}.top10`
          ? ev.sections.find(s => s.id === ev.changed && s.state === 'ready' && s.items.length)
          : null;
        if (top10) top10Cache.set(route.tab, top10);
        if (firstShelfPaintPending) { deferredTabEvents.push(ev); return; }
        applyTabEvent(ev);
        if (top10 && !firstShelfPainted) holdUntilFirstPaint();
      }

      function showTab(r) {
        const oldPane = pane;
        pane = h('div.world-pane.th-pane');
        oldPane.replaceWith(pane);
        retiredPanes.push(oldPane);
        clearTimeout(retireTimer);
        retireTimer = setTimeout(clearRetiredPanes, 1000);
        route = r;
        view = null;                 // each tab starts from native defaults
        query.value = '';
        queryBox.hidden = r.tab !== 'library';
        const cachedNode = top10Nodes.get(r.tab);
        const cached = top10Cache.get(r.tab);
        const startFeed = subscribeTab(!!(cachedNode || cached));
        if (cachedNode || cached) {
          if (cachedNode) pane.appendChild(cachedNode);
          else { CW.section.sync(pane, { type: 'reset', sections: [cached] }, ctx); mark(pane); }
          holdUntilFirstPaint(startFeed);
        }
      }

      const makeBar = tab => CW.tabBar(CW.contract.TABS.Theatre, tab,
        t => env.router.go({ name: 'world', world: 'Theatre', tab: t }, { replace: true }));
      bar = makeBar(route.tab);
      el.append(topBox, contBox, bar, queryBox, pane);
      showTab(route);

      return {
        update(r) {
          // tab change: the top region and Continue stay put; only the bar state and the pane change
          const hadFocus = bar.contains(document.activeElement);
          bar.setActive(r.tab);
          if (hadFocus) { const on = bar.querySelector('.tab.on'); if (on) on.focus({ preventScroll: true }); }
          showTab(r);
        },
        unmount() {
          ++paintEpoch;
          clearTimeout(paintFallback);
          clearRetiredPanes();
          deferredTabEvents = [];
          contSub.close(); if (tabSub) tabSub.close(); if (bar) bar.dispose();
        }
      };
    }
  });
})(window.CW = window.CW || {});
