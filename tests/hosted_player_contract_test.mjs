// hosted_player_contract_test.mjs — the cross-file contract for the VidKing hosted
// player. It reads the REAL source and pins the seams that keep VidKing honest:
//   - it is a removable, reinstallable, APP-OWNED Theatre extension;
//   - a `hosted-player` extension NEVER enters the Stremio stream ladder (mpv/torrent);
//   - its Sources row carries no quality/seed/download claim and no copy/download action;
//   - its selection is a typed hosted request, never a torrent/url/Stream/Download route;
//   - its player surface is a locked, off-the-record WebEngine page with clipboard off;
//   - Main routes hosted sessions and Continue back to VidKing without touching mpv.
//
// Grows across tasks 2, 4, 6, 7. Each block reads a file and asserts one seam. C++/QML
// seams are text assertions (they can't be VM-loaded); AddonClient is loaded and run.
import fs from 'fs';

let failures = 0;
const ok  = m => console.log('  ok   ' + m);
const bad = m => { console.log('  FAIL ' + m); failures++; };
const check = (c, m) => c ? ok(m) : bad(m);
const read = p => fs.readFileSync(p, 'utf8');
const has  = (src, re, m) => check(re.test(src), m);
const hasnt = (src, re, m) => check(!re.test(src), m);

// ============================ Task 2 — removable, app-owned extension ===========
const storeCpp = read('native/engine/ExtensionsStore.cpp');
const storeH   = read('native/engine/ExtensionsStore.h');
const page     = read('qml/ExtensionsPage.qml');

console.log('ExtensionsStore seeds VidKing as a non-core hosted-player row');
has(storeCpp, /net\.vidking\.player/, 'store references net.vidking.player');
has(storeCpp, /hosted-player/, 'store seeds the hosted-player resource');
has(storeCpp, /add\("net\.vidking\.player",\s*"bundled:vidking",\s*false,/,
    'VidKing is added core:false with the bundled: transport (never a remote manifest)');

console.log('ExtensionsStore exposes a bundled reinstall entry point');
has(storeH, /Q_INVOKABLE\s+void\s+installBundled\s*\(\s*const\s+QString\s*&/,
    'ExtensionsStore.h declares installBundled(const QString&)');
has(storeCpp, /installBundled/, 'ExtensionsStore.cpp implements installBundled');
has(storeCpp, /Unknown bundled extension\./,
    'installBundled rejects unknown ids with an honest failure');

console.log('ExtensionsPage routes bundled cards through installBundled, not install(url)');
has(page, /bundled\s*===\s*true/, 'installFromCard branches on item.bundled === true');
has(page, /Extensions\.installBundled\(\s*item\.id\s*\)/,
    'a bundled card installs by id, never by fetching a URL');

// ---- AddonClient: hosted-player is recognised but NEVER a stream well ----
const ac = read('qml/AddonClient.js').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', ac +
  '\nmodule.hostedPlayerExtensions=typeof hostedPlayerExtensions==="function"?hostedPlayerExtensions:null;' +
  '\nmodule.streamExtensions=streamExtensions;' +
  '\nmodule.parseStream=parseStream;')(mod);

const vidking = { id: 'net.vidking.player', enabled: true,
  manifest: { id: 'net.vidking.player', resources: ['hosted-player'],
              types: ['movie', 'series'], idPrefixes: ['tt'] } };

console.log('AddonClient exposes hostedPlayerExtensions and keeps it out of the stream ladder');
check(typeof mod.hostedPlayerExtensions === 'function', 'hostedPlayerExtensions is defined');
if (mod.hostedPlayerExtensions) {
  check(mod.hostedPlayerExtensions([vidking], 'movie', 'tt1375666').length === 1,
        'an enabled VidKing matches a movie hosted-player ask');
  check(mod.hostedPlayerExtensions([vidking], 'series', 'tt0903747:2:3').length === 1,
        'VidKing matches a series episode ask');
  check(mod.hostedPlayerExtensions([{ ...vidking, enabled: false }], 'movie', 'tt1375666').length === 0,
        'a disabled VidKing matches nothing');
  check(mod.hostedPlayerExtensions(null, 'movie', 'tt1').length === 0,
        'null list -> [], never throws');
}
check(mod.streamExtensions([vidking], 'movie', 'tt1375666').length === 0,
      'VidKing is NEVER a stream well — it can never enter loadStreams / mpv');

// ============================ Task 4 — hosted rows in the Sources sheet =========
const sheetSrc = read('qml/SourcesSheet.qml');
const seriesSrc = read('qml/TheatreSeries.qml');

console.log('SourcesSheet builds hosted rows independently, ahead of stream rows');
has(sheetSrc, /import "HostedPlayerApi\.js"/, 'SourcesSheet imports HostedPlayerApi');
has(sheetSrc, /property var hostedRows/, 'SourcesSheet has a separate hostedRows property');
has(sheetSrc, /HostedPlayerApi\.rowsFor\(/, 'SourcesSheet builds trusted provider rows synchronously');
has(sheetSrc, /mode === "play"[\s\S]{0,80}HostedPlayerApi\.rowsFor/,
    'hosted rows are built only in play mode');
has(sheetSrc, /visibleRows[\s\S]{0,120}hostedRows[\s\S]{0,60}filteredRows\(\)/,
    'visibleRows = hosted rows followed by filtered stream rows');
has(sheetSrc, /signal hostedPlayerRequested\(/, 'SourcesSheet emits hostedPlayerRequested');
has(sheetSrc, /kind === "hostedPlayer"/, 'the delegate recognises a hosted row');

console.log('a hosted row shows no torrent claims and no copy/download/prefetch');
has(sheetSrc, /HOSTED PLAYER/, 'hosted quality line reads HOSTED PLAYER');
has(sheetSrc, /availability checked when opened/, 'hosted detail states availability is checked on open');
has(sheetSrc, /id: copyBtn[\s\S]{0,220}!row\.isHosted/, 'copy action hidden for hosted rows');
has(sheetSrc, /id: dlBtn[\s\S]{0,240}!row\.isHosted/, 'download action hidden for hosted rows');
has(sheetSrc, /function warmTopRow\(\)[\s\S]{0,400}filteredRows\(\)/,
    'warmTopRow prefetches only stream rows (filteredRows), never hosted rows');

console.log('TheatreSeries forwards a typed hosted request, never a torrent/url route');
has(seriesSrc, /signal hostedPlayerRequested\(var request\)/,
    'TheatreSeries exposes hostedPlayerRequested(var request)');
has(seriesSrc, /onHostedPlayerRequested/, 'TheatreSeries handles the sheet hosted selection');
has(seriesSrc, /"providerId": row\.providerId/, 'the request carries the providerId');
has(seriesSrc, /"tmdbId": context\.tmdbId/, 'the request carries the tmdbId');
has(seriesSrc, /page\.hostedPlayerRequested\(request\)/,
    'the typed request is emitted upward, not through playRequested');

// ============================ Task 6 — the restricted player surface ============
const pageSrc = read('qml/HostedPlayerPage.qml');

console.log('HostedPlayerPage is a locked, off-the-record WebEngine surface');
has(pageSrc, /import QtWebEngine/, 'imports QtWebEngine');
has(pageSrc, /import QtWebChannel/, 'imports QtWebChannel');
has(pageSrc, /WebEngineProfile\s*\{/, 'declares a dedicated WebEngineProfile');
has(pageSrc, /offTheRecord:\s*true/, 'the profile is off-the-record');
has(pageSrc, /NoPersistentCookies/, 'the profile keeps no persistent cookies');
has(pageSrc, /registerObject\(\s*["']hostedPlayerBridge["']\s*,\s*HostedPlayerBridge\s*\)/,
    'registers ONLY the least-privilege HostedPlayerBridge on the channel');
has(pageSrc, /qrc:\/hostedplayer\/host\.html/, 'loads only the local wrapper page');

console.log('the surface refuses popups, navigation, downloads, permissions, clipboard');
has(pageSrc, /onNewWindowRequested/, 'rejects new-window/popups');
has(pageSrc, /onNavigationRequested/, 'gates top-level navigation');
has(pageSrc, /onDownloadRequested/, 'rejects downloads');
has(pageSrc, /\.cancel\(\)/, 'a download request is cancelled');
has(pageSrc, /onPermissionRequested/, 'rejects permission requests');
has(pageSrc, /\.deny\(\)/, 'a permission request is denied');
has(pageSrc, /javascriptCanAccessClipboard:\s*false/, 'clipboard read is pinned off');
has(pageSrc, /javascriptCanPaste:\s*false/, 'clipboard paste is pinned off');

console.log('the surface exposes the six lifecycle methods and writes Progress honestly');
for (const fn of ['open', 'captureState', 'restoreState', 'suspendForMinimize', 'resumeFromMinimize', 'stop'])
  has(pageSrc, new RegExp('function\\s+' + fn + '\\s*\\('), `exposes ${fn}()`);
has(pageSrc, /HostedPlayerApi\.embedUrl\(/, 'validates + builds the embed URL through the trusted registry');
has(pageSrc, /Progress\.recordSilent\(/, 'writes the 5s heartbeat silently');
has(pageSrc, /Progress\.record\(/, 'writes lifecycle progress with a notify');
has(pageSrc, /"hostedPlayerId":/, 'the resume payload marks the hosted provider');
has(pageSrc, /signal backRequested\(\)/, 'exposes backRequested');
hasnt(pageSrc, /MpvItem|Colosseum\.Player/, 'never instantiates the mpv player');

if (failures) { console.log('\nFAIL — ' + failures + ' check(s) failed'); process.exit(1); }
console.log('\nPASS — hosted player contract holds');
