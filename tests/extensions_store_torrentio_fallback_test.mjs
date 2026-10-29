import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';

const id = 'com.stremio.torrentio.addon';
const url = 'https://torrentio.strem.fun/manifest.json';
const script = readFileSync('qml/ExtensionsStoreApi.js', 'utf8').replace('.pragma library', '');

function raw(name, options = {}) {
  return { slug: name.toLowerCase(), stars: 4, categories: [{name: 'movies'}],
    manifestUrl: options.url || `https://example.test/${name}/manifest.json`,
    configureUrl: options.configureUrl || '',
    manifest: {id: options.id || name, name, resources: ['stream'], types: ['movie', 'series'],
      behaviorHints: {configurable: true}} };
}

function load(responses = []) {
  const api = {XMLHttpRequest: class {
    static DONE = 4;
    open() {}
    send() {
      assert.ok(responses.length, 'Only the expected catalogue requests are made');
      const response = responses.shift();
      this.status = response.status;
      this.responseText = JSON.stringify(response.body || {});
      this.readyState = 4;
      this.onreadystatechange();
    }
  }};
  vm.createContext(api);
  vm.runInContext(script, api);
  return api;
}

function fetch(responses) {
  const api = load(responses);
  let result, calls = 0;
  api.fetchAll((addons, error) => { calls++; result = {addons, error}; });
  assert.equal(calls, 1);
  return {api, ...result};
}

function torrentio(api, addons) {
  const found = addons.filter(a => a.id === id || a.manifestUrl === url);
  assert.equal(found.length, 1, 'One Torrentio entry is available');
  assert.ok(api.visible(found[0], false), 'Torrentio is eligible for the search pool');
  assert.ok(api.essentials(addons).includes(found[0]), 'Torrentio appears in Essentials');
  return found[0];
}

test('Torrentio is available before the online catalogue loads', () => {
  const api = load();
  const addon = torrentio(api, api.withTorrentio([]));
  assert.equal(addon.name, 'Torrentio');
  assert.equal(addon.configurable, true);
  assert.equal(addon.setupRequired, false);
  assert.equal(addon.configureUrl, 'https://torrentio.strem.fun/configure');
});

test('successful catalogue missing Torrentio keeps other entries and supplies it', () => {
  const {api, addons, error} = fetch([{status: 200, body: {addons: [raw('Another')]}}]);
  torrentio(api, addons);
  assert.equal(addons.length, 2);
  assert.equal(addons[0].name, 'Another');
  assert.equal(error, '');
});

test('a valid remote Torrentio keeps its metadata and configuration URL', () => {
  const remote = raw('Torrentio', {id, url, configureUrl: 'https://torrentio.strem.fun/options/configure'});
  const {api, addons} = fetch([{status: 200, body: {addons: [remote]}}]);
  assert.equal(torrentio(api, addons).configureUrl, remote.configureUrl);
  assert.equal(addons.length, 1);
});

test('a renamed Torrentio still belongs in Essentials by stable identity', () => {
  const {api, addons} = fetch([{status: 200, body: {addons: [raw('Torrentio Community', {id, url})]}}]);
  assert.equal(torrentio(api, addons).name, 'Torrentio Community');
  assert.ok(!api.rows(addons, false).some(r => r.items.some(a => a.id === id)),
    'A renamed essential is not repeated in other rails');
});

test('canonical transport identity does not create a duplicate', () => {
  const {api, addons} = fetch([{status: 200, body: {addons: [raw('Torrentio', {id: 'legacy.torrentio', url})]}}]);
  torrentio(api, addons);
  assert.equal(addons.length, 1);
});

test('an unusable remote Torrentio entry is replaced by the usable bundled entry', () => {
  const remote = raw('Torrentio', {id});
  remote.manifestUrl = '';
  const {api, addons} = fetch([{status: 200, body: {addons: [remote]}}]);
  assert.equal(torrentio(api, addons).manifestUrl, url);
});

test('failed and partial catalogue loads retain Torrentio and report the error', () => {
  for (const responses of [
    [{status: 0}],
    [{status: 200, body: {addons: [raw('Another')], pagination: {hasNextPage: true}}}, {status: 503}]
  ]) {
    const {api, addons, error} = fetch(responses);
    torrentio(api, addons);
    assert.ok(error.length);
  }
});
