// surfaces/biblio/surface.js — Biblio world browsing surface.
(function (CW) {
  'use strict';

  const TABS = [
    { key: 'discover', label: 'Discover' },
    { key: 'explore', label: 'Explore' },
    { key: 'library', label: 'Library' }
  ];

  function mount(el, route, env) {
    const chrome = CW.h('div.world-pane.biblio-chrome');
    const tabsHost = CW.h('div.biblio-tabs');
    const controls = CW.h('div.biblio-local-controls');
    const body = CW.h('div.world-pane.biblio-body');
    el.appendChild(CW.h('div.biblio-surface', {}, chrome, tabsHost, controls, body));

    const views = { discover: {}, explore: {}, library: { sort: 'added' } };
    let activeTab = '';
    let tabs = null;
    let sub = null;
    let queryTimer = 0;
    let latest = [];

    function currentView() {
      return views[activeTab] || (views[activeTab] = {});
    }

    function params() {
      const view = { ...currentView() };
      const base = { world: 'Biblio', tab: activeTab };
      return Object.keys(view).length ? { ...base, view } : base;
    }

    function resubscribe() {
      if (sub) sub.close();
      sub = env.port.subscribe('world', params(), sync);
    }

    function applyView(patch) {
      const delta = patch || {};
      const queryOnly = activeTab === 'library'
        && Object.keys(delta).length === 1
        && Object.prototype.hasOwnProperty.call(delta, 'query');
      const next = { ...currentView(), ...delta };
      Object.keys(next).forEach(key => {
        if (next[key] === '' || next[key] == null) delete next[key];
      });
      views[activeTab] = next;
      // Do not replace the live search input while the user is typing. The feed
      // resubscribe updates only the section body, so focus and caret stay put.
      if (!queryOnly) renderLocalControls();
      resubscribe();
    }

    function rowAction(action, section, extra) {
      return env.act(action, {
        feed: 'world', params: params(), sectionId: section.id, ...(extra || {})
      }).then(answer => {
        if (answer && answer.ok) resubscribe();
        return answer;
      });
    }

    const ctx = {
      open: env.open,
      forget: env.forget,
      act: env.act,
      seeAll(section) {
        const id = String(section && section.id || '');
        if (id === 'biblio.explore.most-read' || id === 'biblio.explore.classics') {
          const catalogue = id.substring('biblio.explore.'.length);
          return env.act('world.biblio.requestEnrichment', { catalogue })
            .then(() => env.seeAll(section));
        }
        return env.seeAll(section);
      },
      removeChoice: env.removeChoice,
      choose(choice, section) {
        return env.choose(choice, section, null, applyView);
      },
      more(section) {
        return sub ? env.more(sub, section) : Promise.resolve({ ok: false });
      }
    };

    function decoratePrefs() {
      if (activeTab !== 'explore' || currentView().customize !== true) return;
      latest.filter(section => section.pref).forEach(section => {
        const selector = '[data-section="' + CSS.escape(section.id) + '"]';
        const widget = body.querySelector(selector);
        const header = widget && widget.querySelector('.wh');
        if (!header || header.querySelector('.biblio-pref-actions')) return;
        header.appendChild(CW.h('span.biblio-pref-actions', {},
          CW.h('button.more', {
            type: 'button', 'data-focus': true, 'data-key': section.id + '#up',
            'aria-label': 'Move ' + section.title + ' up',
            onclick: () => rowAction('rows.move', section, { dir: -1 })
          }, '↑'),
          CW.h('button.more', {
            type: 'button', 'data-focus': true, 'data-key': section.id + '#down',
            'aria-label': 'Move ' + section.title + ' down',
            onclick: () => rowAction('rows.move', section, { dir: 1 })
          }, '↓'),
          CW.h('button.more', {
            type: 'button', 'data-focus': true, 'data-key': section.id + '#visibility',
            onclick: () => rowAction('rows.hide', section, { hidden: !section.pref.hidden })
          }, section.pref.hidden ? 'Show' : 'Hide')));
      });
    }

    function sync(ev) {
      latest = ev.sections || [];
      const chromeSections = latest.filter(section =>
        String(section.id || '').startsWith('biblio.chrome.'));
      const bodySections = latest.filter(section =>
        !String(section.id || '').startsWith('biblio.chrome.'));
      if (ev.type === 'reset' || !ev.changed) {
        CW.section.sync(chrome, { type: 'reset', sections: chromeSections }, ctx);
        CW.section.sync(body, { type: 'reset', sections: bodySections }, ctx);
      } else {
        const changed = String(ev.changed);
        if (changed.startsWith('biblio.chrome.'))
          CW.section.sync(chrome, { type: ev.type, changed, sections: chromeSections }, ctx);
        else
          CW.section.sync(body, { type: ev.type, changed, sections: bodySections }, ctx);
      }
      decoratePrefs();
    }

    function renderLocalControls() {
      clearTimeout(queryTimer);
      controls.replaceChildren();
      if (activeTab === 'library') {
        const input = CW.h('input', {
          type: 'search', autocomplete: 'off',
          placeholder: 'Search by title or author',
          'aria-label': 'Search by title or author',
          'data-focus': true, 'data-key': 'biblio:library:query',
          value: currentView().query || ''
        });
        input.addEventListener('input', () => {
          clearTimeout(queryTimer);
          queryTimer = setTimeout(() => applyView({ query: input.value.trim() }), 180);
        });
        controls.appendChild(CW.h('label.sfield.biblio-library-search', {}, input));
      } else if (activeTab === 'explore' && currentView().customize === true) {
        controls.appendChild(CW.h('button.more', {
          type: 'button', 'data-focus': true, 'data-key': 'biblio:explore:reset',
          onclick: () => env.act('rows.reset', {
            feed: 'world', params: params()
          }).then(answer => { if (answer && answer.ok) resubscribe(); })
        }, 'Reset shelves'));
      }
    }

    function installTabs(tab) {
      if (tabs && tabs.dispose) tabs.dispose();
      tabs = CW.tabBar(TABS, tab, key => {
        if (key !== activeTab)
          env.router.go({ name: 'world', world: 'Biblio', tab: key }, { replace: true });
      });
      tabsHost.replaceChildren(tabs);
    }

    function subscribe(tab) {
      if (sub) sub.close();
      activeTab = tab;
      latest = [];
      installTabs(tab);
      renderLocalControls();
      sub = env.port.subscribe('world', params(), sync);
    }

    subscribe(route.tab || 'discover');

    return {
      update(next) {
        const tab = next.tab || 'discover';
        if (tab !== activeTab) subscribe(tab);
      },
      unmount() {
        clearTimeout(queryTimer);
        if (sub) sub.close();
        if (tabs && tabs.dispose) tabs.dispose();
      }
    };
  }

  CW.router.register('biblio', { mount });
})(window.CW = window.CW || {});
