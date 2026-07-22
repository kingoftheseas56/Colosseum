#include "EpubTextIndexer.h"

#include <QXmlStreamReader>
#include <QCryptographicHash>
#include <QSet>
#include <QHash>
#include <QStringList>
#include <QPair>
#include <cstring>

// Vendored single-file ZIP reader (public domain). Opens the EPUB container without
// a Qt private API, per the approved design.
#include "third_party/miniz/miniz.h"

namespace alignment {

// ── Shared canonical fold (mirror of alignment_text.js::canonicalFold) ────────

static bool isCanonWs(char32_t cp) {
    switch (cp) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0:
        case 0x1680: case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
        case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009: case 0x200A:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return false;
    }
}

static QString foldChar(char32_t cp) {
    switch (cp) {
        case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: return QStringLiteral("'");
        case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: return QStringLiteral("\"");
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212:
            return QStringLiteral("-");
        case 0x2026: return QStringLiteral("...");
        default: break;
    }
    if (cp >= 0x41 && cp <= 0x5A) return QString(QChar(cp + 32)); // ASCII upper -> lower
    char32_t c = cp;
    return QString::fromUcs4(&c, 1);
}

CanonicalFold canonicalFold(const QString &src) {
    CanonicalFold out;
    bool pendingSpace = false, emitted = false;
    int spaceSrc = -1;
    const int n = src.size();
    int i = 0;
    while (i < n) {
        char32_t cp;
        int width;
        const QChar c0 = src.at(i);
        if (c0.isHighSurrogate() && i + 1 < n && src.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(c0, src.at(i + 1));
            width = 2;
        } else {
            cp = c0.unicode();
            width = 1;
        }
        if (isCanonWs(cp)) {
            if (emitted && !pendingSpace) { pendingSpace = true; spaceSrc = i; }
            i += width;
            continue;
        }
        // NFD-decompose this code point and drop non-spacing marks.
        char32_t cp32 = cp;
        const QString decomp = QString::fromUcs4(&cp32, 1).normalized(QString::NormalizationForm_D);
        for (const QChar &dch : decomp) {
            if (QChar::category(dch.unicode()) == QChar::Mark_NonSpacing) continue;
            const QString folded = foldChar(dch.unicode());
            for (const QChar &fc : folded) {
                if (pendingSpace) { out.canonical.append(QChar(u' ')); out.map.append(spaceSrc); pendingSpace = false; }
                out.canonical.append(fc);
                out.map.append(i);
                emitted = true;
            }
        }
        i += width;
    }
    return out;
}

// ── Sentence segmentation ─────────────────────────────────────────────────────

QList<IndexedSentence> segmentSentences(const QString &canonical, const QString &href) {
    static const QSet<QString> kAbbrev = {
        QStringLiteral("mr"), QStringLiteral("mrs"), QStringLiteral("ms"), QStringLiteral("dr"),
        QStringLiteral("st"), QStringLiteral("jr"), QStringLiteral("sr"), QStringLiteral("prof"),
        QStringLiteral("vs"), QStringLiteral("vol"), QStringLiteral("no"), QStringLiteral("fig"),
        QStringLiteral("etc"), QStringLiteral("dept"), QStringLiteral("mt"), QStringLiteral("rev"),
        QStringLiteral("gen"), QStringLiteral("col"), QStringLiteral("capt"), QStringLiteral("sgt"),
        QStringLiteral("co"), QStringLiteral("inc"), QStringLiteral("ltd"),
    };
    const int n = canonical.size();
    auto isSpace = [&](int i) { return i >= 0 && i < n && canonical.at(i) == QChar(u' '); };
    auto isDigit = [&](int i) { return i >= 0 && i < n && canonical.at(i).isDigit(); };

    QList<int> boundaries; // index just AFTER a terminal punctuation
    for (int p = 0; p < n; ++p) {
        const QChar ch = canonical.at(p);
        if (ch == QChar(u'!') || ch == QChar(u'?')) {
            if (p + 1 == n || isSpace(p + 1)) boundaries.append(p + 1);
        } else if (ch == QChar(u'.')) {
            if (p + 1 < n && canonical.at(p + 1) == QChar(u'.')) continue;   // ellipsis interior
            if (p > 0 && canonical.at(p - 1) == QChar(u'.')) continue;        // ellipsis interior/end
            if (!(p + 1 == n || isSpace(p + 1))) continue;                    // not sentence-final
            if (isDigit(p - 1) && isDigit(p + 1)) continue;                   // decimal
            int j = p - 1;
            while (j >= 0 && canonical.at(j).isLetter()) --j;
            const QString token = canonical.mid(j + 1, p - (j + 1)).toLower();
            if (!token.isEmpty() && kAbbrev.contains(token)) continue;        // abbreviation
            boundaries.append(p + 1);
        }
    }

    QList<QPair<int, int>> segs;
    int start = 0;
    for (int b : boundaries) { segs.append({start, b}); start = b; }
    if (start < n) segs.append({start, n});

    QList<IndexedSentence> out;
    for (const auto &seg : segs) {
        int a = seg.first, b = seg.second;
        while (a < b && canonical.at(a) == QChar(u' ')) ++a;
        while (b > a && canonical.at(b - 1) == QChar(u' ')) --b;
        if (a >= b) continue;
        IndexedSentence s;
        s.spineHref = href;
        s.canonicalStart = a;
        s.canonicalEnd = b;
        s.text = canonical.mid(a, b - a);
        s.sentenceHash = QString::fromLatin1(
            QCryptographicHash::hash(s.text.toUtf8(), QCryptographicHash::Sha256).toHex());
        out.append(s);
    }
    return out;
}

// ── EPUB container / OPF / XHTML ──────────────────────────────────────────────

namespace {

QByteArray readZipEntry(mz_zip_archive *zip, const QString &name) {
    size_t sz = 0;
    void *p = mz_zip_reader_extract_file_to_heap(zip, name.toUtf8().constData(), &sz, 0);
    if (!p) return QByteArray();
    QByteArray data(reinterpret_cast<const char *>(p), static_cast<int>(sz));
    mz_free(p);
    return data;
}

QString findOpfPath(const QByteArray &container) {
    QXmlStreamReader r(container);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement
            && r.name().toString().compare(QLatin1String("rootfile"), Qt::CaseInsensitive) == 0) {
            const QString fp = r.attributes().value(QLatin1String("full-path")).toString();
            if (!fp.isEmpty()) return fp;
        }
    }
    return QString();
}

QString opfDir(const QString &opfPath) {
    const int slash = opfPath.lastIndexOf(QChar(u'/'));
    return slash < 0 ? QString() : opfPath.left(slash);
}

QString joinPath(const QString &dir, const QString &href) {
    const QString p = dir.isEmpty() ? href : dir + QChar(u'/') + href;
    QStringList norm;
    for (const QString &part : p.split(QChar(u'/'))) {
        if (part.isEmpty() || part == QLatin1String(".")) continue;
        if (part == QLatin1String("..")) { if (!norm.isEmpty()) norm.removeLast(); continue; }
        norm.append(part);
    }
    return norm.join(QChar(u'/'));
}

QStringList parseOpfSpine(const QByteArray &opf, const QString &dir) {
    QHash<QString, QString> href, media;
    QStringList spine;
    QXmlStreamReader r(opf);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        const QString tag = r.name().toString().toLower();
        if (tag == QLatin1String("item")) {
            const QString id = r.attributes().value(QLatin1String("id")).toString();
            if (id.isEmpty()) continue;
            href.insert(id, r.attributes().value(QLatin1String("href")).toString());
            media.insert(id, r.attributes().value(QLatin1String("media-type")).toString());
        } else if (tag == QLatin1String("itemref")) {
            const QString idref = r.attributes().value(QLatin1String("idref")).toString();
            if (!idref.isEmpty()) spine.append(idref);
        }
    }
    QStringList out;
    for (const QString &idref : spine) {
        if (!href.contains(idref)) continue;
        const QString h = href.value(idref);
        const QString mt = media.value(idref);
        const bool xhtml = mt.contains(QLatin1String("xhtml"))
            || h.endsWith(QLatin1String(".xhtml"), Qt::CaseInsensitive)
            || h.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
            || h.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive);
        if (xhtml) out.append(joinPath(dir, h));
    }
    return out;
}

QString extractDisplay(const QByteArray &xhtml) {
    static const QSet<QString> kBlock = {
        QStringLiteral("P"), QStringLiteral("DIV"), QStringLiteral("BR"), QStringLiteral("LI"),
        QStringLiteral("UL"), QStringLiteral("OL"), QStringLiteral("TABLE"), QStringLiteral("TR"),
        QStringLiteral("TD"), QStringLiteral("TH"), QStringLiteral("SECTION"), QStringLiteral("ARTICLE"),
        QStringLiteral("ASIDE"), QStringLiteral("HEADER"), QStringLiteral("FOOTER"), QStringLiteral("NAV"),
        QStringLiteral("FIGURE"), QStringLiteral("FIGCAPTION"), QStringLiteral("BLOCKQUOTE"),
        QStringLiteral("PRE"), QStringLiteral("HR"), QStringLiteral("DL"), QStringLiteral("DT"),
        QStringLiteral("DD"), QStringLiteral("H1"), QStringLiteral("H2"), QStringLiteral("H3"),
        QStringLiteral("H4"), QStringLiteral("H5"), QStringLiteral("H6"), QStringLiteral("MAIN"),
        QStringLiteral("ADDRESS"), QStringLiteral("CAPTION"), QStringLiteral("HGROUP"),
    };
    static const QSet<QString> kSkip = {
        QStringLiteral("SCRIPT"), QStringLiteral("STYLE"), QStringLiteral("HEAD"),
        QStringLiteral("TEMPLATE"), QStringLiteral("NOSCRIPT"),
    };
    QString text;
    auto pushSep = [&]() {
        if (!text.isEmpty() && text.at(text.size() - 1) != QChar(u'\n')) text.append(QChar(u'\n'));
    };
    QXmlStreamReader r(xhtml);
    QList<bool> blockStack;
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const QString tag = r.name().toString().toUpper();
            if (kSkip.contains(tag)) { r.skipCurrentElement(); continue; }
            if (r.attributes().value(QLatin1String("aria-hidden")) == QLatin1String("true")) {
                r.skipCurrentElement();
                continue;
            }
            const bool block = kBlock.contains(tag);
            if (block) pushSep();
            blockStack.append(block);
        } else if (t == QXmlStreamReader::EndElement) {
            if (!blockStack.isEmpty() && blockStack.takeLast()) pushSep();
        } else if (t == QXmlStreamReader::Characters) {
            text.append(r.text().toString());
        }
    }
    return text;
}

} // namespace

EpubIndex EpubTextIndexer::index(const QString &epubPath) const {
    EpubIndex idx;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, epubPath.toLocal8Bit().constData(), 0)) {
        idx.error = QStringLiteral("cannot open epub zip");
        return idx;
    }

    const QByteArray container = readZipEntry(&zip, QStringLiteral("META-INF/container.xml"));
    if (container.isEmpty()) { idx.error = QStringLiteral("missing META-INF/container.xml"); mz_zip_reader_end(&zip); return idx; }
    const QString opfPath = findOpfPath(container);
    if (opfPath.isEmpty()) { idx.error = QStringLiteral("no rootfile in container"); mz_zip_reader_end(&zip); return idx; }
    const QByteArray opf = readZipEntry(&zip, opfPath);
    if (opf.isEmpty()) { idx.error = QStringLiteral("missing opf: ") + opfPath; mz_zip_reader_end(&zip); return idx; }

    const QStringList spineHrefs = parseOpfSpine(opf, opfDir(opfPath));
    for (const QString &href : spineHrefs) {
        const QByteArray xhtml = readZipEntry(&zip, href);
        if (xhtml.isEmpty()) continue;
        SpineDocument d;
        d.href = href;
        d.displaySource = extractDisplay(xhtml);
        const CanonicalFold cf = canonicalFold(d.displaySource);
        d.canonical = cf.canonical;
        d.map = cf.map;
        d.sentences = segmentSentences(d.canonical, href);
        idx.documents.append(d);
    }
    mz_zip_reader_end(&zip);

    idx.ok = !idx.documents.isEmpty();
    if (!idx.ok && idx.error.isEmpty()) idx.error = QStringLiteral("no spine documents indexed");
    return idx;
}

} // namespace alignment
