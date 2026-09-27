// node web/developer-colosseum/dev/parity/parity.mjs '<route json>' <label>
// Runs two isolated copies of the same compiled app against one frozen real-library seed.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import net from 'node:net';
import crypto from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { connectColosseum, evaluate } from '../e2e/cdp.mjs';
import { compare, normal } from './compare.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../../..');
const OUT = path.join(HERE, 'out');
const sizes = [[1920, 1080], [1280, 720]];
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const die = message => { throw new Error(message); };
const route = JSON.parse(process.argv[2] || die('usage: parity.mjs \'<route json>\' <label> [--seed dir] [--qml-click objectName]'));
const nativeStaying = /^(?:PlayerPage|Player2Shell|ReaderShell|VaultPage|player|player2|reader|vault)$/i;
if ([route.name, route.world, route.page, route.owner].some(value => nativeStaying.test(String(value || '')))) {
  console.error('OUT_OF_SCOPE: native-staying Player, reader, and Vault pages have no Arc 54 web parity route');
  process.exit(2);
}
if ([route.name, route.world, route.page].some(value => /^(?:Connections|Update)$/i.test(String(value || '')))) {
  console.error('OUT_OF_SCOPE: Connections and Update no longer have reference QML in this build');
  process.exit(2);
}
const label = process.argv[3] || die('label required');
if (!/^[a-z0-9][a-z0-9_-]{0,63}$/i.test(label)) die('label must be a simple directory name');
const opts = { clicks: [], labels: [], scrolls: [], findLabels: [] };
for (let i = 4; i < process.argv.length; i += 2) {
  const key = process.argv[i], value = process.argv[i + 1];
  if (!value) die(`missing value for ${key}`);
  if (key === '--qml-click') opts.clicks.push(value);
  else if (key === '--qml-label') opts.labels.push(value);
  else if (key === '--qml-find-label') opts.findLabels.push(value);
  else if (key === '--qml-scroll') opts.scrolls.push(value);
  else if (['--seed', '--exe', '--lanista', '--allow', '--journey', '--inject-word', '--hide-control'].includes(key))
    opts[key.slice(2)] = value;
  else die(`unknown option ${key}`);
}
if (!opts.clicks.length && !opts.labels.length && !opts.findLabels.length) {
  if (route.name === 'search' && route.scope === 'Theatre') opts.labels.push('Search');
  if (route.name === 'seeAll' && route.world === 'Theatre' && route.title)
    opts.findLabels.push(`See all ${route.title}`);
}
const exe = path.resolve(opts.exe || path.join(REPO, 'native/build-msvc/colosseum.exe'));
const lanista = path.resolve(opts.lanista || path.join(REPO, 'native/build-msvc/lanista.exe'));
const seedSource = path.resolve(opts.seed || path.join(process.env.APPDATA || '', 'Brotherhood/Colosseum'));
const allowPath = path.resolve(opts.allow || path.join(HERE, 'pages', label, 'allow.json'));
for (const file of [exe, lanista, allowPath]) if (!fs.existsSync(file)) die(`required file missing: ${file}`);
if (!fs.statSync(seedSource).isDirectory()) die(`seed is not a directory: ${seedSource}`);
const allow = JSON.parse(fs.readFileSync(allowPath, 'utf8'));
const runDir = path.join(OUT, label);
fs.mkdirSync(runDir, { recursive: true });
fs.rmSync(path.join(runDir, 'error.txt'), { force: true });
const statusPath = path.join(runDir, 'run-status.json');
fs.writeFileSync(statusPath, JSON.stringify({ status: 'running', route, startedAt: new Date().toISOString() }, null, 2));

function copyRealSeed(from, to) {
  // Library metadata and catalogue indexes, without downloaded media or caches.
  // One frozen copy is then cloned into the QML and web tagged roots.
  const roots = new Set(['profiles', 'profile-session', 'catalog', 'videos', 'vault',
    'extensions', 'anime-order', 'ratings-reviews']);
  const omitted = new Set(['.mp4', '.mkv', '.avi', '.webm', '.mp3', '.flac', '.epub',
    '.cbz', '.pdf', '.jpg', '.jpeg', '.png', '.webp', '.gif', '.log']);
  let count = 0, bytes = 0;
  const hash = crypto.createHash('sha256');
  function visit(src, rel) {
    for (const entry of fs.readdirSync(src, { withFileTypes: true })
      .sort((a, b) => a.name.localeCompare(b.name))) {
      const next = path.join(rel, entry.name);
      const top = next.split(path.sep)[0];
      if (!roots.has(top) || entry.isSymbolicLink()) continue;
      const file = path.join(src, entry.name);
      if (entry.isDirectory()) { visit(file, next); continue; }
      if (!entry.isFile() || omitted.has(path.extname(entry.name).toLowerCase())) continue;
      const stat = fs.statSync(file);
      if (stat.size > 512 * 1024 * 1024) die(`seed file too large: ${next}`);
      const dest = path.join(to, next);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.copyFileSync(file, dest);
      hash.update(next); hash.update(fs.readFileSync(dest));
      count++; bytes += stat.size;
    }
  }
  fs.mkdirSync(to, { recursive: true });
  visit(from, '');
  if (!count) die('real-library seed is empty');
  return { files: count, bytes, sha256: hash.digest('hex') };
}

const seedDir = path.join(runDir, 'seed');
// The run directory is private and ignored. A label may be reused, but never mix
// yesterday's seed files with today's copy.
if (fs.existsSync(seedDir)) {
  const resolved = path.resolve(seedDir), base = path.resolve(OUT) + path.sep;
  if (!resolved.startsWith(base)) die('seed cleanup escaped parity/out');
  fs.rmSync(resolved, { recursive: true, force: true });
}
const seed = copyRealSeed(seedSource, seedDir);
fs.writeFileSync(path.join(runDir, 'seed-manifest.json'), JSON.stringify(seed, null, 2));

async function freePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => server.once('error', reject).listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  await new Promise(resolve => server.close(resolve));
  return port;
}

function lanistaCall(pipe, command, args = [], grab = false, timeoutMs = 30000) {
  const argv = ['--pipe', pipe, '--timeout', String(timeoutMs), command, ...args];
  if (grab) argv.push('--grab', 'window');
  const result = spawnSync(lanista, argv, { cwd: REPO, encoding: 'utf8', timeout: 40000,
    maxBuffer: 4 * 1024 * 1024,
    env: { ...process.env, PATH: `${runtimeBins().join(path.delimiter)}${path.delimiter}${process.env.PATH || ''}` } });
  if (result.error) throw result.error;
  let reply;
  try { reply = JSON.parse(result.stdout); }
  catch { die(`Lanista ${command} returned invalid JSON (exit ${result.status}): ${result.stderr || result.stdout}`); }
  if (result.status !== 0 || reply.type !== 'reply')
    die(`Lanista ${command}: ${reply.code || result.status}: ${reply.message || result.stderr || ''}`);
  return reply;
}

async function waitFor(fn, description, limitMs = 30000) {
  const until = Date.now() + limitMs;
  let last;
  while (Date.now() < until) {
    try { const value = await fn(); if (value) return value; }
    catch (error) { last = error; }
    await sleep(400);
  }
  die(`${description} timed out${last ? `: ${last.message}` : ''}`);
}

function taggedRoot(tag) {
  return path.join(process.env.APPDATA, 'Brotherhood', `Colosseum-dltest-${tag}`);
}
function runtimeBins() {
  const bins = [];
  const cache = path.join(REPO, 'native/build-msvc/CMakeCache.txt');
  if (fs.existsSync(cache)) {
    const contents = fs.readFileSync(cache, 'utf8');
    for (const key of ['MPVQT_PREFIX', 'LIBMPV_PREFIX', 'LIBTORRENT_ROOT', 'OPENSSL_MSVC_ROOT']) {
      const match = contents.match(new RegExp(`^${key}:[^=]*=(.+)$`, 'm'));
      if (match) bins.push(path.join(match[1].trim(), 'bin'));
    }
    const qt = contents.match(/^Qt6_DIR:[^=]*=(.+)$/m);
    if (qt) bins.push(path.resolve(qt[1].trim(), '../../..', 'bin'));
  }
  // The repo launcher also records the local FFmpeg runtime that libmpv loads.
  const launcher = path.join(REPO, 'launch.bat');
  if (fs.existsSync(launcher)) {
    for (const match of fs.readFileSync(launcher, 'utf8').matchAll(/if exist "([A-Z]:\\[^"%]+\\bin)" set "PATH=/gi))
      bins.push(match[1]);
  }
  if (process.env.COLOSSEUM_PARITY_RUNTIME_PATH)
    bins.push(...process.env.COLOSSEUM_PARITY_RUNTIME_PATH.split(path.delimiter));
  return [...new Set(bins.filter(dir => fs.existsSync(dir)))];
}
async function launchSide(side, tag, port) {
  const root = taggedRoot(tag);
  if (fs.existsSync(root)) die(`tagged root unexpectedly exists: ${root}`);
  fs.cpSync(seedDir, root, { recursive: true });
  const pipe = `ColosseumLanista-${tag}`;
  const bins = runtimeBins();
  if (!bins.length) die('no runtime DLL directories found in this build or launcher');
  const env = { ...process.env, PATH: `${bins.join(path.delimiter)}${path.delimiter}${process.env.PATH || ''}`,
    COLOSSEUM_APPDATA_TAG: tag, COLOSSEUM_LANISTA_PIPE: pipe,
    COLOSSEUM_LANISTA_DRIVE: '1', COLOSSEUM_LANISTA_SELFTEST: '1',
    COLOSSEUM_WEBUI: side === 'web' ? '1' : '0',
    COLOSSEUM_DEV: '1', QT_FORCE_STDERR_LOGGING: '1', QML_DISABLE_DISK_CACHE: '1',
    QT_SCREEN_SCALE_FACTORS: '1', QT_SCALE_FACTOR: '1' };
  const openingWorld = route.world || (route.name === 'search' && route.scope !== 'all' ? route.scope : '');
  if (openingWorld) env.COLOSSEUM_OPEN_WORLD = openingWorld;
  else delete env.COLOSSEUM_OPEN_WORLD;
  if (side === 'web') env.QTWEBENGINE_REMOTE_DEBUGGING = String(port);
  else delete env.QTWEBENGINE_REMOTE_DEBUGGING;
  let child;
  try {
    const log = fs.openSync(path.join(runDir, `${side}.log`), 'w');
    child = spawn(exe, [path.join(REPO, 'qml/Main.qml')],
      { cwd: REPO, env, stdio: ['ignore', log, log], windowsHide: false });
    fs.closeSync(log);
    const ping = await waitFor(() => {
      if (child.exitCode !== null) die(`${side} app exited with ${child.exitCode}`);
      const r = lanistaCall(pipe, 'ping', [], false, 8000);
      return r.pid === child.pid ? r : null;
    }, `${side} tagged app readiness`, 120000);
    const state = lanistaCall(pipe, 'get-state');
    if (!state.appDataRoot?.includes(tag) || !state.cacheRoot?.includes(tag))
      die(`${side} storage isolation proof failed`);
    return { child, pipe, root, pid: ping.pid, appDataRoot: state.appDataRoot };
  } catch (error) {
    await stopSide({ child, root });
    throw error;
  }
}

async function stopSide(session) {
  if (!session) return;
  const { child } = session;
  if (child && child.exitCode === null) {
    child.kill();
    await Promise.race([new Promise(resolve => child.once('exit', resolve)), sleep(8000)]);
    if (child.exitCode === null) child.kill('SIGKILL');
  }
  // The generated tag is unique to this invocation. Never remove any other root.
  const resolved = path.resolve(session.root);
  const parent = path.resolve(process.env.APPDATA, 'Brotherhood') + path.sep;
  if (resolved.startsWith(parent) && path.basename(resolved).startsWith('Colosseum-dltest-parity-')) {
    for (let attempt = 0; attempt < 3; attempt++) {
      try { fs.rmSync(resolved, { recursive: true, force: true }); return; }
      catch (error) {
        if (attempt === 2) console.warn(`tagged storage retained (${error.code}): ${resolved}`);
        else await sleep(800);
      }
    }
  }
}

async function clearBootAndOnboarding(session) {
  await waitFor(() => {
    const splash = lanistaCall(session.pipe, 'ui-query', ['object=bootSplash']);
    return splash.visible === false;
  }, 'QML boot splash to clear', 120000);
  const onboarding = lanistaCall(session.pipe, 'ui-query', ['object=accountHost']);
  if (onboarding.visible) {
    lanistaCall(session.pipe, 'ui-click', ['target=accountWelcomeContinueLocal']);
    await waitFor(() => !lanistaCall(session.pipe, 'ui-query', ['object=accountHost']).visible,
      'local-only onboarding to close');
  }
}

async function navigateQml(session) {
  await clearBootAndOnboarding(session);
  const openingWorld = route.world || (route.name === 'search' && route.scope !== 'all' ? route.scope : '');
  const defaultJourney = route.name === 'home' ? 'arc54-parity-home'
    : openingWorld === 'Theatre' ? 'arc54-parity-theatre-discover' : '';
  const journey = opts.journey || defaultJourney;
  if (!journey) die(`no registered Harness journey for ${JSON.stringify(route)}; pass --journey`);
  const result = spawnSync('python', [path.join(REPO, 'tools/colosseum-harness/run.py'),
    '--root', REPO, 'journey', journey, '--mode', 'attached', '--pipe', session.pipe, '--run', '--json'],
    { cwd: REPO, encoding: 'utf8', timeout: 150000, maxBuffer: 6 * 1024 * 1024,
      env: { ...process.env, PATH: `${runtimeBins().join(path.delimiter)}${path.delimiter}${process.env.PATH || ''}` } });
  let receipt;
  try { receipt = JSON.parse(result.stdout); }
  catch { die(`Harness journey ${journey} gave invalid JSON: ${result.stderr || result.stdout}`); }
  fs.writeFileSync(path.join(runDir, 'qml-journey.json'), JSON.stringify(receipt, null, 2));
  if (result.error || result.status !== 0 || !receipt.ok)
    die(`Harness journey ${journey} failed: ${receipt.error?.message || receipt.data?.stderr || result.stderr || result.error || result.status}`);
  if (route.world === 'Theatre' && route.tab) {
    const selected = () => {
      const bar = lanistaCall(session.pipe, 'parity-snapshot').controls
        .find(control => control.objectName === 'theatreTabBar');
      return bar && normal(bar.label).toLowerCase() === route.tab;
    };
    if (route.tab !== 'discover') {
      for (let attempt = 0; attempt < 3; attempt++) {
        if (selected()) break;
        try { lanistaCall(session.pipe, 'ui-click', [`target=theatreTab_${route.tab}`]); }
        catch (error) { if (!/TIMEOUT/.test(error.message)) throw error; }
        try { await waitFor(selected, `QML Theatre tab ${route.tab}`, 15000); break; }
        catch (error) { if (attempt === 2) throw error; }
      }
    }
    await waitFor(selected, `QML Theatre tab ${route.tab}`);
  }
  for (const spec of opts.scrolls) {
    const match = /^(.*):(-?[0-9]+)$/.exec(spec);
    if (!match) die(`QML scroll must be objectName:dy: ${spec}`);
    lanistaCall(session.pipe, 'ui-scroll', [`target=${match[1]}`, `dy=${match[2]}`]);
    await sleep(600);
  }
  for (const click of opts.clicks) lanistaCall(session.pipe, 'ui-click', [`target=${click}`]);
  for (const spec of opts.labels) {
    const parsed = /^(.*?)(?:#([1-9][0-9]*))?$/.exec(spec);
    const wanted = normal(parsed[1]);
    const candidates = lanistaCall(session.pipe, 'parity-snapshot').controls
      .filter(control => normal(control.label) === wanted);
    const index = parsed[2] ? Number(parsed[2]) - 1 : 0;
    if (!candidates[index] || (!parsed[2] && candidates.length !== 1))
      die(`QML label ${spec} resolved to ${candidates.length} controls`);
    lanistaCall(session.pipe, 'ui-click', [`target=${candidates[index].handle}`]);
  }
  for (const wanted of opts.findLabels) {
    if (!openingWorld) die('--qml-find-label needs a world-scoped route');
    let match;
    for (let attempt = 0; attempt < 20; attempt++) {
      const candidates = lanistaCall(session.pipe, 'parity-snapshot').controls
        .filter(control => normal(control.label) === normal(wanted));
      if (candidates.length > 1) die(`QML label ${wanted} is ambiguous`);
      if (candidates.length === 1) { match = candidates[0]; break; }
      lanistaCall(session.pipe, 'ui-scroll',
        [`target=${openingWorld.toLowerCase()}WorldScroll`, 'dy=-480']);
      await sleep(350);
    }
    if (!match) die(`QML label ${wanted} not visible after bounded scroll`);
    lanistaCall(session.pipe, 'ui-click', [`target=${match.handle}`]);
  }
  if (['seeAll', 'search'].includes(route.name)
    && !opts.clicks.length && !opts.labels.length && !opts.findLabels.length)
    die(`${route.name} requires native navigation`);
  if (route.name === 'search') {
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        if (lanistaCall(session.pipe, 'ui-query', ['object=theatreSearchSurface']).visible) break;
      } catch (error) {
        if (!/NO_SUCH_ITEM/.test(error.message)) throw error;
      }
      const candidates = lanistaCall(session.pipe, 'parity-snapshot').controls
        .filter(control => normal(control.label) === 'Search');
      if (candidates.length !== 1) die(`QML Search resolved to ${candidates.length} controls`);
      lanistaCall(session.pipe, 'ui-click', [`target=${candidates[0].handle}`]);
      await sleep(1500);
    }
    await waitFor(() => lanistaCall(session.pipe, 'ui-query', ['object=theatreSearchSurface']).visible,
      'QML world search layer');
  }
  if (route.name === 'seeAll' && route.world === 'Theatre')
    await waitFor(() => lanistaCall(session.pipe, 'ui-query', ['object=theatreSeeAllPage']).visible === true,
      'QML Theatre See All page');
  await sleep(3000);
}

function normalizePng(png, width, height) {
  const raw = png.replace(/\.png$/, '-device-pixels.png');
  const script = `from PIL import Image\nimport sys\np,w,h,raw=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]\nim=Image.open(p).convert('RGB')\nif im.size != (w,h):\n im.save(raw)\n im.resize((w,h), Image.Resampling.LANCZOS).save(p)\n`;
  const result = spawnSync('python', ['-c', script, png, String(width), String(height), raw],
    { encoding: 'utf8', timeout: 30000 });
  if (result.status !== 0) die(`PNG normalization failed: ${result.stderr || result.error || result.status}`);
}

async function captureQml(session, width, height) {
  const size = lanistaCall(session.pipe, 'parity-set-size', [`width=${width}`, `height=${height}`]);
  if (size.width !== width || size.height !== height) die(`QML window size is ${size.width}x${size.height}`);
  await sleep(1200);
  const target = route.name === 'search' && route.scope === 'Theatre' ? 'theatreSearchSurface'
    : route.name === 'seeAll' && route.world === 'Theatre' ? 'theatreSeeAllPage' : '';
  let reply;
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      reply = lanistaCall(session.pipe, 'parity-snapshot', target ? [`target=${target}`] : [], true);
      break;
    } catch (error) {
      if (!/TIMEOUT/.test(error.message) || attempt === 2) throw error;
      await sleep(1500);
    }
  }
  if (reply.width !== width || reply.height !== height || !reply.grabPath)
    die('QML capture lacks requested dimensions or screenshot');
  const png = path.join(runDir, `qml-${width}x${height}.png`);
  fs.copyFileSync(reply.grabPath, png);
  normalizePng(png, width, height);
  const capture = { width, height, words: reply.words, controls: reply.controls, png };
  fs.writeFileSync(path.join(runDir, `qml-${width}x${height}.json`), JSON.stringify(capture, null, 2));
  return capture;
}

const domCapture = `(() => {
  const norm = s => String(s || '').replace(/\\s+/g, ' ').trim();
  const viewport = { x: 0, y: 0, width: innerWidth, height: innerHeight };
  const intersect = (a, b) => {
    const x = Math.max(a.x, b.x), y = Math.max(a.y, b.y);
    const right = Math.min(a.x + a.width, b.x + b.width);
    const bottom = Math.min(a.y + a.height, b.y + b.height);
    return { x, y, width: Math.max(0, right - x), height: Math.max(0, bottom - y) };
  };
  const box = r => ({ x:r.x, y:r.y, width:r.width, height:r.height });
  const visible = (el, start) => {
    let r = intersect(box(start), viewport);
    for (let p = el; p && p.nodeType === 1; p = p.parentElement) {
      const s = getComputedStyle(p);
      if (s.display === 'none' || s.visibility === 'hidden' || Number(s.opacity) <= 0 || p.hidden || p.inert)
        return null;
      if (p !== el && ['hidden', 'clip', 'scroll', 'auto'].some(v => s.overflowX === v || s.overflowY === v))
        r = intersect(r, box(p.getBoundingClientRect()));
    }
    return r.width > 0 && r.height > 0 ? r : null;
  };
  const words = [];
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let node; (node = walker.nextNode()); ) {
    const text = norm(node.nodeValue), el = node.parentElement;
    if (!text || !el || /^(SCRIPT|STYLE|NOSCRIPT)$/.test(el.tagName)) continue;
    const range = document.createRange(); range.selectNodeContents(node);
    const rect = visible(el, range.getBoundingClientRect());
    if (rect) words.push({ text, rect });
  }
  const controls = [...document.querySelectorAll('[data-focus],button,input,select,textarea,[tabindex]')]
    .filter((el, i, all) => all.indexOf(el) === i && el.tabIndex >= 0 && !el.disabled)
    .map((el, domIndex) => {
      const rect = visible(el, el.getBoundingClientRect());
      const label = norm(el.getAttribute('aria-label') || el.textContent || el.getAttribute('placeholder'));
      return rect && label ? { label, rect, key: el.getAttribute('data-key') || el.id || '',
        tabIndex: el.tabIndex, domIndex } : null;
    }).filter(Boolean)
    .sort((a, b) => (a.tabIndex > 0 ? a.tabIndex : Infinity) - (b.tabIndex > 0 ? b.tabIndex : Infinity)
      || a.domIndex - b.domIndex)
    .map(({ domIndex, ...control }) => control);
  return { width: innerWidth, height: innerHeight, words, controls,
    route: CW.router.current(), sections: [...document.querySelectorAll('#col [data-section]')]
      .map(s => [s.dataset.section, s.dataset.state]) };
})()`;

async function navigateWeb(cdp) {
  await waitFor(() => evaluate(cdp, '!!(window.CW && CW.router && CW.port && CW.env)'), 'web shell');
  await evaluate(cdp, `CW.router.go(${JSON.stringify(route)})`);
  await waitFor(async () => {
    const current = await evaluate(cdp, 'CW.router.current()');
    return current?.name === route.name && (!route.world || current.world === route.world)
      && (!route.tab || current.tab === route.tab)
      && (!route.scope || current.scope === route.scope)
      && (!route.title || current.title === route.title)
      && (!route.route || JSON.stringify(current.route) === JSON.stringify(route.route));
  }, 'web route');
  await sleep(3000);
}

let defectInjected = false;
async function captureWeb(cdp, session, width, height) {
  const size = lanistaCall(session.pipe, 'parity-set-size', [`width=${width}`, `height=${height}`]);
  if (size.width !== width || size.height !== height) die(`web window size is ${size.width}x${size.height}`);
  await sleep(1500);
  let capture = await evaluate(cdp, domCapture);
  if (capture.width !== width || capture.height !== height)
    die(`web viewport is ${capture.width}x${capture.height}, expected ${width}x${height}`);
  if (!defectInjected && (opts['inject-word'] || opts['hide-control'])) {
    await evaluate(cdp, `(() => {
      const word = ${JSON.stringify(opts['inject-word'] || '')};
      const hide = ${JSON.stringify(opts['hide-control'] || '')};
      if (word) {
        const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
        let found = false;
        for (let n; (n = walker.nextNode()); ) if (n.nodeValue.includes(word)) {
          n.nodeValue = n.nodeValue.replace(word, word + ' WRONG'); found = true; break;
        }
        if (!found) throw Error('injected word not found: ' + word);
      }
      if (hide) {
        const target = [...document.querySelectorAll('[data-focus]')]
          .find(el => (el.getAttribute('aria-label') || el.textContent || '').trim() === hide);
        if (!target) throw Error('control to hide not found: ' + hide);
        target.style.display = 'none';
      }
    })()`);
    defectInjected = true;
    capture = await evaluate(cdp, domCapture);
  }
  if (opts['inject-word'] && !capture.words.some(x => x.text.includes(`${opts['inject-word']} WRONG`)))
    die('planted wrong word is absent from visible web capture');
  if (opts['hide-control'] && capture.controls.some(x => x.label === opts['hide-control']))
    die('planted hidden control remains visible in web capture');
  let shot;
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      shot = await cdp.send('Page.captureScreenshot',
        { format: 'png', captureBeyondViewport: false }, 45000);
      break;
    } catch (error) {
      if (!/timed out/.test(error.message) || attempt === 2) throw error;
      await sleep(1500);
    }
  }
  const png = path.join(runDir, `web-${width}x${height}.png`);
  fs.writeFileSync(png, Buffer.from(shot.data, 'base64'));
  normalizePng(png, width, height);
  capture.png = png;
  fs.writeFileSync(path.join(runDir, `web-${width}x${height}.json`), JSON.stringify(capture, null, 2));
  return capture;
}

const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;' })[c]);
const image = p => fs.existsSync(p) ? `data:image/png;base64,${fs.readFileSync(p).toString('base64')}` : '';
function report(width, height, qml, web, result, heatmap, score) {
  const rows = (title, values, describe) => `<section><h2>${title} (${values.length})</h2><ol>${values.map(v =>
    `<li class="${v.allowed ? 'allowed' : 'bad'}">${esc(describe(v))}${v.allowed ? ' — allowed' : ''}</li>`).join('')}</ol></section>`;
  const html = `<!doctype html><meta charset="utf-8"><title>${esc(label)} ${width}×${height} ${result.pass ? 'PASS' : 'FAIL'}</title>
<style>body{background:#111827;color:#e5e7eb;font:15px system-ui;margin:30px}h1{color:${result.pass ? '#86efac' : '#fca5a5'}}
.shots{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.shots img{width:100%;border:1px solid #475569}
.bad{color:#fca5a5}.allowed{color:#fcd34d}li{margin:5px 0}code{color:#93c5fd}</style>
<h1>${result.pass ? 'PASS' : 'FAIL'} — ${esc(label)} — ${width}×${height}</h1>
<p>Seed SHA-256 <code>${seed.sha256}</code> · ${seed.files} real-library files · build ${esc(buildHash)} · QML PID ${qmlPid} · web PID ${webPid}</p>
<div class="shots"><figure><figcaption>QML</figcaption><img src="${image(qml.png)}"></figure>
<figure><figcaption>Web</figcaption><img src="${image(web.png)}"></figure></div>
<figure><figcaption>Eyes-only pixel heatmap${score == null ? '' : ` · mean absolute channel difference ${score}`}</figcaption>
${heatmap ? `<img style="max-width:100%" src="${image(heatmap)}">` : '<p>Heatmap unavailable.</p>'}</figure>
${rows('Gate failures', result.failures, x => x)}
${rows('Word differences', result.wordDiff, x => `${x.side}: ${x.text}`)}
${rows('Control differences', result.controlDiff, x => x.kind === 'order' ?
  `order QML: ${x.qml.join(' | ')} / web: ${x.web.join(' | ')}` : `${x.side}: ${x.text}`)}
${rows('Layout offenders, worst first', result.layout, x =>
  `${x.kind} ${x.text}: ${x.delta.toFixed(1)} px (allowed ${x.tolerance.toFixed(1)})`)}
<p>Captured ${qml.words.length}/${web.words.length} visible text nodes and ${qml.controls.length}/${web.controls.length} controls (QML/web).</p>`;
  fs.writeFileSync(path.join(runDir, `${width}x${height}.html`), html);
}

function makeHeatmap(width, height) {
  const q = path.join(runDir, `qml-${width}x${height}.png`);
  const w = path.join(runDir, `web-${width}x${height}.png`);
  const out = path.join(runDir, `heatmap-${width}x${height}.png`);
  const result = spawnSync('python', [path.join(HERE, 'heatmap.py'), q, w, out],
    { encoding: 'utf8', timeout: 30000 });
  return result.status === 0 ? { path: out, score: normal(result.stdout) } : { path: null, score: null };
}

const runTag = `parity-${Date.now()}-${crypto.randomBytes(4).toString('hex')}`;
const buildHash = crypto.createHash('sha256').update(fs.readFileSync(exe)).digest('hex');
let qmlSession, webSession, cdp, qmlPid = '', webPid = '';
const qmlCaptures = new Map(), webCaptures = new Map();
try {
  qmlSession = await launchSide('qml', `${runTag}-qml`);
  qmlPid = qmlSession.pid;
  await navigateQml(qmlSession);
  for (const [width, height] of sizes)
    qmlCaptures.set(width, await captureQml(qmlSession, width, height));
  await stopSide(qmlSession); qmlSession = null;

  const port = await freePort();
  webSession = await launchSide('web', `${runTag}-web`, port);
  webPid = webSession.pid;
  await clearBootAndOnboarding(webSession);
  cdp = await waitFor(() => connectColosseum(port), 'Qt WebEngine CDP');
  await navigateWeb(cdp);
  for (const [width, height] of sizes)
    webCaptures.set(width, await captureWeb(cdp, webSession, width, height));

  let passed = true;
  for (const [width, height] of sizes) {
    const qml = qmlCaptures.get(width), web = webCaptures.get(width);
    const result = compare(qml, web, allow);
    if (!qml.words.length || !web.words.length || !qml.controls.length || !web.controls.length) {
      result.pass = false;
      result.failures.push('empty text or control capture');
    }
    const heat = makeHeatmap(width, height);
    report(width, height, qml, web, result, heat.path, heat.score);
    fs.writeFileSync(path.join(runDir, `${width}x${height}.result.json`), JSON.stringify(result, null, 2));
    console.log(`${width}x${height}: ${result.pass ? 'PASS' : 'FAIL'} · ${result.failures.length} gate failures · ${path.join(runDir, `${width}x${height}.html`)}`);
    passed &&= result.pass;
  }
  fs.writeFileSync(statusPath, JSON.stringify({ status: passed ? 'pass' : 'fail', route,
    seed, buildHash, qmlPid, webPid, finishedAt: new Date().toISOString() }, null, 2));
  process.exitCode = passed ? 0 : 1;
} catch (error) {
  fs.writeFileSync(path.join(runDir, 'error.txt'), `${error.stack || error}\n`);
  fs.writeFileSync(statusPath, JSON.stringify({ status: 'infra-fail', route,
    message: error.message, finishedAt: new Date().toISOString() }, null, 2));
  console.error(`INFRA FAIL: ${error.message}`);
  process.exitCode = 2;
} finally {
  cdp?.close();
  await stopSide(webSession);
  await stopSide(qmlSession);
}
// Qt WebEngine can leave the CDP close handshake pending after both tagged
// processes are stopped. All reports and cleanup above are synchronous/awaited.
process.exit(process.exitCode);
