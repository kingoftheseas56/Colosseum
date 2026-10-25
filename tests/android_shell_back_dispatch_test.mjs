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
const body = /function handleEscape\(\) \{([\s\S]*?)\n    \}\n/.exec(main)?.[1];
assert.ok(body, `${fileURLToPath(root)}qml/Main.qml: handleEscape not found`);
const dispatch = new Function('win', 'ShellBackPolicy', 'PlatformRuntime', 'Qt', body);

function invoke(state) {
    const effects = [];
    const win = {
        shellEscapeState: () => state,
        cancelCatalogueRoute: () => effects.push('cancelCatalogueRoute'),
        closeWorld: () => effects.push('closeWorld'),
        goHomeTop: () => effects.push('goHomeTop'),
        closeTheatreSeries: () => effects.push('closeTheatreSeries'),
        closeDownloadsPage: () => effects.push('closeDownloadsPage'),
        requestPlayerEscape: () => effects.push('requestPlayerEscape'),
    };
    const runtime = {
        dismissKeyboard: () => effects.push('dismissKeyboard'),
        backgroundApplication: () => effects.push('backgroundApplication'),
    };
    const qt = { inputMethod: { hide: () => effects.push('dismissKeyboard') } };
    dispatch(win, { actionFor }, runtime, qt);
    return effects;
}

test('Android world root asks the OS to background without closing the world', () => {
    assert.deepEqual(invoke({ androidHost: true, worldOpen: true }),
        ['cancelCatalogueRoute', 'backgroundApplication']);
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

test('desktop Escape retains its world-to-Home and Home-top effects', () => {
    assert.deepEqual(invoke({ worldOpen: true, keyboardVisible: true }),
        ['cancelCatalogueRoute', 'closeWorld']);
    assert.deepEqual(invoke({}), ['cancelCatalogueRoute', 'goHomeTop']);
});
