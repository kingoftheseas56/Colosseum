import assert from 'node:assert/strict';
import fs from 'node:fs';

// Exercise the actual shell dispatch body, not a duplicated navigation rule.
const main = fs.readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8');
const body = main.match(/    function handleEscape\(\) \{([\s\S]*?)\n    \}/);
assert.ok(body, 'Main shell escape dispatcher exists');
const run = new Function('win', 'worldStack', 'ShellBackPolicy', body[1]);
let closed = 0;
let calls = 0;
const vinyl = {
    page: 'song',
    goBack() {
        calls++;
        if (this.page === 'song') this.page = 'album';
        else if (this.page === 'album') this.page = 'library';
        else return false;
        return true;
    }
};
const win = { vinylWorld: vinyl, shellEscapeState: () => ({}), cancelCatalogueRoute() {}, closeWorld: () => closed++ };
const world = { current: 'Vinyl' };
const policy = { actionFor: () => 'world' };
run(win, world, policy);
assert.equal(vinyl.page, 'album'); assert.equal(closed, 0);
run(win, world, policy);
assert.equal(vinyl.page, 'library'); assert.equal(closed, 0);
run(win, world, policy);
assert.equal(closed, 1); assert.equal(calls, 3);
world.current = 'Biblio';
run(win, world, policy);
assert.equal(closed, 2); assert.equal(calls, 3, 'Other worlds retain shell navigation');
world.current = 'Vinyl'; win.vinylWorld = null;
run(win, world, policy);
assert.equal(closed, 3, 'Pending world creation remains safe');
console.log('PASS Vinyl shell Back: Song, Album, Library, other world, pending Loader (5 cases)');
