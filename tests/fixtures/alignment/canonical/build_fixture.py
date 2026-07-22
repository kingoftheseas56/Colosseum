#!/usr/bin/env python3
"""Regenerate the canonical-index fixture: fixture.epub + expected.json.

The EPUB packs one XHTML spine document whose visible text exercises every
canonicalization edge the read-along must fold identically in C++ and JS:
NFC/NFD, curly/straight quotes, en/em/minus dashes, ellipsis, non-breaking space,
"Dr." abbreviation, "3.14" decimal, and a word split across an inline <em>. It also
carries a <script> and an aria-hidden span that must be skipped.

expected.json's canonical stream is produced by the REAL resources/reader2/
alignment_text.js (via node), so the JS test guards it as a regression while the C++
harness must independently reproduce it -- that mutual match is the parity proof.

Run:  python tests/fixtures/alignment/canonical/build_fixture.py
Needs: python 3, node.  [Agent 2 (Claude), biblio]

All non-ASCII characters are written as \\u escapes so the source stays unambiguous.
"""
import io, json, os, subprocess, sys, tempfile, hashlib, zipfile, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
ALIGN_JS = os.path.join(REPO, "resources", "reader2", "alignment_text.js")

# - the four visible paragraphs (exact code points, \u-escaped) ---------------
LDQUO, RDQUO = "“", "”"      # curly double quotes
LSQUO, RSQUO = "‘", "’"      # curly single quotes
HELLIP = "…"                       # horizontal ellipsis
MDASH = "—"                        # em dash
NBSP = " "                         # non-breaking space

P1 = "Dr. Smith paid $3.14 for a " + LDQUO + "quiet" + RDQUO + " book."
P2 = "He said, " + LSQUO + "Wait" + HELLIP + RSQUO + " " + MDASH + " really?"
P3 = unicodedata.normalize("NFD", "Café façade")   # -> e+U0301, c+U0327
P4 = "A" + NBSP + "B"
PARAS = [P1, P2, P3, P4]

# The paper/indexer assemble block text with a '\n' separator between blocks.
DISPLAY_SOURCE = "\n".join(PARAS)

# ch1 XHTML derives its visible text from the SAME constants (no divergence): P2's
# "really" is split across an inline <em>; a <script> and an aria-hidden span must
# contribute NOTHING to the extracted text.
P2_HTML = P2.replace("really", "re<em>al</em>ly")
CH1 = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<html xmlns="http://www.w3.org/1999/xhtml">\n'
    '<head><title>Chapter 1</title></head>\n'
    '<body>\n'
    f'<p>{P1}<span aria-hidden="true">SKIPME</span></p>\n'
    '<script>var skip = 1;</script>\n'
    f'<p>{P2_HTML}</p>\n'
    f'<p>{P3}</p>\n'
    f'<p>{P4}</p>\n'
    '</body>\n</html>\n'
)

CONTAINER = (
    '<?xml version="1.0"?>\n'
    '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
    '  <rootfiles>\n'
    '    <rootfile full-path="content.opf" media-type="application/oebps-package+xml"/>\n'
    '  </rootfiles>\n'
    '</container>\n'
)
OPF = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">\n'
    '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
    '    <dc:title>Alignment Fixture</dc:title>\n'
    '    <dc:language>en</dc:language>\n'
    '    <dc:identifier id="bookid">urn:uuid:fixture-align-1</dc:identifier>\n'
    '  </metadata>\n'
    '  <manifest>\n'
    '    <item id="ch1" href="Text/ch1.xhtml" media-type="application/xhtml+xml"/>\n'
    '  </manifest>\n'
    '  <spine>\n'
    '    <itemref idref="ch1"/>\n'
    '  </spine>\n'
    '</package>\n'
)


def build_epub(path):
    with zipfile.ZipFile(path, "w") as z:
        zi = zipfile.ZipInfo("mimetype")               # OCF: mimetype first, STORED
        zi.compress_type = zipfile.ZIP_STORED
        z.writestr(zi, "application/epub+zip")
        z.writestr("META-INF/container.xml", CONTAINER, zipfile.ZIP_DEFLATED)
        z.writestr("content.opf", OPF, zipfile.ZIP_DEFLATED)
        z.writestr("Text/ch1.xhtml", CH1, zipfile.ZIP_DEFLATED)


def canonical_from_js(display):
    """Run the real alignment_text.js canonicalFold over `display` (via node)."""
    align_url = "file:///" + ALIGN_JS.replace("\\", "/")
    node_src = (
        "import { canonicalFold } from %s;\n"
        "import { readFileSync } from 'node:fs';\n"
        "const display = readFileSync(process.argv[2], 'utf8');\n"
        "process.stdout.write(JSON.stringify(canonicalFold(display)));\n"
    ) % json.dumps(align_url)
    with tempfile.TemporaryDirectory() as td:
        mjs = os.path.join(td, "gen.mjs")
        dsrc = os.path.join(td, "display.txt")
        with open(mjs, "w", encoding="utf-8") as f:
            f.write(node_src)
        # newline="" so Python does NOT translate '\n' -> '\r\n' on Windows, which
        # would shift every source offset after a line break.
        with open(dsrc, "w", encoding="utf-8", newline="") as f:
            f.write(display)
        out = subprocess.check_output(["node", mjs, dsrc])
        return json.loads(out.decode("utf-8"))


def main():
    build_epub(os.path.join(HERE, "fixture.epub"))
    folded = canonical_from_js(DISPLAY_SOURCE)
    canonical = folded["canonical"]

    # Expected 3-sentence partition (abbreviation, decimal, and ellipsis do NOT split).
    s0 = 'dr. smith paid $3.14 for a "quiet" book.'
    s1 = "he said, 'wait...' - really?"
    s2 = "cafe facade a b"
    assert canonical == s0 + " " + s1 + " " + s2, "canonical mismatch:\n%r" % (canonical,)

    sentences, cursor = [], 0
    for text in (s0, s1, s2):
        start = canonical.index(text, cursor)
        end = start + len(text)
        cursor = end
        sentences.append({
            "spineHref": "Text/ch1.xhtml",
            "canonicalStart": start,
            "canonicalEnd": end,
            "sentenceHash": hashlib.sha256(text.encode("utf-8")).hexdigest(),
            "text": text,
        })

    # A few canonical->display offset spot-checks (map[k] == display index).
    m = folded["map"]
    checks = []
    for token in ("smith", "quiet", "really", "cafe", "facade"):
        ci = canonical.index(token)
        checks.append({"canonicalIndex": ci, "displayIndex": m[ci], "note": token})

    expected = {
        "spineHref": "Text/ch1.xhtml",
        "displaySource": DISPLAY_SOURCE,
        "canonical": canonical,
        "sentences": sentences,
        "mapChecks": checks,
    }
    with open(os.path.join(HERE, "expected.json"), "w", encoding="utf-8") as f:
        json.dump(expected, f, ensure_ascii=False, indent=2)
    print("OK fixture.epub + expected.json")
    print("canonical:", repr(canonical))
    print("sentences:", len(sentences), "mapChecks:", checks)


if __name__ == "__main__":
    main()
