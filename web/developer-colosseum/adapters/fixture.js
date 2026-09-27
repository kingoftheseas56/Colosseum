// adapters/fixture.js — swarm mode. Replays RECORDED real-app feed events (CONTRACT §6); never invents data.
//
// fixtures/index.json lists recorder files (dev/index_fixtures.py writes it). Each recorder file is
//   { feed, params, events: [ { t, id, generation, seq, event } ] }
// A subscription whose feed+params match a recording replays its events with their original timing
// (gaps capped). No match → a truthful error section, so missing recordings are visible, not faked.
(function (CW) {
  'use strict';

  CW.adapters = CW.adapters || {};

  const stable = v => Array.isArray(v) ? `[${v.map(stable).join(',')}]`
    : (v && typeof v === 'object') ? `{${Object.keys(v).sort().map(k => JSON.stringify(k) + ':' + stable(v[k])).join(',')}}`
    : JSON.stringify(v);

  CW.adapters.fixture = function (base) {
    base = base || 'fixtures/';
    const feedFns = [];
    const shellFns = [];
    const timers = new Map();
    const paging = new Map();
    let nextId = 1;

    const recordings = fetch(base + 'index.json')
      .then(r => r.ok ? r.json() : [])
      .then(files => Promise.all(files.map(f => fetch(base + f).then(r => r.json()).catch(() => null))))
      .then(list => list.filter(Boolean))
      .catch(() => []);

    let shell = { account: { mode: 'local', username: '', initial: '' },
                  wallpaper: { url: '../../assets/wallpaper/cold-ripple.jpg', kind: 'image' }, covered: false, profileRevision: 1 };
    fetch(base + 'shell.json').then(r => r.ok ? r.json() : null).catch(() => null).then(state => {
      if (state) shell = state;
      shellFns.forEach(fn => fn(shell));
    });

    const emit = env => feedFns.forEach(fn => fn(env));

    function replay(id, rec) {
      const events = rec.events || [];
      const t0 = events.length ? events[0].t : 0;
      const handles = [];
      const state = { rec, seq: 0, cursors: {} };
      paging.set(id, state);
      events.forEach(e => {
        const delay = Math.min(1500, Math.max(0, (e.t - t0)));
        handles.push(setTimeout(() => emit({ id, generation: e.generation || 1,
          seq: ++state.seq, event: e.event }), delay));
      });
      timers.set(id, handles);
    }

    function missing(id, feed, params) {
      emit({ id, generation: 1, seq: 1, event: { type: 'reset', sections: [{
        id: 'fixture.missing', index: 0, title: 'No recording', layout: 'rail', state: 'error', items: [],
        error: `No recorded data for ${feed} ${stable(params)}. Record it with COLOSSEUM_WEBUI_RECORD.`
      }] } });
    }

    return {
      name: 'fixture',
      onFeedEvent: fn => feedFns.push(fn),
      onShellEvent: fn => shellFns.push(fn),
      subscribe(feed, params) {
        const id = nextId++;
        recordings.then(list => {
          const want = stable(params || {});
          const rec = list.find(r => r.feed === feed && stable(r.params || {}) === want);
          rec ? replay(id, rec) : missing(id, feed, params);
        });
        return Promise.resolve({ ok: true, id, generation: 1 });
      },
      unsubscribe(id) {
        (timers.get(id) || []).forEach(clearTimeout);
        timers.delete(id);
        paging.delete(id);
      },
      /** dev only: change shell state, e.g. setShell({covered:true}) to rehearse a native destination. */
      setShell(patch) { shell = { ...shell, ...patch }; shellFns.forEach(fn => fn(shell)); },
      more(id, sectionId) {
        // An explicit test recording may include recorded section windows to release per click.
        const state = paging.get(id);
        const pages = state?.rec.pages?.[sectionId] || [];
        const cursor = state?.cursors[sectionId] || 0;
        if (!state || cursor >= pages.length)
          return Promise.resolve({ ok: false, error: 'Recorded data has no further pages.' });
        state.cursors[sectionId] = cursor + 1;
        emit({ id, generation: 1, seq: ++state.seq,
          event: { type: 'section', section: pages[cursor] } });
        return Promise.resolve({ ok: true });
      },
      act(action, payload) {
        const what = payload && payload.item ? `${action} → ${payload.item.title}` : action;
        if (CW.toast) CW.toast(`Fixture mode: Colosseum would handle “${what}”`);
        return Promise.resolve({ ok: true, result: 'fixture' });
      }
    };
  };
})(window.CW = window.CW || {});
