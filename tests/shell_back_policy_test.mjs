import fs from 'fs';

const root = process.argv[2] || '.';
const policyPath = root + '/qml/ShellBackPolicy.js';
let src = fs.readFileSync(policyPath, 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule.actionFor=actionFor;')(mod);

function fail(message) {
    console.error('FAIL ' + message);
    process.exit(1);
}
function eq(actual, expected, message) {
    if (actual !== expected)
        fail(message + ' (got ' + JSON.stringify(actual) + ', want ' + JSON.stringify(expected) + ')');
}
function action(state) { return mod.actionFor(state || {}); }

// Highest shell shields consume Escape without mutating the surface underneath.
eq(action({ transitioning: true, playerOpen: true }), 'consume', 'fullscreen transition owns Escape');
eq(action({ bootVisible: true, worldOpen: true }), 'consume', 'boot splash owns Escape');
eq(action({ accountHostVisible: true, worldOpen: true }), 'consume', 'mandatory account flow owns Escape');
// Modal/transient shell surfaces beat sessions and ordinary pages.
eq(action({ identityCeremonyOpen: true, playerOpen: true }), 'cancelIdentityCeremony', 'identity ceremony beats player');
eq(action({ watchPartyJoinOpen: true, playerOpen: true }), 'watchPartyJoin', 'watch-party sheet beats player');
eq(action({ wallpaperActive: true, accountCenterVisible: true }), 'wallpaper', 'wallpaper picker beats account centre');
eq(action({ accountFlyoutVisible: true, taskbarOpen: true }), 'accountFlyout', 'account flyout beats taskbar');
eq(action({ accountCenterVisible: true, taskbarOpen: true }), 'accountCenter', 'account centre beats taskbar');
eq(action({ openRecentOpen: true, taskbarOpen: true }), 'openRecent', 'Open Recent beats taskbar');
eq(action({ taskbarOpen: true, worldOpen: true }), 'taskbar', 'expanded taskbar beats page navigation');

// Session-owned immersive surfaces beat full-page and browsing layers.
eq(action({ playerOpen: true, settingsActive: true }), 'player', 'player owns Escape above settings');
eq(action({ activeSessionKind: 'movie', playerOpen: false }), 'player', 'hidden active movie session is healed through player authority');
eq(action({ bookReaderActive: true, vaultActive: true }), 'bookReader', 'book reader owns Escape above Vault');
eq(action({ activeSessionKind: 'book', bookReaderActive: false }), 'bookReader', 'hidden active book session is healed through reader authority');
eq(action({ vaultComicActive: true, vaultActive: true }), 'comicReader', 'standalone Vault comic owns Escape');
eq(action({ comicReaderActive: true, seriesActive: true }), 'comicReader', 'embedded comic reader owns Escape above its host page');
eq(action({ activeSessionKind: 'comic', comicReaderActive: false }), 'comicReader', 'hidden active comic session is healed through comic authority');
// Full-page peers are ordered by actual same-z document order, then browsing z-order.
eq(action({ updateActive: true, keyboardGuideActive: true }), 'update', 'later update page wins a broken same-z overlap');
eq(action({ keyboardGuideActive: true, settingsActive: true }), 'keyboardGuide', 'keyboard guide wins above Settings when both are active');
eq(action({ settingsActive: true, extensionsActive: true }), 'settings', 'settings wins a broken same-z overlap');
eq(action({ extensionsActive: true, vaultActive: true }), 'extensions', 'extensions wins a broken same-z overlap');
eq(action({ vaultActive: true, downloadsActive: true }), 'vault', 'Vault wins a broken same-z overlap');
eq(action({ downloadsActive: true, bookActive: true }), 'downloads', 'taskbar full-page beats detail page');
eq(action({ bookActive: true, theatreSeriesActive: true }), 'book', 'book detail is highest z53 sibling');
eq(action({ theatreSeriesActive: true, westernActive: true }), 'theatreSeries', 'Theatre detail beats earlier z53 western page');
eq(action({ westernActive: true, seriesActive: true }), 'western', 'western page beats earlier z53 manga page');
eq(action({ universeActive: true, searchActive: true }), 'universe', 'universe z52 beats search z51');
eq(action({ comicIndexActive: true, continueSeeAllActive: true }), 'comicIndex', 'later z49 comic index wins');
eq(action({ theatreGenreActive: true, theatreGenreIndexActive: true }), 'theatreGenre', 'Theatre genre page beats its index');
eq(action({ biblioGenreActive: true, biblioGenreIndexActive: true }), 'biblioGenre', 'Biblio genre page beats its index');
eq(action({ genreActive: true, genreIndexActive: true }), 'genre', 'manga genre page beats its index');

// Bottom of the stack is world -> Home -> quit.
eq(action({ worldOpen: true }), 'world', 'world exits to Home');
eq(action({}), 'quit', 'empty Home stack quits');

console.log('PASS shell back policy matrix');
process.exit(0);


