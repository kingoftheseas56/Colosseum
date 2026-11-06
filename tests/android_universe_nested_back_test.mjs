// WT-10: native Back through Main.requestUniverseEscape consumes Galaxy destinations and
// DCAU hubs first and closes the Universe exactly once at their roots. Executes the
// production function bodies with recording doubles (no QML runtime).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

const root = new URL('../', import.meta.url);
const read = name => readFileSync(new URL(`qml/${name}`, root), 'utf8').replace(/\r\n/g, '\n');
function body(source, name) {
    const m = new RegExp(`^    function ${name}\\s*\\([^\\n]*\\)\\s*\\{([\\s\\S]*?)^    \\}`, 'm').exec(source);
    assert.ok(m, `production ${name} exists`);
    return m[1];
}
const main = read('Main.qml');
const shellEscape = new Function('universeLayer', 'win', body(main, 'requestUniverseEscape'));

function galaxy(selected) {
    const src = read('GalaxyUniversePage.qml');
    const events = [];
    const page = { selectedDestinationId: selected,
        closeDestination() { events.push('closeDestination'); this.selectedDestinationId = ''; },
        backRequested() { events.push('backRequested'); } };
    const fn = new Function('page', `with (page) { ${body(src, 'requestEscape')} }`);
    page.requestEscape = () => fn(page);
    return { page, events };
}
function dcau(hub) {
    const src = read('DCAUUniversePage.qml');
    const events = [];
    const rootObj = { currentHubId: hub, backRequested() { events.push('backRequested'); } };
    rootObj.requestEscape = new Function('root', body(src, 'requestEscape')).bind(null, rootObj);
    rootObj.navigateBack = new Function('root', body(src, 'navigateBack')).bind(null, rootObj);
    return { page: rootObj, events };
}
function nativeBack(item) {
    const closes = [];
    shellEscape({ active: true, item }, { closeUniverse() { closes.push('closeUniverse'); } });
    return closes;
}

test('Galaxy destination consumes Back; root closes the Universe once', () => {
    const g = galaxy('andromeda');
    assert.deepEqual(nativeBack(g.page), []);
    assert.deepEqual(g.events, ['closeDestination']);
    assert.deepEqual(nativeBack(g.page), ['closeUniverse']);
    assert.deepEqual(g.events, ['closeDestination'], 'root Back does not also emit backRequested');
});

test('DCAU hub returns to landing; landing closes the Universe once', () => {
    const d = dcau('justice-league');
    assert.deepEqual(nativeBack(d.page), []);
    assert.equal(d.page.currentHubId, '');
    assert.deepEqual(nativeBack(d.page), ['closeUniverse']);
    assert.deepEqual(d.events, []);
});

test('DCAU visible Back button keeps its route through the same contract', () => {
    const d = dcau('batman');
    d.page.navigateBack();
    assert.equal(d.page.currentHubId, '');
    assert.deepEqual(d.events, []);
    d.page.navigateBack();
    assert.deepEqual(d.events, ['backRequested']);
});
