// core/shell.js — the persistent chrome around every surface: wallpaper, TopBar, clock, toast, the
// covered state (CONTRACT §3.4/§5.3), and mounting the one visible surface for the current route.
//
// A surface is registered with CW.router.register(name, { mount(el, route, env) → { update?(route), unmount() } }).
// env gives surfaces everything they may use: port, router, open/forget/seeAll/more helpers and toast.
(function (CW) {
  'use strict';
  const $ = id => document.getElementById(id);

  function start(port) {
    const router = CW.router;
    const col = $('col');
    const board = $('board');
    let live = null;          // { name, instance }
    let opener = null;        // element that opened a native destination; focus returns here

    // ---- toast ----
    let toastTimer = 0;
    CW.toast = msg => {
      const t = $('toast');
      t.textContent = msg;
      t.classList.add('on');
      clearTimeout(toastTimer);
      toastTimer = setTimeout(() => t.classList.remove('on'), 3200);
    };

    const report = r => { if (r && !r.ok) CW.toast(r.error || 'Colosseum could not do that.'); return r; };

    const env = {
      port, router,
      toast: CW.toast,
      open(item, intent) {
        opener = document.activeElement;
        // v2 §12.4: native answers {result:{route}} when the destination is web-owned; web follows it.
        return port.act('open', { item, intent: intent || 'details' }).then(r => {
          if (r && r.ok && r.result && r.result.route && router.has(router.surfaceName(r.result.route))) router.go(r.result.route);
          return report(r);
        });
      },
      forget(item) { return port.act('continue.forget', { item }).then(report); },
      seeAll(section) {
        router.go({ name: 'seeAll', route: section.seeAll.route, title: section.title,
                    world: router.current().world || null });
      },
      more(handle, section) { return port.more(handle, section.id).then(report); },
      /** v1.3: follow a Choice's native-issued target. onQuery(query) lets a search surface rerun itself. */
      choose(choice, section, onQuery, onView) {
        const t = choice.target;
        if (t.view) { if (onView) onView(t.view); return; }        // v2.1 §13.1: the surface resubscribes
        if (t.route) router.go({ name: 'seeAll', route: t.route, title: choice.label, world: router.current().world || null });
        else if (typeof t.query === 'string') { if (onQuery) onQuery(t.query); else router.go({ name: 'search', scope: 'all', query: t.query }); }
        else if (t.act === 'search.surprise') return env.act(t.act, { scope: router.current().scope || router.current().world || 'all' });
        else if (t.act) return env.act(t.act, {});                // v2.2 §14.2 door choices
      },
      removeChoice(choice) {
        const q = choice.target && choice.target.query;
        return port.act('search.history.remove', { scope: router.current().scope || 'all', query: q }).then(report);
      },
      door(door, extra) {
        // v2 §12.4: a page ported to web opens in web; otherwise the native page opens.
        if (router.has('page.' + door)) { router.go({ name: 'page', page: door, params: extra || {} }); return Promise.resolve({ ok: true }); }
        opener = document.activeElement;
        return port.act('open.native', { door, ...(extra || {}) }).then(report);
      },
      act(action, payload) { opener = document.activeElement; return port.act(action, payload).then(report); }
    };
    CW.env = env;

    // ---- surface mounting: only the visible surface is alive ----
    function mount(route, prev) {
      const name = router.surfaceName(route);
      const app = $('app');
      app.dataset.surface = route.name === 'world' ? route.world : route.name;
      if (live && live.name === name && live.instance.update) {
        live.instance.update(route, prev);
      } else {
        if (live) { try { live.instance.unmount(); } catch (e) { console.error(e); } }
        col.replaceChildren();
        board.scrollTop = 0;
        const surface = router.surface(route);
        live = { name, instance: surface ? surface.mount(col, route, env) : { unmount() {} } };
        requestAnimationFrame(() => CW.focus.first(col));
      }
      syncTopBar(route);
    }

    function syncTopBar(route) {
      $('home-button').hidden = route.name === 'home';
      document.querySelectorAll('#world-nav .pill[data-world]').forEach(p =>
        p.classList.toggle('active', route.name === 'world' && p.dataset.world === route.world));
    }

    // ---- TopBar wiring ----
    $('home-button').addEventListener('click', () => router.home());
    document.querySelectorAll('#world-nav .pill[data-world]').forEach(p =>
      p.addEventListener('click', () => router.go({ name: 'world', world: p.dataset.world }, { replace: router.current().name === 'world' })));
    $('search-button').addEventListener('click', () =>
      router.go({ name: 'search', scope: router.current().world || 'all' }));
    $('account-button').addEventListener('click', () => env.door('account'));
    $('trackers-button').addEventListener('click', () => env.door('connections'));
    $('wallpaper-button').addEventListener('click', () => env.door('wallpaperSearch', { world: router.current().world || 'Home' }));
    $('minimize-button').addEventListener('click', () => port.act('window.minimize').then(report));
    $('fullscreen-button').addEventListener('click', () => port.act('window.fullscreen').then(report));
    $('close-button').addEventListener('click', () => port.act('window.close').then(report));

    // ---- clock ----
    const tick = () => {
      const d = new Date();
      const h12 = d.getHours() % 12 || 12;
      $('clkT').textContent = `${h12}:${String(d.getMinutes()).padStart(2, '0')}`;
      $('clkA').textContent = d.getHours() < 12 ? 'AM' : 'PM';
      $('clkD').textContent = d.toLocaleDateString(undefined, { weekday: 'long', month: 'long', day: 'numeric' });
    };
    tick(); setInterval(tick, 20000);

    // ---- shell state: account, wallpaper, covered ----
    port.onShell(state => {
      const wall = $('wall');
      const w = state.wallpaper || {};
      wall.dataset.hasImage = w.kind === 'image' && w.url ? 'true' : 'false';
      wall.style.backgroundImage = w.kind === 'image' && w.url ? `url("${w.url}")` : '';

      const acct = $('account-button');
      const a = state.account || {};
      acct.classList.toggle('signed-in', a.mode === 'account' || a.mode === 'signed-in');
      acct.textContent = a.initial || '';
      acct.setAttribute('aria-label', a.username ? `Account: ${a.username}` : 'Account');

      const covered = !!state.covered;
      document.body.classList.toggle('covered', covered);
      $('app').inert = covered;
      CW.focus.setEnabled(!covered);
      if (!covered && opener && opener.isConnected) { opener.focus({ preventScroll: true }); opener = null; }
    });

    CW.focus.onBack(() => router.back());
    router.onChange(mount);
    router.start();
  }

  CW.shell = { start };
})(window.CW = window.CW || {});
