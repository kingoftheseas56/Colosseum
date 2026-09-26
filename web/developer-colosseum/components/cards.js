// components/cards.js — the shared item cards (Portico geometry). Surfaces use these; they never copy them.
// Every card is a <button data-focus data-key> so the focus engine and focus restore work everywhere.
(function (CW) {
  'use strict';
  const { h, face } = CW;

  const subline = it => it.subtitle || [it.year, it.rating != null ? `★ ${Number(it.rating).toFixed(1)}` : null]
    .filter(Boolean).join(' · ');

  /** CataloguePosterCard — gallery profile. ctx.open(item, intent). v2.3: progress bar + native ⋮ menu. */
  function poster(it, ctx) {
    const card = posterCard(it, ctx);
    if (!Array.isArray(it.menu) || !it.menu.length) return card;
    const more = h('button.pcm', { type: 'button', 'data-focus': true, 'data-key': it.key + '#menu',
                                   'aria-label': `More for ${it.title}`, 'aria-haspopup': 'menu' }, '⋮');
    more.addEventListener('click', e => { e.stopPropagation(); openMenu(it, more, ctx); });
    return h('div.pcw', {}, card, more);
  }

  function posterCard(it, ctx) {
    const theatre = it.world === 'Theatre';
    const hoverLine = theatre
      ? (it.rating != null ? `★ ${Number(it.rating).toFixed(1)}` : '')
      : subline(it);
    return h('button.pc', { type: 'button', 'data-focus': true, 'data-key': it.key, 'aria-label': it.title,
                            onclick: () => ctx.open(it, it.primary || 'details') },   // v2.2 §14.1
      h('span.art', {}, face(it.cover, it.title),
        h('span.rev', {}, h('b', {}, it.title), hoverLine ? h('span', {}, hoverLine) : null),
        it.badge ? h('span.badge', {}, it.badge) : null,
        it.progress != null ? h('span.bar', {}, h('i', { style: { width: Math.round(it.progress * 100) + '%' } })) : null),
      h('span.cap', {}, it.title),
      !theatre && subline(it) ? h('span.sub', {}, subline(it)) : null);
  }

  /** ContinueTile — resume on Enter; progress bar; forget via ctx.forget (context affordance). */
  function continueTile(it, ctx) {
    const pct = Math.round((it.progress || 0) * 100);
    return h('button.ct', { type: 'button', 'data-focus': true, 'data-key': it.key,
                            'aria-label': `Resume ${it.title}${it.subtitle ? ', ' + it.subtitle : ''}`,
                            onclick: () => ctx.open(it, it.primary || 'resume'),
                            oncontextmenu: e => { e.preventDefault(); ctx.forget && ctx.forget(it); } },
      face(it.cover, it.title),
      h('span.shade'),
      h('span.lbl', {}, it.title),
      it.subtitle ? h('span.sub', {}, it.subtitle) : null,
      h('span.play', { 'aria-hidden': true }, playIcon()),
      h('span.bar', {}, h('i', { style: { width: pct + '%' } })),
      h('span.frame'));
  }

  /** List row — for layout:"list" sections. */
  function row(it, ctx) {
    return h('button.lr', { type: 'button', 'data-focus': true, 'data-key': it.key, onclick: () => ctx.open(it, it.primary || 'details') },
      h('span.lr-art', {}, face(it.cover, it.title)),
      h('span.lr-copy', {}, h('b', {}, it.title), subline(it) ? h('span', {}, subline(it)) : null),
      it.progress != null ? h('span.lr-bar', {}, h('i', { style: { width: Math.round(it.progress * 100) + '%' } })) : null);
  }

  /** Featured slide — one item of a layout:"hero" section. */
  function slide(it, ctx, kicker) {
    // a universe's cover is its wordmark, never its background
    const bg = it.kind === 'universe' ? it.backdrop : (it.backdrop || it.cover);
    const art = h('img.art', { alt: '', decoding: 'async', src: bg || '' });
    art.addEventListener('load', () => art.classList.add('on'));
    art.addEventListener('error', () => art.remove());
    return h('div.slide', { 'data-key': it.key },
      bg ? art : null,
      h('div.wash'),
      h('div.copy', {},
        kicker ? h('span.kicker', {}, kicker) : null,
        it.kind === 'universe' && it.cover ? logo(it) : h('h3', {}, it.title),
        subline(it) ? h('p', {}, subline(it)) : null,
        h('div.btns', {},
          h('button.b-gold', { type: 'button', 'data-focus': true, 'data-key': it.key + '#open',
                               onclick: () => ctx.open(it, it.progress != null ? 'resume' : 'details') },
            it.kind === 'universe' ? 'Enter' : it.progress != null ? 'Resume' : 'Open'))));
  }

  /** Universe wordmark; falls back to the title in Fraunces if the logo fails (Cosmere has none). */
  function logo(it) {
    const img = h('img.logo', { alt: it.title, decoding: 'async', src: it.cover });
    img.addEventListener('error', () => img.replaceWith(h('h3', {}, it.title)));
    return img;
  }

  /** The native-issued card menu (§15.1): focus is scoped to it; Escape or a pick closes it and returns focus. */
  function openMenu(it, anchor, ctx) {
    const old = document.querySelector('.cardmenu');
    if (old) old.__close();
    const r = anchor.getBoundingClientRect();
    const menu = h('div.cardmenu', { role: 'menu', 'data-focus-scope': true });
    const close = () => { menu.remove(); document.removeEventListener('mousedown', outside, true); anchor.focus({ preventScroll: true }); };
    const outside = e => { if (!menu.contains(e.target)) close(); };
    menu.__close = close;
    it.menu.forEach(m => menu.appendChild(h('button.cmi' + (m.warn ? '.warn' : ''), {
      type: 'button', role: 'menuitem', 'data-focus': true, 'data-key': it.key + '#m:' + m.key,
      onclick: () => {
        close();
        const t = m.target;
        if (t.intent) ctx.open(it, t.intent);
        else if (ctx.act) ctx.act(t.act, { item: it, ...(t.payload || {}) });
      } }, m.label)));
    document.body.appendChild(menu);
    const w = menu.offsetWidth, hgt = menu.offsetHeight;
    menu.style.left = Math.max(8, Math.min(innerWidth - w - 8, r.right - w)) + 'px';
    menu.style.top = Math.max(8, Math.min(innerHeight - hgt - 8, r.bottom + 6)) + 'px';
    document.addEventListener('mousedown', outside, true);
    const first = menu.querySelector('[data-focus]'); if (first) first.focus({ preventScroll: true });
  }

  function playIcon() {
    const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    s.setAttribute('viewBox', '0 0 24 24');
    s.innerHTML = '<path d="M8 5v14l11-7z" fill="#f7f7f5"/>';
    return s;
  }

  CW.cards = { poster, continueTile, row, slide };
})(window.CW = window.CW || {});
