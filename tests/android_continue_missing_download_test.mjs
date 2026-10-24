import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const main = readFileSync(process.argv[2] || new URL('../qml/Main.qml', import.meta.url), 'utf8');
function production(source, name) {
    const fn = source.match(new RegExp(`    function ${name}\\([^]*?\\n    }`));
    assert.ok(fn, `production ${name} exists`);
    return fn[0];
}
const episodeBrowser = readFileSync(new URL('../qml/EpisodeBrowser.js', import.meta.url), 'utf8')
    .replace(/^\.pragma[^\n]*\n/gm, '').replace(/^\.import[^\n]*\n/gm, '');
const episodes = vm.createContext({});
vm.runInContext(episodeBrowser, episodes);
const cases = [
    ['movie', 'tt0063350', 'Night of the Living Dead', 1227.5, 'tt0063350'],
    ['series', 'tt0052451:1:27', 'Bonanza - S1E27', 83.25, 'tt0052451'],
    ['series', 'kitsu:7442:2:5', 'A series - S2E5', 31, 'kitsu:7442'],
];
let checks = 0;
for (const [type, id, title, position, root] of cases) {
    for (const exists of [false, true]) {
        const calls = [];
        const c = {
            VideoPresentation: { forEntry: () => ({ type }) },
            EpisodeBrowser: episodes,
            Download: { localFileAvailable: path => { assert.equal(path, '/download/removed.mp4'); return exists; } },
            openLocalVideoSession: value => calls.push(['local', value]),
            openMovieSession: () => assert.fail('A missing download must let the user choose a source, not silently play an old torrent pin'),
            openTheatreSeries: value => calls.push(['sources', value]),
        };
        c.win = c;
        vm.createContext(c);
        vm.runInContext(production(main, 'resumeContinue'), c);
        const entry = { kind: 'video', id, title, cover: 'poster',
            resume: { localPath: '/download/removed.mp4', subType: type, subId: id,
                position, infoHash: 'old-torrent-pin' } };
        const original = JSON.stringify(entry);
        c.resumeContinue(entry);
        assert.equal(JSON.stringify(entry), original, 'Routing does not rewrite watch history');
        assert.equal(calls.length, 1);
        assert.equal(calls[0][0], exists ? 'local' : 'sources', 'Missing local video must open its sources');
        if (exists) {
            assert.equal(calls[0][1].path, entry.resume.localPath);
            assert.equal(calls[0][1].position, position);
        } else {
            const target = calls[0][1];
            assert.equal(target.id, root);
            assert.equal(target.type, type);
            assert.equal(target.resumeSources.id, id, 'The exact episode survives root-title navigation');
            assert.equal(target.resumeSources.type, type);
            assert.equal(target.resumeSources.context.resumePosition, position);
            for (const file of ['TheatreSeries.qml', 'TheatreSeriesView.qml']) {
                const pageSource = readFileSync(new URL('../qml/' + file, import.meta.url), 'utf8');
                const sheetCalls = [];
                const pageContext = { page: {}, sources: { show: (...args) => sheetCalls.push(args) } };
                vm.createContext(pageContext);
                vm.runInContext(production(pageSource, 'openResumeSources'), pageContext);
                pageContext.openResumeSources(target.resumeSources);
                assert.equal(sheetCalls[0][0], type);
                assert.equal(sheetCalls[0][1], id);
                assert.equal(sheetCalls[0][3].resumePosition, position);
            }
            const sessions = [];
            const playback = { EpisodeBrowser: episodes, setGuiStallContext() {},
                Sessions: { openOrSwitch: value => sessions.push(value) } };
            playback.win = playback;
            vm.createContext(playback);
            vm.runInContext(production(main, 'openMovieSession'), playback);
            playback.openMovieSession('fresh-source', 3, title, 'art', type, id, [], target.resumeSources.context);
            assert.equal(sessions[0].target.position, position, 'Picking a fresh source retains saved position');
            playback.openMovieSession('fresh-source', 3, title, 'art', type, id, [], target.resumeSources.context, 9);
            assert.equal(sessions[1].target.position, 9, 'An explicit caller position takes precedence');
        }
        checks++;
    }
}
// Warm and initially unloaded detail pages must consume only the current intent.
const request = { id: 'tt0063350', type: 'movie', resumeSources: { id: 'tt0063350' } };
const normal = { id: 'tt0052451', type: 'series' };
for (const warm of [false, true]) {
    const calls = [];
    let playConnected = warm;
    const item = { openResumeSources: intent => {
        assert.ok(playConnected, 'Cold-loader playback connection exists before opening sources');
        calls.push(intent);
    } };
    for (const name of ['backRequested', 'minimizeRequested', 'fullscreenRequested', 'closeRequested',
                        'playLocalRequested', 'playArrivingRequested', 'openItemRequested', 'libraryRemovalRequested'])
        item[name] = { connect() {} };
    item.playRequested = { connect() { playConnected = true; } };
    const c = { theatreSeriesLayer: { active: warm, item: warm ? item : null },
        cancelCatalogueRoute() {}, wall: {}, openMovieSession() {} };
    c.win = c;
    vm.createContext(c);
    vm.runInContext(production(main, 'openTheatreSeries'), c);
    c.openTheatreSeries(request);
    if (!warm) {
        assert.equal(calls.length, 0);
        assert.equal(c.theatreSeriesLayer.active, true);
        const loader = main.slice(main.indexOf('id: theatreSeriesLayer'));
        const loaded = loader.match(/        onLoaded: \{([^]*?)\n        }/);
        assert.ok(loaded);
        c.item = item;
        c.theatreSeriesLayer.item = item;
        vm.runInContext(loaded[1], c);
    }
    assert.equal(calls.length, 1);
    assert.equal(calls[0], request.resumeSources);
    assert.equal(item.itemData, request);
    c.openTheatreSeries(normal);
    assert.equal(calls.length, 1, 'Normal detail navigation does not reopen a stale resume sheet');
    checks++;
}
console.log(`ANDROID_CONTINUE_MISSING_DOWNLOAD PASS cases=${checks}`);
