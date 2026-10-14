// Isolated HTTP fixture: verifies real Qt image requests without provider/network variability.
const http = require('node:http');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {spawn, spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const qt = process.env.COLOSSEUM_QT_BIN || 'C:/Qt/6.11.1/msvc2022_64/bin';
const counts = {};
const server = http.createServer((request, response) => {
    const pathname = new URL(request.url, 'http://localhost').pathname;
    response.setHeader('Cache-Control', 'no-store');
    if (pathname === '/counts') {
        response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify(counts)); return;
    }
    counts[pathname] = (counts[pathname] || 0) + 1;
    if (pathname.startsWith('/slow')) {
        const delayed = setTimeout(() => response.end('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="60"/>'), 15000);
        response.on('close', () => { clearTimeout(delayed); if (!response.writableEnded) counts.aborted = (counts.aborted || 0) + 1; });
        return;
    }
    if (pathname === '/cached.svg') response.setHeader('Cache-Control', 'public, max-age=3600');
    response.setHeader('Content-Type', 'image/svg+xml');
    response.end('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="60"><rect width="40" height="60" fill="gold"/></svg>');
});
server.listen(0, '127.0.0.1', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'colosseum-shelves-requests-'));
    const template = fs.readFileSync(path.join(__dirname, 'qml/fixtures/ShelvesImageRequests.qml.in'), 'utf8');
    const input = path.join(directory, 'tst_shelves_requests.qml');
    const baselineArg = process.argv.find(value => value.startsWith('--baseline='));
    const baseline = Boolean(baselineArg);
    const baselineRef = baselineArg ? baselineArg.slice('--baseline='.length) : '';
    if (baseline && !/^[a-f0-9]{7,40}$/.test(baselineRef)) throw new Error('Baseline requires an explicit commit SHA');
    let app = path.join(root, 'qml');
    if (baseline) {
        app = path.join(directory, 'qml');
        fs.cpSync(path.join(root, 'qml'), app, {recursive: true});
        const old = spawnSync('git', ['show', baselineRef + ':qml/ShelvesUniversePage.qml'], {cwd: root, encoding: 'utf8'});
        if (old.status !== 0) throw new Error('Cannot read committed baseline');
        fs.writeFileSync(path.join(app, 'ShelvesUniversePage.qml'), old.stdout);
        for (const relative of ['fonts', 'icons', 'wallpaper/cold-ripple.jpg']) {
            const target = path.join(directory, 'assets', relative);
            fs.mkdirSync(path.dirname(target), {recursive: true});
            fs.cpSync(path.join(root, 'assets', relative), target, {recursive: true});
        }
    }
    // Exercise the production boot owner without live catalogue/provider calls.
    fs.writeFileSync(path.join(directory, 'BootSplash.qml'), fs.readFileSync(path.join(root, 'qml/BootSplash.qml'), 'utf8')
        .replace('"Catalog.js"', '"StubCatalog.js"').replace('"TheatreApi.js"', '"StubTheatre.js"')
        .replace('../assets/icons/colosseum.svg', pathToFileURL(path.join(root, 'assets/icons/colosseum.svg')).href));
    fs.writeFileSync(path.join(directory, 'StubCatalog.js'), '.pragma library\nfunction allImageUrls(){return []}\n');
    fs.writeFileSync(path.join(directory, 'StubTheatre.js'), '.pragma library\nfunction loadTheatre(callback){}\nfunction normalizeArtUrl(value){return value}\n');
    fs.writeFileSync(input, template.replaceAll('@@APP_URL@@', pathToFileURL(app).href)
        .replaceAll('@@BASELINE@@', baseline ? 'true' : 'false')
        .replaceAll('@@BASE_URL@@', 'http://127.0.0.1:' + server.address().port));
    const child = spawn(path.join(qt, 'qmltestrunner.exe'), ['-platform', 'offscreen', '-input', input, '-import', path.join(root, 'qml')], {
        env: {...process.env, PATH: qt + path.delimiter + process.env.PATH, QT_FORCE_STDERR_LOGGING: '1',
            QT_QUICK_CONTROLS_STYLE: 'Basic', QT_QPA_FONTDIR: 'C:/Windows/Fonts',
            QT_QPA_PLATFORM_PLUGIN_PATH: path.join(qt, '../plugins/platforms')}, stdio: 'inherit'
    });
    let finished = false;
    const deadline = setTimeout(() => { child.kill(); finish(1); }, 30000);
    function finish(code) {
        if (finished) return;
        finished = true;
        clearTimeout(deadline);
        if (path.dirname(path.resolve(directory)) !== path.resolve(os.tmpdir()) || !path.basename(directory).startsWith('colosseum-shelves-requests-')) throw new Error('Unsafe temporary directory');
        if (path.relative(path.resolve(directory), path.resolve(input)) !== 'tst_shelves_requests.qml') throw new Error('Unsafe fixture cleanup');
        fs.rmSync(path.resolve(directory), {recursive: true});
        server.closeAllConnections();
        server.close(() => { process.exitCode = code === 0 ? 0 : 1; });
    }
    child.on('error', error => { console.error(error.message); finish(1); });
    child.on('exit', finish);
});
