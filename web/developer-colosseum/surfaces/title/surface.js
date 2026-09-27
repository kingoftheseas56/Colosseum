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
      let episodeOrder = 'seasons';
      let heroSynopsis = '';
      let seasonCount = 0;
      let hasSeasons = false;
      let seasonData = null;
      let lastEpisodeSection = null;
      let castExpanded = false;
      let synopsisExpanded = false;
      let seasonMenuOpen = false;
      let sourcesOpen = false;
      let sourceMode = 'play';
      let seasonDownloadNumber = -1;
      let sourceOpener = '';
      let lastSourcesSection = null;
      const relatedTrail = [];
      let pendingRelatedFocus = null;
      const episodeWindows = new Map();

      const act = (verb, extra) => env.act('detail.theatre.' + verb, { id: params.id, ...(extra || {}) });
      const restoreRelatedFocus = () => {
        if (!pendingRelatedFocus || params.id !== pendingRelatedFocus.id) return;
        const poster = box.querySelector(`[data-key="${CSS.escape(pendingRelatedFocus.key)}"]`);
        if (!poster) return;
        const board = document.getElementById('board');
        if (board) board.scrollTop = pendingRelatedFocus.scrollTop;
        poster.focus({ preventScroll: true });
        pendingRelatedFocus = null;
      };
      const openRelated = (item, intent) => {
        const key = document.activeElement?.getAttribute('data-key');
        if (!key) return env.open(item, intent);
        const board = document.getElementById('board');
        relatedTrail.push({ id: params.id, key, scrollTop: board?.scrollTop || 0 });
        return env.open(item, intent).then(result => {
          if (!result?.ok) relatedTrail.pop();
          return result;
        });
      };
      const closeSources = () => {
        sourcesOpen = false;
        if (lastSourcesSection) box.querySelector('[data-section="sources"]')
          ?.replaceWith(CW.section.render(lastSourcesSection, ctx));
        if (sourceOpener) box.querySelector(`[data-key="${CSS.escape(sourceOpener)}"]`)
          ?.focus({ preventScroll: true });
      };
      const showSources = (episodeId, mode = 'play') => {
        if (!episodeId) return env.toast('No episode is available to play.');
        sourceMode = mode;
        sourceOpener = document.activeElement?.getAttribute('data-key') || '';
        sourcesOpen = true;
        return act('loadSources', { episodeId, intent: mode }).then(result => {
          if (!result || !result.ok) { closeSources(); return; }
          if (result.result?.openedPlayback) { sourcesOpen = false; return; }
          const source = box.querySelector('[data-section="sources"]');
          const first = source && ((sourceMode === 'season'
            ? source.querySelector('[data-key="theatre.season.auto"]') : null)
            || source.querySelector(`[data-key$=".${sourceMode}"]`)
            || source.querySelector('[data-focus]'));
          if (first) first.focus({ preventScroll: true });
        });
      };

      const redrawEpisodes = focusKey => {
        if (!lastEpisodeSection) return;
        box.querySelector('[data-section="episodes"]')?.replaceWith(CW.section.render(lastEpisodeSection, ctx));
        if (focusKey) box.querySelector(`[data-key="${CSS.escape(focusKey)}"]`)
          ?.focus({ preventScroll: true });
      };
      const seasonPicker = () => {
        const data = seasonData;
        if (!data?.rows?.length) return null;
        const active = data.rows.find(x => x.number === data.selected);
        const label = data.order === 'absolute' ? 'All episodes' : active?.label || 'Choose season';
        const selectOrder = order => {
          seasonMenuOpen = false;
          redrawEpisodes('theatre.season.trigger');
          if (order !== data.order) act('selectSeason', { season: data.selected, order });
        };
        const selectSeason = season => {
          seasonMenuOpen = false;
          redrawEpisodes('theatre.season.trigger');
          act('selectSeason', { season, order: 'seasons' });
        };
        const trigger = h('button.title-season-trigger', { type: 'button', 'data-focus': true,
          'data-key': 'theatre.season.trigger', 'aria-expanded': seasonMenuOpen ? 'true' : 'false',
          onclick: () => {
            seasonMenuOpen = !seasonMenuOpen;
            redrawEpisodes(seasonMenuOpen ? 'theatre.season.mode.' + data.order : 'theatre.season.trigger');
          } }, label, h('span', { 'aria-hidden': 'true' }, seasonMenuOpen ? '⌃' : '⌄'));
        if (!seasonMenuOpen) return h('div.title-season-select', {}, trigger);
        const previous = box.querySelector('.title-season-trigger')?.getBoundingClientRect()
          || { top: 16, bottom: 56, right: window.innerWidth - 16 };
        const below = window.innerHeight - previous.bottom - 16;
        const above = previous.top - 16;
        const up = below < 240 && above > below;
        const menuStyle = {
          right: `${Math.max(16, window.innerWidth - previous.right)}px`,
          maxHeight: up ? `${Math.min(604, Math.max(120, above - 16))}px`
            : `min(31.45vw, 604px, calc(100vh - ${Math.round(previous.bottom + 24)}px))`
        };
        if (up) menuStyle.bottom = `${window.innerHeight - previous.top + 8}px`;
        else menuStyle.top = `${previous.bottom + 8}px`;
        const modes = [{ key: 'seasons', label: 'Aired' }];
        if (data.absoluteAvailable) modes.push({ key: 'absolute', label: 'Absolute' });
        const menu = h('div.title-season-menu', { 'data-focus-scope': true, style: menuStyle,
          role: 'dialog', 'aria-label': 'Episode order and season' },
          h('div.title-season-modes', {}, modes.map(mode =>
            h('button.title-season-mode' + (data.order === mode.key ? '.on' : ''), {
              type: 'button', 'data-focus': true, 'data-key': 'theatre.season.mode.' + mode.key,
              'aria-pressed': data.order === mode.key ? 'true' : 'false',
              onclick: () => selectOrder(mode.key) }, mode.label))),
          h('div.title-season-columns', {}, h('span', {}, 'SEASON'), h('span', {}, 'EPISODES')),
          h('div.title-season-options', {}, data.order === 'absolute'
            ? h('button.title-season-option.on', { type: 'button', 'data-focus': true,
              'data-key': 'theatre.season.all', onclick: () => selectOrder('absolute') },
              h('span.title-season-option-copy', {}, h('span', {}, 'All episodes')),
              h('span', {}, data.totalCount || ''))
            : data.rows.map(season => h('button.title-season-option' +
              (season.number === data.selected ? '.on' : ''), {
                type: 'button', 'data-focus': true, 'data-key': 'theatre.season.' + season.number,
                'aria-selected': season.number === data.selected ? 'true' : 'false',
                onclick: () => selectSeason(season.number)
              }, h('span.title-season-option-copy', {}, h('span', {}, season.label),
                season.from && season.to ? h('small', {}, season.from + ' – ' + season.to) : null),
              h('span', {}, season.count)))));
        menu.__close = () => { seasonMenuOpen = false; redrawEpisodes('theatre.season.trigger'); };
        return h('div.title-season-select', {}, trigger, menu);
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
                  () => showSources(data.primaryTargetId || primaryTarget, 'download')),
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
          return (data.rows || []).length ? h('div.title-information', {},
            h('h2', {}, 'Information'),
            h('div.title-information-grid', {}, data.rows.map(x =>
              h('div.title-information-fact', {}, h('span', {}, x.label), h('strong', {}, x.value)))))
            : h('div.title-information-empty');
        if (data.schema === 'theatre.seasons') {
          hasSeasons = !!(data.rows || []).length;
          episodeOrder = data.order || 'seasons';
          seasonCount = (data.rows || []).find(x => x.number === data.selected)?.count || 0;
          const nextScope = data.selected + ':' + data.order;
          if (nextScope !== seasonScope) { seasonScope = nextScope; episodeWindows.clear(); }
          seasonData = data;
          return h('div.title-season-feed', { hidden: true });
        }
        if (data.schema === 'theatre.episodes') {
          lastEpisodeSection = section;
          if (!hasSeasons && !(data.rows || []).length) return h('div.dt-episodes-empty', { 'aria-label': 'No episode list is available for this title.' });
          if (params.type === 'series') primaryTarget = data.nextUpId || (data.rows || [])[0]?.id || '';
          episodeWindows.set(Number(data.windowStart) || 0, data.rows || []);
          const allRows = [...episodeWindows.keys()].sort((a, b) => a - b)
            .flatMap(start => episodeWindows.get(start));
          const unit = episode => {
            const next = episode.id === data.nextUpId;
            const started = episode.progress > 0 && episode.progress < 0.85;
            const downloadLabel = episode.downloadState === 'downloaded' ? 'In Vault'
              : episode.downloadState === 'downloading'
                ? `Download ${Math.round((episode.downloadProgress || 0) * 100)}%`
                : episode.downloadState === 'queued' || episode.downloadState === 'resolving'
                  ? 'Download queued' : episode.downloadState === 'failed' ? 'Download failed' : '';
            const stamp = episode.airDate ? new Date(episode.airDate) : null;
            const airDate = stamp && !Number.isNaN(stamp.getTime())
              ? stamp.toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' })
              : episode.airDate;
            const number = episodeOrder === 'absolute'
              ? String(episode.displayNumber).padStart(2, '0')
              : `S${episode.season} E${episode.displayNumber}`;
            return h('article.title-unit' + (next ? '.next' : ''), {
              'data-unit-id': episode.id, 'data-next-up': next ? 'true' : 'false' },
              h('button.title-unit-main', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.episode.' + episode.id + '.play',
                'aria-label': `${started ? 'Resume' : 'Play'} ${episode.title || number}`,
                onclick: () => showSources(episode.id) },
                h('div.title-unit-art.title-unit-art-empty', {}, CW.face(episode.thumbnail, ''),
                  h('span.title-unit-number', {}, episode.displayNumber),
                  episode.watched ? h('span.title-unit-watched', { 'aria-label': 'Watched' }, '✓') : null,
                  next ? h('span.title-unit-next', {}, 'NEXT UP') : null,
                  h('span.title-unit-play-mark', { 'aria-hidden': 'true' }, '▶'),
                  episode.progress > 0 ? h('div.title-unit-progress', { role: 'progressbar',
                    'aria-label': 'Episode progress', 'aria-valuenow': Math.round(episode.progress * 100),
                    'aria-valuemin': '0', 'aria-valuemax': '100' },
                    h('i', { style: { width: Math.round(episode.progress * 100) + '%' } })) : null),
                h('div.title-unit-copy', {},
                  h('h3', {}, episode.title || 'Episode ' + episode.displayNumber),
                  h('p.title-unit-meta', {}, [number, episode.duration, airDate,
                    episode.watched ? 'Watched' : started ? `${Math.round(episode.progress * 100)}% watched` : '',
                    downloadLabel]
                    .filter(Boolean).join('  ·  ')),
                  episode.overview ? h('p.title-unit-overview', {}, episode.overview) : null)),
              h('div.title-unit-actions', {},
                h('button.title-unit-sources', { type: 'button', 'data-focus': true,
                  'data-key': 'theatre.episode.' + episode.id + '.sources',
                  'aria-label': 'Sources for ' + (episode.title || number),
                  onclick: () => showSources(episode.id) }, '◉'),
                h('button.title-unit-download', { type: 'button', 'data-focus': true,
                  'data-key': 'theatre.episode.' + episode.id + '.download',
                  'aria-label': 'Download ' + (episode.title || number),
                  onclick: () => showSources(episode.id, 'download') }, '↓')));
          };
          const ledger = h('div.title-units-heading', {},
            h('h2', {}, 'Episodes'),
            h('div.title-units-tools', {},
              h('span.title-units-count', {}, `${data.totalCount || allRows.length} episodes`),
              episodeOrder !== 'absolute' && allRows.length ? h('button.title-season-download', {
                type: 'button', 'data-focus': true, 'data-key': 'theatre.season.download',
                'aria-label': 'Download season', title: 'Download season',
                onclick: () => {
                  if (allRows.every(x => x.downloadState === 'downloaded'))
                    return env.toast('This season is already in Vault.');
                  seasonDownloadNumber = data.season;
                  showSources((allRows.find(x => x.downloadState !== 'downloaded') || allRows[0]).id, 'season');
                } }, '↓') : null,
              seasonPicker()));
          return h('div.dt-episodes', {}, ledger, allRows.map(unit),
            section.hasMore ? h('button.dt-more', { type: 'button', 'data-focus': true,
              'data-key': 'theatre.episodes.more', onclick: () => sub && env.more(sub, section) }, 'Load more episodes') : null);
        }
        if (data.schema === 'theatre.cast') {
          const people = data.people || [];
          if (!people.length) return h('div.dt-cast-empty');
          const initials = name => String(name || '').trim().split(/\s+/).map((x,i,a) => i===0 || i===a.length-1 ? x[0] : '').join('').toUpperCase();
          const draw = () => h('div.title-cast', {},
            h('div.title-lower-heading', {}, h('h2', {}, 'Cast'), h('small', {}, String(people.length))),
            h('div.title-cast-rail', {}, people.slice(0, castExpanded ? people.length : 8).map(person =>
              h('div.title-person', {},
                h('div.title-person-art', {}, person.image ? CW.face(person.image, person.name)
                  : h('span.title-person-monogram', {}, initials(person.name))),
                h('strong', {}, person.name), person.role ? h('small', {}, person.role) : null)),
              people.length > 8 ? h('button.title-cast-more', {
                type: 'button', 'data-focus': true, 'data-key': 'theatre.cast.more',
                onclick: () => { castExpanded = !castExpanded;
                  box.querySelector('.title-cast')?.replaceWith(draw());
                  box.querySelector('[data-key="theatre.cast.more"]')?.focus({ preventScroll: true }); }
              }, castExpanded ? 'Show less' : 'All cast') : null));
          return draw();
        }
        if (data.schema === 'theatre.sources') {
          lastSourcesSection = section;
          if (!sourcesOpen || !data.targetId) return h('div.dt-sources-closed');
          const sheet = h('div.title-source-sheet', { 'data-focus-scope': true,
            'data-target-id': data.targetId, role: 'dialog', 'aria-modal': 'true',
            'aria-label': 'Choose a source' },
            h('div.title-source-head', {},
              h('div', {}, h('span.title-units-kicker', {}, 'THEATRE'),
                h('h2', {}, sourceMode === 'season' ? 'Download season'
                  : sourceMode === 'download' ? 'Choose a download' : 'Choose a source')),
              h('button.title-source-close', { type: 'button', 'data-focus': true,
                'data-key': 'theatre.sources.close', 'aria-label': 'Close sources', onclick: closeSources }, '×')),
            (data.rows || []).length ? h('div.title-source-list', {}, (data.rows || []).map(source =>
              h('div.title-source-row', {},
                h('div', {}, h('b', {}, source.label),
                  h('small', {}, [source.quality, source.provider, source.availability].filter(Boolean).join(' · '))),
                h('div.title-source-actions', {},
                  h('button.dt-small', { type: 'button', 'data-focus': true,
                    'data-key': 'theatre.source.' + source.key + '.play',
                    onclick: () => act('play', { episodeId: data.targetId, sourceKey: source.key }) }, 'Play'),
                  h('button.dt-small', { type: 'button', 'data-focus': true,
                    'data-key': 'theatre.source.' + source.key + '.download',
                    onclick: () => (sourceMode === 'season'
                      ? act('downloadSeason', { season: seasonDownloadNumber, sourceKey: source.key })
                      : act('download', { episodeId: data.targetId, sourceKey: source.key }))
                      .then(result => { if (result?.ok) closeSources(); }) },
                  sourceMode === 'season' ? 'Use for season' : 'Download')))))
              : h('p.title-source-empty', {}, sourceMode === 'season'
                ? 'No season pack found. Episodes can still be queued individually.'
                : 'No playable sources are available right now.'),
            sourceMode === 'season' ? h('button.title-season-auto', { type: 'button',
              'data-focus': true, 'data-key': 'theatre.season.auto',
              onclick: () => act('downloadSeason', { season: seasonDownloadNumber })
                .then(result => { if (result?.ok) closeSources(); }) },
              'Queue episodes automatically') : null);
          sheet.__close = closeSources;
          return h('div.title-source-overlay', {}, sheet);
        }
        return CW.section.note('Nothing here yet', null);
      }

      const ctx = { open: openRelated, act: env.act, toast: env.toast, custom: render,
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
          if (ev.type === 'section' && ev.changed === 'seasons')
            CW.section.sync(box, { type: 'section', changed: 'episodes', sections: ev.sections }, ctx);
          if (ev.type === 'section' && ev.changed === 'episodes' && ep)
            CW.section.sync(box, { type: 'section', changed: 'hero', sections: ev.sections }, ctx);
          restoreRelatedFocus();
        });
      }
      subscribe();
      return {
        update(next) {
          if (closed) return;
          const previous = JSON.stringify(params);
          params = next.params || {};
          if (JSON.stringify(params) !== previous) {
            if (relatedTrail.length && relatedTrail[relatedTrail.length - 1].id === params.id)
              pendingRelatedFocus = relatedTrail.pop();
            episodeWindows.clear();
            primaryTarget = params.type === 'movie' ? params.id : '';
            heroWatchLabel = 'Watch';
            heroSynopsis = '';
            seasonCount = 0;
            hasSeasons = false;
            seasonData = null;
            lastEpisodeSection = null;
            castExpanded = false;
            synopsisExpanded = false;
            seasonMenuOpen = false;
            sourcesOpen = false;
            seasonDownloadNumber = -1;
            episodeOrder = 'seasons';
            lastSourcesSection = null;
            subscribe();
          }
        },
        unmount() { closed = true; if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
