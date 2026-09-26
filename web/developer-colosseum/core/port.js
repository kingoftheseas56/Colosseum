// core/port.js — THE CONTRACT (CONTRACT.md v1.1 §3). Owned by Claude; swarm agents consume, never edit.
//
// Surfaces talk to Colosseum only through CW.port. The port validates every inbound event, drops stale ones
// (closed subscription, old generation, non-increasing seq), keeps each subscription's ordered sections, and
// hands the surface the whole ordered list plus what changed — so a surface re-renders only that section.
(function (CW) {
  'use strict';

  const WORLDS = ['Tankoban', 'Biblio', 'Theatre'];
  const KINDS = ['movie', 'series', 'anime', 'manga', 'comic', 'book', 'audiobook'];
  const LAYOUTS = ['hero', 'rail', 'grid', 'list', 'continue'];
  const STATES = ['loading', 'ready', 'empty', 'error'];
  const TABS = {
    Tankoban: [['discover', 'Discover'], ['manga', 'Manga'], ['comics', 'Comics'], ['library', 'Library']],
    Biblio: [['discover', 'Discover'], ['explore', 'Explore'], ['library', 'Library']],
    Theatre: [['discover', 'Discover'], ['movies', 'Movies'], ['shows', 'Shows'], ['anime', 'Anime'], ['library', 'Library']]
  };
  const FEEDS = {
    home: () => true,
    continue: p => ['all', 'Tankoban', 'Biblio', 'Theatre'].includes(p.scope),
    world: p => WORLDS.includes(p.world) && TABS[p.world].some(([k]) => k === p.tab),
    seeAll: p => !!p.route && typeof p.route === 'object',
    search: p => typeof p.query === 'string' && ['all', ...WORLDS].includes(p.scope)
  };

  const isObj = v => !!v && typeof v === 'object' && !Array.isArray(v);
  const isStr = v => typeof v === 'string';

  function itemProblem(it) {
    if (!isObj(it)) return 'item is not an object';
    if (!isStr(it.key) || !it.key) return 'item.key missing';
    if (!WORLDS.includes(it.world)) return `item ${it.key}: bad world "${it.world}"`;
    if (!KINDS.includes(it.kind)) return `item ${it.key}: bad kind "${it.kind}"`;
    if (!isStr(it.title)) return `item ${it.key}: title missing`;
    if (!isObj(it.ref)) return `item ${it.key}: ref missing`;
    if (it.progress != null && !(it.progress >= 0 && it.progress <= 1)) return `item ${it.key}: progress out of range`;
    return null;
  }

  function sectionProblem(s) {
    if (!isObj(s)) return 'section is not an object';
    if (!isStr(s.id) || !s.id) return 'section.id missing';
    if (typeof s.index !== 'number') return `section ${s.id}: index missing`;
    if (!LAYOUTS.includes(s.layout)) return `section ${s.id}: bad layout "${s.layout}"`;
    if (!STATES.includes(s.state)) return `section ${s.id}: bad state "${s.state}"`;
    if (!Array.isArray(s.items)) return `section ${s.id}: items is not an array`;
    for (const it of s.items) { const p = itemProblem(it); if (p) return `section ${s.id}: ${p}`; }
    return null;
  }

  function createPort(adapter) {
    const subs = new Map();        // id → { feed, params, generation, seq, sections: Map, onEvent }
    const early = new Map();       // id → envelopes that arrived before subscribe() resolved
    const shellListeners = new Set();
    let shell = null;

    function ordered(sub) {
      return [...sub.sections.values()].sort((a, b) => a.index - b.index);
    }

    function deliver(env) {
      const sub = subs.get(env.id);
      if (!sub) {
        if (!early.has(env.id)) early.set(env.id, []);
        early.get(env.id).push(env);
        setTimeout(() => early.delete(env.id), 5000);
        return;
      }
      if (env.generation < sub.generation) return;
      if (env.generation > sub.generation) { sub.generation = env.generation; sub.seq = 0; }
      if (!(env.seq > sub.seq)) return;
      sub.seq = env.seq;

      const ev = env.event || {};
      let changed = null;
      if (ev.type === 'reset') {
        if (!Array.isArray(ev.sections)) return drop(sub, 'reset without sections');
        const next = new Map();
        for (const s of ev.sections) {
          const p = sectionProblem(s);
          if (p) { drop(sub, p); continue; }
          next.set(s.id, s);
        }
        sub.sections = next;
      } else if (ev.type === 'section') {
        const p = sectionProblem(ev.section);
        if (p) return drop(sub, p);
        sub.sections.set(ev.section.id, ev.section);
        changed = ev.section.id;
      } else if (ev.type === 'remove') {
        if (!isStr(ev.id)) return drop(sub, 'remove without id');
        sub.sections.delete(ev.id);
        changed = ev.id;
      } else {
        return drop(sub, `unknown event type "${ev.type}"`);
      }
      try {
        sub.onEvent({ type: ev.type, changed, sections: ordered(sub) });
      } catch (err) {
        console.error('[port] surface handler threw', err);
      }
    }

    function drop(sub, why) {
      console.warn(`[port] dropped event on ${sub.feed}: ${why}`);
    }

    adapter.onFeedEvent(deliver);
    adapter.onShellEvent(state => {
      const revChanged = shell && shell.profileRevision !== state.profileRevision;
      shell = state;
      for (const fn of shellListeners) fn(state, { profileChanged: revChanged });
    });

    return Object.freeze({
      /** subscribe(feed, params, onEvent) → { close() }. onEvent({type, changed, sections}) */
      subscribe(feed, params, onEvent) {
        params = params || {};
        const handle = { id: null, closed: false, close() {
          handle.closed = true;
          if (handle.id != null) { subs.delete(handle.id); adapter.unsubscribe(handle.id); }
        } };
        if (!FEEDS[feed] || !FEEDS[feed](params)) {
          console.error(`[port] invalid subscribe: ${feed}`, params);
          onEvent({ type: 'reset', changed: null, sections: [] });
          return handle;
        }
        adapter.subscribe(feed, params).then(res => {
          if (!res || !res.ok) {
            console.error(`[port] subscribe failed: ${feed}`, res && res.error);
            return;
          }
          if (handle.closed) { adapter.unsubscribe(res.id); return; }
          handle.id = res.id;
          subs.set(res.id, { feed, params, generation: 0, seq: 0, sections: new Map(), onEvent });
          const waiting = early.get(res.id) || [];
          early.delete(res.id);
          waiting.forEach(deliver);
        });
        return handle;
      },

      /** more(handle, sectionId) → Promise<{ok, error?}> — next page of a section with hasMore. */
      more(handle, sectionId) {
        if (!handle || handle.id == null) return Promise.resolve({ ok: false, error: 'Not ready yet.' });
        return adapter.more(handle.id, sectionId);
      },

      /** act(action, payload) → Promise<{ok, result?, error?}> — resolves when native has finished. */
      act(action, payload) {
        return adapter.act(action, payload || {}).then(
          r => (r && typeof r.ok === 'boolean') ? r : { ok: false, error: 'Colosseum did not answer.' },
          () => ({ ok: false, error: 'Colosseum did not answer.' }));
      },

      /** onShell(fn) → unsubscribe. fn(state, {profileChanged}) — called immediately if state is known. */
      onShell(fn) {
        shellListeners.add(fn);
        if (shell) fn(shell, { profileChanged: false });
        return () => shellListeners.delete(fn);
      },

      shell: () => shell
    });
  }

  CW.createPort = createPort;
  CW.contract = Object.freeze({ WORLDS, KINDS, LAYOUTS, STATES,
    TABS: Object.fromEntries(Object.entries(TABS).map(([w, t]) => [w, t.map(([key, label]) => ({ key, label }))])) });
})(window.CW = window.CW || {});
