import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const main = fs.readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8');
const body = main.match(/function openWorld\(medium\) \{([\s\S]*?)\n    \}/)[1];
const context = { openModes: { count: 0, append() { throw Error('Unexpected loader creation'); } },
    win: { cancelCatalogueRoute() {}, feriaComingSoon: true, vinylEnabled: false, ensureFeriaRuntime() { return false; }, openFeriaPage() { throw Error('Unexpected Feria open'); }, parkFeria() {} },
    worldStack: { current: '' }, currentSurface: 'Home',
    setGuiStallContext() { throw Error('Unexpected navigation'); },
    refreshWallpaper() { throw Error('Unexpected wallpaper change'); } };
vm.createContext(context);
vm.runInContext(`function openWorld(medium) { ${body} }`, context);
for (const name of ['Feria', 'Vinyl', 'feria', 'VINYL', ' Feria ', ' vinyl ']) {
    context.openWorld(name);
    assert.equal(context.worldStack.current, '');
    assert.equal(context.currentSurface, 'Home');
}
assert.match(main, /readonly property bool feriaComingSoon: true/);
assert.match(main, /function ensureFeriaRuntime\(\)/);
assert.match(main, /function openFeriaPage\(\) \{\s*if \(!win\.ensureFeriaRuntime\(\)\) return/);
assert.doesNotMatch(main, /ComingNextPreview|ComingNextSection|comingNextLayer|openComingNextPreview/);
assert.doesNotMatch(main, /text: "Coming Next"/);
const topbar = fs.readFileSync(new URL('../qml/TopBar.qml', import.meta.url), 'utf8');
assert.match(topbar, /Pill \{ label: "Feria"; comingSoon: true \}/);
assert.match(topbar, /readonly property bool qualificationEnabled: typeof VinylLiveEnabled !== "undefined" && VinylLiveEnabled/);
assert.match(topbar, /comingSoon: !qualificationEnabled/);
assert.match(topbar, /text: "SOON"/);
assert.match(topbar, /enabled: !pill.comingSoon/);
console.log('Coming Soon world guard and disabled top tabs PASS');
