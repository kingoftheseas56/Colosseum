// components/tab-bar.js — TheatreTabBar geometry, shared by every world. The in-page bar docks under the
// TopBar (#dock) once it scrolls away, and the docked copy stays in sync.
(function (CW) {
  'use strict';
  const { h } = CW;

  /** tabBar(tabs:[{key,label}], active, onPick) → element. Call .dispose() when the surface unmounts. */
  function tabBar(tabs, active, onPick) {
    const build = cls => h('div.tabs.glass' + (cls || ''), { role: 'tablist' }, tabs.map(t =>
      h('button.tab' + (t.key === active ? '.on' : ''), {
        type: 'button', role: 'tab', 'aria-selected': t.key === active ? 'true' : 'false',
        'data-focus': true, 'data-key': 'tab:' + t.key, onclick: () => onPick(t.key)
      }, t.label)));

    const host = h('div.tabs-host', {}, build());
    const dock = document.getElementById('dock');
    const board = document.getElementById('board');
    if (dock) dock.replaceChildren(build());

    let io = null;
    if (dock && board && 'IntersectionObserver' in window) {
      io = new IntersectionObserver(([e]) => {
        const docked = !e.isIntersecting && e.boundingClientRect.top < e.rootBounds.top;
        document.body.classList.toggle('docked', docked);
        dock.setAttribute('aria-hidden', docked ? 'false' : 'true');
        dock.inert = !docked;
      }, { root: board, threshold: 0 });
      io.observe(host);
    }
    host.dispose = () => {
      if (io) io.disconnect();
      document.body.classList.remove('docked');
      if (dock) { dock.replaceChildren(); dock.setAttribute('aria-hidden', 'true'); dock.inert = true; }
    };
    return host;
  }

  CW.tabBar = tabBar;
})(window.CW = window.CW || {});
