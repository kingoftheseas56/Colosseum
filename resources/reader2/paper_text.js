// paper_text.js — TXT support, the ONE format the Anx fork lacks (its getView throws
// on text/plain). We do NOT bolt on a second renderer: we synthesize a single
// REFLOWABLE XHTML section and hand it to <foliate-view> like any other book, so
// pagination, appearance (font/size/margins/justify), selection, highlight and search
// all come for free from the SAME paginator path as EPUB. Shape mirrors the fork's
// makeFB2 (a section with load()/createDocument()/size + toc/resolveHref/...).
//
// Kept as its own ES module so paper_glue.js dynamic-imports it exactly like the
// vendored book-makers (epub.js/mobi.js/fb2.js), AND a headless node test can import
// the pure string helpers below directly (the QML .pragma-library harness can't host
// browser-context glue). [Agent 2 (Claude), biblio]

// XML 1.0 forbids most C0 control chars (all except tab/newline/CR). A .txt in the
// wild may carry form-feeds (page breaks), NULs, etc.; left in, DOMParser emits a
// <parsererror> and the page never renders. Strip the disallowed range up front.
const XML_INVALID = /[\u0000-\u0008\u000B\u000C\u000E-\u001F\uFFFE\uFFFF]/g

export const escapeXml = s => String(s ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;')     // defense-in-depth (element text today; harmless, future-proof for attrs)

// Blank-line-separated blocks -> paragraphs (same split the old engine_txt used). A
// hard-wrapped file with no blank lines becomes ONE paragraph and still reflows fine;
// single newlines inside a block collapse to spaces at render (CSS white-space:normal).
export const splitParagraphs = text => String(text ?? '')
    .replace(/\r\n?/g, '\n')          // normalize CRLF / lone CR -> LF
    .split(/\n[ \t]*\n/)              // one or more blank lines separate blocks
    .map(b => b.trim())
    .filter(Boolean)

// text -> a full, valid XHTML document string (the fork opens sections as XHTML).
export const txtToXhtml = (text, title) => {
    const clean = String(text ?? '').replace(XML_INVALID, '')
    const paras = splitParagraphs(clean)
    const body = (paras.length ? paras : ['']).map(p => `<p>${escapeXml(p)}</p>`).join('\n')
    return '<?xml version="1.0" encoding="utf-8"?>\n'
        + '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>'
        + escapeXml(title || 'Text')
        + '</title></head><body>' + body + '</body></html>'
}

// Decode the raw bytes. UTF-8 is the default (TextDecoder strips a UTF-8 BOM); sniff a
// UTF-16 BOM too so a "Unicode" .txt saved by Windows Notepad isn't read as mojibake.
export const decodeText = buf => {
    const b = new Uint8Array(buf)
    let enc = 'utf-8'
    if (b.length >= 2 && b[0] === 0xFF && b[1] === 0xFE) enc = 'utf-16le'
    else if (b.length >= 2 && b[0] === 0xFE && b[1] === 0xFF) enc = 'utf-16be'
    try { return new TextDecoder(enc).decode(b) }
    catch (e) { return new TextDecoder('utf-8').decode(b) }
}

// Build the parsed "book" object <foliate-view>.open() consumes — one reflowable
// section. load() returns a blob: URL (paginator fetches it); createDocument() parses
// the SAME string (both must agree). destroy() revokes the URL.
export const makeTextBook = async file => {
    const text = decodeText(await file.arrayBuffer())
    const title = String(file.name || 'Text').replace(/\.[^.]+$/, '')
    const str = txtToXhtml(text, title)
    const blob = new Blob([str], { type: 'application/xhtml+xml' })
    const url = URL.createObjectURL(blob)

    const section = {
        id: 0,
        load: () => url,
        createDocument: () => new DOMParser().parseFromString(str, 'application/xhtml+xml'),
        size: str.length,
    }

    return {
        metadata: { title, author: [] },
        sections: [section],
        toc: [{ label: title, href: '0' }],
        getCover: () => null,
        resolveHref: href => {
            const [a, b] = String(href ?? '0').split('#')
            return { index: Number(a) || 0, anchor: doc => (b ? doc.getElementById(b) : doc.documentElement) }
        },
        splitTOCHref: href => [Number(String(href ?? '0').split('#')[0]) || 0],
        getTOCFragment: doc => doc.documentElement,
        destroy: () => { try { URL.revokeObjectURL(url) } catch (e) { /* already gone */ } },
    }
}
