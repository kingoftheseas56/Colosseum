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

if (failures) { console.log('\nFAIL — ' + failures + ' check(s) failed'); process.exit(1); }
console.log('\nPASS — hosted player contract holds');
