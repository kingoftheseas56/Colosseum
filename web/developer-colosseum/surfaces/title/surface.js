// Media-neutral title page. Theatre is the first feed; units and scores stay feed-owned.
(function (CW) {
  'use strict';
  const { h } = CW;

  CW.router.register('detail.theatre', {
    mount(el, route, env) {
      const box = h('div.world-pane.dt-page.title-page');
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
      let synopsisExpanded = false;
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

      const iconAction = (key, label, glyph, pressed, onclick) =>
        h('button.title-action', { type: 'button', 'data-focus': true,
          'data-key': 'theatre.' + key, 'aria-label': label, title: label,
          'aria-pressed': pressed ? 'true' : 'false', onclick },
          h('span.title-action-glyph', { 'aria-hidden': 'true' }, glyph),
          h('span.title-action-label', { 'aria-hidden': 'true' }, label));

      const identityArt = data => {
        const name = h('h1.title-name', {}, data.title || 'Untitled');
        if (!data.logo) return name;
        const logo = h('img.title-logo', { src: data.logo, alt: data.title || 'Title',
          decoding: 'async', onload: () => logo.parentElement?.classList.add('loaded'),
          onerror: () => logo.remove() });
        return h('div.title-identity', {}, name, logo);
      };

      function render(section) {
        const data = section.data || {};
        if (data.schema === 'theatre.hero') {
          heroSynopsis = data.synopsis || '';
          const kind = data.kind || (String(data.id || '').startsWith('mal:') ? 'anime' : data.type);
          const wallpaper = location.protocol === 'qrc:'
            ? 'qrc:/developer-webui/cold-ripple.jpg' : '../../assets/wallpaper/cold-ripple.jpg';
          const art = h('img.title-backdrop-art', { src: data.banner || data.cover || wallpaper,
            alt: '', decoding: 'async', onerror: event => {
              if (event.target.getAttribute('src') !== wallpaper) event.target.src = wallpaper;
              else event.target.remove();
            } });
          const scores = Array.isArray(data.scores) ? data.scores : [];
          return h('div.title-hero', {},
            h('div.title-backdrop', { 'aria-hidden': 'true',
              style: { backgroundImage: `url(${JSON.stringify(wallpaper)})`, backgroundSize: 'cover' } }, art),
            h('div.title-hero-copy', {},
              h('span.title-kicker', {}, kind === 'anime' ? 'ANIME  /  THEATRE'
                : kind === 'series' ? 'SERIES  /  THEATRE' : 'FILM  /  THEATRE'),
              identityArt(data),
              h('div.title-pills', {},
                data.year ? h('span.title-pill', {}, data.year) : null,
                scores.length ? h('span.title-score-group', { 'aria-label': 'Scores' },
                  scores.map(score => h('span.title-score', { 'data-provider': score.provider,
                    'aria-label': `${score.provider === 'mal' ? 'MyAnimeList' : 'IMDb'} ${score.value} out of ${score.scale}` },
                    h('b.title-score-mark', {}, score.provider === 'mal' ? 'MAL' : 'IMDb'),
                    h('span', {}, String(score.value))))) : null,
                data.runtime ? h('span.title-pill', {}, data.runtime) : null,
                (data.genres || []).slice(0, 3).map(genre => h('span.title-pill', {}, genre))),
              h('div.title-actions', {},
                (data.type === 'movie' || data.primaryTargetId || primaryTarget)
                  ? h('button.title-play', { type: 'button', 'data-focus': true,
                    'data-key': 'theatre.play', onclick: () => showSources(data.primaryTargetId || primaryTarget) },
                    h('span', { 'aria-hidden': 'true' }, '▶'), data.primaryLabel || heroWatchLabel) : null,
                iconAction('collection', data.saved ? 'Remove from Library' : 'Add to Library',
                  data.saved ? '▣' : '▢', !!data.saved, () => act('collection', {
                    saved: !data.saved, title: data.title, cover: data.cover, notify: !!data.notify })),
                h('span.title-action.title-rate-pending', { 'aria-hidden': 'true', title: 'Rate' },
                  h('span.title-action-glyph', {}, '☆')),
                iconAction('markWatched', data.watchedMark === 1 ? 'Mark unwatched' : 'Mark watched',
                  data.watchedMark === 1 ? '✓' : '○', data.watchedMark === 1,
                  () => act('markWatched', { watched: data.watchedMark !== 1 })),
                iconAction('download', 'Download', '↓', false,
                  () => showSources(data.primaryTargetId || primaryTarget)),
                data.type === 'series' && data.saved
                  ? iconAction('notify', data.notify ? 'Turn notifications off' : 'Notify me',
                    data.notify ? '♬' : '♩', !!data.notify,
                    () => act('collection', { saved: true, title: data.title,
                      cover: data.cover, notify: !data.notify })) : null),
              heroSynopsis ? h('div.title-synopsis', {},
                h('p' + (synopsisExpanded ? '.expanded' : ''), {}, heroSynopsis),
                heroSynopsis.length > 220 ? h('button.title-synopsis-more', { type: 'button',
                  'data-focus': true, 'data-key': 'theatre.synopsis',
                  onclick: () => { synopsisExpanded = !synopsisExpanded;
                    box.querySelector('[data-section="hero"]')?.replaceWith(
                      CW.section.render(section, ctx)); } },
                  synopsisExpanded ? 'Show less' : 'Read more') : null) : null));
        }
        if (data.schema === 'theatre.facts')
          return h('div.dt-facts', {},
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
            heroWatchLabel = heroEpisode ? 'Play S' + heroEpisode.season + ' E' + heroEpisode.displayNumber : 'Play';
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
            synopsisExpanded = false;
            subscribe();
          }
        },
        unmount() { closed = true; if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
