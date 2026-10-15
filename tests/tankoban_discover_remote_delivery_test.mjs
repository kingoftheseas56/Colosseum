// Executes the actual Tankoban Discover adapter against a fake catalogue that models
// CatalogRemote's per-key semantics: deferred enqueue signal, failure cooldown, explicit
// retry, validated stale cache. No application, network or database needed.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const root = new URL('../', import.meta.url);
const strip = text => text.replace(/^\.(pragma|import).*$/gm, '');
const source = name => strip(fs.readFileSync(new URL('qml/' + name, root), 'utf8'));

function adapterModule() {
    const ctx = vm.createContext({console, setTimeout});
    const delivery = vm.createContext({console});
    vm.runInContext(source('CatalogDelivery.js'), delivery);
    const policy = vm.createContext({console});
    vm.runInContext(source('ExplicitContentPolicy.js'), policy);
    ctx.CatalogDelivery = delivery;
    ctx.Policy = policy;
    vm.runInContext(source('TankobanDiscoverApi.js'), ctx);
    return ctx;
}

function signal() {
    const slots = new Set();
    return {connect: f => slots.add(f), disconnect: f => slots.delete(f),
            emit: (...a) => { for (const f of [...slots]) f(...a); }, get count() { return slots.size; }};
}
const tick = () => new Promise(resolve => setImmediate(resolve));

function fakeCatalog() {
    const cache = new Map(), pending = new Set(), failures = new Map(), retries = [];
    const empty = {items: [], nextOffset: 0, exhausted: true};
    const key = (...args) => JSON.stringify(args);
    const cat = {
        remoteEnabled: true, remoteQueryChanged: signal(), revisionChanged: signal(), readyChanged: signal(),
        requestFailed: signal(),
        // Global, as natively: true while ANY uncached query is in flight.
        remoteWaiting: () => [...pending].some(k => !cache.has(k)),
        pending, retries,
        discoverPageFiltered(...args) {
            const k = key(...args), hit = cache.get(k);
            if (!hit || hit.stale) queue(k, false);
            return hit ? hit.page : empty;
        },
        discoverPageFilteredState(...args) {
            const k = key(...args), inFlight = pending.has(k), failed = failures.has(k), cached = cache.has(k);
            return {source: 'remote', key: k, enabled: true,
                    status: inFlight ? 'pending' : failed ? 'failure' : cached ? 'success' : 'missing',
                    pending: inFlight, waiting: inFlight && !cached, cached,
                    error: failed ? failures.get(k) : '', retryable: failed && !inFlight};
        },
        retryDiscoverPageFiltered(...args) {
            retries.push(args);
            const k = key(...args);
            return failures.has(k) ? queue(k, true) : false;
        },
        seed(args, page, stale) { cache.set(key(...args), {page, stale: !!stale}); },
        succeed(args, page) {
            const k = key(...args);
            pending.delete(k); failures.delete(k); cache.set(k, {page, stale: false});
            cat.remoteQueryChanged.emit(k); cat.revisionChanged.emit();
        },
        fail(args, reason) {
            const k = key(...args);
            pending.delete(k); failures.set(k, reason); cat.remoteQueryChanged.emit(k); cat.requestFailed.emit('discoverPageFiltered', reason);
        },
    };
    function queue(k, bypass) {
        if (pending.has(k) || (!bypass && failures.has(k))) return false;
        pending.add(k);
        setImmediate(() => { if (pending.has(k)) cat.remoteQueryChanged.emit(k); });
        return true;
    }
    return cat;
}

const manga = n => Array.from({length: n}, (_, i) => ({mal_id: i + 1, title: 'Manga ' + (i + 1)}));
const comics = n => Array.from({length: n}, (_, i) => ({locgId: 'locg:' + (i + 1), title: 'Comic ' + (i + 1)}));
const state = (type) => ({type, catalogKey: 'popular', filters: {}, filterGroup: '', filterKey: ''});

function harness(type) {
    const mod = adapterModule(), cat = fakeCatalog();
    const adapter = type === 'comics' ? mod.create(null, cat, [], false, () => null)
                                      : mod.create(cat, null, [], false, null);
    const pages = [];
    const fetch = cursor => adapter.fetchPage(state(type), cursor, 7, (gen, page) => pages.push(page));
    return {adapter, cat, pages, fetch};
}

for (const type of ['manga', 'comics']) {
    const rows = type === 'comics' ? comics : manga;

    test(`${type}: uncached first-page failure is a failed page, not an exhausted empty catalogue`, async () => {
        const {cat, pages, fetch} = harness(type);
        fetch(null);
        await tick();
        assert.equal(pages.length, 0, 'pending query delivers nothing early');
        cat.fail(['popular', [], false, 0, 24], 'HTTP_503');
        assert.equal(pages.length, 1);
        assert.equal(pages[0].failed, true);
        assert.equal(pages[0].exhausted, false, 'failure must not end paging');
        assert.equal(pages[0].items.length, 0);
        assert.equal(pages[0].retryable, true);
        assert.equal(pages[0].error, 'HTTP_503');
        assert.equal(cat.remoteQueryChanged.count + cat.revisionChanged.count + cat.requestFailed.count, 0, 'unsubscribed');
    });

    test(`${type}: remote success paints exact page rows and native cursor`, async () => {
        const {cat, pages, fetch} = harness(type);
        fetch(null);
        await tick();
        cat.succeed(['popular', [], false, 0, 24], {items: rows(24), nextOffset: 24, exhausted: false});
        assert.equal(pages.length, 1);
        assert.notEqual(pages[0].failed, true);
        assert.equal(pages[0].items.length, 24);
        assert.equal(pages[0].nextCursor, 24);
        assert.equal(pages[0].exhausted, false);
    });

    test(`${type}: valid remote empty page is a real exhausted success`, async () => {
        const {cat, pages, fetch} = harness(type);
        fetch(null);
        await tick();
        cat.succeed(['popular', [], false, 0, 24], {items: [], nextOffset: 0, exhausted: true});
        assert.notEqual(pages[0].failed, true);
        assert.equal(pages[0].exhausted, true);
    });

    test(`${type}: failed pagination keeps the cursor for an explicit retry`, async () => {
        const {adapter, cat, pages, fetch} = harness(type);
        fetch(24);
        await tick();
        cat.fail(['popular', [], false, 24, 24], 'HTTP_500');
        assert.equal(pages[0].failed, true);
        assert.equal(pages[0].nextCursor, 24, 'cursor retained');
        assert.equal(adapter.retryPage(state(type), 24), true, 'retry re-queues the exact failed key');
        assert.equal(JSON.stringify(cat.retries[0]), JSON.stringify(['popular', [], false, 24, 24]));
        fetch(24);
        await tick();
        assert.equal(pages.length, 1, 'retried page waits on the in-flight key');
        cat.succeed(['popular', [], false, 24, 24], {items: rows(3), nextOffset: 27, exhausted: true});
        assert.equal(pages.length, 2);
        assert.notEqual(pages[1].failed, true);
        assert.equal(pages[1].items.length, 3);
    });

    test(`${type}: failed refresh of validated stale cache paints rows under the offline notice`, async () => {
        const {cat, pages, fetch} = harness(type);
        cat.seed(['popular', [], false, 0, 24], {items: rows(5), nextOffset: 5, exhausted: true}, true);
        fetch(null);
        await tick();
        assert.equal(pages.length, 0, 'stale refresh in flight is not terminal');
        cat.fail(['popular', [], false, 0, 24], 'HTTP_502');
        assert.equal(pages.length, 1);
        assert.notEqual(pages[0].failed, true);
        assert.equal(pages[0].items.length, 5, 'validated stale rows retained');
        assert.equal(pages[0].warning, 'Showing offline catalogue');
    });

    test(`${type}: cancelling the page handle ignores the late completion`, async () => {
        const {cat, pages, fetch} = harness(type);
        const cancel = fetch(null);
        await tick();
        cancel();
        cat.succeed(['popular', [], false, 0, 24], {items: rows(2), nextOffset: 2, exhausted: true});
        assert.equal(pages.length, 0);
        assert.equal(cat.remoteQueryChanged.count, 0);
    });

    test(`${type}: a peer query completing never settles this page`, async () => {
        const {cat, pages, fetch} = harness(type);
        fetch(null);
        await tick();
        cat.succeed(['popular', [], false, 24, 24], {items: rows(1), nextOffset: 25, exhausted: true});
        assert.equal(pages.length, 0);
    });
}
