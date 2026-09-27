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
      let heroWatchLabel = 'Watch';
      let seasonScope = '';
      let heroSynopsis = '';
      let seasonCount = 0;
      let hasSeasons = false;
      let castExpanded = false;
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
          heroSynopsis = data.synopsis || '';
          return h('div.dt-hero', {},
            data.banner || data.cover ? h('div.dt-back', {}, CW.face(data.banner || data.cover, data.title || 'Theatre')) : null,
            h('div.dt-hero-copy', {},
              h('span.dt-kicker', {}, data.type === 'series' ? 'SERIES - THEATRE' : 'MOVIE - THEATRE'),
              h('h1', {}, data.title || 'Untitled'),
              h('div.dt-hero-meta', {},
                data.year ? h('b', {}, data.year) : null,
                data.genres && data.genres.length ? h('span', {}, data.genres.join(' - ')) : null,
                data.rating ? h('span.dt-rating', {}, h('i', {}, 'IMDb'), data.rating) : null,
                data.runtime ? h('span', {}, data.runtime) : null),
              h('div.dt-actions', {},
                (data.type === 'movie' || primaryTarget) ? h('button.dt-primary', {
                  type: 'button', 'data-focus': true, 'data-key': 'theatre.play',
                  onclick: () => showSources(primaryTarget) }, heroWatchLabel) : null,
                h('button.dt-secondary', { type: 'button', 'data-focus': true, 'data-key': 'theatre.collection',
                  'aria-pressed': !!data.saved, onclick: () => act('collection', {
                    saved: !data.saved, title: data.title, cover: data.cover, notify: !!data.notify
                  }) }, data.saved ? '✓  In Library' : '+  Library'),
                data.saved && data.type === 'series' ? h('button.dt-secondary', {
                  type: 'button', 'data-focus': true, 'data-key': 'theatre.notify',
                  'aria-pressed': !!data.notify, onclick: () => act('collection', {
                    saved: true, title: data.title, cover: data.cover, notify: !data.notify
                  }) }, data.notify ? 'Notifications on' : 'Notifications off') : null)));
        }
        if (data.schema === 'theatre.facts')
          return h('div.dt-facts', {},
            heroSynopsis ? h('p.dt-synopsis', {}, heroSynopsis) : null,
            (data.rows || []).length ? h('div.dt-fact-list', {}, data.rows.map(x => row(x.label, x.value))) : null);
        if (data.schema === 'theatre.seasons') {
          hasSeasons = !!(data.rows || []).length;
          seasonCount = (data.rows || []).find(x => x.number === data.selected)?.count || 0;
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
          return choices.length ? h('div.dt-season-row', {}, choices) : h('div.dt-season-empty');
        }
        if (data.schema === 'theatre.episodes') {
          if (!hasSeasons && !(data.rows || []).length) return h('div.dt-episodes-empty', { 'aria-label': 'No episode list is available for this title.' });
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
              h('span.dt-status', {}, episode.id === data.nextUpId ? 'NEXT UP'
                : episode.watched ? 'WATCHED'
                : episode.progress > 0.01 ? Math.round(episode.progress * 100) + '% WATCHED'
                : episode.airDate ? 'AVAILABLE' : 'UPCOMING'),
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.play',
                onclick: () => showSources(episode.id) }, 'Play'),
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.sources',
                onclick: () => showSources(episode.id) }, 'Sources'),
              h('button.dt-small', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.download',
                onclick: () => showSources(episode.id) }, 'Download'))));
          const ledger = h('div.dt-ledger', {},
            h('h2', {}, data.season === 0 ? 'Specials' : 'Season ' + data.season),
            h('small', {}, seasonCount + ' episodes'),
            h('div.dt-ledger-head', {}, h('span', {}, 'EPISODE'), h('span', {}, 'STORY'), h('span', {}, 'STATUS')));
          return h('div.dt-episodes', {}, ledger, lines,
            section.hasMore ? h('button.dt-more', { type: 'button', 'data-focus': true,
              'data-key': 'theatre.episodes.more', onclick: () => sub && env.more(sub, section) }, 'More episodes') : null);
        }
        if (data.schema === 'theatre.cast') {
          const people = data.people || [];
          if (!people.length) return h('div.dt-cast-empty');
          const initials = name => String(name || '').trim().split(/\s+/).map((x,i,a) => i===0 || i===a.length-1 ? x[0] : '').join('').toUpperCase();
          const draw = () => h('div.dt-cast', {}, h('span.dt-cast-label', {}, 'CAST'),
            h('div.dt-cast-flow', {}, people.slice(0, castExpanded ? people.length : 8).map(person =>
              h('div.dt-person', {}, h('span.dt-avatar', {}, person.image ? CW.face(person.image, person.name) : initials(person.name)),
                h('b', {}, person.name), person.role ? h('small', {}, person.role) : null)),
              !castExpanded && people.length > 8 ? h('button.dt-person.dt-cast-more', {
                type: 'button', 'data-focus': true, 'data-key': 'theatre.cast.more',
                onclick: () => { castExpanded = true; box.querySelector('.dt-cast')?.replaceWith(draw()); }
              }, h('span.dt-avatar', {}, '›'), h('b', {}, 'All cast')) : null));
          return draw();
        }
        if (data.schema === 'theatre.sources')
          return !data.targetId ? h('div.dt-sources-closed') : (data.rows || []).length ? h('div.dt-sources', {}, h('p', {}, 'Sources for ' + data.targetId),
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
          const ep = ev.sections.find(s => s.id === 'episodes');
          if (ep && ep.data && params.type === 'series') {
            primaryTarget = ep.data.nextUpId || (ep.data.rows || [])[0]?.id || '';
            const heroEpisode = (ep.data.rows || []).find(x => x.id === primaryTarget);
            heroWatchLabel = heroEpisode ? 'Watch  S' + heroEpisode.season + ' · E' + heroEpisode.displayNumber : 'Watch';
          }
          CW.section.sync(box, ev, ctx);
          if (ev.type === 'section' && ev.changed === 'episodes' && ep)
            CW.section.sync(box, { type: 'section', changed: 'hero', sections: ev.sections }, ctx);
        });
      }
      subscribe();
      return {
        update(next) {
          if (closed) return;
          const previous = JSON.stringify(params);
          params = next.params || {};
          if (JSON.stringify(params) !== previous) {
            episodeWindows.clear();
            primaryTarget = params.type === 'movie' ? params.id : '';
            heroWatchLabel = 'Watch';
            heroSynopsis = '';
            seasonCount = 0;
            hasSeasons = false;
            castExpanded = false;
            subscribe();
          }
        },
        unmount() { closed = true; if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
