// components/section.js — renders one CONTRACT §3.3 Section in any layout and all four states, and keeps a
// container of sections in sync with port events (only the changed section's DOM is replaced).
//
// ctx = { open(item, intent), forget?(item), seeAll?(section), more?(section) }
(function (CW) {
  'use strict';
  const { h, cards } = CW;

  function header(s, ctx) {
    if (!s.title) return null;
    return h('div.wh', {},
      h('h2', {}, s.title),
      s.headerAction && ctx.act
        ? h('button.more', { type: 'button', 'data-focus': true, 'data-key': s.id + '#action',
                             onclick: () => ctx.act(s.headerAction.action, {}) },
            s.headerAction.label, h('span.ch', { 'aria-hidden': true }, '›'))
        : null,
      s.seeAll && ctx.seeAll
        ? h('button.more', { type: 'button', 'data-focus': true, 'data-key': s.id + '#all', onclick: () => ctx.seeAll(s) },
            'See all', h('span.ch', { 'aria-hidden': true }, '›'))
        : null);
  }

  function skeleton(layout) {
    const n = layout === 'grid' ? 12 : 7;
    const cell = () => h('span.pc.skel', { 'aria-hidden': true }, h('span.art'), h('span.bar2'));
    return layout === 'grid' ? h('div.grid', {}, Array.from({ length: n }, cell))
                             : h('div.rail', {}, Array.from({ length: n }, cell));
  }

  function note(title, text, kind) {
    return h('div.empty-state' + (kind ? '.' + kind : ''), { role: kind === 'err' ? 'alert' : null },
      h('div', {}, h('b', {}, title), text ? h('span', {}, text) : null));
  }

  function moreButton(s, ctx) {
    if (!s.hasMore || !ctx.more) return null;
    return h('button.loadmore', { type: 'button', 'data-focus': true, 'data-key': s.id + '#more', onclick: () => ctx.more(s) },
      'Load more');
  }

  function carousel(s, ctx) {
    let at = 0;
    const slides = s.items.map(it => cards.slide(it, ctx, s.title));
    const track = h('div.track', {}, slides);
    const dots = h('div.dots', {}, s.items.map((it, i) =>
      h('button.dot', { type: 'button', tabindex: '-1', 'aria-label': `Show ${it.title}`, onclick: () => show(i) })));
    function show(i) {
      at = (i + slides.length) % slides.length;
      track.style.transform = `translateX(${-100 * at}%)`;
      slides.forEach((el, k) => { el.inert = k !== at; });
      [...dots.children].forEach((d, k) => d.classList.toggle('on', k === at));
    }
    const el = h('div.car', { 'data-arrows': true }, track, slides.length > 1 ? dots : null);
    show(0);
    // Left/Right on the focused slide pages the carousel; at either end the focus engine takes over.
    el.addEventListener('cw-arrow', e => {
      const step = e.detail === 'right' ? 1 : e.detail === 'left' ? -1 : 0;
      if (!step || at + step < 0 || at + step >= slides.length) return;
      e.preventDefault();
      show(at + step);
      const btn = slides[at].querySelector('[data-focus]');
      if (btn) btn.focus({ preventScroll: true });
    });
    if (slides.length > 1) {
      const timer = setInterval(() => { if (!el.isConnected) return clearInterval(timer);
                                        if (!el.contains(document.activeElement)) show(at + 1); }, 8000);
    }
    return el;
  }

  // v1.3 choices: pills (recent queries, genres) or tiles (genre index). ctx.choose(choice, section).
  function choices(s, ctx) {
    const tiles = s.layout === 'tiles';
    return h(tiles ? 'div.gm' : 'div.chips', {}, s.choices.map(c => {
      const pick = h(tiles ? 'button.gt' : 'button.chip', { type: 'button', 'data-focus': true, 'data-key': c.key,
                                                           onclick: () => ctx.choose && ctx.choose(c, s) },
        tiles && c.art ? CW.face(c.art, '') : null,
        h(tiles ? 'span.n' : 'span', {}, c.label),
        c.sublabel ? h(tiles ? 'span.c' : 'span.cs', {}, c.sublabel) : null);
      if (!c.removable || !ctx.removeChoice) return pick;
      return h('span.chipwrap', {}, pick,
        h('button.chipx', { type: 'button', 'data-focus': true, 'data-key': c.key + '#remove', 'aria-label': `Remove ${c.label}`,
                            onclick: () => ctx.removeChoice(c, s) }, '×'));
    }));
  }

  function body(s, ctx) {
    if (s.state === 'loading') return skeleton(s.layout);
    if (s.state === 'error') return note('Couldn’t load this', s.error || 'Colosseum could not load this section.', 'err');
    if (s.layout === 'custom') return ctx.custom ? ctx.custom(s) : note('Not drawn yet', `No renderer for ${s.data ? s.data.schema : s.id}.`);
    if (s.choices && s.choices.length) return choices(s, ctx);
    if (s.state === 'empty' || !s.items.length) return note('Nothing here yet', s.error || null);
    switch (s.layout) {
      case 'hero': return carousel(s, ctx);
      case 'continue': return h('div.rail-wrap', {}, h('div.rail', {}, s.items.map(it => cards.continueTile(it, ctx)), moreButton(s, ctx)));
      case 'grid': return h('div', {}, h('div.grid', {}, s.items.map(it => cards.poster(it, ctx))), moreButton(s, ctx));
      case 'list': return h('div.list', {}, s.items.map(it => cards.row(it, ctx)), moreButton(s, ctx));
      default: return h('div.rail-wrap', {}, h('div.rail', {}, s.items.map(it => cards.poster(it, ctx)), moreButton(s, ctx)));
    }
  }

  function render(s, ctx) {
    return h('section.widget', { 'data-section': s.id, 'data-layout': s.layout, 'data-state': s.state,
                                 'aria-busy': s.state === 'loading' ? 'true' : null },
      s.layout === 'hero' && !s.headerAction ? null : header(s, ctx), body(s, ctx));
  }

  /** Apply a port event to a container: full rebuild on reset, single-section swap otherwise. */
  function sync(container, ev, ctx) {
    CW.focus.preserve(container, () => {
      if (ev.type === 'reset' || !ev.changed) {
        container.replaceChildren(...ev.sections.map(s => render(s, ctx)));
        return;
      }
      const old = container.querySelector(`[data-section="${CSS.escape(ev.changed)}"]`);
      const s = ev.sections.find(x => x.id === ev.changed);
      if (!s) { if (old) old.remove(); return; }
      const fresh = render(s, ctx);
      if (old) old.replaceWith(fresh); else container.appendChild(fresh);
      // keep DOM order == index order (a moved section arrives as a section event with a new index)
      ev.sections.forEach(x => { const n = container.querySelector(`[data-section="${CSS.escape(x.id)}"]`); if (n) container.appendChild(n); });
    });
  }

  CW.section = { render, sync, note };
})(window.CW = window.CW || {});
