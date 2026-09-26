// app.js — bootstrap only (CONTRACT §4). Picks the adapter, builds the port, starts the shell.
// Inside Colosseum → QWebChannel. In a plain browser (no qt) → recorded fixtures + a small dev panel.
(function (CW) {
  'use strict';
  const query = new URLSearchParams(location.search);
  const root = document.documentElement;
  if (query.get('power') === 'low') root.dataset.power = 'low';

  const adapter = (query.get('adapter') !== 'fixture' && CW.adapters.qwebchannel()) || CW.adapters.fixture();
  const port = CW.createPort(adapter);
  CW.port = port;
  root.dataset.adapter = adapter.name;
  CW.shell.start(port);
  // CONTRACT §12.4: tell native which destinations web owns, so `open` can answer with a web route.
  port.act('shell.surfaces', { names: CW.router.names().filter(n => n !== 'reference') });

  if (adapter.name !== 'fixture') return;

  // ---- dev panel (fixture mode only): rehearse states the swarm must handle ----
  const { h } = CW;
  const toggle = (label, on, off) => {
    let state = false;
    const b = h('button.devb', { type: 'button', onclick: () => { state = !state; b.classList.toggle('on', state); (state ? on : off)(); } }, label);
    return b;
  };
  document.body.appendChild(h('div.devpanel', { 'aria-hidden': true },
    h('span', {}, 'fixture mode'),
    toggle('low power', () => { root.dataset.power = 'low'; }, () => { delete root.dataset.power; }),
    toggle('covered', () => adapter.setShell({ covered: true }), () => adapter.setShell({ covered: false }))));
  const css = document.createElement('style');
  css.textContent = '.devpanel{position:fixed;right:12px;bottom:12px;z-index:90;display:flex;gap:6px;align-items:center;padding:6px 8px;border-radius:9px;border:1px dashed rgba(255,255,255,.35);background:rgba(0,0,0,.72);font:12px var(--ui-stack);color:var(--inkDim)}'
    + '.devb{padding:3px 8px;border-radius:6px;border:1px solid rgba(255,255,255,.25)}.devb.on{background:var(--gold);color:#1a1408}';
  document.head.appendChild(css);
})(window.CW = window.CW || {});
