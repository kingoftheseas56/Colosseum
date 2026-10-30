// Run the production shell dispatcher and policy, replacing only UI/OS effects.
// A missing Android branch must neither navigate away from its world nor let
// keyboard dismissal cancel a pending catalogue route.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const root = new URL('../', import.meta.url);
const main = readFileSync(new URL('qml/Main.qml', root), 'utf8').replace(/\r\n/g, '\n');
const policy = readFileSync(new URL('qml/ShellBackPolicy.js', root), 'utf8')
    .replace(/^\.pragma library\s*$/m, '');
const actionFor = new Function(policy + '\nreturn actionFor;')();
const overlaysSource = readFileSync(new URL('qml/PhoneOverlayRegistry.js', root), 'utf8')
    .replace(/^\.pragma library\s*$/m, '');
const PhoneOverlays = new Function(overlaysSource + '\nreturn {claim, release, dismiss};')();
const body = /function handleEscape\(\) \{([\s\S]*?)\n    \}\n/.exec(main)?.[1];
assert.ok(body, `${fileURLToPath(root)}qml/Main.qml: handleEscape not found`);
const dispatch = new Function('win', 'ShellBackPolicy', 'PlatformRuntime', 'Qt', 'theatreSeriesLayer', 'worldStack', 'PhoneOverlays', body);

function invoke(state) {
    const effects = [];
    const win = {
        androidHost: state.androidHost === true,
        shellEscapeState: () => state,
        cancelCatalogueRoute: () => effects.push('cancelCatalogueRoute'),
        closeWorld: () => effects.push('closeWorld'),
        goHomeTop: () => effects.push('goHomeTop'),
        closeTheatreSeries: () => effects.push('closeTheatreSeries'),
        closeDownloadsPage: () => effects.push('closeDownloadsPage'),
        requestPlayerEscape: () => effects.push('requestPlayerEscape'),
    };
    const theatreSeriesLayer = {
        active: !!state.theatreSeriesActive,
        item: state.theatreItem === 'withoutRequest' ? {} : (state.theatreItem ? {
            requestEscape: () => {
                effects.push('detailRequestEscape');
                return state.theatreRequestResult;
            },
        } : null),
    };
    const runtime = {
        dismissKeyboard: () => effects.push('dismissKeyboard'),
        backgroundApplication: () => effects.push('backgroundApplication'),
    };
    const sheet = { visible: !!state.sheetOpen, close() { effects.push('closeSheet'); this.visible = false; } };
    if (state.sheetOpen) PhoneOverlays.claim(sheet, win);
    const qt = { inputMethod: { visible: !!state.keyboardVisible, hide: () => effects.push('dismissKeyboard') } };
    dispatch(win, { actionFor }, runtime, qt, theatreSeriesLayer, { current: 'Theatre' }, PhoneOverlays);
    PhoneOverlays.release(sheet);
    return effects;
}

test('Android world root asks the OS to background without closing the world', () => {
    assert.deepEqual(invoke({ androidHost: true, worldOpen: true }),
        ['cancelCatalogueRoute', 'backgroundApplication']);
});

test('a modal sheet consumes native Back before the player or world', () => {
    assert.deepEqual(invoke({androidHost:true, playerOpen:true, sheetOpen:true}), ['closeSheet']);
    assert.deepEqual(invoke({androidHost:true, worldOpen:true, sheetOpen:true}), ['closeSheet']);
    assert.deepEqual(invoke({androidHost:true, worldOpen:true, sheetOpen:true, keyboardVisible:true}), ['dismissKeyboard']);
});

test('Android empty root also backgrounds without navigating to desktop Home', () => {
    assert.deepEqual(invoke({ androidHost: true }),
        ['cancelCatalogueRoute', 'backgroundApplication']);
});

test('visible Android keyboard consumes Back without changing the page or route', () => {
    assert.deepEqual(invoke({ androidHost: true, keyboardVisible: true,
        theatreSeriesActive: true, worldOpen: true }), ['dismissKeyboard']);
});

test('Android detail, download page and player still receive Back before root', () => {
    assert.deepEqual(invoke({ androidHost: true, theatreSeriesActive: true, worldOpen: true }),
        ['cancelCatalogueRoute', 'closeTheatreSeries']);
    assert.deepEqual(invoke({ androidHost: true, downloadsActive: true, worldOpen: true }),
        ['cancelCatalogueRoute', 'closeDownloadsPage']);
    assert.deepEqual(invoke({ androidHost: true, playerOpen: true, worldOpen: true }),
        ['cancelCatalogueRoute', 'requestPlayerEscape']);
});

test('Theatre detail requestEscape gets first chance; false or absent closes detail', () => {
    const consumed = invoke({ androidHost: true, theatreSeriesActive: true,
        theatreItem: true, theatreRequestResult: true });
    assert.deepEqual(consumed, ['cancelCatalogueRoute', 'detailRequestEscape']);

    const fallsThrough = invoke({ androidHost: true, theatreSeriesActive: true,
        theatreItem: true, theatreRequestResult: false });
    assert.deepEqual(fallsThrough, ['cancelCatalogueRoute', 'detailRequestEscape', 'closeTheatreSeries']);

    assert.deepEqual(invoke({ androidHost: true, theatreSeriesActive: true, theatreItem: 'withoutRequest' }),
        ['cancelCatalogueRoute', 'closeTheatreSeries']);
});

test('desktop Escape retains its world-to-Home and Home-top effects', () => {
    assert.deepEqual(invoke({ worldOpen: true, keyboardVisible: true }),
        ['cancelCatalogueRoute', 'closeWorld']);
    assert.deepEqual(invoke({}), ['cancelCatalogueRoute', 'goHomeTop']);
    assert.deepEqual(invoke({ theatreSeriesActive: true, theatreItem: true, theatreRequestResult: true }),
        ['cancelCatalogueRoute', 'closeTheatreSeries']);
});
