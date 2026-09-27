// surfaces/downloads/surface.js — 1:1 port of qml/DownloadsPage.qml (+ BackgroundActivitySection.qml).
// All grouping, counts and live/attention classification arrive from the page.downloads feed
// (DownloadsFeed.cpp); exact display copy is composed here with the QML's own templates.
// Talks only through env/port; actions are page.downloads.* (SCHEMA.md).
(function (CW) {
  'use strict';
  const { h } = CW;

  // ---- formatters (DownloadsPage.qml:284-321) ----
  const fmtBytes = b => {
    if (b >= 1073741824) return (b / 1073741824).toFixed(1) + ' GB';
    if (b >= 1048576) return Math.round(b / 1048576) + ' MB';
    if (b > 0) return Math.max(1, Math.round(b / 1024)) + ' KB';
    return '';
  };
  const fmtWhen = secs => {
    if (!secs) return '';
    const d = new Date(secs * 1000), now = new Date();
    const days = Math.floor((now - d) / 86400000);
    if (days <= 0) return 'added today';
    if (days === 1) return 'added yesterday';
    return 'added ' + d.toLocaleDateString('en-US', { month: 'long', day: 'numeric' });
  };
  const fmtSpeed = bps => {
    if (bps >= 1048576) return (bps / 1048576).toFixed(1) + ' MB/s';
    if (bps >= 1024) return Math.round(bps / 1024) + ' KB/s';
    return '';
  };
  const fmtEta = secs => {
    if (secs === undefined || secs === null || secs < 0) return '';
    if (secs >= 5400) return '~' + (secs / 3600).toFixed(1) + ' h left';
    if (secs >= 60) return '~' + Math.round(secs / 60) + ' min left';
    return '~' + Math.round(secs) + ' s left';
  };
  const fmtCooldown = ms => {
    const s = Math.ceil(ms / 1000);
    const mm = Math.floor(s / 60), ss = s % 60;
    return mm + ':' + (ss < 10 ? '0' : '') + ss;
  };
  // deterministic quiet cover tones (DownloadsPage.qml:382-388)
  const coverTone = (title, dark) => {
    let hh = 0;
    for (let i = 0; i < title.length; i++) hh = (((hh << 5) - hh + title.charCodeAt(i)) | 0);
    const hue = ((hh % 360) + 360) % 360;
    return `hsl(${hue}, 22%, ${dark ? 10 : 22}%)`;
  };
  const keySafe = s => String(s == null ? '' : s).replace(/[^a-zA-Z0-9_.-]/g, '_');
  const WORLD_LABEL = { tankoban: 'Tankoban', biblio: 'Biblio', theatre: 'Theatre' };

  CW.router.register('page.downloads', {
    mount(el, route, env) {
      const ground = h('div.dl-ground', { 'aria-hidden': true });
      const box = h('div.world-pane.dl-page', { role: 'region', 'aria-label': 'Downloads content' });
      el.append(ground, box);

      let sub = null;
      let alive = true;
      const sections = {};                      // id → last section object
      const folds = {};                         // group key → open (QML openGroups :213)
      const openSeasons = {};                   // ledger seasons → open (QML :221)
      let seasonsDefaulted = '';                // ledger key the newest-open default was applied to
      let ledger = { world: route.params && route.params.ledgerWorld || '', key: route.params && route.params.ledgerKey || '' };
      let mutation = '';
      let coolTicker = 0;

      const act = (verb, payload) =>
        env.act('page.downloads.' + verb, payload).then(r => {           // finishMutation (:74-82)
          if (r && r.ok === false) mutation = r.error || 'That could not be done.';
          else mutation = '';
          const hd = box.querySelector('[data-section="downloads.header"]');
          if (hd) hd.replaceWith(render(headerSection()));
          return r;
        });

      // ---- confirm dialog (:52-73, :1581-1675) ----
      let confirmState = null;
      const shade = h('div.dl-shade');
      shade.setAttribute('data-focus-scope', '');
      shade.__close = () => closeConfirm();
      function confirm(title, body, label, onGo) {
        confirmState = { invoker: document.activeElement, onGo };
        shade.replaceChildren(h('div.dl-confirm', { role: 'alertdialog', 'aria-label': title },
          h('h3', { text: title }),
          h('p', { text: body }),
          h('div.dl-confirm-btns', {},
            h('button.dl-confirm-back', {
              type: 'button', 'data-focus': true, 'data-key': 'dl.confirm.back',
              'aria-label': 'Go back',
              onclick: () => closeConfirm()
            }, 'Go back'),
            h('button.dl-confirm-go', {
              type: 'button', 'data-focus': true, 'data-key': 'dl.confirm.go',
              'aria-label': label,
              onclick: () => { const go = confirmState && confirmState.onGo; closeConfirm(); if (go) go(); }
            }, label))));
        shade.classList.add('on');
        box.appendChild(shade);
        requestAnimationFrame(() => { const b = shade.querySelector('[data-focus]'); if (b) b.focus({ preventScroll: true }); });
      }
      function closeConfirm() {
        const invoker = confirmState && confirmState.invoker;
        confirmState = null;
        shade.classList.remove('on');
        shade.replaceChildren();
        if (invoker && invoker.isConnected) requestAnimationFrame(() => invoker.focus({ preventScroll: true }));
      }

      // ---- open-world CTA (routeDownloadWorld, Main.qml:2083 — web-internal nav §3.6) ----
      const goWorld = world => env.router.go({
        name: 'world',
        world: world === 'tankoban' ? 'Tankoban' : (world === 'biblio' || world === 'audiobook') ? 'Biblio' : 'Theatre'
      });

      // ---- renderers ----
      const headerSection = () => ({ id: 'downloads.header', index: 0, layout: 'custom', state: 'ready',
        items: [], data: { schema: 'downloads.header', ...(sections['downloads.header'] && sections['downloads.header'].data || {}), __mutation: mutation } });

      function renderHeader(s) {
        const d = s.data || {};
        const t = d.totals || d;
        const metrics = h('p.dl-metrics', {},
          h('b', { text: String(t.items || 0) }), ' items');
        const bits = [];
        if (t.bytes) { metrics.append('  ·  ', h('b', { text: fmtBytes(t.bytes) }), ' on disk'); }
        metrics.append('  ·  ', `Tankoban `, h('span.w', { text: String(t.tankoban || 0) }));
        metrics.append('  ·  ', `Biblio `, h('span.w', { text: String(t.biblio || 0) }));
        metrics.append('  ·  ', `Theatre `, h('span.w', { text: String(t.theatre || 0) }));
        metrics.append('  ·  ', `Audiobooks `, h('span.w', { text: String(t.audiobook || 0) }));
        if (t.active) metrics.append('  ·  ', h('span.arr', { text: String(t.active) + ' arriving' }));
        if (t.attention) metrics.append('  ·  ', h('span.att', { text: String(t.attention) + ' need attention' }));
        return h('header', {},
          h('div.dl-eyebrow', { text: 'COLOSSEUM · LOCAL' }),
          h('h1.dl-title', { text: 'Downloads' }),
          h('div.dl-tick', { 'aria-hidden': true }),
          metrics,
          mutation ? h('p.dl-mutation', { text: mutation }) : null);
      }

      // group subtitle — exact templates (DownloadsPage.qml:586-627)
      function groupSubtitle(g) {
        const wn = WORLD_LABEL[g.world] || 'Theatre';
        if (g.single) {
          const r0 = g.rows[0] || {};
          if (r0.state === 'failed') return wn + ' · ' + (r0.error || 'download failed');
          if (r0.state === 'queued') return wn + ' · queued — waits its turn';
          if (r0.state === 'paused') return wn + ' · paused — holds its place';
          if (r0.state === 'resolving') return wn + ' · ' + (g.world === 'theatre' ? 'resolving — finding the best stream' : 'resolving — reading the torrent');
          if (r0.state === 'extracting') return wn + ' · unpacking';
          if (r0.state === 'done') return wn + ' · landed';
          const d = r0.detail || '';
          const base = d.length ? (wn + ' · ' + d) : wn;
          const cd = coolLeft(g.coolResumeAt);
          if (cd > 0) return base + ' · source cooling down — resumes in ' + fmtCooldown(cd);
          return base;
        }
        return g.groupUnit
          ? wn + ' · ' + g.count + ' ' + g.groupUnit
          : wn + ' · season checkout · ' + g.count + ' episodes';
      }
      const coolLeft = resumeAt => resumeAt > 0 ? Math.max(0, resumeAt - Date.now()) : 0;

      function renderNow(s) {
        const d = s.data || {};
        const groups = d.groups || [];
        const abActive = d.audiobookActive || [];
        const wrap = h('section.dl-sect');
        if (!groups.length && !abActive.length) return wrap;

        wrap.appendChild(h('div.dl-sect-head', {},
          h('h2', { text: 'Now arriving' }),
          h('span.sub', { text: String(d.liveCount || 0) + ((d.liveCount || 0) === 1 ? ' live job' : ' live jobs')
            + ((d.attentionCount || 0) > 0 ? ' · ' + d.attentionCount + ' need attention' : '') })));

        const panel = h('div.dl-panel');

        groups.forEach((g, gi) => {
          const open = folds[g.key] === true;
          const anyRunning = g.rows.some(r => r.canPause === true);
          const anyPaused = g.rows.some(r => r.canResume === true);
          const anyCancelable = g.rows.some(r => r.canCancel === true);
          const r0 = g.rows[0] || {};
          const canDismissSingle = g.single && r0.canDismiss === true;

          const nums = [];
          if (!g.single) nums.push(h('b', { text: g.doneCount + ' of ' + g.count }), ' landed');
          if (g.total > 0) nums.push(' · ', fmtBytes(g.received) + ' of ' + fmtBytes(g.total));
          const hsp = fmtSpeed(g.speed || 0);
          if (hsp.length) nums.push(' · ', h('span.spd', { text: hsp }));
          const eta = fmtEta(g.eta);
          if (eta.length) nums.push(' · ' + eta);

          const acts = h('div.dl-acts');
          if (g.single && r0.canPlay === true)
            acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.group.' + gi + '.play',
              'aria-label': 'Play arriving download',
              onclick: () => act('playArriving', { world: g.world, id: r0.id }).then(r => { if (r && r.ok) env.router.back(); }) }, 'Play'));
          if (g.single && r0.canRetry === true)
            acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.group.' + gi + '.retry',
              'aria-label': 'Retry download', onclick: () => act('retry', { world: g.world, id: r0.id }) }, 'Retry'));
          if (anyRunning || anyPaused) {
            const label = anyRunning ? (g.single ? 'Pause' : 'Pause season') : (g.single ? 'Resume' : 'Resume season');
            acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.group.' + gi + '.toggle',
              'aria-label': label, onclick: () => act('groupToggle', { world: g.world, groupKey: g.key, pause: anyRunning }) }, label));
          }
          if (g.single && r0.state === 'failed' && r0.canRetry !== true)
            acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.group.' + gi + '.open',
              'aria-label': 'Open ' + (g.world === 'biblio' ? 'Biblio' : 'Tankoban'), onclick: () => goWorld(g.world) },
              g.world === 'biblio' ? 'Open Biblio' : 'Open Tankoban'));
          if (anyCancelable || canDismissSingle) {
            const label = canDismissSingle ? 'Dismiss' : (g.single ? 'Cancel' : 'Cancel season');
            acts.appendChild(h('button.dl-link', { type: 'button', 'data-focus': true, 'data-key': 'dl.group.' + gi + '.cancel',
              'aria-label': label,
              onclick: () => {
                if (canDismissSingle) return act('dismissFailure', { world: g.world, id: r0.id });
                confirm(g.single ? 'Cancel download?' : 'Cancel season?', 'Partial files will be deleted.', 'Cancel download',
                  () => act('cancelGroup', { world: g.world, groupKey: g.key }));
              } }, label));
          }

          const head = h('div.dl-group-head', {},
            h('span.dl-chev', { 'aria-hidden': true, text: '›' }),
            h('div.dl-group-main', {},
              h('div.dl-group-title', { text: g.title }),
              h('div.dl-group-sub' + (g.coolResumeAt && coolLeft(g.coolResumeAt) > 0 ? '[data-cool="' + g.coolResumeAt + '"]' : ''),
                { text: groupSubtitle(g) })),
            h('div.dl-group-nums', {}, ...nums),
            acts);
          if (g.liveCount > 0 && g.hasKnownTotal)
            head.appendChild(h('span.dl-group-bar', { style: { width: (100 * Math.max(0, Math.min(1, g.ratio || 0))) + '%' } }));
          if (!g.single)
            head.appendChild(h('button.dl-disclosure', { type: 'button', 'data-focus': true, 'data-key': 'dl.group.' + gi,
              'aria-label': (open ? 'Collapse ' : 'Expand ') + g.title,
              onclick: () => { folds[g.key] = !open; rerender('downloads.now'); } }));

          const grp = h('div.dl-group' + (open ? '.open' : ''), {}, head);
          if (open && !g.single) g.rows.forEach(r => grp.appendChild(renderJobRow(r, g.world)));
          panel.appendChild(grp);
        });

        abActive.forEach(a => {
          const st = a.state;
          const sub = st === 'failed' ? 'Audiobook · failed — ' + (a.error || 'download failed')
            : st === 'resolving' ? 'Audiobook · resolving'
            : (a.total > 0) ? 'Audiobook · ' + fmtBytes(a.received) + ' of ' + fmtBytes(a.total)
            : 'Audiobook · downloading';
          panel.appendChild(h('div.dl-row.dl-abrow', {},
            h('div.dl-row-main', {},
              h('div.dl-row-title', { text: a.title || String(a.id).split('|')[0] || '' }),
              h('div.dl-row-state' + (st === 'failed' ? '.fail' : ''), { text: sub })),
            h('div.dl-row-acts', {},
              h('button.dl-link', { type: 'button', 'data-focus': true, 'data-key': 'dl.ab.' + keySafe(a.id) + '.cancel',
                'aria-label': st === 'failed' ? 'Dismiss' : 'Cancel',
                onclick: () => {
                  if (st === 'failed') return act('dismissAudiobookFailure', { id: a.id });
                  confirm('Cancel audiobook download?', 'The partial files will be deleted.', 'Cancel download',
                    () => act('cancelAudiobook', { id: a.id }));
                } }, st === 'failed' ? 'Dismiss' : 'Cancel'))));
        });

        wrap.appendChild(panel);
        return wrap;
      }

      // episode row — exact copy (:792-962)
      function renderJobRow(m, world) {
        const nums = [];
        if (m.state === 'downloading') {
          const sp = fmtSpeed(m.speed || 0);
          if (sp.length) nums.push(h('span.spd', { text: sp }));
          if (m.total > 0) {
            if (nums.length) nums.push(' · ');
            nums.push(Math.round((m.ratio || 0) * 100) + '%', ' · ', fmtBytes(m.received || 0) + ' of ' + fmtBytes(m.total));
          }
        } else if (m.state === 'done' && m.total > 0) nums.push(fmtBytes(m.total));
        else if (m.state === 'paused' && m.total > 0) nums.push(fmtBytes(m.received || 0) + ' of ' + fmtBytes(m.total));
        else nums.push('—');

        const stateText = m.state === 'downloading' ? 'downloading'
          : m.state === 'resolving' ? (world === 'theatre' ? 'resolving — finding the best stream' : 'resolving — reading the torrent')
          : m.state === 'queued' ? 'queued — waits its turn'
          : m.state === 'paused' ? 'paused — holds its place'
          : m.state === 'failed' ? 'failed — ' + (m.error || 'download failed')
          : 'landed — on the Theatre shelf';

        const acts = h('div.dl-row-acts');
        if (m.state === 'done') acts.appendChild(h('span.dl-check', { 'aria-hidden': true, text: '✓' }));
        if (m.canPlay === true)
          acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.row.' + keySafe(m.id) + '.play',
            'aria-label': 'Play arriving download', onclick: () => act('playArriving', { world: m.world || world, id: m.id }) }, 'Play'));
        if (m.canPause === true || m.canResume === true)
          acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.row.' + keySafe(m.id) + '.toggle',
            'aria-label': m.canResume === true ? 'Resume' : 'Pause',
            onclick: () => act(m.canResume === true ? 'resume' : 'pause', { world: m.world || world, id: m.id }) },
            m.canResume === true ? 'Resume' : 'Pause'));
        if (m.canRetry === true)
          acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.row.' + keySafe(m.id) + '.retry',
            'aria-label': 'Retry download', onclick: () => act('retry', { world: m.world || world, id: m.id }) }, 'Retry'));
        if (m.canCancel === true || m.canDismiss === true)
          acts.appendChild(h('button.dl-link', { type: 'button', 'data-focus': true, 'data-key': 'dl.row.' + keySafe(m.id) + '.cancel',
            'aria-label': m.canDismiss === true ? 'Dismiss' : 'Cancel',
            onclick: () => {
              if (m.canDismiss === true) return act('dismissFailure', { world: m.world || world, id: m.id });
              confirm('Cancel download?', 'The partial file will be deleted.', 'Cancel download',
                () => act('cancel', { world: m.world || world, id: m.id }));
            } }, m.canDismiss === true ? 'Dismiss' : 'Cancel'));

        const row = h('div.dl-row', {},
          h('span.dl-badge', { text: m.badge ? m.badge : (m.episode > 0 ? 'E' + (m.episode < 10 ? '0' : '') + m.episode : '') }),
          h('div.dl-row-main', {},
            h('div.dl-row-title' + (m.state === 'done' || m.state === 'queued' || m.state === 'paused' ? '.dim' : ''), { text: m.title || 'Episode' }),
            h('div.dl-row-state' + (m.state === 'failed' ? '.fail' : ''), { text: stateText })),
          h('div.dl-row-nums', {}, ...nums),
          acts);
        if (m.state === 'downloading' && (m.total || 0) > 0)
          row.appendChild(h('span.dl-row-bar', { style: { width: 'calc((100% - 78px) * ' + Math.max(0, Math.min(1, m.ratio || 0)) + ')' } }));
        return row;
      }

      function renderActivity(s) {
        const rows = (s.data || {}).rows || [];
        const wrap = h('section.dl-activity' + (rows.length ? '.on' : ''));
        wrap.setAttribute('data-section-holder', 'downloads.activity');
        wrap.appendChild(h('div.dl-activity-label', { text: 'Background activity' }));
        rows.forEach(r => wrap.appendChild(h('div.dl-act-row', {},
          h('div.dl-act-main', {},
            h('div.dl-act-title', { text: (r.title || '') + '  ·  ' + (r.stage || '') }),
            h('div.dl-act-track', {}, h('div.dl-act-fill', { style: { width: (100 * Math.max(0, Math.min(1, r.progress || 0))) + '%' } }))),
          r.canPause === true ? h('button.dl-act-toggle', { type: 'button', 'data-focus': true, 'data-key': 'dl.act.' + keySafe(r.id),
            'aria-label': r.paused ? 'Resume' : 'Pause',
            onclick: () => act(r.paused ? 'background.resume' : 'background.pause', { id: r.id }) },
            r.paused ? 'Resume' : 'Pause') : null)));
        return wrap;
      }

      function renderRemote(s) {
        const items = (s.data || {}).items || [];
        const wrap = h('section.dl-sect');
        if (!items.length) return wrap;
        wrap.appendChild(h('div.dl-sect-head', {},
          h('h2', { text: 'Available on another device' }),
          h('span.sub', { text: items.length + (items.length === 1 ? ' item' : ' items') })));
        wrap.appendChild(h('p.dl-remote-blurb', { text: 'The file itself is not stored on this device. Download it here when you want it.' }));
        const panel = h('div.dl-panel');
        items.forEach((m, i) => panel.appendChild(h('div.dl-remote-row', {},
          h('div.dl-lrow-main', {},
            h('div.dl-lrow-title', { text: m.title || 'Untitled' }),
            h('div.dl-lrow-meta', { text: (m.subtitle || m.seriesTitle || '') + ' · not downloaded here' })),
          m.canRedownload
            ? h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.remote.' + i,
                'aria-label': 'Download here', style: { fontSize: '13px', fontWeight: 600 },
                onclick: () => act('redownload', { world: m.world, id: m.id }) }, 'Download here')
            : h('span.dl-link.gold', { style: { fontSize: '13px', fontWeight: 600, cursor: 'default' } }, 'Source needed'))));
        wrap.appendChild(panel);
        return wrap;
      }

      function renderShelf(s) {
        const d = s.data || {};
        const wrap = h('section.dl-sect.shelf');
        let n = 0, bytes = 0;
        (d.series || []).forEach(sr => { n += sr.itemCount || 0; bytes += sr.bytes || 0; });
        wrap.appendChild(h('div.dl-sect-head', {},
          h('h2', { text: d.title || WORLD_LABEL[d.world] || '' }),
          h('span.sub', {}, h('b', { text: String(n) }), ' ' + (d.unit || ''), bytes ? ' · ' + fmtBytes(bytes) : '')));
        const panel = h('div.dl-panel.dl-shelf');
        if (!(d.series || []).length) {
          panel.appendChild(h('div.dl-empty', {},
            h('div.big', { text: 'Nothing from ' + (d.title || '') + ' lives here yet.' }),
            h('button.go', { type: 'button', 'data-focus': true, 'data-key': 'dl.go.' + d.world,
              'aria-label': 'Open ' + (d.title || ''), onclick: () => goWorld(d.world) },
              'Open ' + (d.title || '') + ' and pick something ›')));
        } else {
          const rail = h('div.dl-rail', { role: 'list', 'aria-label': (d.title || '') + ' downloaded series' });
          (d.series || []).forEach(sr => {
            const on = ledger.world === d.world && ledger.key === sr.key;
            const card = h('button.dl-card' + (on ? '.on' : ''), {
              type: 'button', 'data-focus': true, 'data-key': 'dl.card.' + d.world + '.' + keySafe(sr.key),
              'aria-label': sr.title || 'Untitled',
              onclick: () => toggleLedger(d.world, sr.key) },
              h('span.dl-cover', { style: { background: 'linear-gradient(' + coverTone(sr.title || '', false) + ', ' + coverTone(sr.title || '', true) + ')' } },
                sr.art ? h('img', { alt: '', src: sr.art, onload: e => e.target.classList.add('on') }) : null,
                h('span.foot', { 'aria-hidden': true })),
              h('span.dl-card-title', { text: sr.title || 'Untitled' }),
              h('span.dl-card-count', { text: sr.unitText || (sr.itemCount + (sr.itemCount === 1 ? ' item' : ' items')) }));
            rail.appendChild(card);
          });
          panel.appendChild(rail);
          if (d.ledger && ledger.world === d.world) panel.appendChild(renderLedger(d.ledger));
        }
        wrap.appendChild(panel);
        return wrap;
      }

      // ledger + season folds (:1323-1402) + LedgerRow (:1712-1810)
      function renderLedger(ld) {
        const col = h('div.dl-ledger');
        const applyDefault = seasonsDefaulted !== ledger.world + '|' + ledger.key;
        const seasons = ld.seasons || [];
        if (seasons.length) {
          if (applyDefault) {
            seasonsDefaulted = ledger.world + '|' + ledger.key;
            seasons.forEach(ss => { if (ss.defaultOpen) openSeasons[ss.season] = true; });
          }
          seasons.forEach(ss => {
            const open = openSeasons[ss.season] === true;
            const meta = ss.items.length + ' episode' + (ss.items.length === 1 ? '' : 's')
              + (ss.bytes ? ' · ' + fmtBytes(ss.bytes) : '')
              + (ss.arriving > 0 ? ' · ' + ss.arriving + ' still arriving above' : '');
            const grp = h('div');
            grp.appendChild(h('button.dl-season-head', { type: 'button', 'data-focus': true, 'data-key': 'dl.season.' + ss.season,
              'aria-label': (open ? 'Collapse season ' : 'Expand season ') + ss.season,
              onclick: () => { openSeasons[ss.season] = !open; rerender(ledgerSectionId()); } },
              h('span.dl-chev', { 'aria-hidden': true, text: '›' }),
              h('span.dl-season-name', { text: 'SEASON ' + ss.season }),
              h('span.dl-season-meta', { text: meta })));
            if (open) ss.items.forEach(it => grp.appendChild(renderLedgerRow(it, true)));
            col.appendChild(grp);
          });
        } else {
          (ld.flat || []).forEach(it => col.appendChild(renderLedgerRow(it, false)));
        }
        return col;
      }

      function renderLedgerRow(m, inSeason) {
        const meta = m.missing
          ? 'the file left the disk outside the app — dismiss the entry or fetch it again'
          : [m.subtitle, fmtBytes(m.bytes || 0), fmtWhen(m.addedAt || 0)].filter(Boolean).join(' · ');
        const acts = h('div.dl-lrow-acts');
        if (!m.missing)
          acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.lrow.' + keySafe(m.id) + '.open',
            'aria-label': m.world === 'theatre' ? 'Play' : 'Read',
            onclick: () => act('openItem', { world: m.world, id: m.id }).then(r => {
              if (!r || !r.ok) return;
              // a pack child answers with a native-issued comic route the web surface owns (§12.4)
              const route = r.result && r.result.route;
              if (route && env.router.has(env.router.surfaceName(route))) env.router.go(route);
              else env.router.back();
            }) },
            m.world === 'theatre' ? 'Play' : 'Read'));
        if (m.missing)
          acts.appendChild(h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.lrow.' + keySafe(m.id) + '.again',
            'aria-label': 'Download again',
            onclick: () => act('redownload', { world: m.world, id: m.id }) }, 'Download again'));
        acts.appendChild(h('button.dl-link', { type: 'button', 'data-focus': true, 'data-key': 'dl.lrow.' + keySafe(m.id) + '.delete',
          'aria-label': m.missing ? 'Dismiss missing entry' : 'Delete local copy',
          onclick: () => confirm(
            m.missing ? 'Dismiss missing entry?' : 'Delete local copy?',
            m.missing ? 'This removes the stale Downloads entry.' : 'The downloaded file will be deleted from this device.',
            m.missing ? 'Dismiss entry' : 'Delete local copy',
            () => act('remove', { world: m.world, id: m.id })) },
          m.missing ? 'Dismiss missing entry' : 'Delete local copy'));
        return h('div.dl-lrow' + (inSeason ? '.indented' : ''), { 'data-row-id': m.id, 'data-row-world': m.world },
          h('span.mark', { 'aria-hidden': true, text: m.missing ? '✕' : '✓' }),
          h('div.dl-lrow-main', {},
            h('div.dl-lrow-title' + (m.missing ? '.dim' : ''), { text: m.title || 'Untitled' }),
            h('div.dl-lrow-meta', { text: meta })),
          acts);
      }

      function renderAudiobooks(s) {
        const d = s.data || {};
        const done = d.done || [];
        const active = (sections['downloads.now'] && sections['downloads.now'].data || {}).audiobookActive || [];
        const wrap = h('section.dl-sect.shelf');
        if (!(d.visibleWhenIdle !== false) && !(done.length || active.length === 0)) return wrap;
        let bytes = 0; done.forEach(a => bytes += a.bytes || 0);
        wrap.appendChild(h('div.dl-sect-head', {},
          h('h2', { text: 'Audiobooks' }),
          h('span.sub', {}, h('b', { text: String(done.length) }), ' audiobooks', bytes ? ' · ' + fmtBytes(bytes) : '')));
        const panel = h('div.dl-panel.dl-shelf');
        if (!active.length && !done.length) {
          panel.appendChild(h('div.dl-empty', {},
            h('div.big', { text: 'No audiobooks live here yet.' }),
            h('button.go.dim', { type: 'button', 'data-focus': true, 'data-key': 'dl.go.audiobook',
              'aria-label': 'Open Biblio audiobooks', onclick: () => goWorld('audiobook') },
              'Open a book in Biblio and pick a release ›')));
        }
        done.forEach(a => {
          const meta = a.missing
            ? 'the files left the disk outside the app — delete the entry or fetch it again'
            : [a.author, fmtBytes(a.bytes || 0), fmtWhen(a.addedAt || 0),
               a.bookReady ? 'ready to listen' : 'the paired book is not available locally'].filter(Boolean).join(' · ');
          panel.appendChild(h('div.dl-lrow.dl-abdone', {},
            h('span.mark', { 'aria-hidden': true, text: a.missing ? '✕' : '✓' }),
            h('div.dl-lrow-main', {},
              h('div.dl-lrow-title' + (a.missing ? '.dim' : ''), { text: a.title || String(a.id).split('|')[0] }),
              h('div.dl-lrow-meta', { text: meta })),
            h('div.dl-lrow-acts', {},
              (!a.missing && a.bookReady)
                ? h('button.dl-link.gold', { type: 'button', 'data-focus': true, 'data-key': 'dl.abdone.' + keySafe(a.id) + '.open',
                    'aria-label': 'Open book', onclick: () => act('openAudiobook', { id: a.id }).then(r => { if (r && r.ok) env.router.back(); }) }, 'Open book')
                : null,
              h('button.dl-link', { type: 'button', 'data-focus': true, 'data-key': 'dl.abdone.' + keySafe(a.id) + '.delete',
                'aria-label': 'Delete local audiobook copy',
                onclick: () => confirm('Delete local audiobook?', 'The downloaded audiobook files will be deleted.', 'Delete local copy',
                  () => act('deleteAudiobook', { id: a.id })) }, 'Delete local copy'))));
        });
        wrap.appendChild(panel);
        return wrap;
      }

      const RENDER = {
        'downloads.header': renderHeader,
        'downloads.now': renderNow,
        'downloads.activity': renderActivity,
        'downloads.remote': renderRemote,
        'downloads.shelf.tankoban': renderShelf,
        'downloads.shelf.biblio': renderShelf,
        'downloads.shelf.theatre': renderShelf,
        'downloads.audiobooks': renderAudiobooks
      };
      const ledgerSectionId = () => 'downloads.shelf.' + ledger.world;

      function rerender(id) {
        const node = box.querySelector(`[data-section="${CSS.escape(id)}"]`);
        const s = sections[id];
        if (!node || !s) return;
        CW.focus.preserve(node, () => node.replaceWith(render(s)));
      }

      function render(s) {
        const fn = RENDER[s.id];
        const node = fn ? fn(s) : h('section');
        if (!node.hasAttribute('data-section')) { node.setAttribute('data-section', s.id); node.dataset.state = s.state; }
        return node;
      }

      const ctx = { open: env.open, act: env.act, toast: env.toast };
      function subscribe() {
        if (sub) sub.close();
        const params = (ledger.world && ledger.key) ? { ledgerWorld: ledger.world, ledgerKey: ledger.key } : {};
        sub = env.port.subscribe('page.downloads', params, ev => {
          if (!alive) return;
          for (const s of ev.sections) sections[s.id] = s;
          CW.section.sync(box, { ...ev, sections: ev.sections.map(s => ({ ...s, layout: 'custom' })) },
            { ...ctx, custom: render });
          armCoolTicker();
        });
      }

      function toggleLedger(world, key) {                       // QML :200-211
        if (ledger.world === world && ledger.key === key) { ledger = { world: '', key: '' }; }
        else ledger = { world, key };
        for (const k in openSeasons) delete openSeasons[k];
        subscribe();
      }

      // cooldown countdown (:140-145, :311-321) — 1s tick while any group cooling
      function armCoolTicker() {
        const cooling = Object.values(sections['downloads.now'] && sections['downloads.now'].data || {})
          .flatMap(d => (d && d.groups) || []).some(g => coolLeft(g.coolResumeAt || 0) > 0);
        if (cooling && !coolTicker) {
          coolTicker = setInterval(() => {
            if (!alive) { clearInterval(coolTicker); coolTicker = 0; return; }
            let any = false;
            box.querySelectorAll('[data-cool]').forEach(el => {
              const left = coolLeft(Number(el.getAttribute('data-cool')));
              const g = findGroupByCool(Number(el.getAttribute('data-cool')));
              if (left > 0 && g) { el.textContent = groupSubtitle(g); any = true; }
            });
            if (!any) { clearInterval(coolTicker); coolTicker = 0; }
          }, 1000);
        }
      }
      function findGroupByCool(resumeAt) {
        const d = (sections['downloads.now'] || {}).data || {};
        return ((d.groups) || []).find(g => g.coolResumeAt === resumeAt);
      }

      subscribe();
      return {
        unmount() {
          alive = false;
          clearInterval(coolTicker);
          closeConfirm();
          if (sub) sub.close();
        }
      };
    }
  });
})(window.CW = window.CW || {});
