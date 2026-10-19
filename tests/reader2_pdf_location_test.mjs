// Real PDF + FixedLayout regression: node tests/reader2_pdf_location_test.mjs
// COLOSSEUM_PDF_SOURCE_ROOT selects an unchanged baseline for a red/green comparison.
// COLOSSEUM_PDF_EVIDENCE_DIR optionally retains the fixture and JSON transcript.
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { existsSync } from 'node:fs'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(process.env.COLOSSEUM_PDF_SOURCE_ROOT
    || resolve(dirname(fileURLToPath(import.meta.url)), '..'))
const evidence = process.env.COLOSSEUM_PDF_EVIDENCE_DIR
const browser = [process.env.COLOSSEUM_TEST_BROWSER,
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/chromium', '/usr/bin/google-chrome',
].find(path => path && existsSync(path))
assert.ok(browser, 'Chrome/Chromium required; set COLOSSEUM_TEST_BROWSER if needed')

function makeFixture() {
    const markers = ['ALPHAONLY', 'BRAVOONLY', 'CHARLIEONLY',
        'DELTAONLY', 'ECHOONLY', 'FOXTROTONLY']
    const objects = ['<< /Type /Catalog /Pages 2 0 R >>',
        '<< /Type /Pages /Kids [4 0 R 6 0 R 8 0 R 10 0 R 12 0 R 14 0 R] /Count 6 >>',
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    for (const [index, marker] of markers.entries()) {
        const text = 'Physical page ' + (index + 1) + ': ' + marker
        const stream = 'BT /F1 22 Tf 72 700 Td (' + text + ') Tj ET\n'
        const content = objects.length + 2
        objects.push('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] '
            + '/Resources << /Font << /F1 3 0 R >> >> /Contents ' + content + ' 0 R >>',
        '<< /Length ' + stream.length + ' >>\nstream\n' + stream + 'endstream')
    }
    let pdf = '%PDF-1.4\n'
    const offsets = [0]
    for (const [index, object] of objects.entries()) {
        offsets.push(pdf.length)
        pdf += (index + 1) + ' 0 obj\n' + object + '\nendobj\n'
    }
    const xref = pdf.length
    pdf += 'xref\n0 ' + offsets.length + '\n0000000000 65535 f \n'
    for (const offset of offsets.slice(1))
        pdf += String(offset).padStart(10, '0') + ' 00000 n \n'
    return Buffer.from(pdf + 'trailer\n<< /Size ' + offsets.length
        + ' /Root 1 0 R >>\nstartxref\n' + xref + '\n%%EOF\n')
}

async function browserTest() {
    const { makePDF } = await import('/resources/reader2/vendor/foliate-anx/src/pdf.js')
    await import('/resources/reader2/vendor/foliate-anx/src/view.js')
    pdfjsLib.GlobalWorkerOptions.workerSrc =
        '/resources/reader2/vendor/foliate-anx/src/vendor/pdfjs/pdf.worker.js'
    const markers = ['ALPHAONLY', 'BRAVOONLY', 'CHARLIEONLY',
        'DELTAONLY', 'ECHOONLY', 'FOXTROTONLY']
    const fixture = await (await fetch('/fixture.pdf')).blob()
    const cases = []
    const check = (condition, message, state) => {
        if (!condition) throw new Error(message + ': ' + JSON.stringify(state))
    }
    const settle = () => new Promise(resolve =>
        requestAnimationFrame(() => requestAnimationFrame(resolve)))
    async function open(mode, cfi) {
        const book = await makePDF(fixture)
        if (mode.spread) book.rendition.spread = mode.spread
        if (mode.rtl) {
            book.dir = 'rtl'
            book.sections[0].pageSpread = 'left'
        }
        const view = document.createElement('foliate-view')
        view.style.cssText = 'display:block;width:' + mode.width
            + 'px;height:' + mode.height + 'px'
        document.body.append(view)
        const events = []
        view.addEventListener('relocate', event => events.push(event.detail.cfi))
        await view.open(book)
        await view.init(cfi ? { lastLocation: cfi } : {})
        await settle()
        return { view, book, events }
    }
    function close(reader) {
        if (!reader.view.isConnected) return
        reader.view.close()
        reader.view.remove()
    }
    function snapshot(reader) {
        const { view, book, events } = reader
        const cfi = view.lastLocation?.cfi
        const contents = view.renderer.getContents().flatMap(({ doc }) => {
            const text = doc.body?.textContent || ''
            const index = markers.findIndex(marker => text.includes(marker))
            if (index < 0) return []
            const frame = doc.defaultView.frameElement
            const rect = frame.getBoundingClientRect()
            const wrapper = frame.parentElement.getBoundingClientRect()
            const hasArea = box => Number.isFinite(box.width) && box.width > 0
                && Number.isFinite(box.height) && box.height > 0
            return [{ index, physicalPage: index + 1, marker: markers[index],
                side: doc.position, width: rect.width, height: rect.height,
                visible: frame.style.display !== 'none'
                    && frame.parentElement.style.display !== 'none'
                    && hasArea(rect) && hasArea(wrapper) }]
        })
        return { index: view.renderer.index, cfi,
            cfiIndex: cfi ? view.resolveNavigation(cfi).index : null,
            chapterCurrent: view.lastLocation?.chapterLocation?.current,
            selectedSide: view.renderer.getSpreadOf(book.sections[view.renderer.index])?.side,
            eventCount: events.length, contents }
    }
    function expectPage(reader, index, label, transcript) {
        const state = snapshot(reader)
        transcript.push({ label, expectedIndex: index, expectedPhysicalPage: index + 1, ...state })
        check(state.index === index, label + ' reports wrong physical page index', state)
        check(state.cfiIndex === index, label + ' saves wrong page CFI', state)
        check(state.chapterCurrent === index + 1,
            label + ' displays wrong physical page number', state)
        check(state.contents.some(page => page.index === index && page.visible),
            label + ' target marker is not visible', state)
        const rect = reader.view.getBoundingClientRect()
        if (rect.height > rect.width && !['both', 'portrait'].includes(reader.view.renderer.spread))
            check(state.contents.filter(page => page.visible).length === 1,
                label + ' single-page viewport shows multiple pages', state)
        return state.cfi
    }
    async function run(mode, name, exercise) {
        const transcript = []
        let reader
        try {
            reader = await open(mode)
            await exercise(reader, transcript)
            cases.push({ mode: mode.name, name, pass: true, transcript })
        } catch (error) {
            cases.push({ mode: mode.name, name, pass: false, error: error.stack, transcript })
        } finally {
            if (reader) close(reader)
        }
    }
    const modes = [
        { name: 'single', width: 1000, height: 800, spread: 'none' },
        { name: 'portrait', width: 700, height: 1000 },
        { name: 'landscape-spread', width: 1200, height: 800 },
        { name: 'forced-spread', width: 700, height: 1000, spread: 'both' },
        { name: 'rtl-spread', width: 1200, height: 800, rtl: true },
    ]
    for (const mode of modes) {
        await run(mode, 'save physical page 2 and reopen', async (reader, transcript) => {
            await reader.view.goTo(1)
            const cfi = expectPage(reader, 1, 'save physical page 2', transcript)
            close(reader)
            const reopened = await open(mode, cfi)
            try { expectPage(reopened, 1, 'fresh view restored physical page 2', transcript) }
            finally { close(reopened) }
        })
        await run(mode, 'search within spread, resize and restore', async (reader, transcript) => {
            await reader.view.goTo(2)
            expectPage(reader, 2, 'start on physical page 3', transcript)
            const hits = []
            for await (const result of reader.view.search({ query: 'BRAVOONLY', index: null }))
                if (result.subitems) hits.push(...result.subitems)
            check(hits.length === 1, 'unique page-2 search match missing', hits)
            const target = reader.view.resolveNavigation(hits[0].cfi)
            check(target.index === 1, 'search CFI resolves to wrong physical page', target)
            const before = reader.events.length
            await reader.view.goTo(hits[0].cfi)
            const cfi = expectPage(reader, 1, 'search selects physical page 2', transcript)
            check(reader.events.length > before, 'same-spread navigation omitted relocate', snapshot(reader))
            check(reader.view.renderer.getContents().some(({ doc }) => {
                try { return target.anchor(doc).toString() === 'BRAVOONLY' }
                catch { return false }
            }), 'search anchor no longer resolves in rendered PDF', snapshot(reader))
            reader.view.style.width = '600px'
            reader.view.style.height = '1100px'
            await settle()
            expectPage(reader, 1, 'resize retains selected side', transcript)
            close(reader)
            const reopened = await open(mode, cfi)
            try { expectPage(reopened, 1, 'search position restored in fresh view', transcript) }
            finally { close(reopened) }
        })
        await run(mode, 'all pages, paired sides and boundaries', async (reader, transcript) => {
            await reader.view.prev()
            expectPage(reader, 0, 'before-first boundary', transcript)
            for (const index of [1, 2, 1, 3, 4, 5, 4, 3, 2, 1, 0, 5]) {
                await reader.view.goTo(index)
                expectPage(reader, index, 'direct navigation ' + (index + 1), transcript)
            }
            await reader.view.next()
            const cfi = expectPage(reader, 5, 'after-last boundary', transcript)
            close(reader)
            const reopened = await open(mode, cfi)
            try { expectPage(reopened, 5, 'last-page boundary restored', transcript) }
            finally { close(reopened) }
        })
    }
    await run(modes[1], 'portrait next/previous crosses both sides', async (reader, transcript) => {
        for (let index = 1; index < 6; index++) {
            await reader.view.next()
            expectPage(reader, index, 'next ' + (index + 1), transcript)
        }
        for (let index = 4; index >= 0; index--) {
            await reader.view.prev()
            expectPage(reader, index, 'previous ' + (index + 1), transcript)
        }
    })
    async function epubReflowGuard() {
        const base = '/resources/reader2/vendor/foliate-anx/src/'
        const { EPUB } = await import(base + 'epub.js')
        const { configure, ZipReader, BlobReader, TextWriter, BlobWriter } =
            await import(base + 'vendor/zip.js')
        configure({ useWebWorkers: false })
        const params = new URLSearchParams(location.search)
        params.set('style', JSON.stringify({ allowScript: false }))
        history.replaceState(null, '', location.pathname + '?' + params)
        const blob = await (await fetch('/reflow.epub')).blob()
        const mounted = []
        async function fresh() {
            const zip = new ZipReader(new BlobReader(blob))
            const entries = await zip.getEntries()
            const byName = new Map(entries.map(entry => [entry.filename, entry]))
            const book = await new EPUB({
                entries,
                loadText: name => byName.has(name) ? byName.get(name).getData(new TextWriter()) : null,
                loadBlob: (name, type) => byName.has(name) ? byName.get(name).getData(new BlobWriter(type)) : null,
                getSize: name => byName.get(name)?.uncompressedSize ?? 0,
            }).init()
            const view = document.createElement('foliate-view')
            view.style.cssText = 'display:block;width:640px;height:800px'
            document.body.append(view)
            mounted.push(view)
            await view.open(book)
            check(!view.isFixedLayout && view.renderer.localName === 'foliate-paginator',
                'EPUB must exercise ordinary reflow', view.renderer.localName)
            const renderer = view.renderer
            renderer.setAttribute('flow', 'paginated')
            renderer.setAttribute('max-column-count', '1')
            renderer.setAttribute('top-margin', '32px')
            renderer.setAttribute('bottom-margin', '32px')
            renderer.setAttribute('gap', '5%')
            renderer.removeAttribute('animated')
            renderer.setStyles('html { font-size:18px !important } '
                + 'body { font-family:monospace !important; line-height:1.5 !important }')
            return view
        }
        try {
            const first = await fresh()
            await first.init({})
            await first.goToFraction(0.5)
            const page = first.renderer.page
            const cfi = first.lastLocation?.cfi
            check(cfi && page > 1 && page < first.renderer.pages - 2,
                'EPUB must reach a distinguishable interior page', { page, cfi })
            await first.next()
            check(first.renderer.page === page + 1 && first.lastLocation.cfi !== cfi,
                'EPUB next must advance one page', { page: first.renderer.page, cfi: first.lastLocation.cfi })
            await first.prev()
            check(first.renderer.page === page && first.lastLocation.cfi === cfi,
                'EPUB previous must restore page and CFI', { page: first.renderer.page, cfi: first.lastLocation.cfi })
            first.close()
            first.remove()
            const reopened = await fresh()
            await reopened.init({ lastLocation: cfi })
            check(reopened.renderer.page === page && reopened.lastLocation?.cfi === cfi,
                'EPUB close/reopen must restore page and CFI',
                { expectedPage: page, page: reopened.renderer.page, expectedCfi: cfi, cfi: reopened.lastLocation?.cfi })
            check(reopened.resolveNavigation(cfi).index === reopened.renderer.getContents()[0].index,
                'EPUB restored wrong spine', cfi)
            return [{ page, cfi, restoredPage: reopened.renderer.page,
                restoredCfi: reopened.lastLocation.cfi }]
        } finally {
            for (const view of mounted) {
                if (view.isConnected) { view.close(); view.remove() }
            }
        }
    }
    try {
        const transcript = await epubReflowGuard()
        cases.push({ mode: 'reflow-epub', name: 'interior page navigation and close/reopen',
            pass: true, transcript })
    } catch (error) {
        cases.push({ mode: 'reflow-epub', name: 'interior page navigation and close/reopen',
            pass: false, error: error.stack })
    }
    return { userAgent: navigator.userAgent, cases }
}

const fixture = makeFixture()
const reflowFixture = await readFile(join(root,
    'tests/lanista-seeds/biblio-long-epub-v1/books/long-book.epub'))
const profile = await mkdtemp(join(tmpdir(), 'colosseum-pdf-location-'))
const sha256 = data => createHash('sha256').update(data).digest('hex')
const identity = { root, browser, node: process.version,
    fixtureSha256: sha256(fixture), epubFixtureSha256: sha256(reflowFixture),
    testSha256: sha256(await readFile(fileURLToPath(import.meta.url))),
    ownerSha256: sha256(await readFile(join(root,
        'resources/reader2/vendor/foliate-anx/src/fixed-layout.js'))) }
let finish
const verdict = new Promise(resolve => { finish = resolve })
const server = createServer(async (req, res) => {
    try {
        const pathname = new URL(req.url, 'http://localhost').pathname
        if (pathname === '/result' && req.method === 'POST') {
            let body = ''
            for await (const chunk of req) body += chunk
            finish(JSON.parse(body))
            return res.end('ok')
        }
        if (pathname === '/') {
            res.setHeader('Content-Type', 'text/html')
            return res.end('<style>body{margin:0}</style><script src="/resources/reader2/vendor/foliate-anx/src/vendor/pdfjs/pdf.js"></script>'
                + '<script type="module">const run=' + browserTest.toString() + ';'
                + 'try{await fetch("/result",{method:"POST",body:JSON.stringify(await run())})}'
                + 'catch(e){await fetch("/result",{method:"POST",body:JSON.stringify({error:e.stack})})}</script>')
        }
        if (pathname === '/reflow.epub') {
            res.setHeader('Content-Type', 'application/epub+zip')
            return res.end(reflowFixture)
        }
        if (pathname === '/fixture.pdf') {
            res.setHeader('Content-Type', 'application/pdf')
            return res.end(fixture)
        }
        const path = resolve(root, '.' + decodeURIComponent(pathname))
        if (!path.startsWith(join(root, 'resources', 'reader2') + sep))
            return res.writeHead(404).end()
        res.setHeader('Content-Type', 'text/javascript')
        res.end(await readFile(path))
    } catch (error) { res.writeHead(500).end(String(error)) }
})
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
const chrome = spawn(browser, ['--headless=new', '--disable-gpu', '--no-first-run',
    '--no-default-browser-check', '--disable-background-timer-throttling',
    '--disable-renderer-backgrounding', '--user-data-dir=' + profile,
    'http://127.0.0.1:' + server.address().port + '/'], { stdio: 'ignore', windowsHide: true })
const exited = new Promise(resolve => chrome.on('close', code => {
    finish({ error: 'Browser exited before result: ' + code })
    resolve()
}))
chrome.on('error', error => finish({ error: String(error) }))
const timer = setTimeout(() => finish({ error: 'PDF location browser test timed out' }), 120000)
try {
    const result = { ...identity, ...await verdict }
    if (evidence) {
        await mkdir(evidence, { recursive: true })
        await writeFile(join(evidence, 'pdf-location-fixture.pdf'), fixture)
        await writeFile(join(evidence, 'pdf-location-result.json'), JSON.stringify(result, null, 2) + '\n')
    }
    assert.equal(result.error, undefined, result.error)
    for (const test of result.cases)
        console.log((test.pass ? 'PASS ' : 'FAIL ') + test.mode + ': ' + test.name
            + (test.pass ? '' : '\n' + test.error))
    const failures = result.cases.filter(test => !test.pass)
    console.log(JSON.stringify({ ...identity, userAgent: result.userAgent,
        passed: result.cases.length - failures.length, failed: failures.length }))
    assert.equal(failures.length, 0, 'PDF physical-page/location restoration regression')
    console.log('VERDICT: PASS (real PDF, fixed-layout navigation and fresh-view CFI restoration)')
} finally {
    clearTimeout(timer)
    chrome.kill()
    await exited
    await new Promise(resolve => server.close(resolve))
    assert.ok(profile.startsWith(join(tmpdir(), 'colosseum-pdf-location-')))
    await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 })
}
