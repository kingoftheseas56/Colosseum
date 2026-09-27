// Comic detail. Native owns provenance, acquisition, progress and every reader handoff.
(function (CW) {
  'use strict';
  const { h } = CW;

  CW.router.register('detail.comic', {
    mount(el, route, env) {
      const box = h('div.world-pane.dc-page');
      el.appendChild(box);
      let sub = null;
      let params = route.params || {};
      let closed = false;
      let queryTimer = 0;
      let releaseScope = '';
      const releaseWindows = new Map();

      const act = (verb, extra) =>
        env.act('detail.comic.' + verb, { id: params.id, ...(extra || {}) });

      function subscribe() {
        if (sub) sub.close();
        sub = env.port.subscribe('detail.comic', params, ev => {
          if (ev.type === 'reset') releaseWindows.clear();
          CW.section.sync(box, ev, ctx);
        });
      }

      function applyView(patch) {
        params = { ...params, view: { ...(params.view || {}), ...(patch || {}) } };
        releaseWindows.clear();
        subscribe();
      }

      function navigate(targetId) {
        return act('navigate', { targetId }).then(result => {
          const next = result && result.ok && result.result && result.result.route;
          if (next) env.router.go(next);
          return result;
        });
      }

      function statusText(row) {
        const state = row.downloadState || 'none';
        if (state === 'done') return row.readingProgress > 0
          ? 'Downloaded · ' + Math.round(row.readingProgress * 100) + '% read'
          : 'Downloaded';
        if (state === 'resolving') return 'Resolving link…';
        if (state === 'queued') return 'Queued…';
        if (state === 'downloading') return 'Downloading…';
        if (state === 'extracting') return 'Extracting pages…';
        if (state === 'error') return 'Failed · try again';
        if (!row.available) return 'Not available from this source';
        return [row.year, row.sizeMB ? row.sizeMB + ' MB' : ''].filter(Boolean).join(' · ');
      }

      function seriesHero(data) {
        const meta = [data.releaseCount ? data.releaseCount + ' releases' : '',
          data.publisher || '', data.sourceLabel || ''].filter(Boolean).join(' · ');
        const actions = [
          data.resumeUnitId ? h('button.dc-primary', {
            type: 'button', 'data-focus': true, 'data-key': 'comic.resume',
            onclick: () => act('read', { unitId: data.resumeUnitId })
          }, 'Continue', data.resumeUnitLabel ? h('small', {}, data.resumeUnitLabel) : null) : null,
          h('button.dc-secondary', {
            type: 'button', 'data-focus': true, 'data-key': 'comic.collection',
            'aria-pressed': !!data.saved,
            onclick: () => act('collection', { saved: !data.saved })
          }, data.saved ? 'In Collection' : 'Add to Collection')
        ];
        return h('div.dc-hero', {},
          h('div.dc-back', {}, CW.face(data.cover, data.title || 'Comics')),
          h('div.dc-cover', {}, CW.face(data.cover, data.title || 'Comics')),
          h('div.dc-intro', {},
            h('span.dc-kicker', {}, 'WESTERN COMICS · TANKOBAN'),
            h('h1', {}, data.title || 'Untitled'),
            meta ? h('p.dc-meta', {}, meta) : null,
            data.synopsis ? h('p.dc-synopsis', {}, data.synopsis) : null,
            h('div.dc-actions', {}, actions)));
      }

      function filterControl(data) {
        const input = h('input.dc-filter-input', {
          type: 'search', autocomplete: 'off', placeholder: 'Search releases…',
          value: data.query || '', 'data-focus': true, 'data-key': 'comic.filter',
          'aria-label': 'Search releases'
        });
        input.addEventListener('input', () => {
          clearTimeout(queryTimer);
          queryTimer = setTimeout(() => applyView({ query: input.value }), 220);
        });
        return h('div.dc-filter', {},
          h('label', {}, input),
          data.query ? h('span', {}, data.matchCount + (data.matchCount === 1 ? ' match' : ' matches')) : null);
      }

      function releaseRows(section, data) {
        const scope = data.scope || '';
        if (scope !== releaseScope) {
          releaseScope = scope;
          releaseWindows.clear();
        }
        releaseWindows.set(Number(data.windowStart) || 0, data.rows || []);
        const rows = [...releaseWindows.keys()].sort((a, b) => a - b)
          .flatMap(start => releaseWindows.get(start));
        if (!rows.length) return CW.section.note('No releases here', 'Try a different search.');

        const groups = new Map();
        rows.forEach(row => {
          const label = row.group || 'Releases';
          if (!groups.has(label)) groups.set(label, []);
          groups.get(label).push(row);
        });

        const tables = [...groups.entries()].map(([label, groupRows]) =>
          h('div.dc-table', {},
            h('div.dc-table-head', {}, h('span', {}, label.toUpperCase()), h('b', {}, groupRows.length)),
            groupRows.map(row => {
              const state = row.downloadState || 'none';
              const busy = ['resolving', 'queued', 'downloading', 'extracting'].includes(state);
              return h('div.dc-release', { 'data-key': 'comic.row.' + row.id },
                h('span.dc-thumb', {}, CW.face(row.cover, row.title || '#')),
                h('div.dc-release-copy', {},
                  h('b', {}, row.title || 'Untitled release'),
                  h('small', {}, statusText(row)),
                  row.description ? h('p', {}, row.description) : null,
                  row.readingProgress > 0 ? h('span.dc-progress', {
                    role: 'progressbar', 'aria-valuemin': '0', 'aria-valuemax': '100',
                    'aria-valuenow': Math.round(row.readingProgress * 100)
                  }, h('i', { style: { width: Math.round(row.readingProgress * 100) + '%' } })) : null),
                h('div.dc-release-actions', {},
                  h('button.dc-small', {
                    type: 'button', 'data-focus': true,
                    'data-key': 'comic.unit.' + row.id + '.read',
                    disabled: !row.available || busy ? true : null,
                    onclick: () => act('read', { unitId: row.id })
                  }, state === 'done' ? 'Read' : busy ? 'Working…' : row.available ? 'Read' : 'Unavailable'),
                  state !== 'done' && row.available ? h('button.dc-small', {
                    type: 'button', 'data-focus': true,
                    'data-key': 'comic.unit.' + row.id + '.download',
                    disabled: busy ? true : null,
                    onclick: () => act('download', { unitId: row.id })
                  }, busy ? 'Working…' : 'Download') : null));
            })));
        return h('div.dc-release-list', {}, tables,
          section.hasMore ? h('button.dc-more', {
            type: 'button', 'data-focus': true, 'data-key': 'comic.releases.more',
            onclick: () => sub && env.more(sub, section)
          }, 'More releases') : null);
      }

      function archiveHero(data, kicker) {
        const bits = [data.seriesCount ? data.seriesCount + ' series archives' : '',
          data.count ? data.count + ' releases' : ''].filter(Boolean);
        return h('div.dc-simple-hero', {},
          h('span.dc-kicker', {}, kicker),
          h('h1', {}, data.title || 'Comics'),
          data.subtitle ? h('p', {}, data.subtitle) : null,
          bits.length ? h('p.dc-meta', {}, bits.join(' · ')) : null);
      }

      function cardGrid(rows, kind) {
        if (!(rows || []).length)
          return CW.section.note('Nothing here yet', 'No matching comic archives are available.');
        return h('div.dc-grid', {}, rows.map(row =>
          h('button.dc-card', {
            type: 'button', 'data-focus': true, 'data-key': kind + '.' + row.id,
            onclick: () => navigate(row.id)
          },
          h('span.dc-card-art', {}, CW.face(row.cover, row.title || 'Comics')),
          h('b', {}, row.title || 'Untitled'),
          row.count ? h('small', {}, row.count + ' releases') : row.year ? h('small', {}, row.year) : null)));
      }

      function render(section) {
        const data = section.data || {};
        if (data.schema === 'comic.hero') return seriesHero(data);
        if (data.schema === 'comic.filter') return filterControl(data);
        if (data.schema === 'comic.releases') return releaseRows(section, data);
        if (data.schema === 'comic.archiveHero')
          return archiveHero(data, 'GETCOMICS · TANKOBAN');
        if (data.schema === 'comic.archiveBoxes')
          return cardGrid(data.rows, 'comic.archive');
        if (data.schema === 'comic.archiveIndexHero')
          return archiveHero(data, 'WESTERN COMICS · TANKOBAN');
        if (data.schema === 'comic.archiveSeries')
          return cardGrid(data.rows, 'comic.archive.series');
        if (data.schema === 'comic.publisherHero')
          return archiveHero(data, 'LOCG · TANKOBAN');
        if (data.schema === 'comic.publisherSeries') {
          const grid = cardGrid(data.rows, 'comic.publisher.series');
          if (!section.hasMore) return grid;
          return h('div', {}, grid, h('button.dc-more', {
            type: 'button', 'data-focus': true, 'data-key': 'comic.publisher.more',
            onclick: () => sub && env.more(sub, section)
          }, 'More series'));
        }
        return CW.section.note('Nothing here yet', null);
      }

      const ctx = {
        open: env.open,
        act: env.act,
        toast: env.toast,
        custom: render,
        more: section => sub ? env.more(sub, section) : Promise.resolve({ ok: false }),
        choose: (choice, section) => env.choose(choice, section, null, patch => applyView(patch))
      };

      subscribe();
      return {
        update(next) {
          if (closed) return;
          const previous = JSON.stringify(params);
          params = next.params || {};
          if (JSON.stringify(params) !== previous) {
            releaseWindows.clear();
            subscribe();
          }
        },
        unmount() {
          closed = true;
          clearTimeout(queryTimer);
          if (sub) sub.close();
        }
      };
    }
  });
})(window.CW = window.CW || {});
