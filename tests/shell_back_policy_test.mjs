import fs from 'fs';
import vm from 'node:vm';

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
eq(action({ feedbackActive: true, playerOpen: true, taskbarOpen: true }), 'feedback', 'feedback closes before retained app surfaces');
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

// Bottom of the stack is world -> Home -> Home top. Back never quits (world-feel Slice 6).
eq(action({ worldOpen: true }), 'world', 'world exits to Home');
eq(action({}), 'homeTop', 'empty Home stack returns to the top of Home, never quits');

// Exercise the actual Taskbar handler, Main route functions and shell Back
// dispatcher together. Only the catalogue delivery and native loaders are faked.
const main = fs.readFileSync(root + '/qml/Main.qml', 'utf8').replace(/\r\n/g, '\n');
const taskbarSource = fs.readFileSync(root + '/qml/Taskbar.qml', 'utf8');
const pickerHandler = /^        onPickerPickRequested: \(medium, entry, returnState\) => \{([\s\S]*?)^        \}/m.exec(main);
if (!pickerHandler) fail('Taskbar picker signal must feed Main with the return state');
if (!/bar\.open = false\s+bar\.pickerPickRequested\(medium, entry, returnState\)/.test(taskbarSource))
    fail('Taskbar closes before handing the captured picker state to Main');

function dockRouteFixture() {
    const restored = [];
    const deferred = [];
    const makeLayer = (dockPickAction, dockPickFields) => ({
        active: false, item: null, status: 1, dockPickAction, dockPickFields, dockPickReturn: null,
    });
    const ctx = vm.createContext({
        Loader: { Error: 3 },
        worldStack: { current: 'Theatre' },
        contentPreferences: { showExplicit: false },
        taskbar: { visible: true, restorePicker: state => { restored.push(state); return true; } },
        seriesLayer: makeLayer('series', ['title', 'malId', 'seriesIdOverride', 'resumeSeriesId']),
        comicSeriesLayer: makeLayer('comicSeries', ['locgSid', 'title', 'cover', 'locgMeta']),
        theatreSeriesLayer: makeLayer('theatreSeries', ['pendingItem']),
        bookLayer: makeLayer('book', ['book']),
        universeLayer: Object.assign(makeLayer('universe', ['extensionId', 'universeName', 'templatePage', 'plain']),
                                     { templateSources: { shelves: 'ShelvesUniversePage.qml' } }),
        bookReaderLayer: makeLayer('bookReader', []),
        westernLayer: makeLayer('western', []),
        shellInputMethod: { visible: false },
        androidHost: false,
        namedCollectionsProfileGeneration: 0,
        bookRouteGeneration: 0,
        _catalogueRouteGeneration: 0,
        _catalogueRouteCancel: null,
        ShellBackPolicy: {actionFor: action},
        guardNamedCollectionsNavigation: () => false,
        restoreNamedCollectionsReturn: () => false,
        applySeriesEditionProfile(_profile, id) { ctx.seriesLayer.malId = id; },
        closeOnePieceArc() {},
        _captureBookReturn: () => null,
        setGuiStallContext() {},
        teardownSession() { ctx.bookReaderLayer.active = false; },
        requestPlayerEscape() { ctx.playerOpen = false; },
        goHomeTop() {},
        CatalogDelivery: {
            wait(_catalog, _read, done) {
                let canceled = false;
                deferred.push(() => { if (!canceled) done(); });
                return () => { canceled = true; };
            },
        },
        Sessions: {
            activeId: '',
            openOrSwitch() { this.activeId = 'audio'; ctx.bookReaderLayer.active = true; return this.activeId; },
            get(id) { return id === this.activeId && id ? { id, contentKind: 'book' } : {}; },
            close() { this.activeId = ''; },
        },
    });
    ctx.win = ctx;
    ctx.shellEscapeState = () => ({
        seriesActive: ctx.seriesLayer.active,
        comicSeriesActive: ctx.comicSeriesLayer.active,
        theatreSeriesActive: ctx.theatreSeriesLayer.active,
        bookActive: ctx.bookLayer.active,
        universeActive: ctx.universeLayer.active,
        bookReaderActive: ctx.bookReaderLayer.active,
        playerOpen: ctx.playerOpen,
    });
    ctx.catalogueRouteSnapshot = () => JSON.stringify(ctx.shellEscapeState());
    for (const name of [
        'openDockPick', 'handleEscape', 'openSeries', 'closeSeries', 'openComicSeries',
        '_openComicSeriesReady', 'closeComicSeries', 'openTheatreSeries', 'closeTheatreSeries',
        'openBook', 'closeBook', 'openRosterUniverse', 'openArchivedUniverse', 'openTribute', 'openUniverse', 'closeUniverse',
        'requestUniverseEscape', 'routeDownloadedAudiobook', 'openBookSession', 'closeBookReaderSession',
        'requestBookReaderEscape', 'closeSession', 'cancelCatalogueRoute', 'returnDockComicDetail',
        'backFromMangaReader', 'closeComicReader', 'snapshotDockPickDetail', 'prepareDockPickDetail',
        'attachDockPickDetail', 'takeDockPickDetail', 'restoreDockPickDetail', 'canReturnToDockPick',
        'finishDockPickDetail',
    ]) {
        const fn = new RegExp(`^    function ${name}\\([^\\n]*\\)\\s*\\{[\\s\\S]*?^    \\}`, 'm').exec(main);
        if (!fn) fail('Missing Main route function ' + name);
        vm.runInContext(fn[0], ctx);
    }
    vm.runInContext(`function launchFromPicker(medium, entry, returnState) { ${pickerHandler[1]} }`, ctx);
    return { ctx, restored, deferred };
}

for (const [medium, rootClose] of [
    ['anime', 'closeTheatreSeries'], ['tv', 'closeTheatreSeries'], ['movies', 'closeTheatreSeries'],
    ['manga', 'closeSeries'], ['comics', 'closeComicSeries'], ['manhwa', 'closeSeries'],
    ['books', 'closeBook'], ['audiobooks', 'closeBookReaderSession'], ['universes', 'closeUniverse'], ['tributes', 'closeUniverse'],
]) {
    const signal = rootClose === 'closeBookReaderSession' ? 'closed' : 'backRequested';
    if (!main.includes(`item.${signal}.connect(win.${rootClose})`)) fail(medium + ' Page Back has no route-owner binding');
    const { ctx, restored, deferred } = dockRouteFixture();
    const entry = { id: 'demo', title: 'Demo', name: 'Demo', page: 'shelves', bookPath: 'test.epub', extensionId: 'tribute.demo' };
    const state = { medium, key: 'demo', slide: 2, column: 0 };
    ctx.launchFromPicker(medium, entry, state);
    if (medium === 'tributes') {
        eq(ctx.universeLayer.extensionId, entry.extensionId, 'Tribute opens by its installed ID');
        eq(ctx.universeLayer.templatePage, '', 'Tribute bypasses the standard Universe template');
        eq(ctx.universeLayer.plain, false, 'Tribute selects its bespoke page');
    }
    deferred.splice(0).forEach(deliver => deliver());
    ctx.handleEscape();
    eq(restored.length, 1, medium + ' shell Back restores the picker once');
    eq(restored[0], state, medium + ' restores the exact picker snapshot');
    ctx.handleEscape();
    eq(restored.length, 1, medium + ' later ordinary Back cannot resurrect the picker');
}

{
    const { ctx, restored } = dockRouteFixture();
    const prior = { id: 'tt1', title: 'Previous title' };
    ctx.openTheatreSeries(prior);
    const pickerState = { medium: 'tv', key: 'tt2' };
    ctx.launchFromPicker('tv', { id: 'tt2', title: 'Picker title' }, pickerState);
    ctx.playerOpen = true;
    ctx.handleEscape();
    eq(ctx.playerOpen, false, 'The nested player session handles Back on its own');
    eq(restored.length, 0, 'The nested player Back cannot consume the picker return');
    ctx.theatreSeriesLayer.item = { requestEscape: () => true };
    ctx.handleEscape();
    eq(restored.length, 0, 'Theatre transient consumes Back before the picker return');
    ctx.theatreSeriesLayer.item.requestEscape = () => false;
    ctx.closeTheatreSeries();
    eq(ctx.theatreSeriesLayer.pendingItem, prior, 'Page Back restores the invoker title on a reused loader');
    eq(restored[0], pickerState, 'Page Back returns to the picker on the invoker title');
}

{
    const { ctx, restored, deferred } = dockRouteFixture();
    ctx.guardNamedCollectionsNavigation = () => true;
    ctx.launchFromPicker('tv', { id: 'tt1', title: 'Canceled' }, {});
    eq(ctx.theatreSeriesLayer.active, false, 'A canceled guarded open leaves no picker route');
    ctx.guardNamedCollectionsNavigation = () => false;
    ctx.launchFromPicker('comics', { id: '1', title: 'Pending' }, {});
    ctx.cancelCatalogueRoute();
    deferred[0]();
    eq(ctx.comicSeriesLayer.active, false, 'A canceled catalogue delivery cannot arm a picker return');
    ctx.theatreSeriesLayer.status = ctx.Loader.Error;
    ctx.launchFromPicker('tv', { id: 'tt1', title: 'Failed detail' }, {});
    ctx.closeTheatreSeries();
    eq(restored.length, 0, 'A failed loader has no picker return');
    ctx.theatreSeriesLayer.status = 1;
    ctx.launchFromPicker('tv', { id: 'tt2', title: 'Picker title' }, {});
    ctx.openTheatreSeries({ id: 'tt3', title: 'Ordinary title' });
    ctx.closeTheatreSeries();
    eq(restored.length, 0, 'An ordinary route replacement disarms the old picker return');
}

{
    const { ctx, restored } = dockRouteFixture();
    const pickerState = { medium: 'manga', key: '123' };
    ctx.launchFromPicker('manga', { id: '123', title: 'Series' }, pickerState);
    ctx.seriesLayer.item = { openChapterId: 'chapter-1' };
    ctx.backFromMangaReader();
    eq(restored.length, 0, 'Manga reader Back returns to its series before the picker');
    eq(ctx.seriesLayer.active, true, 'The series owner is still active');
    ctx.closeSeries();
    eq(restored[0], pickerState, 'The next root Back restores the picker');
}

console.log('PASS shell back policy matrix');
process.exit(0);
