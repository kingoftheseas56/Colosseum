// WT-06: Tankoban Featured Read/Details route the exact selected slide; a deferred manga read
// fires once, only for the title whose page finished loading, and any later catalogue route
// cancels it. Executes production Main/TankobanWorld bodies with recording doubles.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

const root = new URL('../', import.meta.url);
const main = readFileSync(new URL('qml/Main.qml', root), 'utf8').replace(/\r\n/g, '\n');
const world = readFileSync(new URL('qml/TankobanWorld.qml', root), 'utf8').replace(/\r\n/g, '\n');
function fn(source, name) {
    const m = new RegExp(`^    function ${name}\\s*\\(([^)]*)\\)\\s*\\{([\\s\\S]*?)^    \\}`, 'm').exec(source);
    assert.ok(m, `production ${name} exists`);
    return { params: m[1].split(',').map(s => s.trim()).filter(Boolean), body: m[2] };
}
function shell({ ready = false, currentTitle = '', getComics = true } = {}) {
    const events = [];
    const page = { seriesTitle: currentTitle, loading: !ready,
        readPrimary() { events.push('read:' + this.seriesTitle); } };
    const win = { _catalogueRouteGeneration: 0, _catalogueRouteCancel: null, events,
        openSeries(title) { win.cancelCatalogueRoute(); events.push('open:' + title);
            if (page.seriesTitle !== title) { page.seriesTitle = title; page.loading = true; } },
        openWestern(d) { win.cancelCatalogueRoute(); events.push('western:' + d.title); },
        openExtensionsPage(w) { events.push('extensions:' + w); } };
    const seriesLayer = { item: page, featuredRead: null };
    const Extensions = { installed: () => [{ id: 'colosseum.well.getcomics.issues', enabled: getComics }] };
    for (const name of ['cancelCatalogueRoute', 'readTankobanFeatured', 'consumeFeaturedRead']) {
        const f = fn(main, name);
        const body = f.body.replace(/(?<![.\w])_catalogueRoute/g, 'win._catalogueRoute');
        win[name] = new Function('win', 'seriesLayer', 'Extensions', ...f.params, body)
            .bind(null, win, seriesLayer, Extensions);
    }
    return { win, page, seriesLayer, events };
}
function finishLoading(s) { s.page.loading = false; s.win.consumeFeaturedRead(); }

test('cold/new title: read waits for that title to load, then fires once', () => {
    const s = shell();
    s.win.readTankobanFeatured({ title: 'Vagabond' });
    assert.deepEqual(s.events, ['open:Vagabond']);
    finishLoading(s);
    assert.deepEqual(s.events, ['open:Vagabond', 'read:Vagabond']);
    s.win.consumeFeaturedRead();
    assert.equal(s.events.length, 2, 'no second read');
});

test('warm same title already loaded reads immediately', () => {
    const s = shell({ ready: true, currentTitle: 'Berserk' });
    s.win.readTankobanFeatured({ title: 'Berserk' });
    assert.deepEqual(s.events, ['open:Berserk', 'read:Berserk']);
    assert.equal(s.seriesLayer.featuredRead, null);
});

test('a later route cancels the deferred read (no landing on a new context)', () => {
    const s = shell();
    s.win.readTankobanFeatured({ title: 'Vagabond' });
    s.win.openSeries('Monster');            // user navigated elsewhere before load finished
    finishLoading(s);
    assert.deepEqual(s.events, ['open:Vagabond', 'open:Monster']);
    const t = shell();
    t.win.readTankobanFeatured({ title: 'Vagabond' });
    t.win.cancelCatalogueRoute();           // Back / profile / world change
    finishLoading(t);
    assert.deepEqual(t.events, ['open:Vagabond']);
});

test('comic slides need GetComics; otherwise Extensions opens', () => {
    const on = shell({ getComics: true });
    on.win.readTankobanFeatured({ title: 'Saga', ghost: 'C' });
    assert.deepEqual(on.events, ['western:Saga']);
    const off = shell({ getComics: false });
    off.win.readTankobanFeatured({ title: 'Saga', ghost: 'C' });
    assert.deepEqual(off.events, ['extensions:tankoban']);
    const empty = shell();
    empty.win.readTankobanFeatured({ title: '' });
    assert.deepEqual(empty.events, []);
});

test('carousel forwards the exact selected slide for Read and Details', () => {
    const start = world.indexOf('automationId: "tankobanFeaturedCarousel"');
    const block = world.slice(start, world.indexOf('\n    }\n', start));
    const handler = name => {
        const m = new RegExp(`${name}: \\(index\\) => \\{([\\s\\S]*?)\\n        \\}`).exec(block);
        assert.ok(m, name);
        return m[1];
    };
    const slides = [{ title: 'One' }, { title: 'Two', ghost: 'C' }, { title: 'Three' }];
    const events = [];
    const tanko = { featuredReadRequested: s => events.push(['read', s.title]),
        westernRequested: t => events.push(['western', t]), seriesRequested: t => events.push(['series', t]) };
    const run = (name, index) => new Function('tanko', 'Catalog', 'index', handler(name))(tanko, { featured: slides }, index);
    run('onPrimaryClicked', 2); run('onSecondaryClicked', 1); run('onSecondaryClicked', 0); run('onPrimaryClicked', 9);
    assert.deepEqual(events, [['read', 'Three'], ['western', 'Two'], ['series', 'One']]);
});

test('Main wires the Tankoban world signal', () => {
    assert.match(main, /item\.featuredReadRequested\.connect\(win\.readTankobanFeatured\)/);
});
