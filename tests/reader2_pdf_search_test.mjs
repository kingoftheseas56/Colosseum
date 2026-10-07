// Real browser/PDFjs regression: node tests/reader2_pdf_search_test.mjs
// Optional live fixture: node tests/reader2_pdf_search_test.mjs file.pdf expectedCount
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { existsSync } from 'node:fs'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const browser = [process.env.COLOSSEUM_TEST_BROWSER,
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/chromium', '/usr/bin/google-chrome',
].find(path => path && existsSync(path))
assert.ok(browser, 'Chrome/Chromium required; set COLOSSEUM_TEST_BROWSER if needed')

// Four pages include repeated/case-varied matches, a blank page, and a distant hit.
function makeFixture() {
    const texts = ['Beacon one BEACON two', '', 'quiet page', 'A Beacon at the end.']
    const objects = ['<< /Type /Catalog /Pages 2 0 R >>',
        '<< /Type /Pages /Kids [4 0 R 6 0 R 8 0 R 10 0 R] /Count 4 >>',
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    for (const text of texts) {
        const stream = `BT /F1 18 Tf 72 700 Td (${text}) Tj ET\n`
        const content = objects.length + 2
        objects.push(`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents ${content} 0 R >>`,
            `<< /Length ${stream.length} >>\nstream\n${stream}endstream`)
    }
    let pdf = '%PDF-1.4\n'
    const offsets = [0]
    for (const [i, object] of objects.entries()) {
        offsets.push(pdf.length)
        pdf += `${i + 1} 0 obj\n${object}\nendobj\n`
    }
    const xref = pdf.length
    pdf += `xref\n0 ${offsets.length}\n0000000000 65535 f \n`
    for (const offset of offsets.slice(1)) pdf += `${String(offset).padStart(10, '0')} 00000 n \n`
    return Buffer.from(pdf + `trailer\n<< /Size ${offsets.length} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`)
}

async function browserTest(liveExpected) {
    const { makePDF } = await import('/resources/reader2/vendor/foliate-anx/src/pdf.js')
    await import('/resources/reader2/vendor/foliate-anx/src/view.js')
    pdfjsLib.GlobalWorkerOptions.workerSrc = '/resources/reader2/vendor/foliate-anx/src/vendor/pdfjs/pdf.worker.js'
    const check = (ok, message) => { if (!ok) throw new Error(message) }
    let bitmaps = 0
    const toBlob = HTMLCanvasElement.prototype.toBlob
    HTMLCanvasElement.prototype.toBlob = function (...args) { bitmaps++; return toBlob.apply(this, args) }
    const results = []
    async function run(url, expected, synthetic) {
        const book = await makePDF(await (await fetch(url)).blob())
        const view = document.createElement('foliate-view')
        view.style.cssText = 'display:block;width:800px;height:900px'
        document.body.append(view)
        await view.open(book)
        const before = bitmaps
        const hits = []
        for await (const result of view.search({ query: 'beacon', index: null })) {
            if (result.subitems) hits.push(...result.subitems)
        }
        check(hits.length === expected, `${url}: expected ${expected} Beacon hits, got ${hits.length}`)
        check(bitmaps === before, 'Search rasterized PDF pages')
        check(hits.every(hit => /^beacon$/i.test(hit.excerpt.match)), 'Incorrect search excerpts')
        if (synthetic) {
            const indices = hits.map(hit => view.resolveNavigation(hit.cfi).index)
            check(JSON.stringify(indices) === '[0,0,3]', `Wrong page indices: ${indices}`)
            const pageHits = []
            for await (const result of view.search({ query: 'Beacon', index: 3 })) {
                if (result.cfi) pageHits.push(result)
            }
            check(pageHits.length === 1, 'Single-page PDF search failed')
            const missing = []
            for await (const result of view.search({ query: 'absent', index: null })) {
                if (result.subitems) missing.push(...result.subitems)
            }
            check(missing.length === 0, 'Missing query returned matches')
        }
        // Navigate with the real fixed-layout renderer and resolve search CFIs
        // against the loaded iframe DOM (not the detached search document).
        const targets = synthetic ? hits : [hits[0], hits.at(-1)]
        for (const hit of targets) {
            const resolved = await view.goTo(hit.cfi)
            check(resolved && view.renderer.index === resolved.index, 'Result navigated to wrong page')
            const content = view.renderer.getContents().find(({ doc }) => {
                try { return /^beacon$/i.test(resolved.anchor(doc).toString()) } catch { return false }
            })
            check(content, 'Result CFI did not resolve to Beacon in the rendered page')
        }
        results.push({ fixture: url, count: hits.length, navigated: targets.length })
        view.close()
        view.remove()
    }
    await run('/fixture.pdf', 3, true)
    if (liveExpected !== null) await run('/live.pdf', liveExpected, false)
    return results
}

const livePDF = process.argv[2] ? await readFile(resolve(process.argv[2])) : null
const liveExpected = livePDF ? Number(process.argv[3]) : null
if (livePDF) assert.ok(Number.isInteger(liveExpected) && liveExpected > 0, 'Supply expected live-fixture count')
const profile = await mkdtemp(join(tmpdir(), 'colosseum-pdf-search-'))
let finish
const verdict = new Promise(resolve => { finish = resolve })
const server = createServer(async (req, res) => {
    try {
        const pathname = new URL(req.url, 'http://localhost').pathname
        if (pathname === '/result' && req.method === 'POST') {
            let body = ''
            for await (const chunk of req) body += chunk
            finish(JSON.parse(body))
            res.end('ok')
            return
        }
        if (pathname === '/') {
            res.setHeader('Content-Type', 'text/html')
            res.end(`<script src="/resources/reader2/vendor/foliate-anx/src/vendor/pdfjs/pdf.js"></script><script type="module">
                const run = ${browserTest.toString()};
                try { const results = await run(${JSON.stringify(liveExpected)}); await fetch('/result', {method:'POST', body:JSON.stringify({results})}) }
                catch (error) { await fetch('/result', {method:'POST', body:JSON.stringify({error:error.stack})}) }
            </script>`)
            return
        }
        if (pathname === '/fixture.pdf' || pathname === '/live.pdf') {
            res.setHeader('Content-Type', 'application/pdf')
            res.end(pathname === '/fixture.pdf' ? makeFixture() : livePDF)
            return
        }
        const path = resolve(root, '.' + decodeURIComponent(pathname))
        if (!path.startsWith(join(root, 'resources', 'reader2') + sep)) {
            res.writeHead(404).end()
            return
        }
        res.setHeader('Content-Type', 'text/javascript')
        res.end(await readFile(path))
    } catch (error) { res.writeHead(500).end(String(error)) }
})
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
const chrome = spawn(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    `--user-data-dir=${profile}`, `http://127.0.0.1:${server.address().port}/`], { stdio: 'ignore', windowsHide: true })
const exited = new Promise(resolve => chrome.on('close', code => {
    finish({ error: `Browser exited before returning a result (code ${code})` })
    resolve()
}))
chrome.on('error', error => finish({ error: String(error) }))
const timer = setTimeout(() => finish({ error: 'PDF search browser test timed out' }), 90000)
try {
    const result = await verdict
    assert.equal(result.error, undefined, result.error)
    console.log(JSON.stringify(result.results))
    console.log('VERDICT: PASS (real PDFjs search and rendered-page navigation)')
} finally {
    clearTimeout(timer)
    chrome.kill()
    await exited
    await new Promise(resolve => server.close(resolve))
    // profile is the exact newly-created temporary directory, outside the repository.
    assert.ok(profile.startsWith(join(tmpdir(), 'colosseum-pdf-search-')))
    await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 })
}
