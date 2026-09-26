// core/router.js — web-internal navigation (CONTRACT §3.6: tabs, See All, search, back are NOT actions).
//
// Routes:  { name:'home' } · { name:'world', world, tab } · { name:'seeAll', route, title, world }
//          · { name:'search', scope }
// Surfaces register by name; the route picks the surface. A route change inside the same surface calls
// its update(route) so tab switches don't tear the page down. Only the visible surface is alive.
(function (CW) {
  'use strict';

  const DEFAULT_TABS = { Tankoban: 'discover', Biblio: 'discover', Theatre: 'discover' };
  const registry = new Map();
  const listeners = new Set();
  let stack = [{ name: 'home' }];

  const surfaceFor = r =>
    r.name === 'world' ? r.world.toLowerCase()
    : r.name === 'seeAll' ? 'seeall'
    : r.name === 'page' ? 'page.' + r.page          // v2: { name:'page', page:'downloads', params }
    : r.name === 'detail' ? 'detail.' + r.kind      // v2: { name:'detail', kind:'theatre', params } (native-issued)
    : r.name;

  function writeHash() {
    try { history.replaceState(null, '', '#' + encodeURIComponent(JSON.stringify(current()))); } catch (_) { /* qrc */ }
  }

  function readHash() {
    try {
      const r = JSON.parse(decodeURIComponent(location.hash.slice(1)));
      if (r && typeof r.name === 'string') return r;
    } catch (_) { /* no hash */ }
    return null;
  }

  function current() { return stack[stack.length - 1]; }

  function notify(prev) {
    writeHash();
    for (const fn of listeners) fn(current(), prev);
  }

  const router = {
    register(name, surface) { registry.set(name, surface); },
    surface(route) { return registry.get(surfaceFor(route)) || registry.get('reference'); },
    has(name) { return registry.has(name); },
    names() { return [...registry.keys()]; },
    surfaceName: surfaceFor,
    current,
    depth: () => stack.length,

    go(route, opts) {
      if (route.name === 'world' && !route.tab) route = { ...route, tab: DEFAULT_TABS[route.world] };
      const prev = current();
      if (opts && opts.replace) stack[stack.length - 1] = route;
      else stack.push(route);
      notify(prev);
    },

    /** Back inside web. Returns false when there is nowhere to go (already Home). */
    back() {
      const prev = current();
      if (stack.length > 1) { stack.pop(); notify(prev); return true; }
      if (prev.name !== 'home') { stack = [{ name: 'home' }]; notify(prev); return true; }
      return false;
    },

    home() { const prev = current(); stack = [{ name: 'home' }]; notify(prev); },

    onChange(fn) { listeners.add(fn); return () => listeners.delete(fn); },

    start() {
      const r = readHash();
      if (r) stack = r.name === 'home' ? [r] : [{ name: 'home' }, r];
      notify(null);
    }
  };

  CW.router = router;
})(window.CW = window.CW || {});
