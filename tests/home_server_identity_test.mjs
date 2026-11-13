import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source = readFileSync(new URL('../qml/EpisodeBrowser.js', import.meta.url), 'utf8')
    .split('\n').filter(line => !line.startsWith('.pragma') && !line.startsWith('.import')).join('\n');
const browser = vm.createContext({});
vm.runInContext(source, browser);
test('typed server show IDs retain each title and exact episode', () => {
    for (const id of ['tmdb:tv:101', 'tmdb:tv:102', 'tvdb:tv:103']) {
        assert.equal(browser.seriesRootId(id + ':0:2'), id);
        assert.equal(browser.seriesRootId(id), id);
        assert.equal(browser.seasonOf(id + ':0:2'), 0);
    }
});
test('typed server movie is its own identity', () => {
    assert.equal(browser.seriesRootId('tmdb:movie:101'), 'tmdb:movie:101');
    assert.equal(browser.seasonOf('tmdb:movie:101'), -1);
});
test('existing IMDb Kitsu and Vault identities retain behavior', () => {
    assert.equal(browser.seriesRootId('tt123:1:2'), 'tt123');
    assert.equal(browser.seriesRootId('kitsu:123:1:2'), 'kitsu:123');
    assert.equal(browser.seriesRootId('vault:abcdef'), 'vault:abcdef');
});
