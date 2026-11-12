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
