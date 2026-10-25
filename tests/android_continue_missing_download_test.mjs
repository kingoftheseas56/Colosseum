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
// The real deleted-download record was at 88.5%: an explicit Continue intent
// must survive the player's ordinary near-finished restart heuristic.
const playerSource = readFileSync(process.argv[3] || new URL('../qml/PlayerPage.qml', import.meta.url), 'utf8');
const p = { resumePromptMinSec: 30, resumeRestartThreshold: 0.8,
    normalizeStreamCandidates: () => [], findStreamIndex: () => 0,
    resolveAdjacentContext: () => ({}), wakeChrome() {} };
for (const name of ['clearActiveDirectHeaders', 'clearAbLoop', 'cancelSleepTimer',
    'resetSkipSegments', 'resetTrackAutomation', 'cancelUpNext', 'updateMediaSubtitle',
    'fetchSubtitles', 'playStreamAt', 'maybeHydrateContext']) p[name] = () => {};
const playerContext = { root: p, mpv: { duration: 5796.46, pause: false } };
vm.createContext(playerContext);
for (const name of ['clearStreamRouteRestore', 'resetRecoveryResume', 'playTorrent', 'restoreState', 'shouldSkipResumePrompt', 'prepareResumeChoice'])
    vm.runInContext(production(playerSource, name), playerContext);
Object.assign(p, playerContext);
for (const [position, explicit, prompt] of [[5132.649, true, true], [1200, true, true],
                                         [5132.649, false, false], [1200, false, true]]) {
    p.playTorrent('hash', 0, 'Night of the Living Dead', 'https://art/tt0063350/img',
        'movie', 'tt0063350', [], explicit ? { resumePosition: position } : {});
    p.restoreState({ position });
    assert.equal(p.prepareResumeChoice(), prompt,
        'An explicit Continue preserves even late progress; ordinary completion rules remain');
    if (prompt) assert.equal(p.resumeChoiceSec, position);
    else assert.equal(p.pendingSeekSec, 0);
    checks++;
}
for (const name of ['playUrl', 'playLocalFile', 'playRemoteUrl'])
    assert.match(production(playerSource, name), /root\.explicitResumePosition = false/,
        `${name} cannot inherit an earlier explicit Continue intent`);
// Android-only deletion handling closes only the live local movie whose
// download record disappeared; session-backed movies are closed by identity.
function closingBrace(source, openIndex) {
    let depth = 0;
    let quote = "";
    let lineComment = false;
    let blockComment = false;
    const templateQuote = String.fromCharCode(96);
    for (let i = openIndex; i < source.length; i++) {
        const ch = source[i];
        const next = source[i + 1];
        if (lineComment) { if (ch === "\n") lineComment = false; continue; }
        if (blockComment) {
            if (ch === "*" && next === "/") { blockComment = false; i++; }
            continue;
        }
        if (quote) {
            if (ch === "\\") { i++; continue; }
            if (ch === quote) quote = "";
            continue;
        }
        if (ch === "/" && next === "/") { lineComment = true; i++; continue; }
        if (ch === "/" && next === "*") { blockComment = true; i++; continue; }
        if (ch === "'" || ch === '"' || ch === templateQuote) { quote = ch; continue; }
        if (ch === "{") depth++;
        else if (ch === "}" && --depth === 0) return i;
    }
    return -1;
}
const releaseSource = production(main, "releaseRemovedLocalVideo");
function runRelease(removedId, options = {}) {
    const calls = [];
    const records = options.records || {};
    const context = {
        androidHost: options.androidHost !== false,
        playerOpen: options.playerOpen !== false,
        warmPlayerSessionId: options.warmPlayerSessionId || "",
        playerLayer: { item: options.player === undefined
            ? { mediaLocalPath: "/cache/deleted.mp4", mediaId: "tt-deleted" }
            : options.player },
        Sessions: {
            activeId: options.activeId || "",
            get(id) { return records[id] || null; }
        },
        closeSession(id) { calls.push("session:" + String(id)); },
        closePlayer() { calls.push("player"); }
    };
    context.win = context;
    vm.createContext(context);
    vm.runInContext(releaseSource, context);
    context.releaseRemovedLocalVideo(removedId);
    return calls;
}
const deletedPath = "/cache/deleted.mp4";
const deletedId = "tt-deleted";
const movieRecord = (id, path) => ({ contentKind: "movie",
    target: { id, localPath: path } });
const removalCases = [
    ["matching minimized warm local session",
        { playerOpen: false, warmPlayerSessionId: "warm",
            records: { warm: movieRecord(deletedId, deletedPath) } },
        ["session:warm"]],
    ["matching active local session",
        { activeId: "active", records: { active: movieRecord(deletedId, deletedPath) } },
        ["session:active"]],
    ["legacy local player without a session",
        { playerOpen: true, warmPlayerSessionId: "", activeId: "", records: {} },
        ["player"]],
    ["streamed session with the same content ID",
        { player: { mediaLocalPath: "", mediaId: deletedId }, activeId: "stream",
            records: { stream: { contentKind: "movie",
                target: { id: deletedId, infoHash: "stream-hash" } } } },
        []],
    ["current local player has a different ID",
        { player: { mediaLocalPath: deletedPath, mediaId: "tt-other" },
            activeId: "other", records: { other: movieRecord("tt-other", deletedPath) } },
        []],
    ["unrelated warm session is preserved",
        { playerOpen: false, warmPlayerSessionId: "other",
            records: { other: movieRecord("tt-other", "/cache/other.mp4") } },
        []],
    ["desktop playback policy is unchanged",
        { androidHost: false, activeId: "active",
            records: { active: movieRecord(deletedId, deletedPath) } },
        []],
    ["missing player item is ignored",
        { player: null, activeId: "active",
            records: { active: movieRecord(deletedId, deletedPath) } },
        []]
];
for (const [name, options, expected] of removalCases) {
    assert.deepEqual(runRelease(deletedId, options), expected, name);
    checks++;
}
assert.deepEqual(runRelease("", { records: { active: movieRecord(deletedId, deletedPath) },
    activeId: "active" }), [], "empty removed ID is ignored");
checks++;

// Execute the production Download.removed handler body to prove signal wiring.
let removedHandler = null;
const connections = /Connections\s*\{/g;
let connectionMatch;
while ((connectionMatch = connections.exec(main)) !== null) {
    const open = main.indexOf("{", connectionMatch.index);
    const close = closingBrace(main, open);
    if (close < 0) continue;
    const block = main.slice(connectionMatch.index, close + 1);
    if (!/\btarget\s*:\s*Download\b/.test(block)) continue;
    const handler = /function\s+onRemoved\s*\(\s*([A-Za-z_$][\w$]*)\s*\)\s*\{/.exec(block);
    if (!handler) continue;
    const bodyOpen = block.indexOf("{", handler.index);
    const bodyClose = closingBrace(block, bodyOpen);
    if (bodyClose >= 0) {
        removedHandler = { parameter: handler[1], body: block.slice(bodyOpen + 1, bodyClose) };
        break;
    }
}
assert.ok(removedHandler, "Download.removed is connected to a production onRemoved handler");
const signalCalls = [];
const signalContext = {
    win: { androidHost: true,
        releaseRemovedLocalVideo(id) { signalCalls.push(String(id)); } },
    releaseRemovedLocalVideo(id) { signalCalls.push(String(id)); }
};
vm.runInNewContext("(function(" + removedHandler.parameter + ") {"
    + removedHandler.body + "\n})(" + JSON.stringify(deletedId) + ")", signalContext);
assert.deepEqual(signalCalls, [deletedId], "Download.removed forwards the exact removed ID");
checks++;
console.log(`ANDROID_CONTINUE_MISSING_DOWNLOAD PASS cases=${checks}`);
