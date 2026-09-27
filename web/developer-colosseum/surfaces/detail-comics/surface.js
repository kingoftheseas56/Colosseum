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
      let sourceFocusManual = false;
      const releaseWindows = new Map();
      const publisherWindows = new Map();

      const act = (verb, extra) =>
        env.act('detail.comic.' + verb, { id: params.id, ...(extra || {}) });

      function subscribe() {
        if (sub) sub.close();
        sub = env.port.subscribe('detail.comic', params, ev => {
          if (ev.type === 'reset') {
            releaseWindows.clear();
            publisherWindows.clear();
          }
          CW.section.sync(box, ev, ctx);
        });
      }

      function applyView(patch) {
        params = { ...params, view: { ...(params.view || {}), ...(patch || {}) } };
        releaseWindows.clear();
        publisherWindows.clear();
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
        if (state === 'done') {
          if (row.hasReadingProgress && row.readingFinished) return 'Finished';
          if (row.hasReadingProgress) return Math.round((row.readingProgress || 0) * 100) + '% read';
          return '● Downloaded';
        }
        if (state === 'resolving') return 'Resolving link…';
        if (state === 'queued') return 'Queued…';
        if (state === 'downloading') return row.downloadTotal > 0
          ? 'Downloading ' + Math.round(row.downloadDone / row.downloadTotal * 100) + '%'
          : 'Downloading…';
        if (state === 'extracting') return 'Extracting pages…';
        if (state === 'dead') return 'Not available from this source';
        if (state === 'error') return '⚠ Failed — Read to retry';
        return [row.year, row.sizeMB ? row.sizeMB + ' MB' : ''].filter(Boolean).join(' · ');
      }

      function readLabel(row, state, busy) {
        if (state === 'dead') return 'Unavailable';
        if (row.readPending && busy) return 'Reading when ready';
        if (state === 'done' && row.hasReadingProgress && !row.readingFinished) {
          const pct = Math.round((row.readingProgress || 0) * 100);
          return pct > 0 ? 'Continue · ' + pct + '%' : 'Continue';
        }
        if (state === 'error') return 'Retry Read';
        if (busy) return 'Read when ready';
        return 'Read';
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
                    disabled: !row.available ? true : null,
                    onclick: () => act('read', { unitId: row.id })
                  }, readLabel(row, state, busy)),
                  row.alternateAvailable ? h('button.dc-alt-source', {
                    type: 'button', 'data-focus': true,
                    'data-key': 'comic.unit.' + row.id + '.sources',
                    'aria-label': 'Find alternate sources',
                    title: 'Find alternate sources',
                    onclick: () => act('openSources', { unitId: row.id })
                  }, '⌕') : null,
                  (row.available || state === 'done') ? h('button.dc-trailing', {
                    type: 'button', 'data-focus': true,
                    'data-key': 'comic.unit.' + row.id + '.download',
                    'aria-label': state === 'done' ? 'Remove downloaded release'
                      : busy ? 'Cancel release download' : 'Download release',
                    onclick: () => act('download', { unitId: row.id })
                  }, state === 'done' ? '✓' : busy ? '✕' : state === 'error' ? '↻' : '↓') : null));
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

      function publisherHero(data) {
        return h('div.dc-simple-hero', {},
          h('span.dc-kicker', {}, 'LOCG · Tankoban'),
          h('h1', {}, data.title || ''),
          h('p.dc-meta', {}, (data.count || 0) + ' series' + (data.hasMore ? ' so far' : '')));
      }

      function archiveIndexHero(data) {
        return h('div.dc-simple-hero', {},
          h('span.dc-kicker', {}, 'Western Comics · Tankoban'),
          h('h1', {}, data.title || ''),
          h('div.dc-archive-meta', {},
            h('p.dc-meta', {}, (data.seriesCount || 0) + ' series archives in the latest releases'),
            data.allTarget ? h('button.dc-archive-all', {
              type: 'button', 'data-focus': true, 'data-key': 'comic.archive.all',
              onclick: () => navigate(data.allTarget)
            }, 'All ' + (data.count > 0 ? data.count + ' ' : '') + 'releases ›') : null));
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

      function sourcePicker(data) {
        const sourceAct = (verb, extra) => act(verb, extra).then(result => {
          if (result && result.ok === false && result.error) env.toast(result.error);
          return result;
        });
        const confidenceLabel = value => value === 'strong' ? 'STRONG MATCH'
          : value === 'possible' ? 'POSSIBLE MATCH' : 'WEAK MATCH';
        const input = h('input.dc-source-query', {
          type: 'search', autocomplete: 'off',
          placeholder: 'Search another title or ISBN',
          value: data.query || '', 'data-focus': true,
          'data-key': 'comic.sources.query', 'aria-label': 'Search alternate sources'
        });
        const search = () => {
          const query = input.value.trim();
          if (query) sourceAct('searchSources', { query });
        };
        input.addEventListener('keydown', event => {
          if (event.key === 'Enter') search();
        });
        if (sourceFocusManual) {
          sourceFocusManual = false;
          requestAnimationFrame(() => input.focus());
        }

        let body;
        if (data.selectionState === 'inspecting') {
          body = h('div.dc-source-state', {},
            h('h3', {}, 'Inspecting pack…'),
            data.pendingTitle ? h('p', {}, data.pendingTitle) : null);
        } else if (data.selectionState === 'ambiguous') {
          body = h('div.dc-source-table', {},
            h('div.dc-source-table-head', {},
              h('b', {}, (data.archiveFiles || []).length
                + ((data.archiveFiles || []).length === 1 ? ' comic archive' : ' comic archives')),
              h('span', {}, 'CHOOSE ONE')),
            (data.archiveFiles || []).map(file =>
              h('button.dc-archive-choice', {
                type: 'button', 'data-focus': true,
                'data-key': 'comic.sources.archive.' + file.index,
                'aria-label': 'Choose ' + (file.name || 'archive'),
                onclick: () => sourceAct('chooseArchive', { fileIndex: file.index })
              },
              h('span.dc-ext-badge', {}, file.extension || ''),
              h('span.dc-archive-copy', {}, h('b', {}, file.name || ''), h('small', {}, file.sizeText || '')),
              h('span.dc-pick', {}, '↓'))));
        } else if (data.selectionState === 'incomplete') {
          body = h('div.dc-source-state', {},
            h('p.dc-source-lead', {}, 'This pack is missing issues this edition needs.'),
            h('p.dc-source-gold', {}, (data.missingIssues || []).join('   ·   ')),
            h('div.dc-source-buttons', {},
              h('button.dc-secondary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.tryAnother',
                onclick: () => sourceAct('rejectIncomplete', { manual: false })
              }, 'Try another source'),
              h('button.dc-secondary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.manual',
                onclick: () => {
                  sourceFocusManual = true;
                  sourceAct('rejectIncomplete', { manual: true });
                }
              }, 'Search manually')));
        } else if (data.selectionState === 'combined') {
          body = h('div.dc-source-state', {},
            h('p.dc-source-lead', {}, 'Only a combined archive covers this edition — it likely includes other editions too.'),
            h('div.dc-source-buttons', {},
              h('button.dc-secondary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.combined.back',
                onclick: () => sourceAct('rejectCombined')
              }, 'Go back'),
              h('button.dc-primary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.combined.go',
                onclick: () => sourceAct('confirmCombined')
              }, 'Download whole archive anyway')));
        } else {
          const rows = data.rows || [];
          body = h('div.dc-source-results', {},
            h('div.dc-source-querybar', {}, input,
              h('button.dc-primary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.search',
                onclick: search
              }, 'Search')),
            rows.length ? h('div.dc-source-table', {},
              h('div.dc-source-table-head', {},
                h('b', {}, rows.length + (rows.length === 1 ? ' result' : ' results')
                  + (data.loading ? '   ·   still searching…' : '')),
                h('span', {}, 'Pirate Bay · ExtraTorrents · Torrents-CSV')),
              rows.map(row => {
                const evidence = Array.isArray(row.evidence) ? row.evidence : [];
                return h('button.dc-source-row', {
                  type: 'button', 'data-focus': true,
                  'data-key': 'comic.sources.row.' + row.id,
                  'aria-label': 'Choose ' + (row.title || row.sourceName || 'source'),
                  onclick: () => sourceAct('selectSource', { sourceId: row.id })
                },
                h('span.dc-source-badge', {}, String(row.sourceName || '?').charAt(0).toUpperCase()),
                h('span.dc-source-copy', {},
                  h('span.dc-source-line', {},
                    h('b', {}, row.sourceName || 'Torrent'),
                    h('strong', { 'data-confidence': row.confidence || 'weak' }, confidenceLabel(row.confidence))),
                  h('span.dc-source-title', {}, row.title || ''),
                  h('span.dc-source-evidence', {},
                    row.coverage ? h('i', {}, 'FORMAT RANGE') : null,
                    evidence.includes('ISSUES') ? h('i', {}, 'ISSUES') : null,
                    row.uploader && Number(row.trustTier) <= 2 ? h('i', {}, 'TRUSTED · ' + row.uploader) : null),
                  h('small', {}, [row.sizeText || '',
                    row.seeders !== undefined ? '👤 ' + row.seeders : ''].filter(Boolean).join('   ·   '))),
                h('span.dc-pick', {}, '↓'));
              })) : h('div.dc-source-empty', {},
                data.loading ? 'Searching comic sources…'
                  : data.error ? 'Some sources did not answer. Showing the results that arrived.'
                    : 'No torrents matched this query. Try another title or ISBN.'));
        }

        const weak = data.confirmingWeak ? h('div.dc-source-prompt', {},
          h('div.dc-source-prompt-card', {},
            h('p.dc-source-lead', {}, 'This release does not closely match the collected edition.'),
            data.pendingTitle ? h('p', {}, data.pendingTitle) : null,
            h('div.dc-source-buttons', {},
              h('button.dc-secondary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.weak.back',
                onclick: () => sourceAct('cancelWeakSource')
              }, 'Go back'),
              h('button.dc-primary', {
                type: 'button', 'data-focus': true, 'data-key': 'comic.sources.weak.choose',
                onclick: () => sourceAct('confirmWeakSource')
              }, 'Choose anyway')))) : null;

        return h('div.dc-sources-layer', {},
          h('div.dc-source-backdrop', {}, data.cover ? CW.face(data.cover, data.editionTitle || 'Alternate sources') : null),
          h('button.dc-source-back', {
            type: 'button', 'data-focus': true, 'data-key': 'comic.sources.back',
            onclick: () => sourceAct('closeSources')
          }, 'Back'),
          h('div.dc-source-hero', {},
            h('span.dc-kicker', {}, 'ALTERNATE SOURCES · COLLECTED EDITION'),
            h('h2', {}, data.editionTitle || 'Alternate sources'),
            data.identityLine ? h('p', {}, data.identityLine) : null),
          h('div.dc-source-panel', {}, body),
          weak);
      }

      function render(section) {
        const data = section.data || {};
        if (data.schema === 'comic.hero') return seriesHero(data);
        if (data.schema === 'comic.filter') return filterControl(data);
        if (data.schema === 'comic.releases') return releaseRows(section, data);
        if (data.schema === 'comic.sources') return sourcePicker(data);
        if (data.schema === 'comic.archiveHero')
          return archiveHero(data, 'GetComics · Tankoban');
        if (data.schema === 'comic.archiveBoxes')
          return cardGrid(data.rows, 'comic.archive');
        if (data.schema === 'comic.archiveIndexHero')
          return archiveIndexHero(data);
        if (data.schema === 'comic.archiveSeries')
          return cardGrid(data.rows, 'comic.archive.series');
        if (data.schema === 'comic.publisherHero')
          return publisherHero(data);
        if (data.schema === 'comic.publisherSeries') {
          publisherWindows.set(Number(data.windowStart) || 0, data.rows || []);
          const rows = [...publisherWindows.keys()].sort((a, b) => a - b)
            .flatMap(start => publisherWindows.get(start));
          const grid = cardGrid(rows, 'comic.publisher.series');
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
            publisherWindows.clear();
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
