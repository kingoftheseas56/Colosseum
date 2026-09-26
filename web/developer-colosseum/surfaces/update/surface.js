// Update web destination. State and human copy are owned by UpdateFeed.cpp.
(function (CW) {
  'use strict';

  const { h } = CW;

  CW.router.register('page.update', {
    mount(el, route, env) {
      const box = h('div.update-page');
      el.appendChild(box);

      let sub = null;
      let alive = true;
      let watching = false;
      let chapterIndex = 0;
      let releaseIdentity = '';
      let lastSection = null;

      function chaptersFor(data) {
        const release = data.release || {};
        const chapters = Array.isArray(data.chapters) ? data.chapters : [];
        if (chapters.length) return chapters;
        return [{
          kind: 'feature',
          section: 'RELEASE',
          title: release.title || 'The latest chapter',
          body: release.summary || 'The latest Colosseum chronicle lives here.',
          artwork: []
        }];
      }

      function restoreFocus(root, key) {
        if (!key) return;
        requestAnimationFrame(() => {
          const target = root.querySelector('[data-key="' + key + '"]');
          if (target) target.focus({ preventScroll: true });
        });
      }

      function redraw(section, focusKey) {
        const old = box.querySelector('.up-stage');
        if (!old) return;
        const fresh = renderUpdate(section);
        old.replaceWith(fresh);
        restoreFocus(fresh, focusKey);
      }

      function renderUpdate(section) {
        lastSection = section;
        const data = section.data || {};
        const release = data.release || {};
        const chapters = chaptersFor(data);
        const identity = String(release.version || data.latestVersion || data.installedVersion || release.title || '');
        if (identity !== releaseIdentity) {
          releaseIdentity = identity;
          chapterIndex = 0;
        }
        chapterIndex = Math.max(0, Math.min(chapterIndex, chapters.length - 1));
        const chapter = chapters[chapterIndex] || {};
        const art = Array.isArray(chapter.artwork) && chapter.artwork.length ? chapter.artwork[0] : '';
        const version = release.version || data.latestVersion || data.installedVersion || release.title || '';
        const primary = data.primary || {};

        const stage = h('div.up-stage', {},
          CW.face(art, chapter.title || release.title || 'Colosseum'),
          h('div.up-wash', { 'aria-hidden': true }),
          h('div.up-release', {},
            h('span.up-label', {}, 'COLOSSEUM UPDATE'),
            h('h1', {}, version)),
          h('div.up-copy', {},
            h('span.up-section', {}, chapter.section || 'RELEASE'),
            h('h2', {}, chapter.title || release.title || 'The latest chapter'),
            h('p', {}, chapter.body || release.summary || '')),
          h('div.up-nav', {},
            h('span.up-count', {},
              String(chapterIndex + 1).padStart(2, '0') + ' / ' + String(chapters.length).padStart(2, '0')),
            chapters.length > 1 ? h('span.up-nav-label', {}, chapter.section || 'RELEASE') : null,
            chapters.length > 1 ? h('span.up-divider', { 'aria-hidden': true }) : null,
            chapters.length > 1 ? chapters.map((item, index) =>
              h('button.up-chapter' + (index === chapterIndex ? '.on' : ''), {
                type: 'button', 'data-focus': true, 'data-key': 'update.chapter.' + index,
                'aria-label': 'Chapter ' + (index + 1) + ': ' + (item.title || 'Chapter'),
                onclick: () => {
                  chapterIndex = index;
                  redraw(section, 'update.chapter.' + index);
                }
              }, String(index + 1).padStart(2, '0'))) : null,
            chapters.length > 1 ? h('button.up-next', {
              type: 'button', 'data-focus': true, 'data-key': 'update.next',
              onclick: () => {
                chapterIndex = (chapterIndex + 1) % chapters.length;
                redraw(section, 'update.next');
              }
            }, 'Next') : null),
          h('div.up-status', {},
            h('div.up-status-copy', {},
              h('b', {}, data.statusText || 'No update check yet'),
              data.metadataText ? h('span', {}, data.metadataText) : null),
            data.progressVisible ? h('div.up-progress', {},
              h('span', {}, data.progressText || ''),
              h('span.up-track', {},
                h('i', { style: { width: data.progressIndeterminate ? '0%' :
                  String(Math.max(0, Math.min(1, Number(data.progress) || 0)) * 100) + '%' } }))) : null,
            primary.visible ? h('button.up-primary', {
              type: 'button', 'data-focus': true, 'data-key': 'update.primary',
              disabled: primary.enabled === false ? true : null,
              onclick: () => {
                if (!primary.action) return;
                env.act(primary.action, {}).then(result => {
                  if (result && result.ok) refreshFeed();
                });
              }
            }, primary.label || '') : null));

        return stage;
      }

      const ctx = {
        open: env.open, act: env.act, door: env.door, toast: env.toast,
        custom: renderUpdate,
        more: section => sub ? env.more(sub, section) : Promise.resolve({ ok: false })
      };

      function subscribe() {
        if (!alive) return;
        if (sub) sub.close();
        sub = env.port.subscribe('page.update', route.params || {},
          ev => CW.section.sync(box, ev, ctx));
      }

      function refreshFeed() {
        if (!alive) return;
        const key = document.activeElement && document.activeElement.getAttribute
          ? document.activeElement.getAttribute('data-key') : null;
        subscribe();
        if (key) requestAnimationFrame(() => restoreFocus(box, key));
      }

      function armWatch() {
        if (!alive || watching) return;
        watching = true;
        env.act('page.update.wait', {}).then(result => {
          watching = false;
          if (!alive) return;
          // The fixture adapter resolves every action immediately. Do not turn
          // its canned answer into a tight resubscribe loop; live native waits
          // return only "changed" or "idle" after a real signal/timeout.
          if (result && result.result === 'fixture') return;
          if (result && result.ok) refreshFeed();
          armWatch();
        });
      }

      subscribe();
      env.act('page.update.seen', {}).then(result => {
        if (alive && result && result.ok) refreshFeed();
      });
      armWatch();

      return {
        unmount() {
          alive = false;
          lastSection = null;
          if (sub) sub.close();
        }
      };
    }
  });
})(window.CW = window.CW || {});
