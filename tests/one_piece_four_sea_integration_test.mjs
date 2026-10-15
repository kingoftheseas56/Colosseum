import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import vm from 'node:vm';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = name => fs.readFileSync(path.join(repo, 'qml', name), 'utf8');
function qmlJs(name) {
    const context = vm.createContext({});
    vm.runInContext(source(name).replace(/^\s*\.pragma library\s*$/m, ''), context);
    return context;
}
function method(file, name, args) {
    const match = source(file).match(new RegExp('function ' + name + '\\([^)]*\\) \\{([\\s\\S]*?)\\n    \\}'));
    assert.ok(match, `${name} exists`);
    return new Function(...args, match[1]);
}

test('unconsumed atlas Escape closes the universe; consumed Escape keeps it open', () => {
    const escape = method('Main.qml', 'requestUniverseEscape', ['universeLayer', 'win']);
    let closed = 0;
    const win = { closeUniverse() { closed++; } };
    escape({ active: true, item: { requestEscape: () => false } }, win);
    assert.equal(closed, 1);
    escape({ active: true, item: { requestEscape: () => true } }, win);
    assert.equal(closed, 1);
    escape({ active: false, item: null }, win);
    assert.equal(closed, 2);
});

test('later seas use their explicit poster and related media without East Blue leakage', () => {
    const hero = method('OnePieceArcPage.qml', 'heroSourceForArc', ['root']);
    const related = method('OnePieceArcPage.qml', 'relatedItemsForArc', ['root']);
    const root = { arc: { id: 'egghead', heroSource: '../assets/universes/one-piece/final-sea/banners/egghead.png', relatedMedia: [] } };
    assert.equal(hero(root), root.arc.heroSource);
    assert.deepEqual(related(root), []);
    root.arc = { id: 'arlong' };
    assert.match(hero(root), /east-blue-markers\/arlong\.png$/);
    assert.deepEqual(related(root).map(row => row.id), ['tt11757066', 'tt2598466']);
});

test('all 26 later-sea arcs route finite catalogues and existing local artwork', () => {
    const api = qmlJs('OnePieceCatalogApi.js');
    let count = 0;
    for (const name of ['Paradise', 'NewWorld', 'FinalSea']) {
        const story = qmlJs(`OnePiece${name}Data.js`);
        const adapter = qmlJs(`OnePiece${name}Adapter.js`);
        for (const arc of story.arcs) {
            const before = JSON.stringify(arc);
            const payload = adapter.cataloguePayload(arc);
            assert.ok(payload, arc.id);
            assert.equal(payload.id, arc.id);
            assert.ok(api.numbersFromSpec(payload.anime).length, arc.id + ' anime selector');
            assert.ok(api.volumeNumbers(payload).length, arc.id + ' volume selector');
            for (const field of ['heroSource', 'markerSource']) {
                assert.ok(fs.existsSync(path.resolve(repo, 'qml', payload[field])), arc.id + ' ' + field);
            }
            assert.ok(Array.isArray(payload.relatedMedia));
            assert.equal(JSON.stringify(arc), before, arc.id + ' story is not mutated');
            count++;
        }
        assert.equal(adapter.cataloguePayload({ id: 'constructor' }), null);
    }
    assert.equal(count, 26);
    const final = qmlJs('OnePieceFinalSeaData.js');
    const elbaf = qmlJs('OnePieceFinalSeaAdapter.js').cataloguePayload(final.arc('elbaf'));
    assert.equal(elbaf.storyRanges.anime, '1156+');
    assert.equal(elbaf.anime, '1156-1180');
    assert.equal(api.volumeNumbers(elbaf).at(-1), 115);
});

test('badge packs cover distinct Paradise arc art and non-routable Reverie context', () => {
    const story = qmlJs('OnePieceParadiseData.js');
    const adapter = qmlJs('OnePieceParadiseAdapter.js');
    const correctedFiles = {
        'whisky-peak': 'little-garden', 'little-garden': 'whisky-peak',
        'amazon-lily': 'impel-down', 'impel-down': 'amazon-lily'
    };
    const sources = new Set();
    for (const arc of story.arcs) {
        const art = adapter.artForArc(arc);
        const payload = adapter.cataloguePayload(arc);
        assert.ok(art.badgeSource.endsWith(`/badges/${correctedFiles[arc.id] || arc.id}.png`), arc.id);
        assert.equal(payload.markerSource, art.badgeSource);
        assert.equal(payload.id, arc.id);
        assert.equal(payload.markerId, art.markerId);
        assert.ok(payload.heroSource.endsWith(`/banners/${arc.id}.png`));
        assert.deepEqual(Array.from(payload.onePacePrefixes), Array.from(arc.onePacePrefixes));
        assert.ok(arc.markerSource.endsWith(`/badges/${arc.id}.png`), 'Story identity stays unchanged');
        sources.add(art.badgeSource);
    }
    assert.equal(sources.size, 18);
    const nw = qmlJs('OnePieceNewWorldAdapter.js');
    const context = nw.contextArt('levely');
    assert.ok(context.badgeSource.endsWith('/badges/reverie.png'));
    assert.equal(context.markerSource, '');
    assert.equal(nw.isRoutableArcId('levely'), false);
    assert.equal(nw.isRoutableArcId('reverie'), false);
    for (const [sea, expected] of [['paradise', 18], ['new-world', 7]]) {
        const root = path.join(repo, 'assets/universes/one-piece', sea, 'badges');
        const manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json'), 'utf8').replace(/^\uFEFF/, ''));
        assert.equal(manifest.badges.length, expected);
        for (const row of manifest.badges) {
            assert.equal(row.file, row.arcId + '.png');
            assert.ok(fs.existsSync(path.join(root, row.file)), row.file);
        }
    }
});

test('Paradise addon evidence selects each arc without cover-story or adjacent-arc leakage', () => {
    const fixture = JSON.parse(fs.readFileSync(path.join(repo, 'tests/fixtures/one-piece/paradise-onepace-meta.json'), 'utf8').replace(/^\uFEFF/, ''));
    const story = qmlJs('OnePieceParadiseData.js');
    const adapter = qmlJs('OnePieceParadiseAdapter.js');
    const api = qmlJs('OnePieceCatalogApi.js');
    const videos = fixture.arcs.flatMap(row => [row.first, row.last, ...row.excludedExtras]);
    assert.equal(fixture.arcs.length, 18);
    for (const row of fixture.arcs) {
        const arc = adapter.cataloguePayload(story.arc(row.arcId));
        const selected = api.selectOnePaceEpisodes({ videos }, arc);
        assert.deepEqual(Array.from(selected, video => video.id), [row.first.id, row.last.id], row.arcId);
        assert.deepEqual(Array.from(arc.onePacePrefixes), [row.prefix]);
    }
});
