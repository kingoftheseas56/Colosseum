#!/usr/bin/env python3
"""Regenerate the end-to-end pipeline EPUB fixtures: passage.epub + unrelated.epub.

passage.epub carries EXACTLY the multi-sentence passage the SAPI clip
tests/fixtures/alignment/audio/passage.wav narrates:

    "The old lighthouse keeper carefully polished his enormous brass lantern
     every single evening. Distant sailors navigating the frozen northern waters
     trusted that steady golden beam completely. It guided their weary wooden
     vessels safely homeward through treacherous midnight fog and violent storms."

A distinctive ~41-word / three-sentence passage (clearly-articulated, low-collision
vocabulary) so the coarse whisper transcript gives EpubSequenceMatcher several unique,
monotonic, non-overlapping 4-token anchors and it accepts the chapter (matched=true).
This is the realistic shape of a real audiobook chapter — the pipeline's TRUE matched
path (per-region forced alignment) is proven against it end-to-end.

The audio is regenerated with the same recipe the other alignment fixtures use:
  powershell -NoProfile -Command "Add-Type -AssemblyName System.Speech; \
    $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.Rate = -1; \
    $s.SetOutputToWaveFile('<abs>/passage.wav'); $s.Speak('<the passage>'); $s.Dispose()"
(slower Rate=-1 for crisp diction). The EPUB text below MUST match that Speak() string.

unrelated.epub carries a DIFFERENT passage whose words never appear in the audio. It is
the mismatch fixture: the same audio against this book shares too few phrases for the
matcher to anchor, so the matcher rejects it (matched=false) and the chapter records
CouldntSync / edition_mismatch — no fabricated cues. The matcher's own rejection is the
discriminator; there is no honesty threshold in the pipeline.

Each EPUB is a minimal, valid OCF ZIP: a STORED `mimetype` first, then
META-INF/container.xml, a package OPF, and one XHTML spine document — the same shape as
tests/fixtures/alignment/canonical/build_fixture.py.

Run:  python tests/fixtures/alignment/pipeline/make_passage_epub.py
Needs: python 3 only.  [Agent 2 (Claude), biblio]
"""
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

# MUST match, word-for-word, the string handed to SAPI's Speak() for passage.wav.
PASSAGE = (
    "The old lighthouse keeper carefully polished his enormous brass lantern every "
    "single evening. Distant sailors navigating the frozen northern waters trusted "
    "that steady golden beam completely. It guided their weary wooden vessels safely "
    "homeward through treacherous midnight fog and violent storms."
)
UNRELATED = (
    "Colorless green ideas sleep furiously beneath a silver autumn moon. Bureaucratic "
    "committees quarrel endlessly about quarterly budget spreadsheets. Purple elephants "
    "waltz gracefully across the abandoned railway platform at dawn."
)

# The SKIPPED-NARRATION book: the SAME passage plus one extra sentence the audio never
# speaks, inserted BETWEEN the two narrated sentences 2 and 3. Its words are distinctive and
# share no 4-token phrase with the narration, so the matcher classifies it as a BookOnly gap
# that SPLITS the surrounding narration into two Aligned regions. This is the genuine
# audiobook case where the book contains text the reader skipped. It is the fixture for the
# pipeline's per-region ASSEMBLY proof: the pipeline aligns both regions, rebases sentence
# ordinals CONTINUOUSLY across the gap (the base>0 path), and carries the BookOnly gap through
# with no fabricated cues over it. (The audio is passage.wav — the full narration.)
_SKIP_INSERT = (
    "Meanwhile a mischievous tabby cat chased crimson dragonflies around the dusty "
    "village courtyard."
)
PASSAGE_SKIP = (
    "The old lighthouse keeper carefully polished his enormous brass lantern every "
    "single evening. Distant sailors navigating the frozen northern waters trusted "
    "that steady golden beam completely. " + _SKIP_INSERT + " It guided their weary "
    "wooden vessels safely homeward through treacherous midnight fog and violent storms."
)

CONTAINER = (
    '<?xml version="1.0"?>\n'
    '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
    '  <rootfiles>\n'
    '    <rootfile full-path="content.opf" media-type="application/oebps-package+xml"/>\n'
    '  </rootfiles>\n'
    '</container>\n'
)


def opf(title, book_id):
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">\n'
        '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        f'    <dc:title>{title}</dc:title>\n'
        '    <dc:language>en</dc:language>\n'
        f'    <dc:identifier id="bookid">urn:uuid:{book_id}</dc:identifier>\n'
        '  </metadata>\n'
        '  <manifest>\n'
        '    <item id="ch1" href="Text/ch1.xhtml" media-type="application/xhtml+xml"/>\n'
        '  </manifest>\n'
        '  <spine>\n'
        '    <itemref idref="ch1"/>\n'
        '  </spine>\n'
        '</package>\n'
    )


def xhtml(passage):
    # Split the passage into one <p> per sentence so the extracted display text carries
    # real paragraph breaks (still one spine document, one canonical stream).
    paras = "".join(
        f'<p>{s.strip()}.</p>\n' for s in passage.rstrip(".").split(". ")
    )
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        '<head><title>Chapter 1</title></head>\n'
        '<body>\n'
        f'{paras}'
        '</body>\n</html>\n'
    )


def build_epub(path, title, book_id, passage):
    with zipfile.ZipFile(path, "w") as z:
        zi = zipfile.ZipInfo("mimetype")            # OCF: mimetype first, STORED
        zi.compress_type = zipfile.ZIP_STORED
        z.writestr(zi, "application/epub+zip")
        z.writestr("META-INF/container.xml", CONTAINER, zipfile.ZIP_DEFLATED)
        z.writestr("content.opf", opf(title, book_id), zipfile.ZIP_DEFLATED)
        z.writestr("Text/ch1.xhtml", xhtml(passage), zipfile.ZIP_DEFLATED)


def main():
    build_epub(os.path.join(HERE, "passage.epub"),
               "Lighthouse Alignment Fixture", "pipeline-passage-1", PASSAGE)
    build_epub(os.path.join(HERE, "passage_skip.epub"),
               "Lighthouse Skip Fixture", "pipeline-passage-skip-1", PASSAGE_SKIP)
    build_epub(os.path.join(HERE, "unrelated.epub"),
               "Unrelated Alignment Fixture", "pipeline-unrelated-1", UNRELATED)
    print("OK passage.epub      :", repr(PASSAGE))
    print("OK passage_skip.epub :", repr(PASSAGE_SKIP))
    print("OK unrelated.epub    :", repr(UNRELATED))


if __name__ == "__main__":
    main()
