// Exercises TheatreApi.loadMeta's local-catalogue fallback against the real QML JS
// module. Cinemeta uses TheatreApi's injected request adapter; installed-extension
// metadata uses a deterministic XMLHttpRequest fake, so this test never reaches network.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const root = new URL('../', import.meta.url);
const strip = text => text.replace(/^\.(pragma|import).*$/gm, '');
const source = name => strip(fs.readFileSync(new URL('qml/' + name, root), 'utf8'));

const TITLE_ID = 'tt1254207';
const EXTENSION_URL = `https://fixture.invalid/meta/movie/${TITLE_ID}.json`;
const catalogueRecord = () => ({
    id: TITLE_ID,
    type: 'movie',
    name: 'Big Buck Bunny',
    releaseInfo: '2008',
    imdbRating: 6.4,
    poster: 'https://images.example.invalid/big-buck-bunny.jpg'
});

function adapterModule({extensionMeta = null, onExtensionRequest = () => {}} = {}) {
    const extensionRequests = [];
    class FakeXMLHttpRequest {
        static DONE = 4;
        constructor() {
            this.readyState = 0;
            this.status = 0;
            this.responseText = '';
        }
        open(method, url) {
            this.method = method;
            this.url = url;
        }
        send() {
            extensionRequests.push(this.url);
            onExtensionRequest(this.url);
            this.status = this.url === EXTENSION_URL && extensionMeta ? 200 : 503;
            this.responseText = this.status === 200
                ? JSON.stringify({meta: extensionMeta})
                : '';
            this.readyState = FakeXMLHttpRequest.DONE;
            if (this.onreadystatechange) this.onreadystatechange();
        }
        abort() {}
    }

    const ctx = vm.createContext({console, setTimeout, Date, Math});
    const delivery = vm.createContext({console});
    vm.runInContext(source('CatalogDelivery.js'), delivery);
    const policy = vm.createContext({console});
    vm.runInContext(source('ExplicitContentPolicy.js'), policy);
    const rules = vm.createContext({console, Date, Math});
    vm.runInContext(source('TheatreCatalogRules.js'), rules);
    const addon = vm.createContext({console, setTimeout, XMLHttpRequest: FakeXMLHttpRequest});
    vm.runInContext(source('AddonClient.js'), addon);
    ctx.CatalogDelivery = delivery;
    ctx.Policy = policy;
    ctx.Rules = rules;
    ctx.AddonClient = addon;
    vm.runInContext(source('TheatreApi.js'), ctx);
    return {mod: ctx, extensionRequests};
}

function loadMeta({type = 'movie', id = TITLE_ID, record, cinemetaMeta = null, extensionMeta = null} = {}) {
    const events = [];
    const {mod, extensionRequests} = adapterModule({
        extensionMeta,
        onExtensionRequest: () => events.push('extension-request')
    });
    const cinemetaRequests = [];
    mod.setExtensions([{
        id: 'fixture.meta.extension',
        enabled: true,
        core: false,
        transportUrl: 'https://fixture.invalid/manifest.json',
        manifest: {resources: [{name: 'meta', types: ['movie', 'series'], idPrefixes: ['tt']}]}
    }]);
    mod.setRequestAdapter((url, done) => {
        cinemetaRequests.push(url);
        events.push('cinemeta-request');
        done(cinemetaMeta ? {meta: cinemetaMeta} : null);
        return () => {};
    });
    let result;
    let doneCount = 0;
    mod.loadMeta(type, id, value => {
        doneCount++;
        result = value;
        events.push('done');
    }, record);
    return {result, doneCount, events, cinemetaRequests, extensionRequests};
}

const plain = value => value == null ? value : JSON.parse(JSON.stringify(value));

test('a typed local movie record is used only after Cinemeta and extensions both miss', () => {
    const record = catalogueRecord();
    const before = JSON.stringify(record);
    const result = loadMeta({record});

    assert.deepEqual(plain(result.result), record,
        'fallback returns only the record’s known identity, title, release, rating, and poster fields');
    assert.deepEqual(Object.keys(plain(result.result)).sort(),
        ['id', 'imdbRating', 'name', 'poster', 'releaseInfo', 'type']);
    assert.equal(JSON.stringify(record), before, 'the caller-owned catalogue record is unchanged');
    assert.equal(result.doneCount, 1);
    assert.deepEqual(result.events, ['cinemeta-request', 'extension-request', 'done']);
    assert.deepEqual(result.cinemetaRequests, [
        `https://v3-cinemeta.strem.io/meta/movie/${TITLE_ID}.json`
    ]);
    assert.deepEqual(result.extensionRequests, [EXTENSION_URL]);
    for (const forbidden of ['videos', 'episodes', 'description', 'streams'])
        assert.equal(Object.hasOwn(plain(result.result), forbidden), false, `${forbidden} is not fabricated`);
});

test('live Cinemeta metadata keeps precedence over a matching local catalogue record', () => {
    const local = catalogueRecord();
    const live = {id: TITLE_ID, type: 'movie', name: 'Cinemeta title', releaseInfo: '2008', imdbRating: 7.1};
    const result = loadMeta({record: local, cinemetaMeta: live});

    assert.deepEqual(plain(result.result), live);
    assert.equal(result.doneCount, 1);
    assert.deepEqual(result.events, ['cinemeta-request', 'done']);
    assert.deepEqual(result.extensionRequests, [], 'an extension is not consulted after live success');
});

test('installed-extension metadata keeps precedence over a local catalogue record', () => {
    const local = catalogueRecord();
    const extension = {id: TITLE_ID, type: 'movie', name: 'Extension title', releaseInfo: '2008', imdbRating: 6.9};
    const result = loadMeta({record: local, extensionMeta: extension});

    assert.deepEqual(plain(result.result), extension);
    assert.equal(result.doneCount, 1);
    assert.deepEqual(result.events, ['cinemeta-request', 'extension-request', 'done']);
    assert.deepEqual(result.extensionRequests, [EXTENSION_URL]);
});

test('fallback excludes unvalidated payload fields from a caller-owned movie record', () => {
    const record = {...catalogueRecord(), videos: [{id: 'invented'}],
        streams: ['unvalidated'], description: 'provider-only field', background: 'unvalidated'};
    const before = JSON.stringify(record);
    const result = loadMeta({record});
    assert.deepEqual(plain(result.result), catalogueRecord());
    assert.equal(JSON.stringify(record), before);
});

const invalidRecords = [
    ['missing record', undefined, 'movie', TITLE_ID],
    ['mismatched IMDb id', {...catalogueRecord(), id: 'tt9999999'}, 'movie', TITLE_ID],
    ['non-IMDb id', {...catalogueRecord(), id: 'tmdb:123'}, 'movie', TITLE_ID],
    ['untyped record', {id: TITLE_ID, name: 'Big Buck Bunny', releaseInfo: '2008', imdbRating: 6.4, poster: 'https://images.example.invalid/big-buck-bunny.jpg'}, 'movie', TITLE_ID],
    ['non-movie record', {...catalogueRecord(), type: 'series'}, 'movie', TITLE_ID],
    ['blank title', {...catalogueRecord(), name: ' \t\n '}, 'movie', TITLE_ID],
    ['movie record for a series request', catalogueRecord(), 'series', TITLE_ID]
];

for (const [label, record, type, id] of invalidRecords) {
    test(`rejects ${label} after provider misses`, () => {
        const result = loadMeta({record, type, id});
        assert.equal(result.result, null);
        assert.equal(result.doneCount, 1);
    });
}
