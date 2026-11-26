import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
function shipped(file, name, context) {
    const source = readFileSync(new URL('../qml/' + file, import.meta.url), 'utf8');
    const start = source.indexOf('function ' + name + '(');
    assert.ok(start >= 0, name + ' exists');
    let end = source.indexOf('{', start), depth = 1;
    while (depth && ++end < source.length) {
        if (source[end] === '{') depth++;
        if (source[end] === '}') depth--;
    }
    return vm.runInNewContext('(' + source.slice(start, end + 1) + ')', context);
}
test('Vault opens existing Theatre with exact movie or series identity', () => {
    const calls = [];
    const win = { closeVaultPage() {}, openWorld(world) { calls.push(world); },
        openTheatreSeries(row) { calls.push(row); } };
    const open = shipped('Main.qml', 'openVaultIdentity', {win});
    open({world:'Theatre', identityId:'imdb:tt123', title:'Movie'});
    assert.equal(calls[1].id, 'tt123'); assert.equal(calls[1].type, 'movie');
    open({world:'Theatre', identityId:'tmdb:tv:101', type:'series', title:'Show', tmdbId:101, tvdbId:202});
    assert.equal(calls[3].id, 'tmdb:tv:101'); assert.equal(calls[3].type, 'series');
    assert.equal(calls[3].tmdbId, 101); assert.equal(calls[3].tvdbId, 202);
});
test('existing provider details stay authoritative and do not call server fallback', () => {
    const native = { titleMetadata() { throw new Error('provider hit must not call fallback'); } };
    const fn = shipped('TheatreSeries.qml', 'homeServerMetadataFallback', {HomeServers:native, itemData:{}});
    const meta = {id:'tt123',name:'Movie'};
    assert.equal(fn(meta,'movie','tt123'), meta);
});
test('missing details use only current native identified server metadata', () => {
    let requested;
    const meta = {id:'tmdb:tv:101',type:'series',name:'Show',videos:[{id:'tmdb:tv:101:0:2',season:0,episode:2}]};
    const native = { titleMetadata(query) { requested = query; return meta; } };
    const fn = shipped('TheatreSeries.qml', 'homeServerMetadataFallback', {HomeServers:native,itemData:{tmdbId:101,tvdbId:202}});
    assert.equal(fn(null,'series','tmdb:tv:101'), meta);
    assert.equal(requested.type,'series'); assert.equal(requested.id,'tmdb:tv:101'); assert.equal(requested.tmdbId,101);
    native.titleMetadata = () => ({}); assert.equal(fn(null,'series','tmdb:tv:101'),null);
    const disconnected = shipped('TheatreSeries.qml', 'homeServerMetadataFallback', {itemData:{}});
    assert.equal(disconnected(null,'movie','tt123'),null);
});

for (const file of ['TheatreSeries.qml', 'TheatreSeriesView.qml']) {
    function resolveTitle(providerMeta, native, itemData = {id:'tmdb:tv:101', type:'series', tmdbId:101, tvdbId:202}) {
        const queries = [], shown = [];
        const id = itemData.id;
        const state = {
            itemData,
            currentId: () => id,
            defaultAnimeOrder: () => ({}), cancelMoreLike() {},
            rebuildAnimeOrder() {}, onMetaLoaded() { this.loaded = true; },
            revealGuard: {restart() {}, stop() {}},
            TheatreFacts: {factRows: () => []},
            TheatreApi: {
                loadMeta(type, requestedId, callback) { callback(providerMeta); },
                normalizeArtUrl: value => value,
                loadAnimeCast(id, callback) { callback(null); }, moreLikeThis() {},
            },
            sources: {show(...args) { shown.push(args); }},
            tryPlayLocal: () => false, tryPlayArriving: () => false,
            sourceMetaLine: () => '', sourceBackdrop: () => '',
            episodeSourceLine: () => '', adjacentEpisodeContext: () => ({}),
            episodeSeason: ep => ep.season, episodeNumber: ep => ep.episode,
        };
        if (native) state.HomeServers = {titleMetadata(query) { queries.push(query); return native(query); }};
        state.page = state;
        state.homeServerMetadataFallback = shipped(file, 'homeServerMetadataFallback', state);
        state.episodeStreamId = shipped(file, 'episodeStreamId', state);
        shipped(file, 'resolve', state)();
        return {state, queries, shown};
    }

    test(file + ': actual resolve preserves provider success without calling native', () => {
        const {state, queries} = resolveTitle({id:'tt123', name:'Provider title', tmdbId:303}, () => {
            throw new Error('provider hit must not call fallback');
        });
        assert.equal(state.loaded, true);
        assert.equal(state.title, 'Provider title');
        assert.equal(state.resolvedId, 'tt123');
        assert.equal(state.tmdbId, 303);
        assert.equal(queries.length, 0);
    });

    test(file + ': actual resolve uses current native metadata and carries exact source context', () => {
        const episode = {id:'tmdb:tv:101:0:2', season:0, episode:2};
        const {state, queries, shown} = resolveTitle(null, () => ({
            id:'tmdb:tv:101', name:'Server title', tmdbId:101, videos:[episode],
        }));
        assert.equal(state.loaded, true);
        assert.equal(state.title, 'Server title');
        assert.equal(queries.length, 1);
        assert.equal(queries[0].id, 'tmdb:tv:101');
        assert.equal(queries[0].type, 'series');
        assert.equal(queries[0].tmdbId, 101);
        assert.equal(queries[0].tvdbId, 202);
        assert.equal(state.sourceVideos[0], episode);
        shipped(file, 'openEpisodeForPlay', state)(state.sourceVideos[0]);
        assert.equal(shown[0][0], 'series');
        assert.equal(shown[0][1], 'tmdb:tv:101:0:2');
        assert.equal(shown[0][3].tmdbId, 101);
        assert.equal(shown[0][3].imdbId, 'tmdb:tv:101');
        assert.equal(shown[0][3].season, 0);
        assert.equal(shown[0][3].episode, 2);
    });

    test(file + ': movie fallback carries resolved IDs to the shared source sheet', () => {
        const {state, queries, shown} = resolveTitle(null,
            () => ({id:'tt123', name:'Server movie', tmdbId:101}),
            {id:'tt123', type:'movie', tmdbId:101});
        assert.equal(state.loaded, true);
        assert.equal(queries[0].type, 'movie');
        assert.equal(queries[0].id, 'tt123');
        assert.equal(queries[0].tmdbId, 101);
        shipped(file, 'openHeroForPlay', state)();
        assert.equal(shown[0][0], 'movie');
        assert.equal(shown[0][1], 'tt123');
        assert.equal(shown[0][3].tmdbId, 101);
        assert.equal(shown[0][3].imdbId, 'tt123');
        assert.equal(shown[0][3].season, undefined);
        assert.equal(shown[0][3].episode, undefined);
    });

    test(file + ': actual resolve fails closed without current identified native metadata', () => {
        for (const native of [undefined, () => ({}), () => null]) {
            const {state} = resolveTitle(null, native);
            assert.equal(state.loaded, undefined);
            assert.equal(state.loading, false);
            assert.equal(state.errorMsg, "Couldn't load details.");
        }
    });
}
