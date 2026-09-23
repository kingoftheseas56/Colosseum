const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { parseHTML } = require('linkedom');

const root = path.resolve(__dirname, '..');

function launch(search, fetch) {
  const { window, document } = parseHTML(fs.readFileSync(path.join(root, 'index.html'), 'utf8'));
  window.location = { search };
  const context = vm.createContext({
    window, document, location: window.location, URL, URLSearchParams,
    CustomEvent: window.CustomEvent, setTimeout, clearTimeout, AbortController, fetch, console
  });
  for (const file of ['catalog.js', 'preview-data.js', 'app.js']) {
    vm.runInContext(fs.readFileSync(path.join(root, file), 'utf8'), context, { filename: file });
  }
  return { window, document };
}

function click(window, node) { node.dispatchEvent(new window.Event('click', { bubbles: true })); }
function search(window, document, query) {
  click(window, document.getElementById('search-button'));
  const input = document.getElementById('search-input');
  input.value = query;
  input.dispatchEvent(new window.Event('input', { bubbles: true }));
}

test('preview renders Resume, provider shelves, search, details and Account Center', () => {
  const { window, document } = launch('?preview=1');
  assert.equal(document.querySelectorAll('.resume-card').length, 3);
  assert.equal(document.querySelectorAll('.shelf').length, 2);
  click(window, [...document.querySelectorAll('.provider-button')][1]);
  assert.equal(document.getElementById('provider-name').textContent, 'Prime Video');
  search(window, document, 'Fallout');
  assert.equal(document.querySelectorAll('.search-result').length, 1);
  click(window, document.querySelector('.search-result'));
  assert.equal(document.getElementById('detail-title').textContent, 'Fallout');
  click(window, document.getElementById('drawer-close'));
  click(window, document.getElementById('account-button'));
  assert.equal(document.querySelectorAll('.roster-button').length, 5);
});

test('host data drives progress, canonical search, rich details and account actions', () => {
  const { window, document } = launch('?preview=1');
  const actions = [];
  window.ColosseumStreaming.onAction(action => actions.push(action));
  const title = (id, providerId, name = 'Shared Story') => ({ id, providerId, type: 'series', title: name, year: '2024', genres: ['Drama'], description: 'A title' });
  window.ColosseumStreaming.mount({
    providers: [{ id: 'nfx', name: 'Netflix', mark: 'N' }, { id: 'amp', name: 'Prime Video', mark: 'P' }],
    connections: { amp: { status: 'attention', profile: 'Main', profiles: ['Main', 'Kids'], conflicts: [{ id: 'c1', label: 'Review episode progress' }] } },
    resumes: [{ ...title('local-42', 'amp'), episode: 'S1 E3', progress: .4 }],
    catalogs: {
      nfx: [{ id: 'series', name: 'Series', titles: [title('tt1234567', 'nfx'), title('tt2222222', 'nfx', '夜空'), title('tt3333333', 'nfx', '夜行')] }],
      amp: [{ id: 'series', name: 'Series', titles: [title('local-42', 'amp'), title('local-43', 'amp', 'Another Story')] }]
    }
  });
  assert.equal(document.querySelectorAll('.resume-card').length, 1);
  click(window, document.querySelector('.resume-card .primary-action'));
  assert.equal(actions.at(-1).type, 'resume');
  search(window, document, 'Shared Story');
  assert.equal(document.querySelectorAll('.search-result').length, 1);
  click(window, document.querySelector('.search-result'));
  assert.match(document.getElementById('detail-provider').textContent, /Prime Video/);
  assert.equal(document.querySelectorAll('.related-titles .title-card').length, 1);
  click(window, document.getElementById('drawer-close'));
  click(window, document.getElementById('search-button'));
  const input = document.getElementById('search-input');
  input.value = '夜'; input.dispatchEvent(new window.Event('input', { bubbles: true }));
  assert.equal(document.querySelectorAll('.search-result').length, 2);
  click(window, document.getElementById('search-close'));
  click(window, document.getElementById('account-button'));
  click(window, [...document.querySelectorAll('.roster-button')][1]);
  assert.equal(document.querySelector('.connection-state').textContent, 'Needs attention');
  assert.match(document.getElementById('account-detail').textContent, /Manage connection/);
  assert.doesNotMatch(document.getElementById('account-detail').textContent, /Connect service/);
  assert.equal(document.querySelectorAll('.account-profiles button').length, 2);
  click(window, [...document.querySelectorAll('.account-profiles button')][1]);
  assert.equal(actions.at(-1).profileId, 'Kids');
  click(window, document.querySelector('.account-conflicts button'));
  assert.equal(actions.at(-1).conflictId, 'c1');
});

test('live catalog loads shelves while Resume remains truthful', async () => {
  const fetch = async url => ({
    ok: true,
    json: async () => String(url).endsWith('manifest.json')
      ? { id: 'catalog', resources: ['catalog'], catalogs: [{ type: 'movie', id: 'nfx' }, { type: 'series', id: 'nfx' }] }
      : { metas: [{ id: 'tt1111111', name: 'Live Catalog Title', type: 'movie' }] }
  });
  const { document } = launch('', fetch);
  await new Promise(resolve => setTimeout(resolve, 30));
  assert.equal(document.querySelectorAll('.resume-card').length, 0);
  assert.equal(document.querySelectorAll('.shelf').length, 2);
  assert.equal(document.querySelectorAll('.title-card').length, 2);
  assert.match(document.getElementById('catalog-status').textContent, /loaded/);
});

test('a late manifest failure cannot overwrite host state', async () => {
  let rejectManifest;
  const fetch = () => new Promise((resolve, reject) => { rejectManifest = reject; });
  const { window, document } = launch('', fetch);
  window.ColosseumStreaming.mount({ providers: [{ id: 'nfx', name: 'Netflix', mark: 'N' }] });
  rejectManifest(new Error('offline'));
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(window.ColosseumStreaming.getState().mode, 'host');
  assert.equal(document.getElementById('catalog-status').textContent, 'Catalogue supplied by Colosseum.');
});
