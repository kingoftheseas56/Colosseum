#pragma once

// EpubTextIndexer — the authoritative source of the EPUB's displayed words.
//
// One small thing: open a paired EPUB, walk its spine XHTML, and turn the visible
// text into a stable canonical stream plus canonical sentence locations — the exact
// same fold resources/reader2/alignment_text.js applies to the live DOM. Because both
// sides fold identically, a stored canonical offset resolves to the right on-page
// range without ever depending on generated element ids. The EPUB is the only source
// of displayed text; recognition output is matching evidence only.
//
// A focused ZIP reader (vendored miniz) opens the container rather than a Qt private
// API. Consumes Qt Core only. Not a QObject — pure extraction the service drives.
//
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"

#include <QString>
#include <QList>
#include <QVector>

namespace alignment {

// A canonical sentence located inside one spine document.
struct IndexedSentence {
    QString spineHref;
    qint64 canonicalStart = 0;
    qint64 canonicalEnd = 0;
    QString sentenceHash;   // SHA-256 (hex) of the canonical sentence text
    QString text;           // canonical sentence text (matching evidence, not display)
};

// One spine document's extracted text and its canonical form.
struct SpineDocument {
    QString href;
    QString displaySource;  // extracted visible text, blocks joined by '\n'
    QString canonical;      // canonical fold of displaySource
    QVector<int> map;       // map[k] = displaySource index of canonical char k
    QList<IndexedSentence> sentences;
};

struct EpubIndex {
    bool ok = false;
    QString error;
    QList<SpineDocument> documents;
};

// The shared canonical fold. MUST stay byte-for-byte identical to
// alignment_text.js::canonicalFold: per code point NFD-decompose and drop
// non-spacing marks; fold curly quotes/dashes/ellipsis and ASCII case; collapse
// whitespace runs (incl. NBSP) to a single space, trimming ends.
struct CanonicalFold {
    QString canonical;
    QVector<int> map;       // map[k] = index in displayText of canonical char k
};
CanonicalFold canonicalFold(const QString &displayText);

// Segment a canonical stream into sentences. A '.' does not end a sentence inside a
// decimal (3.14), an abbreviation (Dr.), or an ellipsis (...); '!' and '?' end when
// followed by whitespace or end of text; the trailing run is the last sentence.
QList<IndexedSentence> segmentSentences(const QString &canonical, const QString &spineHref);

class EpubTextIndexer {
public:
    EpubIndex index(const QString &epubPath) const;
};

} // namespace alignment
