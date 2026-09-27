// W2-2 Universes — Hall of Worlds + every web-owned universe detail.
(function (CW) {
  'use strict';
  const { h, face } = CW;
  const ONE_PIECE = 'com.colosseum.universe.onepiece';

  function goSection(id) {
    const node = document.querySelector('[data-section="' + CSS.escape(id) + '"]');
    if (node) node.scrollIntoView({ block: 'start' });
  }

  function scopeFocus(node, scope) {
    const focusables = [];
    if (node && node.matches && node.matches('[data-focus]')) focusables.push(node);
    if (node && node.querySelectorAll) focusables.push(...node.querySelectorAll('[data-focus]'));
    focusables.forEach((el, i) => el.setAttribute('data-key', scope + ':' + (el.getAttribute('data-key') || i)));
    return node;
  }

  function lockedCard(row) {
    return h('div.ulg-locked', { 'aria-disabled': 'true' },
      h('span.ulg-art', {}, face(row.cover || '', row.title || '')),
      h('span.ulg-copy', {},
        h('b', {}, row.title || 'Untitled'),
        row.subtitle ? h('span', {}, row.subtitle) : null));
  }

  function hall(s, ctx) {
    const d = s.data || {};
    if (!s.items.length) return CW.section.note('No universes installed',
      'Install a universe extension and it will appear here.');
    return h('div.uhall', {},
      h('div.uhall-head', {}, h('span.eyebrow', {}, 'THE COLLECTION'),
        h('div.uhall-title', {}, h('h1', {}, 'Hall of Worlds'),
          h('span', {}, String(d.count || s.items.length) + ' universes'))),

      h('div.uhall-ledger', {}, s.items.map((it, i) =>
        h('button.uhall-row', { type: 'button', 'data-focus': true, 'data-key': it.key,
                                onclick: () => ctx.open(it, 'details') },
          h('span.uhall-num', {}, String(i + 1).padStart(2, '0')),
          h('span.uhall-art', {}, face(it.backdrop || it.cover, it.title)),
          h('span.uhall-name', {}, it.title),
          h('span.uhall-enter', {}, 'Enter the universe',
            h('span', { 'aria-hidden': true }, ' →'))))));
  }

  function hero(s, ctx) {
    const d = s.data || {};
    const bg = h('div.uhero-art', { 'aria-hidden': true });
    if (d.banner) bg.style.backgroundImage = 'url("' + String(d.banner).replace(/"/g, '%22') + '")';
    const primary = s.items[0];
    return h('div.uhero.' + (d.template || 'generic'), {}, bg, h('div.uhero-wash'),
      h('div.uhero-copy', {},
        h('span.eyebrow', {}, d.kicker || 'UNIVERSE'),
        h('h1', {}, d.name || 'Universe'),
        d.metaline ? h('p.uhero-meta', {}, d.metaline) : null,
        d.blurb ? h('p.uhero-blurb', {}, d.blurb) : null,
        d.blurbSource ? h('p.uhero-source', {}, d.blurbSource) : null,
        primary ? h('button.uhero-primary', { type: 'button', 'data-focus': true,
                    'data-key': s.id + ':primary:' + primary.key, onclick: () => ctx.open(primary, 'details') },
                    d.primaryLabel || 'Begin here',
                    h('span', { 'aria-hidden': true }, ' →')) : null));
  }

  function dcauNav(entries) {
    const track = h('div.udcau-track', {}, entries.map((e, i) =>
      h('button.udcau-portal', { type: 'button', 'data-focus': true,
          'data-key': 'dcau:' + (e.key || i), onclick: () => goSection(e.targetId) },
        h('span.udcau-art', {}, face(e.art || '', e.label || '')),
        h('span.udcau-shade'),
        h('span.udcau-caption', {},
          h('span.udcau-index', {}, String(i + 1).padStart(2, '0')),
          h('b', {}, e.label || '')))));
    const walk = h('div.udcau-walk', {}, track);
    const scroll = dir => walk.scrollBy({ left: dir * walk.clientWidth * .82, behavior: 'smooth' });
    return h('div.udcau-stage', {},
      walk,
      h('button.udcau-arrow.left', { type: 'button', 'data-focus': true,
          'data-key': 'dcau#previous', 'aria-label': 'Previous portal', onclick: () => scroll(-1) }, '‹'),
      h('button.udcau-arrow.right', { type: 'button', 'data-focus': true,
          'data-key': 'dcau#next', 'aria-label': 'Next portal', onclick: () => scroll(1) }, '›'));
  }

  function cosmereNav(d, entries) {
    return h('div.ucosmere-map', {},
      h('div.ucosmere-map-head', {},
        h('span.eyebrow', {}, d.title || 'THE COGNITIVE ATLAS'),
        h('h2', {}, d.subtitle || 'Six systems. One underlying light.')),
      h('div.ucosmere-field', {},
        h('span.ucosmere-core', { 'aria-hidden': true }),
        entries.map((e, i) =>
          h('button.ucosmere-node.n' + i, { type: 'button', 'data-focus': true,
              'data-key': 'cosmere:' + (e.key || i), onclick: () => goSection(e.targetId) },
            h('span.ucosmere-dot', { 'aria-hidden': true }),
            h('b', {}, e.label || ''),
            e.sublabel ? h('span', {}, e.sublabel) : null))));
  }

  function galaxyNav(entries) {
    const sun = entries[0];
    const gates = entries.slice(1);
    return h('div.ugalaxy', {},
      h('div.ugalaxy-stars', { 'aria-hidden': true }),
      h('h2', {}, 'In a galaxy far far away'),
      sun ? h('button.ugalaxy-sun', { type: 'button', 'data-focus': true,
          'data-key': 'galaxy:' + sun.key, onclick: () => goSection(sun.targetId) },
          h('span.ugalaxy-sun-core', { 'aria-hidden': true }),
          h('b', {}, sun.label || 'SKYWALKER SAGA'),
          sun.sublabel ? h('span', {}, sun.sublabel) : null) : null,
      h('div.ugalaxy-orbits', { 'aria-hidden': true }),
      gates.map((e, i) =>
        h('button.ugalaxy-gate.g' + i, { type: 'button', 'data-focus': true,
            'data-key': 'galaxy:' + (e.key || i), onclick: () => goSection(e.targetId) },
          e.art ? h('span.ugalaxy-art', {}, face(e.art, e.label || '')) : h('span.ugalaxy-node'),
          h('b', {}, e.label || ''),
          e.sublabel ? h('span', {}, e.sublabel) : null)));
  }

  function nav(s) {
    const d = s.data || {};
    const entries = Array.isArray(d.entries) ? d.entries : [];
    if (!entries.length) return CW.section.note('No destinations', null);
    if (d.template === 'dcau') return dcauNav(entries);
    if (d.template === 'cosmere') return cosmereNav(d, entries);
    if (d.template === 'galaxy') return galaxyNav(entries);
    return h('div.unav.' + (d.template || 'generic'), {},
      h('div.unav-head', {},
        d.title ? h('h2', {}, d.title) : null,
        d.subtitle ? h('p', {}, d.subtitle) : null),
      h('div.unav-grid', {}, entries.map((e, i) =>
        h('button.unav-node', { type: 'button', 'data-focus': true,
                                'data-key': 'unav:' + (e.key || i),
                                onclick: () => goSection(e.targetId) },
          e.art ? h('span.unav-art', {}, face(e.art, e.label || '')) : null,
          h('span.unav-index', {}, String(i + 1).padStart(2, '0')),
          h('span.unav-label', {}, e.label || 'Destination'),
          e.sublabel ? h('span.unav-sub', {}, e.sublabel) : null))));
  }

  function dcauGroup(s, ctx, d) {
    const tankoban = d.label === 'Tankoban';
    return h('div.udcau-shelf', {},
      h('h2', {}, d.label || ''),
      d.note ? h('p', {}, d.note) : null,
      h('div.' + (tankoban ? 'udcau-tank-rail' : 'udcau-theatre-rail'), {},
        s.items.map((it, i) => tankoban
          ? h('button.udcau-tank', { type: 'button', 'data-focus': true,
              'data-key': s.id + ':' + i + ':' + it.key, onclick: () => ctx.open(it, 'details') },
              h('span.udcau-tank-art', {}, face(it.cover, it.title)),
              h('b', {}, it.title))
          : h('button.udcau-theatre-card', { type: 'button', 'data-focus': true,
              'data-key': s.id + ':' + i + ':' + it.key, onclick: () => ctx.open(it, 'details') },
              h('span.udcau-poster-frame', {}, face(it.cover, it.title)),
              h('b', {}, it.title),
              it.year ? h('span', {}, String(it.year)) : null))));
  }

  function cosmereGroup(s, ctx, d) {
    return h('div.ucosmere-group', {},
      h('div.ucosmere-group-head', {},
        h('h2', {}, d.label || ''),
        d.note ? h('span', {}, d.note) : null),
      h('div.ucosmere-gates', {}, s.items.map((it, i) =>
        h('button.ucosmere-book', { type: 'button', 'data-focus': true,
            'data-key': s.id + ':' + i + ':' + it.key, onclick: () => ctx.open(it, 'details') },
          h('span.ucosmere-cover', {}, face(it.cover, it.title)),
          h('span.ucosmere-book-copy', {},
            it.badge ? h('span', {}, it.badge) : null,
            h('b', {}, it.title),
            h('small', {}, 'OPEN IN BIBLIO  →'))))));
  }

  function group(s, ctx) {
    const d = s.data || {};
    const locked = Array.isArray(d.locked) ? d.locked : [];
    if (d.pending) return h('div.ugroup-pending', { 'aria-busy': 'true' },
      Array.from({ length: 5 }, () => h('span.ugroup-skeleton', { 'aria-hidden': 'true' })));
    if (!s.items.length && !locked.length)
      return CW.section.note('Nothing here yet', d.note || null);
    if (d.template === 'dcau') return dcauGroup(s, ctx, d);
    if (d.template === 'cosmere') return cosmereGroup(s, ctx, d);
    const cards = s.items.map((it, i) => scopeFocus(CW.cards.poster(it, ctx), s.id + ':' + i));
    locked.forEach(row => cards.push(lockedCard(row)));
    return h('div.ugroup.' + (d.variant || 'rail'), {},
      h('div.ugroup-head', {},
        d.ordinal != null ? h('span.ugroup-ordinal', {}, String(d.ordinal).padStart(2, '0')) : null,
        h('div', {}, h('h2', {}, d.label || s.title || 'Works'),

          d.note ? h('p', {}, d.note) : null)),
      h(d.variant === 'wall' ? 'div.ugroup-wall' : 'div.ugroup-rail', {}, cards));
  }

  function itemFor(s, key) {
    return (s.items || []).find(it => it.key === key) || null;
  }

  function starters(s, ctx) {
    const d = s.data || {};
    const entries = Array.isArray(d.entries) ? d.entries : [];
    return h('div.ustarters', {},
      h('span.eyebrow', {}, d.title || 'BEGIN HERE'),
      h('div.ustarters-map', {}, entries.map((e, i) => {
        const item = itemFor(s, e.itemKey);
        const body = [
          e.short ? h('span.ustarter-short', {}, e.short) : null,
          h('b', {}, e.label || ''),
          e.note ? h('span.ustarter-note', {}, e.note) : null
        ];
        return item
          ? h('button.ustarter', { type: 'button', 'data-focus': true,
              'data-key': s.id + ':starter:' + (e.key || i), onclick: () => ctx.open(item, 'details') }, body)
          : h('div.ustarter.unavailable', { 'aria-disabled': 'true' }, body);
      })));
  }

  function duality(s, ctx) {
    const d = s.data || {};
    const side = (name, key, label, sub) => {
      const item = itemFor(s, key);
      const body = [h('span.eyebrow', {}, label || name), h('b', {}, sub || '')];
      return item
        ? h('button.udual-side', { type: 'button', 'data-focus': true,
            'data-key': s.id + ':duality:' + name, onclick: () => ctx.open(item, 'details') }, body)
        : h('div.udual-side.unavailable', { 'aria-disabled': 'true' }, body);
    };
    return h('div.uduality', {},
      side('left', d.leftKey, d.leftLabel, d.leftSub),
      h('div.udual-mark', { 'aria-hidden': 'true' }, '×'),
      side('right', d.rightKey, d.rightLabel, d.rightSub));
  }

  function eras(s, ctx) {
    const d = s.data || {};
    const columns = Array.isArray(d.columns) ? d.columns : [];
    return h('div.ueras', {},
      h('span.eyebrow', {}, d.kicker || 'THE ERAS'),
      h('div.ueras-grid', {}, columns.map((column, ci) =>
        h('div.uera-column', {},
          h('h2', {}, column.label || ''),
          (column.itemKeys || []).map((key, ri) => {
            const item = itemFor(s, key);
            return item ? scopeFocus(CW.cards.row(item, ctx), s.id + ':' + ci + ':' + ri)
              : h('div.uera-missing', { 'aria-hidden': 'true', 'data-key': 'era:' + ci + ':' + ri });
          })))),
      d.comicKey && itemFor(s, d.comicKey)
        ? h('div.uera-comic', {}, scopeFocus(CW.cards.poster(itemFor(s, d.comicKey), ctx), s.id + ':comic')) : null);
  }

  function custom(s, ctx) {
    const schema = s.data && s.data.schema;
    if (schema === 'universes.hall') return hall(s, ctx);
    if (schema === 'universes.hero') return hero(s, ctx);
    if (schema === 'universes.nav') return nav(s);
    if (schema === 'universes.group') return group(s, ctx);
    if (schema === 'universes.starters') return starters(s, ctx);
    if (schema === 'universes.duality') return duality(s, ctx);
    if (schema === 'universes.eras') return eras(s, ctx);
    return CW.section.note('Universe data is unavailable',
      schema ? 'No renderer for ' + schema + '.' : 'This universe returned no page record.', 'err');
  }

  function mountFeed(el, env, feed, params) {
    const box = h('div.universe-pane');
    el.appendChild(box);
    let sub = null;
    const ctx = {
      open: env.open, act: env.act, forget: env.forget, seeAll: env.seeAll,
      choose: env.choose, removeChoice: env.removeChoice,
      more: section => env.more(sub, section),
      custom: section => custom(section, ctx)
    };
    sub = env.port.subscribe(feed, params, ev => CW.section.sync(box, ev, ctx));
    return { box, close: () => sub.close() };
  }

  const hallSurface = {
    mount(el, route, env) {

      const view = mountFeed(el, env, 'page.universeHall', {});
      return { unmount: view.close };
    }
  };

  const detailSurface = {
    mount(el, route, env) {
      let view = null;
      let note = null;
      const show = r => {
        if (view) { view.close(); view.box.remove(); view = null; }
        if (note) { note.remove(); note = null; }
        const params = r.params || {};
        if (params.extensionId === ONE_PIECE) {
          note = CW.section.note('One Piece stays native',
            'The shared open seam must hand this universe back to its native atlas.', 'err');
          el.appendChild(note);
          return;
        }
        view = mountFeed(el, env, 'detail.universe', params);
      };
      show(route);
      return {
        update: r => show(r),
        unmount: () => { if (view) view.close(); if (note) note.remove(); }
      };
    }
  };

  CW.router.register('page.universeHall', hallSurface);
  CW.router.register('detail.universe', detailSurface);
})(window.CW = window.CW || {});
