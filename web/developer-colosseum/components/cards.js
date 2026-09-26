// components/cards.js — the shared item cards (Portico geometry). Surfaces use these; they never copy them.
// Every card is a <button data-focus data-key> so the focus engine and focus restore work everywhere.
(function (CW) {
  'use strict';
  const { h, face } = CW;

  const subline = it => it.subtitle || [it.year, it.rating != null ? `★ ${Number(it.rating).toFixed(1)}` : null]
    .filter(Boolean).join(' · ');

  /** CataloguePosterCard — gallery profile. ctx.open(item, intent) */
  function poster(it, ctx) {
    return h('button.pc', { type: 'button', 'data-focus': true, 'data-key': it.key, 'aria-label': it.title,
                            onclick: () => ctx.open(it, 'details') },
      h('span.art', {}, face(it.cover, it.title),
        h('span.rev', {}, h('b', {}, it.title), subline(it) ? h('span', {}, subline(it)) : null),
        it.badge ? h('span.badge', {}, it.badge) : null),
      h('span.cap', {}, it.title),
      subline(it) ? h('span.sub', {}, subline(it)) : null);
  }

  /** ContinueTile — resume on Enter; progress bar; forget via ctx.forget (context affordance). */
  function continueTile(it, ctx) {
    const pct = Math.round((it.progress || 0) * 100);
    return h('button.ct', { type: 'button', 'data-focus': true, 'data-key': it.key,
                            'aria-label': `Resume ${it.title}${it.subtitle ? ', ' + it.subtitle : ''}`,
                            onclick: () => ctx.open(it, 'resume'),
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
    return h('button.lr', { type: 'button', 'data-focus': true, 'data-key': it.key, onclick: () => ctx.open(it, 'details') },
      h('span.lr-art', {}, face(it.cover, it.title)),
      h('span.lr-copy', {}, h('b', {}, it.title), subline(it) ? h('span', {}, subline(it)) : null),
      it.progress != null ? h('span.lr-bar', {}, h('i', { style: { width: Math.round(it.progress * 100) + '%' } })) : null);
  }

  /** Featured slide — one item of a layout:"hero" section. */
  function slide(it, ctx, kicker) {
    const art = h('img.art', { alt: '', decoding: 'async', src: it.backdrop || it.cover || '' });
    art.addEventListener('load', () => art.classList.add('on'));
    art.addEventListener('error', () => art.remove());
    return h('div.slide', { 'data-key': it.key },
      (it.backdrop || it.cover) ? art : null,
      h('div.wash'),
      h('div.copy', {},
        kicker ? h('span.kicker', {}, kicker) : null,
        h('h3', {}, it.title),
        subline(it) ? h('p', {}, subline(it)) : null,
        h('div.btns', {},
          h('button.b-gold', { type: 'button', 'data-focus': true, 'data-key': it.key + '#open',
                               onclick: () => ctx.open(it, it.progress != null ? 'resume' : 'details') },
            it.progress != null ? 'Resume' : 'Open'))));
  }

  function playIcon() {
    const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    s.setAttribute('viewBox', '0 0 24 24');
    s.innerHTML = '<path d="M8 5v14l11-7z" fill="#f7f7f5"/>';
    return s;
  }

  CW.cards = { poster, continueTile, row, slide };
})(window.CW = window.CW || {});
