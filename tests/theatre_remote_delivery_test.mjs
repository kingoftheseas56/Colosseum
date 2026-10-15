// Executes the actual Theatre catalogue adapter and row loaders against fake IMDb and MAL
// catalogues that model CatalogRemote's per-key semantics: deferred enqueue signal, failure
// cooldown, explicit retry, validated stale cache, fan-out queries, and More Like This.
// No application, network or database needed.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const root = new URL('../', import.meta.url);
const strip = text => text.replace(/^\.(pragma|import).*$/gm, '');
const source = name => strip(fs.readFileSync(new URL('qml/' + name, root), 'utf8'));

function adapterModule() {
    const ctx = vm.createContext({console, setTimeout, Date, Math});
    const delivery = vm.createContext({console});
    vm.runInContext(source('CatalogDelivery.js'), delivery);
    const policy = vm.createContext({console});
    vm.runInContext(source('ExplicitContentPolicy.js'), policy);
    const rules = vm.createContext({console, Date, Math});
    vm.runInContext(source('TheatreCatalogRules.js'), rules);
    const addon = vm.createContext({console, setTimeout});
    vm.runInContext(source('AddonClient.js'), addon);
    ctx.CatalogDelivery = delivery;
    ctx.Policy = policy;
    ctx.Rules = rules;
    ctx.AddonClient = addon;
    vm.runInContext(source('TheatreApi.js'), ctx);
    return ctx;
}

function signal() {
    const slots = new Set();
    return {
        connect: f => slots.add(f),
        disconnect: f => slots.delete(f),
        emit: (...a) => { for (const f of [...slots]) f(...a); },
        get count() { return slots.size; }
    };
}
const tick = () => new Promise(resolve => setImmediate(resolve));

function fakeImdbCatalog() {
    const cache = new Map(), pending = new Set(), failures = new Map(), retries = [];
    const key = (...args) => JSON.stringify(['titleCatalog', ...args]);
    const cat = {
        available: () => true,
        ready: () => false,
        remoteEnabled: true,
        remoteQueryChanged: signal(),
        revisionChanged: signal(),
        readyChanged: signal(),
        requestFailed: signal(),
        remotePending: () => pending.size > 0,
        remoteWaiting: () => [...pending].some(k => !cache.has(k)),
        pending,
        retries,
        titleCatalog(...args) {
            const k = key(...args), hit = cache.get(k);
            if (!hit || hit.stale) queue(k, false);
            return hit ? hit.rows : [];
        },
        titleCatalogState(...args) {
            const k = key(...args), inFlight = pending.has(k), failed = failures.has(k), cached = cache.has(k);
            return {
                source: 'remote',
                key: k,
                enabled: true,
                status: inFlight ? 'pending' : failed ? 'failure' : cached ? 'success' : 'missing',
                pending: inFlight,
                waiting: inFlight && !cached,
                cached,
                error: failed ? failures.get(k) : '',
                retryable: failed && !inFlight
            };
        },
        retryTitleCatalog(...args) {
            retries.push(args);
            const k = key(...args);
            return failures.has(k) ? queue(k, true) : false;
        },
        seed(args, rows, stale) { cache.set(key(...args), {rows, stale: !!stale}); },
        succeed(args, rows) {
            const k = key(...args);
            pending.delete(k); failures.delete(k); cache.set(k, {rows, stale: false});
            cat.remoteQueryChanged.emit(k); cat.revisionChanged.emit();
        },
        fail(args, reason) {
            const k = key(...args);
            pending.delete(k); failures.set(k, reason);
            cat.remoteQueryChanged.emit(k); cat.requestFailed.emit('titleCatalog', reason);
        }
    };
    function queue(k, bypass) {
        if (pending.has(k) || (!bypass && failures.has(k))) return false;
        pending.add(k);
        setImmediate(() => { if (pending.has(k)) cat.remoteQueryChanged.emit(k); });
        return true;
    }
    return cat;
}

function fakeMalCatalog() {
    const cache = new Map(), pending = new Set(), failures = new Map(), retries = [];
    const key = (method, ...args) => JSON.stringify([method, ...args]);
    const cat = {
        available: () => true,
        ready: () => false,
        remoteEnabled: true,
        remoteQueryChanged: signal(),
        revisionChanged: signal(),
        readyChanged: signal(),
        requestFailed: signal(),
        remotePending: () => pending.size > 0,
        remoteWaiting: () => [...pending].some(k => !cache.has(k)),
        pending,
        retries,
        animeCatalog(...args) {
            const k = key('animeCatalog', ...args), hit = cache.get(k);
            if (!hit || hit.stale) queue(k, false);
            return hit ? hit.rows : [];
        },
        animeCatalogState(...args) {
            const k = key('animeCatalog', ...args), inFlight = pending.has(k), failed = failures.has(k), cached = cache.has(k);
            return {
                source: 'remote',
                key: k,
                enabled: true,
                status: inFlight ? 'pending' : failed ? 'failure' : cached ? 'success' : 'missing',
                pending: inFlight,
                waiting: inFlight && !cached,
                cached,
                error: failed ? failures.get(k) : '',
                retryable: failed && !inFlight
            };
        },
        retryAnimeCatalog(...args) {
            retries.push(args);
            const k = key('animeCatalog', ...args);
            return failures.has(k) ? queue(k, true) : false;
        },
        genreEntries(...args) {
            const k = key('genreEntries', ...args), hit = cache.get(k);
            if (!hit || hit.stale) queue(k, false);
            return hit ? hit.rows : [];
        },
        genreEntriesState(...args) {
            const k = key('genreEntries', ...args), inFlight = pending.has(k), failed = failures.has(k), cached = cache.has(k);
            return {
                source: 'remote',
                key: k,
                enabled: true,
                status: inFlight ? 'pending' : failed ? 'failure' : cached ? 'success' : 'missing',
                pending: inFlight,
                waiting: inFlight && !cached,
                cached,
                error: failed ? failures.get(k) : '',
                retryable: failed && !inFlight
            };
        },
        retryGenreEntries(...args) {
            retries.push(args);
            const k = key('genreEntries', ...args);
            return failures.has(k) ? queue(k, true) : false;
        },
        seed(args, rows, stale, method = 'animeCatalog') {
            cache.set(key(method, ...args), {rows, stale: !!stale});
        },
        succeed(args, rows, method = 'animeCatalog') {
            const k = key(method, ...args);
            pending.delete(k); failures.delete(k); cache.set(k, {rows, stale: false});
            cat.remoteQueryChanged.emit(k); cat.revisionChanged.emit();
        },
        fail(args, reason, method = 'animeCatalog') {
            const k = key(method, ...args);
            pending.delete(k); failures.set(k, reason);
            cat.remoteQueryChanged.emit(k); cat.requestFailed.emit(method, reason);
        }
    };
    function queue(k, bypass) {
        if (pending.has(k) || (!bypass && failures.has(k))) return false;
        pending.add(k);
        setImmediate(() => { if (pending.has(k)) cat.remoteQueryChanged.emit(k); });
        return true;
    }
    return cat;
}

const imdbRows = (type, n) => Array.from({length: n}, (_, i) => ({
    tt: 'tt' + String(1000 + i),
    type: type === 'shows' ? 'series' : 'movie',
    title: (type === 'shows' ? 'Show ' : 'Movie ') + (i + 1),
    year: 2021,
    rating: 8.5,
    votes: 250000 - i,
    runtimeMin: 115,
    genres: ['Drama']
}));

const animeRows = n => Array.from({length: n}, (_, i) => ({
    mal_id: i + 1,
    title: 'Anime ' + (i + 1),
    type: 'TV',
    year: 2022,
    score: 8.6,
    members: 100000 - i,
    images: {jpg: {large_image_url: 'https://example.invalid/' + (i + 1) + '.jpg'}}
}));

const expectedQueryFor = (mod, type, catalogKey = 'top-rated') => {
    if (type === 'anime') {
        return {order: 'score', voteFloor: 5000};
    }
    const T = mod.Rules.THRESHOLDS[type === 'shows' ? 'series' : 'movie'];
    return {
        type: type === 'shows' ? 'series' : 'movie',
        excludeAnime: true,
        order: 'rating',
        ratingMin: T.TR_RATING,
        votesMin: T.TR_VOTES
    };
};

const state = (type, catalogKey = 'top-rated') => ({
    type,
    catalogKey,
    filters: {},
    filterGroup: '',
    filterKey: ''
});

function harness(type, catalogKey = 'top-rated') {
    const mod = adapterModule();
    const cat = type === 'anime' ? fakeMalCatalog() : fakeImdbCatalog();
    const adapter = type === 'anime'
        ? mod.create(null, cat, false, null)
        : mod.create(cat, null, false, null);
    const pages = [];
    const fetch = cursor => adapter.fetchPage(state(type, catalogKey), cursor, 7, (gen, page) => pages.push(page));
    const query = expectedQueryFor(mod, type, catalogKey);
    return {mod, adapter, cat, pages, fetch, query};
}

for (const type of ['movies', 'shows', 'anime']) {
    const rows = n => type === 'anime' ? animeRows(n) : imdbRows(type, n);

    test(`${type}: uncached first-page failure is a failed page, not an exhausted empty catalogue`, async () => {
        const {cat, pages, fetch, query} = harness(type);
        fetch(null);
        await tick();
        assert.equal(pages.length, 0, 'pending query delivers nothing early');
        cat.fail([query, 0, 24], 'HTTP_503');
        assert.equal(pages.length, 1);
        assert.equal(pages[0].failed, true);
        assert.equal(pages[0].exhausted, false, 'failure must not end paging');
        assert.equal(pages[0].items.length, 0);
        assert.equal(pages[0].retryable, true);
        assert.equal(pages[0].error, 'HTTP_503');
        assert.equal(cat.remoteQueryChanged.count + cat.revisionChanged.count + cat.requestFailed.count, 0, 'unsubscribed');
    });

    test(`${type}: remote success paints exact page rows and native cursor`, async () => {
        const {cat, pages, fetch, query} = harness(type);
        fetch(null);
        await tick();
        cat.succeed([query, 0, 24], rows(24));
        assert.equal(pages.length, 1);
        assert.notEqual(pages[0].failed, true);
        assert.equal(pages[0].items.length, 24);
        assert.equal(pages[0].nextCursor, 24);
        assert.equal(pages[0].exhausted, false);
    });

    test(`${type}: valid remote empty page is a real exhausted success`, async () => {
        const {cat, pages, fetch, query} = harness(type);
        fetch(null);
        await tick();
        cat.succeed([query, 0, 24], []);
        assert.equal(pages.length, 1);
        assert.notEqual(pages[0].failed, true);
        assert.equal(pages[0].exhausted, true);
        assert.equal(pages[0].items.length, 0);
    });

    test(`${type}: failed pagination keeps the cursor for an explicit retry`, async () => {
        const {adapter, cat, pages, fetch, query} = harness(type);
        fetch(24);
        await tick();
        cat.fail([query, 24, 24], 'HTTP_500');
        assert.equal(pages[0].failed, true);
        assert.equal(pages[0].nextCursor, 24, 'cursor retained');
        assert.equal(adapter.retryPage(state(type), 24), true, 'retry re-queues the exact failed key');
        assert.equal(JSON.stringify(cat.retries[0]), JSON.stringify([query, 24, 24]));
        fetch(24);
        await tick();
        assert.equal(pages.length, 1, 'retried page waits on the in-flight key');
        cat.succeed([query, 24, 24], rows(3));
        assert.equal(pages.length, 2);
        assert.notEqual(pages[1].failed, true);
        assert.equal(pages[1].items.length, 3);
        assert.equal(pages[1].exhausted, true);
    });

    test(`${type}: failed refresh of validated stale cache paints rows under the offline notice`, async () => {
        const {cat, pages, fetch, query} = harness(type);
        cat.seed([query, 0, 24], rows(5), true);
        fetch(null);
        await tick();
        assert.equal(pages.length, 0, 'stale refresh in flight is not terminal');
        cat.fail([query, 0, 24], 'HTTP_502');
        assert.equal(pages.length, 1);
        assert.notEqual(pages[0].failed, true);
        assert.equal(pages[0].items.length, 5, 'validated stale rows retained');
        assert.equal(pages[0].warning, 'Showing offline catalogue');
    });

    test(`${type}: cancelling the page handle ignores the late completion`, async () => {
        const {cat, pages, fetch, query} = harness(type);
        const cancel = fetch(null);
        await tick();
        cancel();
        cat.succeed([query, 0, 24], rows(2));
        assert.equal(pages.length, 0);
        assert.equal(cat.remoteQueryChanged.count, 0);
    });

    test(`${type}: a peer query completing never settles this page`, async () => {
        const {cat, pages, fetch, query} = harness(type);
        fetch(null);
        await tick();
        cat.succeed([query, 24, 24], rows(1));
        assert.equal(pages.length, 0);
    });
}

test('TheatreSeeAllPage loadRowPage + retryRowPage: fan-out shelves wait on every query key and retry failed leg', async () => {
    const mod = adapterModule();
    const imdb = fakeImdbCatalog();
    const pages = [];
    const pin = {pageKey: 'shows', sourceKind: 'house', rowKey: 'crime-and-mystery', title: 'Crime and Mystery'};
    const opts = {generation: 3, imdbCatalog: imdb, showExplicit: false};
    const crimeQuery = {type: 'series', excludeAnime: true, order: 'votes', genre: 'Crime', votesMin: 5000};
    const mysteryQuery = {type: 'series', excludeAnime: true, order: 'votes', genre: 'Mystery', votesMin: 5000};

    mod.loadRowPage(pin, 0, 40, opts, page => pages.push(page));
    await tick();
    imdb.succeed([crimeQuery, 0, 40], imdbRows('shows', 2));
    assert.equal(pages.length, 0, 'fan-out page waits for both Crime and Mystery queries');
    imdb.fail([mysteryQuery, 0, 40], 'HTTP_503');
    assert.equal(pages.length, 1);
    assert.equal(pages[0].failed, true);
    assert.equal(pages[0].retryable, true);
    assert.equal(pages[0].error, 'HTTP_503');

    assert.equal(mod.retryRowPage(pin, 0, 40, opts), true, 'retryRowPage retries the failed fan-out leg');
    assert.equal(imdb.retries.length, 2);
    mod.loadRowPage(pin, 0, 40, opts, page => pages.push(page));
    await tick();
    imdb.succeed([mysteryQuery, 0, 40], imdbRows('shows', 3));
    assert.equal(pages.length, 2);
    assert.notEqual(pages[1].failed, true);
    assert.equal(pages[1].items.length, 3, 'deduped merged rows delivered after retry');
});

test('moreLikeThis: anime genreEntries uses waitQuery and returns empty array on uncached remote failure', async () => {
    const mod = adapterModule();
    const mal = fakeMalCatalog();
    const results = [];
    mod.moreLikeThis('series', 'mal:1', 'mal:1', 'Action', mal, items => results.push(items));
    await tick();
    assert.equal(results.length, 0);
    mal.fail(['anime', 'Action', 'members', 13], 'HTTP_503', 'genreEntries');
    assert.equal(results.length, 1);
    assert.equal(results[0].length, 0);

    assert.equal(mal.retryGenreEntries('anime', 'Action', 'members', 13), true);
    mod.moreLikeThis('series', 'mal:1', 'mal:1', 'Action', mal, items => results.push(items));
    await tick();
    mal.succeed(['anime', 'Action', 'members', 13], animeRows(3), 'genreEntries');
    assert.equal(results.length, 2);
    assert.equal(results[1].length, 2, 'excludes self mal:1 and maps remaining items');
});

