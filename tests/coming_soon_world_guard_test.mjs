import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const main = fs.readFileSync(new URL('../qml/Main.qml', import.meta.url), 'utf8');
const body = main.match(/function openWorld\(medium\) \{([\s\S]*?)\n    \}/)[1];
const context = { openModes: { count: 0, append() { throw Error('Unexpected loader creation'); } },
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
assert.doesNotMatch(main, /ComingNextPreview|ComingNextSection|comingNextLayer|openComingNextPreview/);
const cards = main.slice(main.indexOf('text: "Coming Next"'), main.indexOf('// ---- 4. MODE-INTRO'));
assert.match(cards, /model: \["Feria", "Vinyl"\]/);
assert.match(cards, /text: "Coming Soon"/);
assert.doesNotMatch(cards, /KeyboardAction|MouseArea|TapHandler|onTriggered|onClicked|Loader/);
console.log('Coming Soon world guard and non-actionable Home labels PASS');
