// SearchSurface.qml: runSearch(), recent queries, genre browse and Surprise me are native feed/Choice data.
// BiblioSearch.qml's field-led composition and Top Match are presented with shared cards and sections.
(function (CW) {
  'use strict';
  const { h } = CW;
  const placeholder = scope => scope === 'Tankoban' ? 'Search manga…'
    : scope === 'Theatre' ? 'Search movies & series…'
    : scope === 'Biblio' ? 'Search books & audiobooks…' : 'Search…';

  CW.router.register('search', {
    mount(el, route, env) {
      const field = h('input', { type: 'search', autocomplete: 'off', placeholder: 'Search…',
        'aria-label': 'Search', 'data-focus': true, 'data-key': 'search#query' });
      const head = h('header.search-head', {}, h('label.sfield', {}, field));
      const pane = h('div.world-pane.search-pane');
      el.append(head, pane);
      let sub = null;
      let timer = 0;
      let generation = 0;
      let scope = route.scope || 'all';
      field.placeholder = placeholder(scope);
      const ctx = {
        open: env.open,
        forget: env.forget,
        seeAll: env.seeAll,
        act: env.act,
        choose: (choice, section) => env.choose(choice, section, query => {
          field.value = query;
          run();
          field.focus({ preventScroll: true });
        }),
        removeChoice: (choice, section) => env.removeChoice(choice, section).then(result => {
          if (result && result.ok && !field.value.trim()) run();
        }),
        more: section => env.more(sub, section)
      };
      function onEvent(ev) {
        CW.section.sync(pane, ev, ctx);
        pane.classList.toggle('search-idle', !field.value.trim());
        const first = pane.querySelector('[data-layout="grid"][data-state="ready"]');
        if (first) first.classList.add('search-top-match');
      }
      function cancel() {
        clearTimeout(timer);
        generation++;
        if (sub) { sub.close(); sub = null; }
        pane.replaceChildren();
      }
      function run() {
        cancel();
        const current = generation;
        sub = env.port.subscribe('search', { scope, query: field.value.trim() }, ev => {
          if (current === generation) onEvent(ev);
        });
      }
      field.value = route.query || '';
      run();
      field.addEventListener('input', () => { cancel(); timer = setTimeout(run, 180); });
      field.addEventListener('change', run);
      requestAnimationFrame(() => field.focus({ preventScroll: true }));
      return {
        update(next) {
          const nextScope = next.scope || 'all';
          const nextQuery = next.query || '';
          if (nextScope === scope && nextQuery === field.value) return;
          scope = nextScope;
          field.value = nextQuery;
          field.placeholder = placeholder(scope);
          run();
        },
        unmount() { cancel(); }
      };
    }
  });
})(window.CW = window.CW || {});
