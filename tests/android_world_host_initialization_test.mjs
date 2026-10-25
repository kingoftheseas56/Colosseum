// Exercise Main's real world-Loader initializer. The phone-shell override must
// reach WorldPage before construction, even on a Windows qualification host.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

const main = readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8').replace(/\r\n/g, '\n');
const loader = main.slice(main.indexOf('id: worldLoader'));
const body = /Component\.onCompleted: \{([\s\S]*?)\n                \}\n                onLoaded:/.exec(loader)?.[1];
assert.ok(body, 'the production world Loader initialization must be found');
const initialize = new Function('env', `with (env) { ${body} }`);

function createWorld(androidHost, platformAndroid, active = true) {
    let child;
    const env = {
        mode: 'Theatre', active: false,
        win: { androidHost, immersiveSurfaceOpen: false, worldSourceFor: () => 'TheatreWorld.qml' },
        worldStack: { current: active ? 'Theatre' : 'Biblio' },
        contentPreferences: { showExplicit: false },
        setSource(source, initialProperties) {
            // WorldPage's native platform default is replaced only by supplied
            // initial properties, as it is at the Loader/component boundary.
            child = { androidHost: platformAndroid, ...initialProperties, source };
        },
    };
    initialize(env);
    assert.equal(env.active, true);
    return child;
}

test('desktop phone-shell creates worlds with Android layout immediately', () => {
    const child = createWorld(true, false);
    assert.equal(child.androidHost, true);
    assert.equal(child.lifecycleActive, true);
    assert.equal(child.showExplicitContent, false);
});

test('normal desktop and native Android retain their platform presentation', () => {
    assert.equal(createWorld(false, false).androidHost, false);
    assert.equal(createWorld(true, true).androidHost, true);
});

test('a retained inactive Android world still starts without lifecycle work', () => {
    const child = createWorld(true, false, false);
    assert.equal(child.androidHost, true);
    assert.equal(child.lifecycleActive, false);
});
