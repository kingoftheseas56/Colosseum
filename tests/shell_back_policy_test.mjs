import fs from 'fs';
import { fileURLToPath } from 'node:url';

const root = process.argv[2] || fileURLToPath(new URL('../', import.meta.url));
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
eq(action({ stremioSyncOpen: true }), 'stremioSync', 'Stremio Main Sync overlay owns Escape');
eq(action({ stremioSyncOpen: true, syncCenterActive: true }), 'stremioSync', 'Main Sync closes before its underlying Sync Center page');
// Modal/transient shell surfaces beat sessions and ordinary pages.
eq(action({ identityCeremonyOpen: true, playerOpen: true }), 'cancelIdentityCeremony', 'identity ceremony beats player');
eq(action({ identityCeremonyOpen: true, stremioSyncOpen: true }), 'cancelIdentityCeremony', 'front identity ceremony closes before background Main Sync');
eq(action({ watchPartyJoinOpen: true, playerOpen: true }), 'watchPartyJoin', 'watch-party sheet beats player');
eq(action({ wallpaperActive: true, accountCenterVisible: true }), 'wallpaper', 'wallpaper picker beats account centre');
eq(action({ accountFlyoutVisible: true, taskbarOpen: true }), 'accountFlyout', 'account flyout beats taskbar');
eq(action({ accountCenterVisible: true, taskbarOpen: true }), 'accountCenter', 'account centre beats taskbar');
eq(action({ openRecentOpen: true, taskbarOpen: true }), 'openRecent', 'Open Recent beats taskbar');
eq(action({ taskbarOpen: true, worldOpen: true }), 'taskbar', 'expanded taskbar beats page navigation');

// Session-owned immersive surfaces beat full-page and browsing layers.
eq(action({ activeSessionKind: 'movie', playerOpen: false }), 'player', 'hidden active movie session is healed through player authority');
eq(action({ bookReaderActive: true, vaultActive: true }), 'bookReader', 'book reader owns Escape above Vault');
eq(action({ activeSessionKind: 'book', bookReaderActive: false }), 'bookReader', 'hidden active book session is healed through reader authority');
eq(action({ vaultComicActive: true, vaultActive: true }), 'comicReader', 'standalone Vault comic owns Escape');
eq(action({ comicReaderActive: true, seriesActive: true }), 'comicReader', 'embedded comic reader owns Escape above its host page');
eq(action({ activeSessionKind: 'comic', comicReaderActive: false }), 'comicReader', 'hidden active comic session is healed through comic authority');
// Full-page peers are ordered by actual same-z document order, then browsing z-order.
eq(action({ updateActive: true, keyboardGuideActive: true }), 'update', 'later update page wins a broken same-z overlap');
eq(action({ syncCenterActive: true }), 'syncCenter', 'Sync Center closes before returning to its underlying world');
eq(action({ historyStatsActive: true }), 'historyStats', 'History, highlights, and stats closes before returning to its underlying world');
eq(action({ syncCenterActive: true, historyStatsActive: true }), 'historyStats', 'later History page wins above retained Sync Center');
eq(action({ syncCenterActive: true, keyboardGuideActive: true }), 'syncCenter', 'later Sync Center page wins above Keyboard Guide');
eq(action({ updateActive: true, syncCenterActive: true }), 'update', 'later Update page wins above Sync Center');
eq(action({ extensionsActive: true, vaultActive: true }), 'extensions', 'extensions wins a broken same-z overlap');
eq(action({ feriaActive: true, worldOpen: true }), 'feria', 'Feria owns Back above its underlying world');
eq(action({ accountCenterVisible: true, feriaActive: true }), 'accountCenter', 'account centre closes before Feria');
eq(action({ taskbarOpen: true, feriaActive: true }), 'taskbar', 'expanded taskbar closes before Feria');
eq(action({ extensionsActive: true, feriaActive: true }), 'extensions', 'later Extensions layer wins a broken overlap with Feria');
// Ratings & Reviews was retired in 1.1.7 (3928d0d2); its Escape rows went with it.
eq(action({ yourFeriaActive: true, feriaActive: true }), 'yourFeria', 'Your Feria closes before its underlying world');
eq(action({ vaultActive: true }), 'vault', 'Vault owns Escape when open');
eq(action({ vaultActive: true, downloadsActive: true }), 'vault', 'Vault wins a broken same-z overlap');
eq(action({ downloadsActive: true, bookActive: true }), 'downloads', 'taskbar full-page beats detail page');
eq(action({ bookActive: true, theatreSeriesActive: true }), 'book', 'book detail is highest z53 sibling');
eq(action({ theatreSeriesActive: true, westernActive: true }), 'theatreSeries', 'Theatre detail beats earlier z53 western page');
eq(action({ westernActive: true, seriesActive: true }), 'western', 'western page beats earlier z53 manga page');
eq(action({ theatreSeriesActive: true, onePieceArcActive: true, universeActive: true }), 'theatreSeries', 'media detail beats One Piece arc page');
eq(action({ onePieceArcActive: true, universeActive: true }), 'onePieceArc', 'One Piece arc page closes before its globe');
eq(action({ theatreSeriesActive: true, onePieceAtlasTransientOpen: true, universeActive: true }), 'theatreSeries', 'Theatre detail beats atlas transient');
eq(action({ onePieceArcActive: true, onePieceAtlasTransientOpen: true, universeActive: true }), 'onePieceArc', 'One Piece arc page beats atlas transient');
eq(action({ onePieceAtlasTransientOpen: true, universeActive: true }), 'onePieceAtlasTransient', 'atlas transient closes before its globe');
eq(action({ universeActive: true, searchActive: true }), 'universe', 'universe z52 beats search z51');
eq(action({ comicIndexActive: true, continueSeeAllActive: true }), 'comicIndex', 'later z49 comic index wins');
eq(action({ theatreGenreActive: true, theatreGenreIndexActive: true }), 'theatreGenre', 'Theatre genre page beats its index');
eq(action({ biblioGenreActive: true, biblioGenreIndexActive: true }), 'biblioGenre', 'Biblio genre page beats its index');
eq(action({ genreActive: true, genreIndexActive: true }), 'genre', 'manga genre page beats its index');

// Platform-specific root actions and IME-first behavior. The Android-only cases are
// deliberately table-driven so the baseline reports every missing branch in one run.
const platformCases = [
    { name: 'Android Back backgrounds from the world root', state: { androidHost: true, worldOpen: true }, expected: 'backgroundApplication' },
    { name: 'Android Back backgrounds from the empty root', state: { androidHost: true }, expected: 'backgroundApplication' },
    { name: 'desktop world Back still exits to Home', state: { androidHost: false, worldOpen: true }, expected: 'world' },
    { name: 'desktop Home Back still returns to Home top', state: { androidHost: false }, expected: 'homeTop' },

    // Existing modal/player/page/detail precedence must remain when no IME is visible.
    { name: 'Android shell menu beats player, page, and detail', state: { androidHost: true, watchPartyJoinOpen: true, playerOpen: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'watchPartyJoin' },
    { name: 'Android player beats page and detail', state: { androidHost: true, playerOpen: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'player' },
    { name: 'Android hidden movie session beats root background', state: { androidHost: true, activeSessionKind: 'movie', downloadsActive: true }, expected: 'player' },
    { name: 'Android full page beats root background', state: { androidHost: true, updateActive: true, worldOpen: true }, expected: 'update' },
    { name: 'Android full page beats detail', state: { androidHost: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'downloads' },
    { name: 'Android Profile page beats root background', state: { androidHost: true, profileActive: true, worldOpen: true }, expected: 'profile' },
    { name: 'Android detail beats world', state: { androidHost: true, theatreSeriesActive: true, worldOpen: true }, expected: 'theatreSeries' },
    { name: 'desktop shell menu beats player, page, and detail', state: { androidHost: false, watchPartyJoinOpen: true, playerOpen: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'watchPartyJoin' },
    { name: 'desktop player beats page and detail', state: { androidHost: false, playerOpen: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'player' },
    { name: 'desktop full page beats detail', state: { androidHost: false, downloadsActive: true, theatreSeriesActive: true }, expected: 'downloads' },
    { name: 'desktop Profile page remains ahead of world', state: { androidHost: false, profileActive: true, worldOpen: true }, expected: 'profile' },
    { name: 'desktop detail beats world', state: { androidHost: false, theatreSeriesActive: true, worldOpen: true }, expected: 'theatreSeries' },

    // On Android a visible IME consumes Back before modals and navigation; absolute
    // shell shields still own Back. Desktop state must ignore the Android-only rule.
    { name: 'Android IME dismisses before identity modal', state: { androidHost: true, keyboardVisible: true, identityCeremonyOpen: true }, expected: 'dismissKeyboard' },
    { name: 'Android IME dismisses before shell menu and player', state: { androidHost: true, keyboardVisible: true, watchPartyJoinOpen: true, playerOpen: true }, expected: 'dismissKeyboard' },
    { name: 'Android IME dismisses before player', state: { androidHost: true, keyboardVisible: true, playerOpen: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'dismissKeyboard' },
    { name: 'Android IME dismisses before full page', state: { androidHost: true, keyboardVisible: true, downloadsActive: true, theatreSeriesActive: true }, expected: 'dismissKeyboard' },
    { name: 'Android IME dismisses before detail and world', state: { androidHost: true, keyboardVisible: true, theatreSeriesActive: true, worldOpen: true }, expected: 'dismissKeyboard' },
    { name: 'Android IME dismisses before root action', state: { androidHost: true, keyboardVisible: true }, expected: 'dismissKeyboard' },
    { name: 'transition shield stays ahead of Android IME', state: { androidHost: true, keyboardVisible: true, transitioning: true, playerOpen: true }, expected: 'consume' },
    { name: 'boot shield stays ahead of Android IME', state: { androidHost: true, keyboardVisible: true, bootVisible: true }, expected: 'consume' },
    { name: 'mandatory account host stays ahead of Android IME', state: { androidHost: true, keyboardVisible: true, accountHostVisible: true }, expected: 'consume' },
    { name: 'desktop keyboard does not invoke Android IME dismissal', state: { androidHost: false, keyboardVisible: true, worldOpen: true }, expected: 'world' },
    { name: 'desktop Home remains Home top with keyboard visible', state: { androidHost: false, keyboardVisible: true }, expected: 'homeTop' }
];
let platformFailures = 0;
for (const testCase of platformCases) {
    const actual = action(testCase.state);
    if (actual !== testCase.expected) {
        console.error('FAIL ' + testCase.name + ' (got ' + JSON.stringify(actual) + ', want ' + JSON.stringify(testCase.expected) + ')');
        platformFailures++;
    }
}

// Bottom of the stack is world -> Home -> Home top on desktop. Back never quits
// there (world-feel Slice 6); Android's product root action is tested above.
eq(action({ worldOpen: true }), 'world', 'world exits to Home');
eq(action({}), 'homeTop', 'empty Home stack returns to the top of Home, never quits');

if (platformFailures) {
    console.error('FAIL Android/Desktop platform Back matrix: ' + platformFailures + '/' + platformCases.length + ' cases');
    process.exitCode = 1;
} else {
    console.log('PASS shell back policy matrix (' + platformCases.length + ' platform cases)');
}
