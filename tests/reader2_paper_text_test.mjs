// reader2_paper_text_test.mjs — headless proof of the PURE TXT helpers in
// resources/reader2/paper_text.js (the TXT format shim, Task 14). Run:
//   node tests/reader2_paper_text_test.mjs
// Verdict via console ("VERDICT: PASS/FAIL") + process exit code.
//
// WHY node, not reader2_logic_harness.qml: the QML harness imports a `.pragma library`
// (Reader2Logic.js) into the QML JS engine; paper_text.js is a browser-context ES module
// that the glue dynamic-imports (like the vendored book-makers). The two JS engines can't
// share a module, so the glue-side pure logic is proven here instead. The end-to-end
// render (open/paginate/appearance) is proven in the browser bench with a real .txt.
//
// [Agent 2 (Claude), biblio]
import { escapeXml, splitParagraphs, txtToXhtml, decodeText } from '../resources/reader2/paper_text.js'

let fails = 0
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) fails++ }

// escapeXml — the four XML-significant chars, null-safe.
check(escapeXml('a & b < c > "d"') === 'a &amp; b &lt; c &gt; &quot;d&quot;', 'escapeXml: escapes & < > "')
check(escapeXml(null) === '', 'escapeXml: null-safe -> ""')
check(escapeXml("plain") === 'plain', 'escapeXml: leaves plain text alone')

// splitParagraphs — blank-line blocks, trimmed, empties dropped; CRLF normalized.
check(splitParagraphs('one\n\ntwo\n\n\nthree').length === 3, 'split: blank-line separated blocks')
check(splitParagraphs('no blank lines here').length === 1, 'split: no blank line -> one block')
check(splitParagraphs('   \n\n   ').length === 0, 'split: whitespace-only -> no blocks')
check(splitParagraphs('a\r\n\r\nb').length === 2, 'split: CRLF blank line splits')
check(splitParagraphs('a\r\rb').length === 2, 'split: lone-CR blank line splits')
check(splitParagraphs(null).length === 0, 'split: null-safe -> []')
check(splitParagraphs('  hi  \n\n  bye  ')[0] === 'hi', 'split: blocks are trimmed')

// txtToXhtml — valid XHTML shell, body + title escaped, invalid XML controls stripped.
const x = txtToXhtml('Hello <b>&</b>\n\nWorld', 'My "Book"')
check(x.startsWith('<?xml version="1.0" encoding="utf-8"?>'), 'xhtml: xml prolog')
check(x.includes('xmlns="http://www.w3.org/1999/xhtml"'), 'xhtml: xhtml namespace')
check(x.includes('<title>My &quot;Book&quot;</title>'), 'xhtml: title escaped')
check(x.includes('<p>Hello &lt;b&gt;&amp;&lt;/b&gt;</p>'), 'xhtml: paragraph body escaped')
check(x.includes('<p>World</p>'), 'xhtml: second paragraph')
// a form-feed / NUL would make DOMParser emit <parsererror>; must be gone.
const ctrl = txtToXhtml('a' + String.fromCharCode(0) + String.fromCharCode(12) + 'b', 't')
check(ctrl.indexOf(String.fromCharCode(0)) < 0 && ctrl.indexOf(String.fromCharCode(12)) < 0,
    'xhtml: strips invalid XML control chars (NUL, form-feed)')
check(ctrl.includes('<p>ab</p>'), 'xhtml: text survives after stripping controls')
// tab / newline are LEGAL XML whitespace — must be preserved (not stripped).
check(txtToXhtml('a\tb', 't').includes('a\tb'), 'xhtml: keeps legal tab whitespace')
// empty input -> one empty paragraph (still a valid, openable doc).
check(txtToXhtml('', 't').includes('<body><p></p></body>'), 'xhtml: empty text -> single empty <p>')
check(txtToXhtml(null, null).includes('<title>Text</title>'), 'xhtml: null title -> "Text" default')

// decodeText — utf-8 default (BOM stripped), utf-16 BOM sniffed.
check(decodeText(new TextEncoder().encode('héllo — dash')) === 'héllo — dash', 'decode: utf-8 round-trip')
check(decodeText(new Uint8Array([0xEF, 0xBB, 0xBF, 0x68, 0x69])) === 'hi', 'decode: utf-8 BOM stripped')
// UTF-16LE BOM + "Hi"
check(decodeText(new Uint8Array([0xFF, 0xFE, 0x48, 0x00, 0x69, 0x00])) === 'Hi', 'decode: utf-16le BOM sniffed')
// UTF-16BE BOM + "Hi"
check(decodeText(new Uint8Array([0xFE, 0xFF, 0x00, 0x48, 0x00, 0x69])) === 'Hi', 'decode: utf-16be BOM sniffed')

console.log(fails ? 'VERDICT: FAIL' : 'VERDICT: PASS')
process.exit(fails ? 1 : 0)
