// Manga detail. The native feed owns title identity, sources, progress and acquisition state.
(function (CW) {
  'use strict';
  const { h } = CW;

  CW.router.register('detail.manga', {
    mount(el, route, env) {
      const box = h('div.world-pane.dm-page');
      el.appendChild(box);
      let sub = null;
      let params = route.params || {};
      let mode = 'volumes';
      let closed = false;
      let chapterScope = '';
      const chapterWindows = new Map();
      const act = (verb, more) => env.act('detail.manga.' + verb, { id: params.id, ...(more || {}) });

      function render(section) {
        const data = section.data || {};
        if (data.schema === 'manga.header')
          return h('div.dm-hero', {},
            h('div.dm-back', {}, CW.face(data.banner || data.cover, data.title || 'Manga')),
            h('div.dm-cover', {}, CW.face(data.cover, data.title || 'Manga')),
            h('div.dm-intro', {},
              h('span.dm-kicker', {}, 'MANGA'),
              h('h1', {}, data.title || 'Untitled'),
              h('p.dm-meta', {}, [data.author, data.year, data.status].filter(Boolean).join(' · ')),
              data.genres && data.genres.length ? h('p.dm-genres', {}, data.genres.join(' · ')) : null,
              data.synopsis ? h('p.dm-synopsis', {}, data.synopsis) : null,
              h('div.dm-actions', {},
                h('button.dm-primary', { type: 'button', 'data-focus': true, 'data-key': 'manga.primary',
                  onclick: () => {
                    const volumes = box.querySelector('[data-section="volumes"]');
                    const first = volumes && volumes.querySelector('[data-key^="manga.volume."]');
                    if (first) first.focus({ preventScroll: false });
                    else act('selectMode', { mode: 'chapters' });
                  } }, data.primaryLabel || 'Read'),
                h('button.dm-secondary', { type: 'button', 'data-focus': true, 'data-key': 'manga.collection',
                  'aria-pressed': !!data.saved, onclick: () => act('collection', {
                    saved: !data.saved, title: data.title, cover: data.cover
                  }) }, data.saved ? 'In Collection' : 'Add to Collection'))));
        if (data.schema === 'manga.modes') {
          mode = data.selected || mode;
          return h('div.dm-mode-wrap', {},
            h('div.dm-modes', {}, ['volumes', 'chapters'].map(choice =>
              h('button.dm-mode' + (choice === mode ? '.on' : ''), {
                type: 'button', 'data-focus': true, 'data-key': 'manga.mode.' + choice,
                'aria-pressed': choice === mode,
                disabled: choice === 'chapters' && !data.chapterEnabled ? true : null,
                onclick: () => act('selectMode', { mode: choice, language: data.selectedLanguage })
              }, choice === 'volumes' ? 'Volumes' : 'Chapters'))),
            mode === 'chapters' ? h('div.dm-languages', {}, (data.languages || []).map(language =>
              h('button.dm-language' + (language.code === data.selectedLanguage ? '.on' : ''), {
                type: 'button', 'data-focus': true, 'data-key': 'manga.language.' + language.code,
                'aria-pressed': language.code === data.selectedLanguage,
                onclick: () => act('selectMode', { mode: 'chapters', language: language.code })
              }, language.label || language.code))) : null);
        }
        if (data.schema === 'manga.volumes') {
          if (!(data.rows || []).length) return CW.section.note('No volume shelf yet', 'This catalogue has no proven volumes.');
          return h('div.dm-volumes', {}, (data.rows || []).map(volume =>
            h('div.dm-volume', {},
              h('span.dm-volume-art', {}, CW.face(volume.cover, 'Volume ' + volume.number)),
              h('div.dm-unit-copy', {}, h('small', {}, 'VOLUME ' + volume.number),
                h('b', {}, volume.title || 'Volume ' + volume.number),
                volume.startChapter ? h('span', {}, 'Chapters ' + volume.startChapter +
                  (volume.endChapter ? '–' + volume.endChapter : '')) : null,
                h('span.dm-unit-state', {}, volume.downloadState || 'Not downloaded')),
              h('div.dm-unit-actions', {},
                h('button.dm-small', { type: 'button', 'data-focus': true,
                  'data-key': 'manga.volume.' + volume.id + '.read',
                  onclick: () => volume.owned
                    ? act('read', { unitKind: 'volume', unitId: volume.id })
                    : act('loadVolumeSources', { unitId: volume.id }) },
                  volume.owned ? 'Read' : 'Get'),
                h('button.dm-small', { type: 'button', 'data-focus': true,
                  'data-key': 'manga.volume.' + volume.id + '.download',
                  onclick: () => act('loadVolumeSources', { unitId: volume.id }) }, 'Find source'),
                h('button.dm-small', { type: 'button', 'data-focus': true,
                  'data-key': 'manga.volume.' + volume.id + '.mark', 'aria-pressed': !!volume.read,
                  onclick: () => act('markRead', { unitKind: 'volume', unitId: volume.id, read: !volume.read }) },
                  volume.read ? 'Read ✓' : 'Mark read')))));
        }
        if (data.schema === 'manga.chapters') {
          const scope = [mode, data.sourceSeriesId, data.language].join(':');
          if (scope !== chapterScope) { chapterScope = scope; chapterWindows.clear(); }
          chapterWindows.set(Number(data.windowStart) || 0, data.rows || []);
          const allRows = [...chapterWindows.keys()].sort((a, b) => a - b)
            .flatMap(start => chapterWindows.get(start));
          if (!allRows.length) return CW.section.note('No chapters yet', 'Choose a language and available source.');
          return h('div.dm-chapters', {}, allRows.map(chapter =>
            h('div.dm-chapter', {},
              h('div.dm-unit-copy', {}, h('small', {}, 'CHAPTER ' + chapter.number),
                h('b', {}, chapter.title || 'Chapter ' + chapter.number),
                h('span.dm-unit-state', {}, [chapter.sourceLabel, chapter.downloadState].filter(Boolean).join(' · '))),
              h('div.dm-unit-actions', {},
                h('button.dm-small', { type: 'button', 'data-focus': true,
                  'data-key': 'manga.chapter.' + chapter.id + '.read',
                  onclick: () => act('read', { unitKind: 'chapter', unitId: chapter.id }) }, 'Read'),
                h('button.dm-small', { type: 'button', 'data-focus': true,
                  'data-key': 'manga.chapter.' + chapter.id + '.download',
                  onclick: () => act('download', { unitKind: 'chapter', unitId: chapter.id }) }, 'Download'),
                h('button.dm-small', { type: 'button', 'data-focus': true,
                  'data-key': 'manga.chapter.' + chapter.id + '.mark', 'aria-pressed': !!chapter.read,
                  onclick: () => act('markRead', { unitKind: 'chapter', unitId: chapter.id, read: !chapter.read }) },
                  chapter.read ? 'Read ✓' : 'Mark read')))),
            section.hasMore ? h('button.dm-more', { type: 'button', 'data-focus': true,
              'data-key': 'manga.chapters.more', onclick: () => sub && env.more(sub, section) }, 'More chapters') : null);
        }
        if (data.schema === 'manga.sources') {
          if (!(data.rows || []).length) return CW.section.note('No sources', data.targetId
            ? 'No enabled volume source was found.' : 'This language has no configured provider.');
          return h('div.dm-sources', {}, (data.rows || []).map(source =>
            h('button.dm-source', { type: 'button', 'data-focus': true,
              'data-key': 'manga.source.' + source.key,
              disabled: !data.targetId && !source.availability ? true : null,
              onclick: () => data.targetId
                ? act('download', { unitKind: 'volume', unitId: data.targetId, sourceKey: source.key })
                : act('selectSource', { sourceKey: source.key }) },
              source.label, h('small', {}, data.targetId ? 'Download' : source.language || ''))));
        }
        if (data.schema === 'manga.downloads') {
          if (!(data.rows || []).length) return CW.section.note('No downloads', 'Queued chapters and volumes appear here.');
          return h('div.dm-downloads', {}, (data.rows || []).map(job =>
            h('div.dm-job', {}, h('b', {}, job.unitKind + ' · ' + job.unitId),
              h('span', {}, [job.state, job.done && job.total ? job.done + ' / ' + job.total : ''].filter(Boolean).join(' · ')),
              job.error ? h('small', {}, job.error) : null)));
        }
        return CW.section.note('Nothing here yet', null);
      }

      const ctx = { open: env.open, act: env.act, toast: env.toast, custom: render,
        more: section => sub ? env.more(sub, section) : Promise.resolve({ ok: false }) };
      function subscribe() {
        if (sub) sub.close();
        sub = env.port.subscribe('detail.manga', params, ev => {
          if (ev.type === 'reset') chapterWindows.clear();
          CW.section.sync(box, ev, ctx);
        });
      }
      subscribe();
      return {
        update(next) {
          if (closed) return;
          const previous = JSON.stringify(params);
          params = next.params || {};
          if (JSON.stringify(params) !== previous) { chapterWindows.clear(); subscribe(); }
        },
        unmount() { closed = true; if (sub) sub.close(); }
      };
    }
  });
})(window.CW = window.CW || {});
