// Update web destination. State and human copy are owned by UpdateFeed.cpp.
(function (CW) {
  'use strict';

  const { h } = CW;

  CW.router.register('page.update', {
    mount(el, route, env) {
      const box = h('div.update-page');
      el.appendChild(box);

      let sub = null;
      let chapterIndex = 0;
      let releaseIdentity = '';

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

      // qml/update/UpdateLivingGallery.qml:149-421 — one monochrome release
      // stage, editorial chapter copy, chapter selector and Next control.
      function renderUpdate(section) {
        const data = section.data || {};
        const release = data.release || {};
        const chapters = Array.isArray(data.chapters) ? data.chapters : [];
        const identity = String(release.version || data.latestVersion || data.installedVersion || release.title || '');
        if (identity !== releaseIdentity) {
          releaseIdentity = identity;
          chapterIndex = 0;
        }
        chapterIndex = Math.max(0, Math.min(chapterIndex, Math.max(0, chapters.length - 1)));
        const chapter = chapters[chapterIndex] || {};
        const art = Array.isArray(chapter.artwork) && chapter.artwork.length
          ? String(chapter.artwork[0] || '') : '';
        const version = release.version || data.latestVersion || data.installedVersion || release.title || '';
        const primary = data.primary || {};

        const stage = h('div.up-stage', {},
          CW.face(art, ''),
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
                'aria-description': index === chapterIndex ? 'Selected' : null,
                onclick: () => {
                  chapterIndex = index;
                  redraw(section, 'update.chapter.' + index);
                }
              }, String(index + 1).padStart(2, '0'))) : null,
            chapters.length > 1 ? h('button.up-next', {
              type: 'button', 'data-focus': true, 'data-key': 'update.next',
              'aria-label': 'Next chapter',
              onclick: () => {
                chapterIndex = (chapterIndex + 1) % chapters.length;
                redraw(section, 'update.next');
              }
            }, 'Next') : null),
          // qml/UpdatePage.qml:178-283 — status, metadata, progress and
          // the state-derived primary updater action live on the page.
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
                env.act(primary.action, {});
              }
            }, primary.label || '') : null));

        return stage;
      }

      const ctx = {
        open: env.open, act: env.act, door: env.door, toast: env.toast,
        custom: renderUpdate,
        more: section => sub ? env.more(sub, section) : Promise.resolve({ ok: false })
      };

      let initialFocusDone = false;
      sub = env.port.subscribe('page.update', route.params || {}, ev => {
        CW.section.sync(box, ev, ctx);
        if (!initialFocusDone && ev.sections && ev.sections.some(s => s.id === 'update.chronicle' && s.state === 'ready')) {
          initialFocusDone = true;
          requestAnimationFrame(() => {
            const primary = box.querySelector('.up-primary');
            if (primary) primary.focus({ preventScroll: true });
          });
        }
      });
      env.act('page.update.seen', {});

      return {
        unmount() {
          if (sub) sub.close();
        }
      };
    }
  });
})(window.CW = window.CW || {});
