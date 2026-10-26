// Exercise the real player Loader wiring and session-close helpers. Only the
// signal/native shell effects are fake; Back must not silently warm-minimize
// an Android movie session.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const root = new URL('../', import.meta.url);
const main = readFileSync(new URL('qml/Main.qml', root), 'utf8').replace(/\r\n/g, '\n');
const playerPage = readFileSync(new URL('qml/PlayerPage.qml', root), 'utf8').replace(/\r\n/g, '\n');

function balancedBlock(source, openingBrace) {
    assert.equal(source[openingBrace], '{', 'expected opening brace');
    let depth = 0;
    let mode = '';
    let escaped = false;
    for (let i = openingBrace; i < source.length; i++) {
        const c = source[i];
        const next = source[i + 1];
        if (mode === '//') {
            if (c === '\n') mode = '';
            continue;
        }
        if (mode === '/*') {
            if (c === '*' && next === '/') { mode = ''; i++; }
            continue;
        }
        if (mode) {
            if (escaped) escaped = false;
            else if (c === '\\') escaped = true;
            else if (c === mode) mode = '';
            continue;
        }
        if (c === '/' && next === '/') { mode = '//'; i++; continue; }
        if (c === '/' && next === '*') { mode = '/*'; i++; continue; }
        if (c === '"' || c === "'" || c === '`') { mode = c; continue; }
        if (c === '{') depth++;
        if (c === '}' && --depth === 0) return source.slice(openingBrace, i + 1);
    }
    throw new Error('unclosed production block');
}

function propertyBlock(marker, from = 0) {
    const at = main.indexOf(marker, from);
    assert.ok(at >= 0, `${fileURLToPath(root)}qml/Main.qml: missing ${marker}`);
    const openingBrace = main.indexOf('{', at + marker.length);
    assert.ok(openingBrace >= 0, `production block not found after ${marker}`);
    return balancedBlock(main, openingBrace);
}

const playerLayerAt = main.indexOf('id: playerLayer');
assert.ok(playerLayerAt >= 0, 'playerLayer Loader not found');
const onLoadedBody = propertyBlock('onLoaded:', playerLayerAt);
const onLoaded = new Function('item', 'win', 'wall', 'Player2Available', 'Qt', 'console',
    `return (function() ${onLoadedBody})();`);

function functionBody(name) {
    const marker = `function ${name}(`;
    const at = main.indexOf(marker);
    assert.ok(at >= 0, `production function not found: ${name}`);
    const openingBrace = main.indexOf('{', at + marker.length);
    return balancedBlock(main, openingBrace);
}

function playerFunctionBody(name) {
    const marker = `function ${name}(`;
    const at = playerPage.indexOf(marker);
    assert.ok(at >= 0, `production PlayerPage function not found: ${name}`);
    const openingBrace = playerPage.indexOf('{', at + marker.length);
    return balancedBlock(playerPage, openingBrace);
}

const anyMenuMatch = playerPage.match(/readonly property bool anyMenuOpen:\s*([^\n]+)/);
assert.ok(anyMenuMatch, 'production PlayerPage anyMenuOpen property not found');
const actualAnyMenuOpen = new Function('root', 'watchPartyMenu', 'audioMenu', 'subMenu',
    'speedMenu', 'fillMenu', 'subStyleBar', `return ${anyMenuMatch[1]};`);

function installPlayerEscape(item) {
    item.shortcutsOpen = false;
    item.resumeChoiceOpen = false;
    item.liveGuideOpen = false;
    item.dvrPanelOpen = false;
    item.overflowOpen = false;
    item.closeConfirmOpen = false;
    item.browserOpen = false;
    item.watchPartyMenu = { panelOpen: false };
    item.audioMenu = { panelOpen: false };
    item.subMenu = { panelOpen: false };
    item.speedMenu = { panelOpen: false };
    item.fillMenu = { panelOpen: false };
    item.subStyleBar = { open: false };
    Object.defineProperty(item, 'anyMenuOpen', {
        get: () => actualAnyMenuOpen(item, item.watchPartyMenu, item.audioMenu,
            item.subMenu, item.speedMenu, item.fillMenu, item.subStyleBar),
    });
    item.closeMenus = new Function('root', 'watchPartyMenu', 'audioMenu', 'subMenu',
        'subStyleBar', 'speedMenu', 'fillMenu',
        `return function closeMenus() ${playerFunctionBody('closeMenus')}`)(
            item, item.watchPartyMenu, item.audioMenu, item.subMenu, item.subStyleBar,
            item.speedMenu, item.fillMenu);
    item.requestEscape = new Function('root',
        `return function requestEscape() ${playerFunctionBody('requestEscape')}`)(item);
}

class Signal {
    constructor() {
        const listeners = [];
        const invoke = (...args) => { for (const listener of listeners) listener(...args); };
        invoke.connect = listener => listeners.push(listener);
        invoke.emit = invoke;
        return invoke;
    }
}

function loadedPlayer(host) {
    const calls = [];
    const item = {
        backRequested: new Signal(),
        minimizeRequested: new Signal(),
        fullscreenRequested: new Signal(),
        closeRequested: new Signal(),
    };
    const win = {
        androidHost: host === 'android',
        usePlayer2: false,
        minimizePlayer: () => calls.push('minimizePlayer'),
        closePlayerSession: () => calls.push('closePlayerSession'),
        toggleFullscreenShell: () => calls.push('toggleFullscreenShell'),
        flushPendingPlayerOpen: () => calls.push('flushPendingPlayerOpen'),
    };
    onLoaded(item, win, {}, false, { callLater() {} }, { log() {}, warn() {} });
    installPlayerEscape(item);
    return { item, calls };
}

test('Android player Back closes its movie session instead of warm-minimizing', () => {
    const player = loadedPlayer('android');
    player.item.backRequested.emit();
    assert.deepEqual(player.calls, ['closePlayerSession']);
});

test('Android Back at the resume choice cancels opening and closes the session once', () => {
    const player = loadedPlayer('android');
    player.item.resumeChoiceOpen = true;
    player.item.requestEscape();
    assert.deepEqual(player.calls, ['closePlayerSession']);
});

test('Android Back closes an open subtitle menu before it can close the movie session', () => {
    const player = loadedPlayer('android');
    player.item.subMenu.panelOpen = true;
    assert.equal(player.item.anyMenuOpen, true);
    let closeMenusCalls = 0;
    const closeMenus = player.item.closeMenus;
    player.item.closeMenus = () => { closeMenusCalls++; closeMenus(); };

    player.item.requestEscape();

    assert.equal(closeMenusCalls, 1);
    assert.equal(player.item.subMenu.panelOpen, false);
    assert.deepEqual(player.calls, []);
});

test('Android Back consumes the shortcuts overlay before other player actions', () => {
    const player = loadedPlayer('android');
    player.item.shortcutsOpen = true;
    player.item.subMenu.panelOpen = true;
    let closeMenusCalls = 0;
    const closeMenus = player.item.closeMenus;
    player.item.closeMenus = () => { closeMenusCalls++; closeMenus(); };

    player.item.requestEscape();

    assert.equal(player.item.shortcutsOpen, false);
    assert.equal(closeMenusCalls, 0);
    assert.equal(player.item.subMenu.panelOpen, true);
    assert.deepEqual(player.calls, []);
});

test('desktop and Windows Back still warm-minimize; explicit close and minimize keep their verbs', () => {
    for (const host of ['desktop', 'windows']) {
        const player = loadedPlayer(host);
        player.item.backRequested.emit();
        assert.deepEqual(player.calls, ['minimizePlayer'], `${host} Back`);

        player.calls.length = 0;
        player.item.minimizeRequested.emit();
        assert.deepEqual(player.calls, ['minimizePlayer'], `${host} explicit minimize`);

        player.calls.length = 0;
        player.item.closeRequested.emit();
        assert.deepEqual(player.calls, ['closePlayerSession'], `${host} explicit close`);
    }
});

test('the production close helper routes an active movie through closeSession', () => {
    const calls = [];
    const movie = { id: 'movie-session-17', contentKind: 'movie' };
    const Sessions = { activeId: movie.id, get: id => id === movie.id ? movie : null };
    const win = {
        closeSession: id => calls.push(['closeSession', id]),
        closePlayer: () => calls.push(['closePlayer']),
    };
    const closePlayerSession = new Function('win', 'Sessions',
        `return (function closePlayerSession() ${functionBody('closePlayerSession')});`)(win, Sessions);
    closePlayerSession();
    assert.deepEqual(calls, [['closeSession', movie.id]]);

    calls.length = 0;
    Sessions.get = () => null;
    closePlayerSession();
    assert.deepEqual(calls, [['closePlayer']]);
});

test('real closeSession stops the warm movie before removing its progress-bearing session', () => {
    const calls = [];
    const progressRoute = { mediaId: 'tt0063350', resumePositionSec: 4777.454 };
    const movie = { id: 'movie-session-17', contentKind: 'movie', progressRoute };
    const Sessions = {
        activeId: movie.id,
        get: id => id === movie.id ? movie : null,
        close: id => calls.push(['Sessions.close', id]),
    };
    const win = {
        warmPlayerSessionId: movie.id,
        teardownSession: record => calls.push(['teardownSession', record.id]),
    };
    const playerLayer = {
        item: { stop: () => calls.push(['player.stop', movie.progressRoute]) },
    };
    const feriaSessionDeck = { closeTab() { throw new Error('movie close must not close a browser tab'); } };
    const closeSession = new Function('win', 'Sessions', 'playerLayer', 'feriaSessionDeck',
        `return (function closeSession(id) ${functionBody('closeSession')});`)(
        win, Sessions, playerLayer, feriaSessionDeck);

    closeSession(movie.id);
    assert.deepEqual(calls, [
        ['teardownSession', movie.id],
        ['player.stop', progressRoute],
        ['Sessions.close', movie.id],
    ]);
    assert.equal(win.warmPlayerSessionId, '');
    assert.equal(movie.progressRoute, progressRoute);
});
