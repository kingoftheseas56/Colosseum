// surfaces/detail-book/surface.js — 1:1 port of qml/BiblioBook.qml.
// All catalogue/source/download state comes from detail.book. Native Reader2 remains the reading destination.
(function (CW) {
  'use strict';
  const { h } = CW;

  CW.router.register('detail.book', {
    mount(el, route, env) {
      const box = h('div.world-pane.db-page');
      el.appendChild(h('div.db-ground', { 'aria-hidden': 'true' }), box);

      let sub = null;
      let params = route.params || {};
      let closed = false;
      let torrentExpanded = false;

      const act = (verb, extra) =>
        env.act('detail.book.' + verb, { id: params.id, ...(extra || {}) });

      const sourceKey = row => String(row.sourceKey || row.key || '');
      const stateOf = row => String(row.state || 'none');
      const pct = row => Number(row.total || 0) > 0
        ? Math.round(Number(row.received || 0) / Number(row.total || 0) * 100) : 0;

      function viewSection(section) {
        const data = { ...(section.data || {}), feedState: section.state, feedError: section.error || '' };
        return { ...section, state: 'ready', data };
      }

      function indicator(row, external) {
        const state = stateOf(row);
        if (state === 'done') return '✓';
        if (state === 'downloading') return pct(row) + '%';
        if (state === 'resolving' || state === 'queued') return '…';
        if (state === 'failed') return 'retry';
        return external ? '↗' : '↓';
      }

      function rowMeta(parts) {
        return (parts || []).filter(Boolean).join('   ·   ');
      }

      function renderHero(section) {
        const d = section.data || {};
        const cover = h('div.db-cover-object', {},
          h('div.db-cover-face', {}, CW.face(d.cover || '', d.title || '')),
          h('span.db-spine', { 'aria-hidden': 'true' }),
          h('span.db-page-edge', { 'aria-hidden': 'true' }));

        const read = h('button.db-read', {
          type: 'button', 'data-focus': true, 'data-key': 'book.read',
          disabled: d.primaryEnabled ? null : true,
          onclick: () => act('read')
        }, d.primaryLabel || 'Read');

        const status = h('div.db-read-status' + (d.readError ? '.error' : ''), {},
          d.primaryStatus || '');

        const library = h('button.db-library', {
          type: 'button', 'data-focus': true, 'data-key': 'book.library',
          'aria-label': d.saved ? 'Remove from Library' : 'Add to Library',
          onclick: () => act('collection', { saved: !d.saved })
        },
          h('span.db-library-glyph' + (d.saved ? '.saved' : ''), {}, d.saved ? '✓' : '+'),
          h('span', {}, d.saved ? 'In Library' : 'Library'));

        const synopsis = h('p.db-synopsis', {}, d.synopsis || '');
        const copy = h('div.db-copy', {},
          d.genreLine ? h('div.db-eyebrow', {}, String(d.genreLine).toUpperCase()) : null,
          h('h1', {}, d.title || ''),
          d.tagline ? h('p.db-tagline', {}, '“' + d.tagline + '”') : null,
          h('div.db-rule', { 'aria-hidden': 'true' }, h('i')),
          synopsis);

        return h('div.db-hero', {},
          h('div.db-cover-column', {}, cover,
            h('div.db-actions', {}, read, status, library)),
          copy);
      }

      function sourceRow(kind, row, index) {
        const key = sourceKey(row);
        const state = stateOf(row);
        const inert = state === 'done' || state === 'resolving'
          || state === 'queued' || state === 'downloading';
        const click = () => {
          if (inert || !key) return;
          if (kind === 'torrent') act('downloadTorrent', { sourceKey: key });
          else if (kind === 'edition') act('downloadEdition', { sourceKey: key });
          else act('downloadAudiobook', { sourceKey: key });
        };

        if (kind === 'torrent') {
          return h('button.db-source-row.db-torrent-row' + (index === 0 ? '.recommended' : ''), {
            type: 'button', 'data-focus': true, 'data-key': 'book.torrent.' + key,
            onclick: click
          },
            h('span.db-source-copy', {},
              h('b.db-source-title', {}, row.title || ''),
              h('small', {}, '▲ ' + (row.seeders || 0) + '   ' + (row.size || '')
                + (row.pack ? '   ·  PACK' : ''))),
            h('span.db-source-indicator', {}, indicator(row, false)));
        }

        if (kind === 'edition') {
          const format = String(row.formatLabel || row.format || (row.md5 ? 'FILE' : 'WEB')).toUpperCase();
          return h('button.db-source-row.db-edition-row' + (row.best ? '.recommended' : ''), {
            type: 'button', 'data-focus': true, 'data-key': 'book.edition.' + key,
            onclick: click
          },
            h('span.db-format-pill' + (row.best ? '.best' : ''), {}, format),
            h('span.db-edition-meta', {}, row.meta || rowMeta([
              row.size,
              row.source ? String(row.source).toUpperCase() : '',
              row.year,
              row.language
            ])),
            h('span.db-source-indicator', {}, indicator(row, !row.md5)));
        }

        const active = String(row.slug || key) === String(row.activeSlug || '');
        const audioState = active ? state : 'idle';
        const audioRow = { ...row, state: audioState };
        return h('button.db-source-row.db-audio-row', {
          type: 'button', 'data-focus': true, 'data-key': 'book.audio.' + key,
          onclick: click
        },
          h('span.db-audio-pill', {}, String(row.format || 'AUDIO').toUpperCase().slice(0, 6)),
          h('span.db-audio-meta', {}, rowMeta([row.size, row.language, row.posted])),
          h('span.db-source-indicator', {}, indicator(audioRow, false)));
      }

      function renderTorrents(section) {
        const d = section.data || {};
        const rows = d.rows || [];
        const loading = d.feedState === 'loading' || !!d.loading;
        const count = rows.length;
        const title = 'TORRENTS' + (loading ? '  ·  SEARCHING…'
          : count ? '  ·  ' + count : '  ·  NONE');

        const rowEls = rows.map((row, i) => {
          const node = sourceRow('torrent', row, i);
          if (!torrentExpanded && i >= 5) node.hidden = true;
          return node;
        });

        const panelChildren = [];
        if (loading || !count)
          panelChildren.push(h('div.db-source-empty', {}, loading ? 'Searching torrents…' : 'No torrents found'));
        panelChildren.push(...rowEls);

        if (count > 5) {
          const more = h('button.db-see-more', {
            type: 'button', 'data-focus': true, 'data-key': 'book.torrents.more',
            'aria-label': torrentExpanded ? 'See less torrents' : 'See more torrents'
          }, torrentExpanded ? 'See less' : 'See ' + (count - 5) + ' more');
          more.addEventListener('click', () => {
            torrentExpanded = !torrentExpanded;
            rowEls.forEach((node, i) => { node.hidden = !torrentExpanded && i >= 5; });
            more.textContent = torrentExpanded ? 'See less' : 'See ' + (count - 5) + ' more';
            more.setAttribute('aria-label', torrentExpanded ? 'See less torrents' : 'See more torrents');
          });
          panelChildren.push(more);
        }

        return h('div.db-source-block', {},
          h('div.db-source-heading', {}, title),
          h('div.glass.db-source-panel', {}, panelChildren));
      }

      function renderEditions(section) {
        const d = section.data || {};
        const rows = d.rows || [];
        const loading = d.feedState === 'loading' || !!d.loading;
        const title = 'EDITIONS' + (loading ? '  ·  SEARCHING…'
          : rows.length ? '  ·  ' + rows.length : '  ·  NONE');
        return h('div.db-source-block', {},
          h('div.db-source-heading', {}, title),
          h('div.glass.db-source-panel', {},
            loading || !rows.length
              ? h('div.db-source-empty', {}, loading ? 'Searching LibGen…' : 'No editions found')
              : rows.map((row, i) => sourceRow('edition', row, i))));
      }

      function renderAudiobooks(section) {
        const d = section.data || {};
        const rows = (d.rows || []).map(row => ({ ...row, activeSlug: d.activeSlug || '', state: d.state || row.state || 'none',
                                                  received: d.received, total: d.total }));
        const loading = d.feedState === 'loading' || !!d.loading;
        const title = 'AUDIOBOOK' + (loading ? '  ·  SEARCHING…'
          : rows.length ? '  ·  ' + rows.length : '  ·  NONE');
        const background = !d.activeSlug && (d.state === 'resolving' || d.state === 'downloading')
          ? h('div.db-audio-background', {}, d.state === 'resolving'
              ? 'Downloading in the background — resolving…'
              : 'Downloading in the background — ' + (Number(d.total || 0) > 0
                  ? Math.round(Number(d.received || 0) / Number(d.total || 0) * 100) : 0) + '%')
          : null;
        return h('div.db-source-block.db-audio-block', {},
          h('div.db-source-heading', {}, title),
          h('div.glass.db-source-panel', {},
            background,
            loading || !rows.length
              ? h('div.db-source-empty', {}, loading ? 'Searching AudioBookBay…' : 'No audiobook found')
              : rows.map((row, i) => sourceRow('audio', row, i))));
      }

      function renderChoice(section) {
        const rows = (section.data && section.data.rows) || [];
        if (!rows.length) return null;
        const scope = h('div.db-choice-shade', { 'data-focus-scope': true },
          h('div.glass.db-choice-card', {},
            h('h2', {}, 'Choose an edition'),
            h('p', {}, 'Read will continue into Reader2 when this exact edition is ready.'),
            h('div.db-choice-list', {}, rows.map((row, i) =>
              h('button.db-choice-row', {
                type: 'button', 'data-focus': true,
                'data-key': 'book.read-choice.' + String(row.transport || '') + '.' + String(row.id || ''),
                onclick: () => act('chooseRead', {
                  transport: String(row.transport || ''), sourceKey: String(row.id || '')
                })
              },
                h('span.db-choice-copy', {},
                  h('b', {}, row.label || 'Edition'),
                  h('small', {}, row.meta || '')),
                h('span.db-choice-verb', {}, 'Read')))),
            h('button.db-choice-cancel', {
              type: 'button', 'data-focus': true, 'data-key': 'book.read-choice.cancel',
              onclick: () => act('cancelRead')
            }, 'Cancel')));

        scope.__close = () => act('cancelRead');
        scope.addEventListener('keydown', event => {
          if (event.key === 'Tab') {
            event.preventDefault();
            return;
          }
          if (event.key !== 'Home' && event.key !== 'End') return;
          const targets = [...scope.querySelectorAll('[data-focus]')];
          if (!targets.length) return;
          event.preventDefault();
          (event.key === 'Home' ? targets[0] : targets[targets.length - 1]).focus({ preventScroll: true });
        });
        requestAnimationFrame(() => {
          if (!scope.isConnected || scope.contains(document.activeElement)) return;
          const first = scope.querySelector('[data-focus]');
          if (first) first.focus({ preventScroll: true });
        });
        return scope;
      }

      function render(section) {
        const schema = section.data && section.data.schema;
        if (schema === 'book.hero') return renderHero(section);
        if (schema === 'book.torrents') return renderTorrents(section);
        if (schema === 'book.editions') return renderEditions(section);
        if (schema === 'book.audiobooks') return renderAudiobooks(section);
        if (schema === 'book.readChoices') return renderChoice(section);
        return CW.section.note('Nothing here yet', null);
      }

      const ctx = { open: env.open, act: env.act, toast: env.toast, custom: render };

      function subscribe() {
        if (sub) sub.close();
        sub = env.port.subscribe('detail.book', params, ev => {
          if (ev.type === 'reset')
            CW.section.sync(box, { ...ev, sections: (ev.sections || []).map(viewSection) }, ctx);
          else if (ev.type === 'section')
            CW.section.sync(box, { ...ev, section: viewSection(ev.section) }, ctx);
          else
            CW.section.sync(box, ev, ctx);
        });
      }

      subscribe();
      return {
        update(next) {
          if (closed) return;
          const previous = JSON.stringify(params);
          params = next.params || {};
          if (JSON.stringify(params) !== previous) {
            torrentExpanded = false;
            subscribe();
          }
        },
        unmount() {
          closed = true;
          if (sub) sub.close();
        }
      };
    }
  });
})(window.CW = window.CW || {});
