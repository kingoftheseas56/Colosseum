(function (CW) {
  'use strict';

  const { h } = CW;
  const FEED = 'page.extensions';
  const ACTION = 'page.extensions.';
  const VIEWS = [
    { key: 'chain', label: 'Chain' },
    { key: 'house', label: 'House' },
    { key: 'explore', label: 'Explore' }
  ];

  CW.router.register('page.extensions', {
    mount(el, initialRoute, env) {
      function routeWorld(params) {
        const world = String((params && params.world) || '').trim().toLowerCase();
        return ['tankoban', 'biblio', 'theatre'].includes(world) ? world : '';
      }

      function contextualView(params, fallback) {
        const explicit = String((params && params.view) || '').trim().toLowerCase();
        if (VIEWS.some(candidate => candidate.key === explicit)) return explicit;
        const world = routeWorld(params);
        if (world === 'tankoban' || world === 'biblio') return 'house';
        if (world === 'theatre') return 'explore';
        return fallback || 'chain';
      }

      function contextualFilter(params, fallback) {
        const world = routeWorld(params);
        if (world === 'tankoban' || world === 'biblio') return world;
        return fallback || 'all';
      }

      let route = initialRoute;
      let view = contextualView(route.params, 'chain');
      let query = (route.params && route.params.query) || '';
      let sub = null;
      let searchTimer = 0;
      let installedCount = null;
      let houseFilter = contextualFilter(route.params, 'all');
      let configuring = null;
      const pendingRemove = new Set();

      const page = h('div.ext-page');
      const count = h('div.ext-count', {}, 'Loading extensions…');
      const search = h('input.ext-search', {
        type: 'search',
        autocomplete: 'off',
        placeholder: 'Search extensions',
        'aria-label': 'Search extensions',
        'data-focus': true,
        'data-key': 'extensions#search',
        value: query
      });
      const tabs = h('div.ext-tabs');
      const feedBox = h('div.ext-feed');
      const panelBox = h('div.ext-panel-host');

      function pageRoute(params, replace) {
        env.router.go({ name: 'page', page: 'extensions', params }, { replace: !!replace });
      }

      function refreshTabs() {
        tabs.replaceChildren(...VIEWS.map(tab =>
          h('button.ext-tab' + (tab.key === view ? '.on' : ''), {
            type: 'button',
            'data-focus': true,
            'data-key': 'extensions#tab-' + tab.key,
            'aria-pressed': tab.key === view ? 'true' : 'false',
            onclick: () => pageRoute({ view: tab.key, query }, true)
          }, tab.label)
        ));
      }

      function updateSummary(data) {
        if (!data || typeof data.installedCount !== 'number') return;
        installedCount = data.installedCount;
        const enabled = typeof data.enabledCount === 'number' ? data.enabledCount : installedCount;
        count.textContent = installedCount + ' installed · ' + enabled + ' carrying';
      }

      function act(name, payload) {
        return env.act(ACTION + name, payload);
      }

      function face(row, cls) {
        const mark = (row.name || '?').trim().slice(0, 1).toUpperCase();
        const img = CW.face(row.logo || '', mark);
        if (cls) img.classList.add(cls);
        return img;
      }

      function controlKey(row, action, context) {
        return row.token + '#' + action + (context ? '-' + context : '');
      }

      function toggle(row, context) {
        return h('button.ext-switch' + (row.enabled ? '.on' : ''), {
          type: 'button',
          'data-focus': true,
          'data-key': controlKey(row, 'toggle', context),
          'aria-label': (row.enabled ? 'Disable ' : 'Enable ') + row.name,
          'aria-pressed': row.enabled ? 'true' : 'false',
          onclick: () => act('enable', { token: row.token, enabled: !row.enabled })
        }, h('span.ext-switch-knob'));
      }

      function removeButton(row, context) {
        const button = h('button.ext-link', {
          type: 'button',
          'data-focus': true,
          'data-key': controlKey(row, 'remove', context),
          onclick: async () => {
            const confirm = pendingRemove.has(row.token);
            const result = await act('remove', { token: row.token, confirm });
            if (result && result.ok) {
              pendingRemove.delete(row.token);
              return;
            }
            if (result && /Press Remove again/i.test(result.error || '')) {
              pendingRemove.add(row.token);
              button.textContent = 'Confirm remove';
            }
          }
        }, pendingRemove.has(row.token) ? 'Confirm remove' : 'Remove');
        return button;
      }

      function configureButton(row, context) {
        if (!(row.configurable || row.configurationRequired || row.id === 'colosseum.well.tankoyomi')) return null;
        return h('button.ext-link', {
          type: 'button',
          'data-focus': true,
          'data-key': controlKey(row, 'configure', context),
          onclick: () => pageRoute({ view, query, configure: row.token })
        }, row.configurationRequired ? 'Configure required' : (row.house ? 'Settings' : 'Configure ↗'));
      }

      function reorderButton(row, world, delta, enabled) {
        return h('button.ext-rank-button', {
          type: 'button',
          disabled: !enabled,
          'data-focus': enabled ? true : null,
          'data-key': enabled ? row.token + (delta < 0 ? '#up-' : '#down-') + world : null,
          'aria-label': (delta < 0 ? 'Move ' : 'Move ') + row.name + (delta < 0 ? ' up' : ' down'),
          onclick: () => act('reorder', { token: row.token, world, delta })
        }, delta < 0 ? '↑' : '↓');
      }

      function chainDoor(world, hasSource) {
        const theatre = world.key === 'theatre';
        if (theatre && hasSource) return null;
        const target = theatre ? 'explore' : 'house';
        const lead = hasSource ? 'Add another · ' : 'No source yet · ';
        return h('span.ext-chain-empty', {},
          lead,
          h('button.ext-link', {
            type: 'button',
            'data-focus': true,
            'data-key': 'extensions#chain-' + world.key + '-' + target,
            onclick: () => pageRoute({ view: target, query }, true)
          }, (theatre ? 'Explore' : 'House') + ' ›'));
      }

      function renderChain(section) {
        const data = section.data || {};
        updateSummary(data);
        return h('div.ext-chain', {},
          h('div.ext-chain-kicker', {}, 'ASKED IN THIS ORDER'),
          (data.worlds || []).map(world => {
            const rows = world.rows || [];
            const catalogues = rows.filter(row => row.catalogue);
            const sources = rows.filter(row => row.source);
            return h('section.ext-chain-world', {},
              h('div.ext-chain-world-name', {}, world.title),
              h('div.ext-chain-flow', {},
                catalogues.map(row =>
                  h('div.ext-chain-node.catalogue', { 'data-key': row.token },
                    face(row, 'ext-chain-face'),
                    h('span.ext-chain-name', {}, row.name),
                    h('span.ext-lock', {}, 'HOUSE CATALOG')
                  )),
                h('span.ext-chain-divider', { 'aria-hidden': true }),
                sources.length ? [
                  sources.map((row, index) =>
                  h('div.ext-chain-source', {},
                    h('span.ext-rank', {}, String(row.rank || index + 1)),
                    face(row, 'ext-chain-face'),
                    h('span.ext-chain-copy', {},
                      h('b', {}, row.name),
                      row.tie ? h('small', {}, row.tie) : null
                    ),
                    toggle(row, world.key),
                    h('span.ext-order', {},
                      reorderButton(row, world.key, -1, index > 0),
                      reorderButton(row, world.key, 1, index < sources.length - 1)
                    ),
                    configureButton(row, world.key),
                    removeButton(row, world.key)
                  )
                  ),
                  chainDoor(world, true)
                ] : chainDoor(world, false)
              )
            );
          })
        );
      }

      function houseWorlds(row) {
        return (row.worlds || []).filter(x => x !== 'universes')
          .map(x => x.charAt(0).toUpperCase() + x.slice(1)).join(' · ');
      }

      function renderHouse(section) {
        const data = section.data || {};
        updateSummary(data);
        const allRows = data.rows || [];
        const visible = allRows.filter(row => houseFilter === 'all' || (row.worlds || []).includes(houseFilter));
        let root = null;

        function pickFilter(next) {
          houseFilter = next;
          const fresh = renderHouse(section);
          root.replaceWith(fresh);
          requestAnimationFrame(() => {
            const target = fresh.querySelector('[data-key="extensions#filter-' + next + '"]');
            if (target) target.focus({ preventScroll: true });
          });
        }

        const filter = key => {
          const label = key === 'all' ? 'All' : key.charAt(0).toUpperCase() + key.slice(1);
          const n = key === 'all' ? allRows.length
            : allRows.filter(row => (row.worlds || []).includes(key)).length;
          return h('button.ext-filter' + (houseFilter === key ? '.on' : ''), {
            type: 'button',
            'data-focus': true,
            'data-key': 'extensions#filter-' + key,
            'aria-pressed': houseFilter === key ? 'true' : 'false',
            onclick: () => pickFilter(key)
          }, label, h('b', {}, String(n)));
        };

        root = h('div.ext-house', {},
          h('div.ext-house-tools', {},
            h('div.ext-filters', {}, filter('all'), filter('tankoban'), filter('biblio'))
          ),
          h('div.ext-house-list', {},
            visible.map(row =>
              h('article.ext-house-card' + ((row.worlds || []).length > 1 ? '.shared' : ''), {
                'data-key': row.token
              },
                face(row, 'ext-house-logo'),
                h('div.ext-house-copy', {},
                  h('div.ext-kicker', {}, houseWorlds(row) + ' · ' + (row.job || 'SOURCE')),
                  h('h3', {}, row.name),
                  h('p', {}, row.description || 'Colosseum source')
                ),
                h('div.ext-house-actions', {},
                  h('span.ext-status', {}, row.enabled ? '✓ Added' : 'Paused'),
                  configureButton(row),
                  row.core ? h('span.ext-lock', {}, 'Locked') : toggle(row),
                  row.core ? null : removeButton(row)
                )
              )
            )
          )
        );
        return root;
      }

      function installRoute(row) {
        pageRoute({ view, query, install: true, url: row.installUrl || '', id: row.id || '' });
      }

      function installButton(row, context) {
        if (row.installed) return h('span.ext-pill.done', {}, '✓ Installed');
        return h('button.ext-pill', {
          type: 'button',
          'data-focus': true,
          'data-key': 'community:' + row.id + '#install-' + context,
          onclick: () => installRoute(row)
        }, row.configurable || row.configurationRequired ? 'Configure & install' : 'Install');
      }

      function renderCommunityCard(row, context) {
        return h('article.ext-community-card', {},
          face(row, 'ext-community-icon'),
          h('div.ext-community-title', {}, row.name),
          h('div.ext-community-job', {}, row.kind || 'Extension'),
          h('div.ext-community-stats', {},
            row.stars ? '★ ' + row.stars.toLocaleString() : 'Community extension'),
          h('div.ext-community-action', {}, installButton(row, context))
        );
      }

      function renderCommunityList(row, index, context) {
        return h('div.ext-community-row', {},
          h('span.ext-community-rank', {}, String(index + 1)),
          face(row, 'ext-list-icon'),
          h('span.ext-list-copy', {},
            h('b', {}, row.name),
            h('small', {}, row.kind || 'Extension')
          ),
          h('span.ext-stars', {}, row.stars ? '★ ' + row.stars.toLocaleString() : ''),
          installButton(row, context)
        );
      }

      function renderExploreUniverses(section) {
        updateSummary(section.data || {});
        return h('div.ext-universes', {},
          section.items.map(item =>
            h('button.ext-universe', {
              type: 'button',
              'data-focus': true,
              'data-key': item.key,
              onclick: () => env.open(item, 'details')
            },
              item.backdrop ? CW.face(item.backdrop, '') : null,
              h('span.ext-universe-scrim'),
              h('span.ext-universe-copy', {},
                h('small', {}, 'UNIVERSE'),
                h('b', {}, item.title),
                h('span', {}, 'Enter ›')
              )
            )
          )
        );
      }

      function renderExploreJobs(section) {
        const data = section.data || {};
        updateSummary(data);
        return h('div.ext-job-grid', {},
          (data.rows || []).map(row =>
            h('button.ext-job', {
              type: 'button',
              'data-focus': true,
              'data-key': 'extensions#job-' + row.name,
              onclick: () => {
                search.value = row.name;
                query = row.name;
                resubscribe();
              }
            },
              h('b', {}, row.name),
              h('span', {}, row.count + ' extensions')
            )
          )
        );
      }

      function renderExploreCards(section) {
        const data = section.data || {};
        updateSummary(data);
        const rows = data.rows || [];
        if (section.id.endsWith('.starred'))
          return h('div.ext-community-list', {},
            rows.map((row, index) => renderCommunityList(row, index, section.id)));
        return h('div.ext-community-rail', {},
          rows.map(row => renderCommunityCard(row, section.id)));
      }

      function custom(section) {
        if (section.state === 'empty') {
          return CW.section.note('Nothing here yet', section.error || null);
        }
        const schema = section.data && section.data.schema;
        if (schema === 'extensions.chain') return renderChain(section);
        if (schema === 'extensions.house') return renderHouse(section);
        if (schema === 'extensions.explore.universes') return renderExploreUniverses(section);
        if (schema === 'extensions.explore.jobs') return renderExploreJobs(section);
        if (schema === 'extensions.explore.cards') return renderExploreCards(section);
        return CW.section.note('Unknown Extensions record', schema || section.id, 'err');
      }

      const ctx = {
        open: env.open,
        act: env.act,
        custom
      };

      function resubscribe() {
        if (sub) sub.close();
        feedBox.replaceChildren();
        sub = env.port.subscribe(FEED, { view, query }, event =>
          CW.section.sync(feedBox, event, ctx));
      }

      function closePanel() {
        if (env.router.depth() > 1) env.router.back();
        else pageRoute({ view, query }, true);
      }

      function panelShell(title, kicker, body) {
        return h('section.ext-panel', {},
          h('div.ext-panel-head', {},
            h('div', {},
              h('div.ext-kicker', {}, kicker),
              h('h2', {}, title)
            ),
            h('button.ext-panel-close', {
              type: 'button',
              'data-focus': true,
              'data-key': 'extensions#panel-close',
              'aria-label': 'Back to Extensions',
              onclick: closePanel
            }, 'Back')
          ),
          body
        );
      }

      function renderTankoyomiPanel(token, state) {
        configuring = state;
        const selected = state.selectedLanguage || '';
        const languageButtons = (state.languages || []).map(language =>
          h('button.ext-language' + (language.code === selected ? '.on' : ''), {
            type: 'button',
            'data-focus': true,
            'data-key': 'tankoyomi#language-' + language.code,
            'aria-pressed': language.code === selected ? 'true' : 'false',
            onclick: async () => {
              const result = await act('configure', { token, op: 'state', language: language.code });
              if (result && result.ok) renderTankoyomiPanel(token, result.result);
            }
          }, language.label || language.code)
        );

        const providers = (state.providers || []).map((provider, index, rows) =>
          h('div.ext-provider-row', {},
            h('span.ext-provider-rank', {}, String(index + 1)),
            h('span.ext-provider-copy', {},
              h('b', {}, provider.name || provider.id),
              h('small', {}, (provider.allowedHosts || []).join(' · ') || provider.id)
            ),
            h('button.ext-switch' + (provider.enabled ? '.on' : ''), {
              type: 'button',
              'data-focus': true,
              'data-key': 'tankoyomi#provider-' + provider.id + '-toggle',
              'aria-pressed': provider.enabled ? 'true' : 'false',
              onclick: async () => {
                const result = await act('configure', {
                  token, op: 'providerEnabled', language: selected,
                  providerId: provider.id, enabled: !provider.enabled
                });
                if (result && result.ok) renderTankoyomiPanel(token, result.result);
              }
            }, h('span.ext-switch-knob')),
            h('button.ext-rank-button', {
              type: 'button',
              disabled: index === 0,
              'data-focus': index > 0 ? true : null,
              'data-key': index > 0 ? 'tankoyomi#provider-' + provider.id + '-up' : null,
              onclick: async () => {
                const result = await act('configure', {
                  token, op: 'providerMove', language: selected,
                  providerId: provider.id, direction: -1
                });
                if (result && result.ok) renderTankoyomiPanel(token, result.result);
              }
            }, '↑'),
            h('button.ext-rank-button', {
              type: 'button',
              disabled: index === rows.length - 1,
              'data-focus': index < rows.length - 1 ? true : null,
              'data-key': index < rows.length - 1 ? 'tankoyomi#provider-' + provider.id + '-down' : null,
              onclick: async () => {
                const result = await act('configure', {
                  token, op: 'providerMove', language: selected,
                  providerId: provider.id, direction: 1
                });
                if (result && result.ok) renderTankoyomiPanel(token, result.result);
              }
            }, '↓')
          )
        );

        const body = h('div.ext-config-body', {},
          h('div.ext-config-top', {},
            h('div', {},
              h('div.ext-kicker', {}, 'COLOSSEUM · STORE · CONFIGURATION'),
              h('h3', {}, 'Tankoyomi'),
              h('p', {}, 'Configure chapter languages and the source order used inside Chapter Mode.')
            ),
            h('button.ext-switch' + (state.enabled ? '.on' : ''), {
              type: 'button',
              'data-focus': true,
              'data-key': 'tankoyomi#master',
              'aria-label': state.enabled ? 'Disable Tankoyomi' : 'Enable Tankoyomi',
              'aria-pressed': state.enabled ? 'true' : 'false',
              onclick: async () => {
                const result = await act('configure', {
                  token, op: 'master', language: selected, enabled: !state.enabled
                });
                if (result && result.ok) renderTankoyomiPanel(token, result.result);
              }
            }, h('span.ext-switch-knob'))
          ),
          h('div.ext-language-row', {}, languageButtons),
          selected && selected !== state.defaultLanguage
            ? h('button.ext-primary', {
                type: 'button',
                'data-focus': true,
                'data-key': 'tankoyomi#make-default',
                onclick: async () => {
                  const result = await act('configure', {
                    token, op: 'defaultLanguage', language: selected
                  });
                  if (result && result.ok) renderTankoyomiPanel(token, result.result);
                }
              }, 'Use ' + selected.toUpperCase() + ' by default')
            : h('span.ext-default-label', {}, selected ? selected.toUpperCase() + ' is the default' : ''),
          h('div.ext-provider-head', {},
            h('div', {}, h('b', {}, 'Auto-pick route'), h('small', {}, 'Highest active provider is tried first.')),
            h('button.ext-link', {
              type: 'button',
              'data-focus': true,
              'data-key': 'tankoyomi#reset-order',
              onclick: async () => {
                const result = await act('configure', {
                  token, op: 'resetOrder', language: selected
                });
                if (result && result.ok) renderTankoyomiPanel(token, result.result);
              }
            }, 'Reset order')
          ),
          h('div.ext-provider-list', {}, providers.length
            ? providers : h('div.ext-inline-empty', {}, 'No providers are configured for this language.'))
        );
        panelBox.replaceChildren(panelShell('Tankoyomi', 'SOURCE SETTINGS', body));
        requestAnimationFrame(() => {
          const first = panelBox.querySelector('[data-focus]');
          if (first) first.focus({ preventScroll: true });
        });
      }

      async function openConfigure(token) {
        panelBox.replaceChildren(panelShell('Loading…', 'SOURCE SETTINGS',
          h('div.ext-panel-loading', {}, 'Reading native configuration…')));
        const result = await act('configure', { token, op: 'state' });
        if (!result || !result.ok) {
          panelBox.replaceChildren(panelShell('Couldn’t open settings', 'SOURCE SETTINGS',
            h('div.ext-inline-error', {}, (result && result.error) || 'Colosseum could not open this source.')));
          return;
        }
        if (result.result && result.result.mode === 'tankoyomi') {
          renderTankoyomiPanel(token, result.result);
        } else if (result.result && result.result.mode === 'notice') {
          panelBox.replaceChildren(panelShell('Source settings', 'SOURCE SETTINGS',
            h('div.ext-inline-empty', {}, result.result.message || 'No settings are exposed here yet.')));
        } else {
          closePanel();
        }
      }

      function installPanel(params) {
        const input = h('input.ext-install-input', {
          type: 'url',
          autocomplete: 'off',
          spellcheck: 'false',
          placeholder: 'https://…/manifest.json',
          'aria-label': 'Extension address',
          'data-focus': true,
          'data-key': 'extensions#install-url',
          value: params.url || ''
        });
        const resultBox = h('div.ext-preview');

        async function preview() {
          resultBox.replaceChildren(h('div.ext-panel-loading', {}, 'Reading the manifest…'));
          const result = await act('preview', { url: input.value });
          if (!result || !result.ok) {
            resultBox.replaceChildren(h('div.ext-inline-error', {},
              (result && result.error) || 'That address could not be read.'));
            return;
          }
          const manifest = result.result || {};
          const install = h('button.ext-primary', {
            type: 'button',
            'data-focus': true,
            'data-key': 'extensions#install-confirm',
            onclick: async () => {
              if (manifest.configurationRequired) {
                const configured = await act('configure', { url: input.value, op: 'state' });
                if (configured && configured.ok) closePanel();
                return;
              }
              const done = await act('install', { url: input.value, id: manifest.id || params.id || '' });
              if (done && done.ok) {
                env.toast((done.result && done.result.name ? done.result.name : manifest.name) + ' installed');
                closePanel();
              } else {
                resultBox.appendChild(h('div.ext-inline-error', {},
                  (done && done.error) || 'The extension could not be installed.'));
              }
            }
          }, manifest.configurationRequired ? 'Configure ↗' : 'Install ' + (manifest.name || 'extension'));
          resultBox.replaceChildren(
            h('div.ext-preview-card', {},
              h('div.ext-kicker', {}, 'MANIFEST'),
              h('h3', {}, manifest.name || 'Extension'),
              manifest.description ? h('p', {}, manifest.description) : null,
              install
            )
          );
          requestAnimationFrame(() => install.focus({ preventScroll: true }));
        }

        const body = h('div.ext-install-body', {},
          h('p', {}, 'Paste a Stremio extension manifest address. Colosseum reads it natively before anything is installed.'),
          h('div.ext-install-row', {},
            input,
            h('button.ext-primary', {
              type: 'button',
              'data-focus': true,
              'data-key': 'extensions#preview',
              onclick: preview
            }, 'Read it first')
          ),
          resultBox
        );
        panelBox.replaceChildren(panelShell('Install from a link', 'EXTENSIONS', body));
        requestAnimationFrame(() => input.focus({ preventScroll: true }));
      }

      function renderPanel() {
        configuring = null;
        const params = route.params || {};
        if (params.configure) {
          openConfigure(params.configure);
        } else if (params.install) {
          installPanel(params);
        } else {
          panelBox.replaceChildren();
        }
      }

      search.addEventListener('input', () => {
        clearTimeout(searchTimer);
        searchTimer = setTimeout(() => {
          query = search.value.trim();
          resubscribe();
        }, 300);
      });

      const installLink = h('button.ext-install-link', {
        type: 'button',
        'data-focus': true,
        'data-key': 'extensions#install-link',
        onclick: () => pageRoute({ view, query, install: true })
      }, 'Install from a link ›');

      const header = h('header.ext-head', {},
        h('div.ext-kicker', {}, 'COLOSSEUM · STORE'),
        h('h1', {}, 'Extensions'),
        h('div.ext-rule'),
        count
      );
      const nav = h('div.ext-nav', {}, tabs, h('div.ext-nav-grow'), search, installLink);
      page.append(header, nav, feedBox, panelBox);
      el.appendChild(page);

      function applyRoute(next) {
        route = next;
        const nextView = contextualView(route.params, view || 'chain');
        const nextFilter = contextualFilter(route.params, houseFilter);
        const nextQuery = (route.params && route.params.query);
        const changedView = nextView !== view;
        const changedFilter = nextFilter !== houseFilter;
        const changedQuery = typeof nextQuery === 'string' && nextQuery !== query;
        view = nextView;
        houseFilter = nextFilter;
        if (changedQuery) {
          query = nextQuery;
          search.value = query;
        }
        refreshTabs();
        if (changedView || changedFilter || changedQuery) resubscribe();
        renderPanel();
      }

      refreshTabs();
      resubscribe();
      renderPanel();

      return {
        update(next) { applyRoute(next); },
        unmount() {
          clearTimeout(searchTimer);
          if (sub) sub.close();
        }
      };
    }
  });
})(window.CW = window.CW || {});
