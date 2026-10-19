import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

const main = fs.readFileSync('qml/Main.qml', 'utf8');
function extract(name) {
    const match = main.match(new RegExp('^    function ' + name + '\\([^]*?^    }', 'm'));
    assert.ok(match, `production function ${name} exists`);
    return match[0];
}
function context({async = false, missing = false, reused = false} = {}) {
    const api = vm.createContext({console});
    vm.runInContext(fs.readFileSync('qml/UniverseExtApi.js', 'utf8').replace(/^\.pragma library\s*/m, ''), api);
    api.setReader(stem => missing ? '' : fs.readFileSync(`assets/universes/${stem}.json`, 'utf8'));
    const requests = [], queued = [], warnings = [];
    const ctx = vm.createContext({westernLayer: {active: reused, item: reused ? {} : null},
        VideoPresentation: {forEntry: () => ({})},
        console: {warn: (...args) => warnings.push(args)},
        UniverseApi: {load: (id, done) => {
            requests.push(id);
            if (async) queued.push(() => api.load(id, done));
            else api.load(id, done);
        }}});
    ctx.win = ctx;
    for (const name of ['openUniverseComic', 'reopenPinnedUniverseComic', 'openCollectionEntry', 'detailContinue'])
        vm.runInContext(extract(name), ctx);
    ctx.openWestern = () => assert.fail('A pinned post collection must never become a tag search');
    return {ctx, requests, warnings, queued};
}
test('Saved opens the exact bundled post list with stable identity on a cold loader', () => {
    const {ctx} = context();
    ctx.openCollectionEntry({world: 'tankoban', type: 'comic', id: 'gc:posts:214639', title: 'Saved title'});
    assert.equal(ctx.westernLayer.active, true);
    assert.equal(ctx.westernLayer.baked.packSeriesId, 'gc:posts:214639');
    assert.equal(ctx.westernLayer.baked.gcdId, 0);
    assert.deepEqual(Array.from(ctx.westernLayer.baked.releases, x => x.id), ['214639', '267513', '282584']);
});
test('Continue detail restores pinned posts when the comic loader is reused', () => {
    const {ctx} = context({reused: true});
    ctx.detailContinue({kind: 'comic', id: 'gc:posts:214639', title: 'Saved title'});
    assert.equal(ctx.westernLayer.item.packSeriesId, 'gc:posts:214639');
    assert.equal(ctx.westernLayer.item.gcdId, 0);
    assert.equal(ctx.westernLayer.item.bakedReleases.length, 3);
});
test('Missing bundled source leaves the existing scene intact without a guessed tag', () => {
    const {ctx, warnings} = context({missing: true});
    const before = JSON.stringify(ctx.westernLayer);
    ctx.openCollectionEntry({world: 'tankoban', type: 'comic', id: 'gc:posts:214639'});
    assert.equal(JSON.stringify(ctx.westernLayer), before);
    assert.equal(warnings.length, 1);
});
test('Asynchronous completion opens once and ignores later source completions', () => {
    const {ctx, queued} = context({async: true});
    const opens = [];
    ctx.openUniverseComic = value => opens.push(value);
    assert.equal(ctx.reopenPinnedUniverseComic('gc:posts:214639', 'Saved title', ''), true);
    assert.equal(opens.length, 0);
    queued.reverse().forEach(done => done());
    assert.equal(opens.length, 1);
});
test('Canonical GCD entries clear a previous multi-run pack identity', () => {
    const {ctx} = context({reused: true});
    ctx.westernLayer.item.packSeriesId = 'gc:posts:214639';
    ctx.openUniverseComic({title: 'Pinned run', gcdId: 114713, posts: [277237]});
    assert.equal(ctx.westernLayer.item.packSeriesId, '');
    assert.equal(ctx.westernLayer.item.gcdId, 114713);
});
test('Pinned keys must match completely; other namespaces cause no payload reads', () => {
    const {ctx, requests} = context();
    for (const id of ['gc:posts:214639\n', 'gc:posts:0', 'gc:posts:0214639', 'gc:posts:214639:1', 'gcd:214639', 'mal:13', 'gc:other'])
        assert.equal(ctx.reopenPinnedUniverseComic(id, '', ''), false, JSON.stringify(id));
    assert.equal(requests.length, 0);
});
