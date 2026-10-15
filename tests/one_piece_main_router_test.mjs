import fs from 'node:fs';
import { execFileSync } from 'node:child_process';

let failed = 0;
const check = (condition, message) => {
    console.log(`${condition ? '  ok  ' : '  FAIL'} ${message}`);
    if (!condition) failed++;
};

const main = fs.readFileSync('qml/Main.qml', 'utf8');
const policy = fs.readFileSync('qml/ShellBackPolicy.js', 'utf8');
const tracked = file => {
    try { execFileSync('git', ['ls-files', '--error-unmatch', file], { stdio: 'ignore' }); return true; }
    catch (_) { return false; }
};

console.log('One Piece Main globe/arc router contract');
for (const needle of [
    'function openOnePieceArc(arc)',
    'function closeOnePieceArc()',
    'function routeUniverseSeries(e)',
    'id: onePieceArcLayer',
    'OnePieceArcPage {',
    'arc: onePieceArcLayer.arcData',
    'extensionId: universeLayer.extensionId',
    'installedExtensions: win.installedExtensions',
    'reducedMotion: win.reducedMotion'
]) check(main.includes(needle), `Main contains ${needle}`);

check(main.includes('z: 52.5'), 'arc layer sits above universe and below z53 media details');
check(main.includes('onePieceArcActive: onePieceArcLayer.active'), 'shell state exposes arc layer');
check(main.includes('onePieceAtlasTransientOpen:'), 'shell state exposes atlas transient only through the One Piece universe');
check(main.includes('case "onePieceArc": win.closeOnePieceArc(); return'), 'Escape closes arc before universe');
check(main.includes('case "onePieceAtlasTransient": win.requestOnePieceAtlasEscape(); return'), 'Escape routes atlas transient through the shell');
check(main.includes('function requestOnePieceAtlasEscape()'), 'Main owns a fail-closed atlas Escape seam');
check(tracked('qml/OnePieceArcPage.qml'), 'clean checkout tracks the One Piece arc page');
check(tracked('qml/OnePieceArcCatalogue.qml'), 'clean checkout tracks the arc catalogue dependency');
check(tracked('tests/one_piece_arc_page_test.mjs'), 'clean checkout tracks the arc page contract test');

for (const needle of [
    'item.arcRequested.connect(win.openOnePieceArc)',
    'item.mediaRequested.connect(win.openTheatreSeries)',
    'item.watchRequested.connect(win.openTheatreSeries)',
    'item.seriesRequested.connect(win.routeUniverseSeries)',
    'item.onePaceRequested.connect(win.openOnePaceArc)'
]) check(main.includes(needle), `Main routes ${needle}`);

check(main.includes('if (universeLayer.extensionId === "com.colosseum.universe.onepiece") {'),
      'One Piece universe has a dedicated signal branch');
check(main.includes('function closeUniverse() {') && main.includes('win.closeOnePieceArc()'),
      'closing the globe clears any arc layer');
check(policy.includes('if (on(s.onePieceArcActive)) return "onePieceArc"'),
      'ShellBackPolicy owns arc-before-universe precedence');

const onePieceBranch = main.indexOf('if (universeLayer.extensionId === "com.colosseum.universe.onepiece") {');
const dcauBranch = main.indexOf('if (universeLayer.extensionId === "com.colosseum.universe.dcau") {', onePieceBranch + 1);
check(onePieceBranch >= 0 && dcauBranch > onePieceBranch,
      'One Piece bespoke branch is isolated before legacy universe signal wiring');

console.log(failed ? `\n${failed} FAILED` : '\nall green');
process.exit(failed ? 1 : 0);
