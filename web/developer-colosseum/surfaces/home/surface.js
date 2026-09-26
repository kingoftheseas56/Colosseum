// Home keeps Main.qml:3290-3450's universe, Continue, and four intro sections in native order.
// The feed owns the data and doors; this surface only presents the sections.
(function (CW) {
  'use strict';
  const { h } = CW;

  CW.router.register('home', {
    mount(el, route, env) {
      const pane = h('div.world-pane.home-pane');
      el.appendChild(pane);
      let sub = null;
      const ctx = {
        open: env.open,
        forget: env.forget,
        seeAll: env.seeAll,
        act: env.act,
        choose: (choice, section) => env.choose(choice, section),
        more: section => env.more(sub, section)
      };
      function onEvent(ev) {
        const focused = document.activeElement && document.activeElement.getAttribute('data-key');
        CW.section.sync(pane, ev, ctx);
        // Main.qml:3391-3438: each intro widget opens its world as well as showing its titles.
        for (const world of ['Tankoban', 'Theatre', 'Biblio']) {
          const header = pane.querySelector(`[data-section="home.${world.toLowerCase()}"] .wh`);
          if (!header || header.querySelector('.home-enter')) continue;
          const enter = h('button.more.home-enter', {
            type: 'button', 'data-focus': true, 'data-key': `home.${world.toLowerCase()}#enter`,
            onclick: () => env.router.go({ name: 'world', world })
          }, `Enter ${world}`, h('span.ch', { 'aria-hidden': true }, '›'));
          header.insertBefore(enter, header.querySelector('.more'));
        }
        if (focused && focused.endsWith('#enter')) {
          const target = [...pane.querySelectorAll('.home-enter')]
            .find(button => button.getAttribute('data-key') === focused);
          if (target) target.focus({ preventScroll: true });
        }
      }
      sub = env.port.subscribe('home', {}, onEvent);
      return { unmount() { sub.close(); } };
    }
  });
})(window.CW = window.CW || {});
