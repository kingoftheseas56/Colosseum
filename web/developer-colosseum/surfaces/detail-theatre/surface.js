// Theatre detail. All titles, unit ids, marks and source picks come from the native feed.
(function (CW) {
  'use strict';
  const { h } = CW;

  CW.router.register('detail.theatre', {
    mount(el, route, env) {
      const box = h('div.world-pane.dt-page');
      el.appendChild(box);
      let sub = null;
      let params = route.params || {};
      let closed = false;
      let primaryTarget = params.type === 'movie' ? params.id : '';
      let seasonScope = '';
      const episodeWindows = new Map();

      const act = (verb, extra) => env.act('detail.theatre.' + verb, { id: params.id, ...(extra || {}) });
      const row = (label, value) => h('div.dt-fact', {}, h('span', {}, label), h('b', {}, value));
      const showSources = episodeId => {
        if (!episodeId) return env.toast('No episode is available to play.');
        return act('loadSources', { episodeId }).then(result => {
          if (!result || !result.ok) return;
          const source = box.querySelector('[data-section="sources"]');
          if (source) source.scrollIntoView({ block: 'nearest' });
          const first = source && source.querySelector('[data-focus]');
          if (first) first.focus({ preventScroll: true });
        });
      };

      function render(section) {
        const data = section.data || {};
        if (data.schema === 'theatre.hero') {
          return h('div.dt-hero', {},
            h('div.dt-back', {}, CW.face(data.banner || data.cover, data.title || 'Theatre')),
            h('div.dt-poster', {}, CW.face(data.cover, data.title || 'Theatre')),
            h('div.dt-hero-copy', {},
              h('span.dt-kicker', {}, data.type === 'series' ? 'SERIES' : 'FILM'),
              h('h1', {}, data.title || 'Untitled'),
              h('p.dt-hero-meta', {}, [data.year, data.rating, data.runtime].filter(Boolean).join('  ·  ')),
              data.genres && data.genres.length ? h('p.dt-genres', {}, data.genres.join('  ·  ')) : null,
              data.synopsis ? h('p.dt-synopsis', {}, data.synopsis) : null,
              h('div.dt-actions', {},
                h('button.dt-primary', { type: 'button', 'data-focus': true, 'data-key': 'theatre.play',
                  onclick: () => showSources(primaryTarget) }, data.primaryLabel || 'Watch'),
                h('button.dt-secondary', { type: 'button', 'data-focus': true, 'data-key': 'theatre.collection',
                  'aria-pressed': !!data.saved, onclick: () => act('collection', {
                    saved: !data.saved, title: data.title, cover: data.cover, notify: !!data.notify
                  }) }, data.saved ? 'In Collection' : 'Add to Collection'),
                data.saved && data.type === 'series' ? h('button.dt-secondary', {
                  type: 'button', 'data-focus': true, 'data-key': 'theatre.notify',
                  'aria-pressed': !!data.notify, onclick: () => act('collection', {
                    saved: true, title: data.title, cover: data.cover, notify: !data.notify
                  }) }, data.notify ? 'Notifications on' : 'Notifications off') : null)));
        }
        if (data.schema === 'theatre.facts')
          return (data.rows || []).length ? h('div.dt-facts', {}, data.rows.map(x => row(x.label, x.value)))
            : CW.section.note('No facts yet', null);
        if (data.schema === 'theatre.seasons') {
          const nextScope = data.selected + ':' + data.order;
          if (nextScope !== seasonScope) { seasonScope = nextScope; episodeWindows.clear(); }
          const choices = (data.rows || []).map(season =>
            h('button.dt-season' + (season.number === data.selected ? '.on' : ''), {
              type: 'button', 'data-focus': true, 'data-key': 'theatre.season.' + season.number,
              'aria-pressed': season.number === data.selected,
              onclick: () => act('selectSeason', { season: season.number, order: data.order })
            }, season.label, h('small', {}, season.count)));
          if (data.absoluteAvailable) choices.push(h('button.dt-season' + (data.order === 'absolute' ? '.on' : ''), {
            type: 'button', 'data-focus': true, 'data-key': 'theatre.absolute',
            'aria-pressed': data.order === 'absolute',
            onclick: () => act('selectSeason', { season: data.selected,
              order: data.order === 'absolute' ? 'seasons' : 'absolute' })
          }, 'Absolute order'));
          return choices.length ? h('div.dt-season-row', {}, choices) : CW.section.note('No seasons', 'This title has no episode list.');
        }
        if (data.schema === 'theatre.episodes') {
          if (params.type === 'series') primaryTarget = data.nextUpId || (data.rows || [])[0]?.id || '';
          episodeWindows.set(Number(data.windowStart) || 0, data.rows || []);
          const allRows = [...episodeWindows.keys()].sort((a, b) => a - b)
            .flatMap(start => episodeWindows.get(start));
          const lines = allRows.map(episode => h('div.dt-episode', {},
            h('span.dt-episode-art', {}, CW.face(episode.thumbnail, episode.title || 'Episode')),
            h('div.dt-episode-copy', {},
              h('span.dt-episode-index', {}, 'S' + episode.season + ' · E' + episode.displayNumber),
              h('b', {}, episode.title || 'Episode ' + episode.displayNumber),
              episode.overview ? h('p', {}, episode.overview) : null,
              h('small', {}, [episode.airDate, episode.duration].filter(Boolean).join(' · '))),
            h('div.dt-episode-actions', {},
              episode.progress > 0 ? h('span.dt-progress', {
                role: 'progressbar', 'aria-valuenow': Math.round(episode.progress * 100),
                'aria-valuemin': '0', 'aria-valuemax': '100'
              }, h('i', { style: { width: Math.round(episode.progress * 100) + '%' } })) : null,
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.play',
                onclick: () => showSources(episode.id) }, 'Play'),
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.sources',
                onclick: () => showSources(episode.id) }, 'Sources'),
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.watched',
                'aria-pressed': !!episode.watched,
                onclick: () => act('markWatched', { episodeId: episode.id, watched: !episode.watched }) },
                episode.watched ? 'Watched' : 'Mark watched'),
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.download',
                onclick: () => showSources(episode.id) }, 'Download'))));
          if (!lines.length) return CW.section.note('No episodes', 'No episode list is available for this title.');
          return h('div.dt-episodes', {}, lines,
            section.hasMore ? h('button.dt-more', { type: 'button', 'data-focus': true,
              'data-key': 'theatre.episodes.more', onclick: () => sub && env.more(sub, section) }, 'More episodes') : null);
        }
        if (data.schema === 'theatre.cast')
          return (data.people || []).length ? h('div.dt-cast', {}, data.people.map(person => h('div.dt-person', {},
            CW.face(person.image, person.name), h('b', {}, person.name), h('small', {}, person.role))))
            : CW.section.note('No cast yet', null);
        if (data.schema === 'theatre.sources')
          return (data.rows || []).length ? h('div.dt-sources', {}, data.targetId ? h('p', {}, 'Sources for ' + data.targetId) : null,
            (data.rows || []).map(source => h('div.dt-source', {},
              h('b', {}, source.label),
              h('small', {}, [source.quality, source.provider, source.availability].filter(Boolean).join(' · ')),
              h('div.dt-source-actions', {},
                h('button.dt-small', { type: 'button', 'data-focus': true,
                  'data-key': 'theatre.source.' + source.key + '.play',
                  onclick: () => act('play', { episodeId: data.targetId, sourceKey: source.key }) }, 'Play'),
                h('button.dt-small', { type: 'button', 'data-focus': true,
                  'data-key': 'theatre.source.' + source.key + '.download',
                  onclick: () => act('download', { episodeId: data.targetId, sourceKey: source.key }) }, 'Download')))))
            : CW.section.note('No sources', 'Choose Play to resolve a playable source.');
        return CW.section.note('Nothing here yet', null);
      }

      const ctx = { open: env.open, act: env.act, toast: env.toast, custom: render,
        more: section => sub ? env.more(sub, section) : Promise.resolve({ ok: false }) };
      function subscribe() {
        if (sub) sub.close();
        sub = env.port.subscribe('detail.theatre', params, ev => {
          if (ev.type === 'reset') episodeWindows.clear();
          CW.section.sync(box, ev, ctx);
        });
      }
      subscribe();
      return {
        update(next) {
          if (closed) return;
          const previous = JSON.stringify(params);
          params = next.params || {};
          if (JSON.stringify(params) !== previous) { episodeWindows.clear(); subscribe(); }
        },
        unmount() { closed = true; if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
