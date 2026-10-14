// Executes actual QML route functions: no application/network or database needed.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';
const main = fs.readFileSync('qml/Main.qml', 'utf8');
const arc = fs.readFileSync('qml/OnePieceArcPage.qml', 'utf8');
function extract(source, name) {
    const match = source.match(new RegExp('^    function ' + name + '\\([^]*?^    }', 'm'));
    assert.ok(match, `production function ${name} exists`);
    return match[0];
}
function context() {
    const ctx = vm.createContext({seriesLayer: {active: false, malId: ''},
        VideoPresentation: {forEntry: () => ({})}, calls: []});
    ctx.win = ctx;
    for (const name of ['clearSeriesEditionProfile', 'applySeriesEditionProfile',
                        'restoreSeriesEditionProfile', 'detailContinue', 'resumeContinue',
                        'openComicSession', 'openSeriesAt', 'routeUniverseSeries'])
        vm.runInContext(extract(main, name), ctx);
    if (main.includes('function canonicalMalId('))
        vm.runInContext(extract(main, 'canonicalMalId'), ctx);
    ctx.openSeries = (...args) => ctx.calls.push(args);
    ctx.openWeebCentralSeries = (...args) => ctx.calls.push(['weebcentral', ...args]);
    ctx.Sessions = {openOrSwitch: request => {ctx.session = request;}};
    return ctx;
}
for (const kind of ['manga', 'tankoban']) {
    test(`Continue ${kind} detail preserves explicit MAL13 without a MAL database`, () => {
        const ctx = context();
        ctx.detailContinue({kind, id: 'mal:13', title: 'One Piece'});
        assert.equal(ctx.calls[0][1], '13');
    });
    test(`Continue ${kind} resume restores explicit MAL13 from the session identity`, () => {
        const ctx = context();
        ctx.resumeContinue({kind, id: 'mal:13', title: 'One Piece', resume: {chapterId: 'volume:1'}});
        assert.equal(ctx.session.target.seriesId, 'mal:13');
        assert.equal(ctx.session.target.chapterId, 'volume:1');
        // Both actual comic-session activation branches restore this profile.
        assert.match(main, /restoreSeriesEditionProfile\(t\.seriesId\)/);
        ctx.restoreSeriesEditionProfile(ctx.session.target.seriesId);
        assert.equal(ctx.seriesLayer.malId, '13');
    });
}
test('canonical MAL identity accepts positive int32 bounds and rejects every other shape', () => {
    const ctx = context();
    for (const [id, expected] of [['mal:1', '1'], ['mal:2147483647', '2147483647'],
        ['mal:0', ''], ['mal:-1', ''], ['mal:01', ''], ['mal:1.5', ''],
        ['mal:2147483648', ''], ['mal:99999999999999999999', ''], ['mal:13:color', ''],
        ['mal:13:volume:1', ''], ['anilist:30013', ''], ['13', ''], ['One Piece', ''],
        [' mal:13', ''], ['mal:13\n', ''], ['', '']]) {
        ctx.calls = [];
        ctx.detailContinue({kind: 'manga', id, title: 'Same title'});
        assert.equal(ctx.calls[0][1] || '', expected, id);
    }
});
test('resume edition profiles keep color and unknown namespaces unchanged', () => {
    const ctx = context();
    ctx.restoreSeriesEditionProfile('mal:13:color');
    assert.equal(ctx.seriesLayer.malId, '13');
    assert.equal(ctx.seriesLayer.seriesIdOverride, 'mal:13:color');
    assert.equal(ctx.seriesLayer.sourceSearchTitle, 'One Piece Colored');
    ctx.restoreSeriesEditionProfile('anilist:30013');
    assert.equal(ctx.seriesLayer.malId, '');
    assert.equal(ctx.seriesLayer.seriesIdOverride, '');
});
function arcContext(color) {
    const ctx = context();
    const ordinary = {id: '30013', provider: 'anilist', title: 'One Piece'};
    const colored = {id: 'color-provider-id', provider: 'weebcentral', title: 'One Piece Colored'};
    ctx.root = {arc: {id: 'romance'}, entry: () => ordinary,
        colorMangaEntry: () => colored, seriesRequested: entry => ctx.routeUniverseSeries(entry)};
    vm.runInContext(extract(arc, 'openCatalogueVolume'), ctx);
    ctx.openCatalogueVolume('1', color);
    return {ctx, ordinary, colored};
}
test('ordinary One Piece arc volume routes known MAL13, never AniList30013', () => {
    const {ctx, ordinary} = arcContext(false);
    assert.equal(ctx.calls[0][1], '13');
    assert.equal(ctx.calls[0][2].seriesId, 'mal:13');
    assert.equal(ctx.calls[0][3], '1');
    assert.equal(ordinary.provider, 'anilist', 'original payload is not mutated');
    assert.equal(ordinary.id, '30013');
});
test('colored arc volume keeps its direct WeebCentral path', () => {
    const {ctx} = arcContext(true);
    assert.deepEqual(ctx.calls[0], ['weebcentral', 'One Piece Colored', '1']);
});
test('unrelated universe entries remain title-only, without namespace conversion', () => {
    const ctx = context();
    ctx.routeUniverseSeries({provider: 'anilist', id: '30013', title: 'Unrelated', requestedVolumeNumber: '2'});
    assert.equal(ctx.calls[0][1], '');
    assert.equal(ctx.calls[0][3], '2');
});
