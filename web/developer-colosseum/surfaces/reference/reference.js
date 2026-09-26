// surfaces/reference/reference.js — the REFERENCE surface (Claude). It renders any v1 feed generically and
// is the fallback for every route whose real surface has not landed yet. Swarm agents copy its SHAPE
// (subscribe → CW.section.sync → close on unmount), not its look: real surfaces match their QML/mock.
(function (CW) {
  'use strict';
  const { h } = CW;

  function feedView(el, env, feed, params) {
    const box = h('div.world-pane');
    el.appendChild(box);
    let handle = null;
    const ctx = {
      open: env.open, forget: env.forget, seeAll: env.seeAll,
      more: section => env.more(handle, section)
    };
    handle = env.port.subscribe(feed, params, ev => CW.section.sync(box, ev, ctx));
    return { box, close: () => handle.close() };
  }

  const surface = {
    mount(el, route, env) {
      if (route.name === 'home') {
        const v = feedView(el, env, 'home', {});
        return { unmount: v.close };
      }

      if (route.name === 'world') {
        let view = null;
        let bar = null;
        const show = r => {
          if (view) { view.close(); view.box.remove(); }
          if (bar) { bar.dispose(); bar.remove(); }
          bar = CW.tabBar(CW.contract.TABS[r.world], r.tab,
            tab => env.router.go({ name: 'world', world: r.world, tab }, { replace: true }));
          el.appendChild(bar);
          view = feedView(el, env, 'world', { world: r.world, tab: r.tab });
        };
        show(route);
        return {
          update: r => show(r),
          unmount: () => { if (view) view.close(); if (bar) bar.dispose(); }
        };
      }

      if (route.name === 'seeAll') {
        el.appendChild(h('div.page-head', {}, h('h1', {}, route.title || 'All')));
        const v = feedView(el, env, 'seeAll', { route: route.route });
        return { unmount: v.close };
      }

      if (route.name === 'search') {
        let view = null;
        let timer = 0;
        const input = h('input', { type: 'search', autocomplete: 'off', placeholder: 'Search', 'data-focus': true,
                                   'aria-label': 'Search' });
        el.appendChild(h('label.sfield', {}, input));
        const results = h('div');
        el.appendChild(results);
        const run = () => {
          if (view) { view.close(); view.box.remove(); view = null; }
          const query = input.value.trim();
          if (query.length < 2) return;
          view = feedView(results, env, 'search', { scope: route.scope || 'all', query });
        };
        input.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(run, 250); });
        requestAnimationFrame(() => input.focus());
        return { unmount: () => { clearTimeout(timer); if (view) view.close(); } };
      }

      el.appendChild(CW.section.note('Unknown page', `No surface for route "${route.name}".`, 'err'));
      return { unmount() {} };
    }
  };

  CW.router.register('reference', surface);
})(window.CW = window.CW || {});
