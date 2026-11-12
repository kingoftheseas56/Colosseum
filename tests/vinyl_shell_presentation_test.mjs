import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const main = new TextDecoder('utf-8', {fatal: true}).decode(fs.readFileSync(new URL('../qml/Main.qml', import.meta.url)));
const predicate = main.match(/    readonly property bool vinylSongPageOpen: ([^\r\n]*(?:\r?\n {8}&&[^\r\n]*)*)/);
assert.ok(predicate, 'extract the production foreground-song binding');
const songBinding = new vm.Script('Boolean(' + predicate[1] + ')');
const taskbarMatch = main.match(/    Taskbar \{\r?\n        id: taskbar[\s\S]*/);
assert.ok(taskbarMatch, 'find the production shell taskbar');
const taskbar = taskbarMatch[0];
const visibility = taskbar.match(/\r?\n {8}visible: ([\s\S]*?)\r?\n {8}enabled: visible/);
assert.ok(visibility, 'extract the production Taskbar visibility binding');
const taskbarBinding = new vm.Script('Boolean(' + visibility[1] + ')');

function fixture() {
    const win = {
        vinylWorld: {page: 'song', visible: true, canEnterVinyl: true, searchOpen: false, artistSearchOpen: false},
        fullWindowPageOpen: false, immersiveSurfaceOpen: false, vinylVideoFullscreen: false, androidHost: false
    };
    const context = {win, worldStack: {current: 'Vinyl'}, feriaLayer: {open: false, item: null}};
    for (const name of ['downloadsLayer', 'extensionsLayer', 'updateLayer', 'historyStatsLayer', 'syncCenterLayer', 'yourFeriaLayer'])
        context[name] = {active: false};
    Object.defineProperty(win, 'vinylSongPageOpen', {get: () => songBinding.runInNewContext(context)});
    return {context, win, songOpen: () => win.vinylSongPageOpen, dockVisible: () => taskbarBinding.runInNewContext(context)};
}

{
    const f = fixture();
    assert.equal(f.songOpen(), true, 'foreground admitted Vinyl song hides its dock');
    assert.equal(f.dockVisible(), false);
    f.win.vinylWorld.page = 'album';
    assert.equal(f.dockVisible(), true, 'leaving song restores shell navigation');
    f.win.vinylWorld.page = 'song';
    assert.equal(f.dockVisible(), false, 'returning to song hides the dock again');
}
for (const page of ['home', 'album', 'artist', 'mix', 'world', 'library', 'playlist', '']) {
    const f = fixture(); f.win.vinylWorld.page = page;
    assert.equal(f.songOpen(), false, page + ' is not the song view');
    assert.equal(f.dockVisible(), true);
}
for (const world of ['', 'Tankoban', 'Biblio', 'Theatre', 'Universes', 'Vault', 'Feria']) {
    const f = fixture(); f.context.worldStack.current = world;
    assert.equal(f.songOpen(), false, world + ' must not inherit Vinyl song presentation');
    assert.equal(f.dockVisible(), true);
}
for (const world of [null, {page: 'song', visible: false, canEnterVinyl: true},
    {page: 'song', visible: true, canEnterVinyl: false}, {page: 'song', visible: true}]) {
    const f = fixture(); f.win.vinylWorld = world;
    assert.equal(f.songOpen(), false, 'absent, invisible or unavailable Vinyl cannot hide the dock');
    assert.equal(f.dockVisible(), true);
}
for (const field of ['searchOpen', 'artistSearchOpen']) {
    const f = fixture(); f.win.vinylWorld[field] = true;
    assert.equal(f.songOpen(), false, field + ' keeps navigation available');
    assert.equal(f.dockVisible(), true);
    f.win.vinylWorld[field] = false;
    assert.equal(f.songOpen(), true);
}
// Each full-page guard is evaluated with the underlying song still selected.
const fullPages = [
    ['fullWindowPageOpen', f => f.win, 'fullWindowPageOpen'],
    ...['downloadsLayer', 'extensionsLayer', 'updateLayer', 'historyStatsLayer', 'syncCenterLayer', 'yourFeriaLayer']
        .map(name => [name, f => f.context[name], 'active']),
    ['feriaLayer', f => f.context.feriaLayer, 'open']
];
for (const [name, owner, field] of fullPages) {
    const f = fixture(); owner(f)[field] = true;
    assert.equal(f.win.vinylWorld.page, 'song');
    assert.equal(f.songOpen(), false, name + ' restores shell presentation above retained song state');
    assert.equal(f.dockVisible(), true, name + ' can be navigated away from');
    owner(f)[field] = false;
    assert.equal(f.songOpen(), true, 'closing ' + name + ' returns to foreground song');
    assert.equal(f.dockVisible(), false);
}
for (const flag of ['immersiveSurfaceOpen', 'vinylVideoFullscreen', 'androidHost']) {
    const f = fixture(); f.win.vinylWorld.page = 'album'; f.win[flag] = true;
    assert.equal(f.songOpen(), false);
    assert.equal(f.dockVisible(), false, flag + ' retains its pre-existing policy');
    assert.match(visibility[1], new RegExp('!win\\.' + flag));
}
{
    const f = fixture(); f.win.vinylWorld.page = 'album';
    f.context.feriaLayer = {open: true, item: {hostFullScreenActive: true}};
    assert.equal(f.dockVisible(), false, 'Feria host fullscreen retains its pre-existing policy');
    f.context.feriaLayer.item.hostFullScreenActive = false;
    assert.equal(f.dockVisible(), true);
}
assert.match(visibility[1], /!win\.vinylSongPageOpen/);
assert.match(taskbar, /onVisibleChanged: if \(!visible\) open = false/);
const immersive = main.match(/readonly property bool immersiveSurfaceOpen:([\s\S]*?)\r?\n\r?\n/);
assert.ok(immersive);
assert.doesNotMatch(immersive[1], /vinylSongPageOpen/, 'song presentation must not suppress the Vinyl root');
assert.match(main, /item\.shellDockRight = Qt\.binding\(function\(\) \{\s*return taskbar\.visible \? taskbar\.leftEdge \+ taskbar\.closedWidth : 0/);
assert.match(main, /item\.shellDockBottomExtent = Qt\.binding\(function\(\) \{\s*return taskbar\.visible \? taskbar\.bottomGap \+ taskbar\.closedSize : 0/);
console.log('PASS Vinyl foreground-song dock policy, other pages/worlds, admission/search gates, all full-page guards and return paths, legacy fullscreen/Android guards, existing dock insets');
