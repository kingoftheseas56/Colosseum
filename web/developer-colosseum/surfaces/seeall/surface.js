// See All / genre directory: GenrePage.qml, GenreIndex.qml and ContinueSeeAllPage.qml
// are represented by native-issued routes and sections; the web never constructs a Route.
(function (CW) {
  'use strict';
  const { h } = CW;

  const label = route => route.title || (route.route.source === 'genreIndex' ? 'Explore Genres' : 'See All');
  const kicker = route => {
    const r = route.route;
    return [r.world === 'all' ? 'Colosseum' : r.world, r.source === 'genreIndex' ? 'Genres'
      : r.source === 'genre' ? 'Genre' : r.source === 'continue' ? 'Continue' : 'Catalogue'].join(' · ');
  };

  CW.router.register('seeall', {
    mount(el, route, env) {
      const head = h('header.page-head.seeall-head', {},
        h('span.seeall-kicker', {}, kicker(route)),
        h('h1', {}, label(route)));
      const pane = h('div.world-pane.seeall-pane');
      el.append(head, pane);
      let sub = null;
      let view = null;
      const ctx = {
        open: env.open,
        forget: env.forget,
        seeAll: env.seeAll,
        act: env.act,
        choose: (choice, section) => env.choose(choice, section, null, patch => {
          view = { ...(view || {}), ...patch };
          subscribe();
        }),
        more: section => env.more(sub, section)
      };
      function onEvent(ev) {
        CW.section.sync(pane, ev, ctx);
        // GenrePage.qml:88-157 washes its first covers behind the genre title.
        // The native result supplies those covers; no source identity is inspected.
        if (route.route.source !== 'genre') return;
        const section = ev.sections.find(s => s.items && s.items.length);
        const art = section && section.items.find(it => it.cover);
        head.style.setProperty('--seeall-art', art ? `url("${art.cover.replaceAll('"', '%22')}")` : 'none');
      }
      function subscribe() {
        if (sub) sub.close();
        pane.replaceChildren();
        sub = env.port.subscribe('seeAll', view ? { route: route.route, view } : { route: route.route }, onEvent);
      }
      subscribe();
      return {
        update(next) {
          if (JSON.stringify(next.route) === JSON.stringify(route.route) && next.title === route.title) return;
          route = next;
          view = null;
          head.querySelector('h1').textContent = label(route);
          head.querySelector('.seeall-kicker').textContent = kicker(route);
          head.style.removeProperty('--seeall-art');
          subscribe();
        },
        unmount() { if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
