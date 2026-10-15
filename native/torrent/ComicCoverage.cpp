#include "ComicCoverage.h"

#include <QHash>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace ComicCoverage {
namespace {

using ComicEditionIdentity::ComicCollectionFormat;

// Closed alias vocabulary mirroring ComicEditionIdentity's format table, with
// the irregular/likely plurals spelled out explicitly (same discipline as
// ComicEditionIdentity's own "compendiums"/"omnibuses" entries). The bare
// single-letter "v" alias is deliberately NOT here — it is handled only as a
// last-resort fallback below, so it never competes with a real format token
// that already governs the same clause (e.g. "TPBs v01-v25").
struct AliasEntry {
    ComicCollectionFormat format;
    QString alias;
};

const QVector<AliasEntry>& aliasTable()
{
    static const QVector<AliasEntry> table = {
        { ComicCollectionFormat::Compendium, QStringLiteral("compendium") },
        { ComicCollectionFormat::Compendium, QStringLiteral("compendiums") },
        { ComicCollectionFormat::Omnibus, QStringLiteral("omnibus") },
        { ComicCollectionFormat::Omnibus, QStringLiteral("omnibuses") },
        { ComicCollectionFormat::Omnibus, QStringLiteral("omni") },
        { ComicCollectionFormat::TradePaperback, QStringLiteral("trade paperback") },
        { ComicCollectionFormat::TradePaperback, QStringLiteral("trade paperbacks") },
        { ComicCollectionFormat::TradePaperback, QStringLiteral("tpb") },
        { ComicCollectionFormat::TradePaperback, QStringLiteral("tpbs") },
        { ComicCollectionFormat::TradePaperback, QStringLiteral("trade") },
        { ComicCollectionFormat::TradePaperback, QStringLiteral("trades") },
        { ComicCollectionFormat::Deluxe, QStringLiteral("deluxe edition") },
        { ComicCollectionFormat::Deluxe, QStringLiteral("deluxe editions") },
        { ComicCollectionFormat::Deluxe, QStringLiteral("deluxe") },
        { ComicCollectionFormat::Absolute, QStringLiteral("absolute edition") },
        { ComicCollectionFormat::Absolute, QStringLiteral("absolute editions") },
        { ComicCollectionFormat::Absolute, QStringLiteral("absolute") },
        { ComicCollectionFormat::Hardcover, QStringLiteral("hard cover") },
        { ComicCollectionFormat::Hardcover, QStringLiteral("hard covers") },
        { ComicCollectionFormat::Hardcover, QStringLiteral("hardcover") },
        { ComicCollectionFormat::Hardcover, QStringLiteral("hardcovers") },
        { ComicCollectionFormat::Hardcover, QStringLiteral("hc") },
        { ComicCollectionFormat::Collection, QStringLiteral("collected edition") },
        { ComicCollectionFormat::Collection, QStringLiteral("collected editions") },
        { ComicCollectionFormat::Collection, QStringLiteral("collection") },
        { ComicCollectionFormat::Collection, QStringLiteral("collections") },
        { ComicCollectionFormat::Volume, QStringLiteral("volume") },
        { ComicCollectionFormat::Volume, QStringLiteral("volumes") },
        { ComicCollectionFormat::Volume, QStringLiteral("vol") },
        { ComicCollectionFormat::Volume, QStringLiteral("vols") },
        { ComicCollectionFormat::Book, QStringLiteral("book") },
        { ComicCollectionFormat::Book, QStringLiteral("books") },
    };
    return table;
}

QRegularExpression aliasPattern(const QString& alias)
{
    const QStringList words = alias.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList escaped;
    escaped.reserve(words.size());
    for (const QString& w : words)
        escaped << QRegularExpression::escape(w);
    const QString pattern = QStringLiteral("\\b") + escaped.join(QStringLiteral("\\s+")) + QStringLiteral("\\b");
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

struct TokenMatch {
    int start = -1;
    int end = -1;
    ComicCollectionFormat format = ComicCollectionFormat::Unknown;
};

// All non-overlapping recognized format-token occurrences in `clause`,
// ordered by position. Longer/more-specific aliases (checked first) win a
// tie over a shorter alias that is one of their leading words (e.g. "trade
// paperback" claims its span before "trade" can independently match it).
QList<TokenMatch> findFormatTokens(const QString& clause)
{
    QVector<AliasEntry> table = aliasTable();
    std::sort(table.begin(), table.end(), [](const AliasEntry& a, const AliasEntry& b) {
        const int wordsA = a.alias.count(QLatin1Char(' '));
        const int wordsB = b.alias.count(QLatin1Char(' '));
        if (wordsA != wordsB) return wordsA > wordsB;
        return a.alias.size() > b.alias.size();
    });

    QList<TokenMatch> matches;
    for (const auto& entry : table) {
        const QRegularExpression re = aliasPattern(entry.alias);
        auto it = re.globalMatch(clause);
        while (it.hasNext()) {
            const auto m = it.next();
            const int s = m.capturedStart();
            const int e = m.capturedEnd();
            bool overlaps = false;
            for (const auto& existing : matches) {
                if (s < existing.end && e > existing.start) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps)
                matches.append(TokenMatch{ s, e, entry.format });
        }
    }
    std::sort(matches.begin(), matches.end(), [](const TokenMatch& a, const TokenMatch& b) {
        return a.start < b.start;
    });
    return matches;
}

// Worded ordinals "One".."Twenty" — same closed vocabulary ComicEditionIdentity
// scopes ordinal parsing to, reused here for worded ranges ("Book One-Three").
int wordedNumber(const QString& word)
{
    static const QHash<QString, int> words = {
        { QStringLiteral("one"), 1 },     { QStringLiteral("two"), 2 },
        { QStringLiteral("three"), 3 },   { QStringLiteral("four"), 4 },
        { QStringLiteral("five"), 5 },    { QStringLiteral("six"), 6 },
        { QStringLiteral("seven"), 7 },   { QStringLiteral("eight"), 8 },
        { QStringLiteral("nine"), 9 },    { QStringLiteral("ten"), 10 },
        { QStringLiteral("eleven"), 11 }, { QStringLiteral("twelve"), 12 },
        { QStringLiteral("thirteen"), 13 }, { QStringLiteral("fourteen"), 14 },
        { QStringLiteral("fifteen"), 15 }, { QStringLiteral("sixteen"), 16 },
        { QStringLiteral("seventeen"), 17 }, { QStringLiteral("eighteen"), 18 },
        { QStringLiteral("nineteen"), 19 }, { QStringLiteral("twenty"), 20 },
    };
    return words.value(word.toLower(), -1);
}

// A numeric prefix a range/ordinal may carry immediately after its governing
// format token: "v01", "#1", "No. 1", "Vol. 1" — consumed as part of the
// number itself, never re-matched as an independent format token.
const QString& numericPrefix()
{
    static const QString prefix = QStringLiteral("(?:volume|vol\\.?|no\\.?|#|v)?\\s*");
    return prefix;
}

// Looks for a range or single ordinal anywhere in `window` (the text between
// a format token and the next token/end of clause). Zero-strips into
// canonical decimal ints. Returns false when nothing recognizable is there.
bool numericRangeIn(const QString& window, int& lo, int& hi, QString& evidence)
{
    const QRegularExpression rangeRe(
        numericPrefix() + QStringLiteral("0*(\\d{1,4})\\s*-\\s*") + numericPrefix() + QStringLiteral("0*(\\d{1,4})"),
        QRegularExpression::CaseInsensitiveOption);
    if (const auto m = rangeRe.match(window); m.hasMatch()) {
        lo = m.captured(1).toInt();
        hi = m.captured(2).toInt();
        evidence = m.captured(0).trimmed();
        return true;
    }

    const QRegularExpression singleRe(
        numericPrefix() + QStringLiteral("0*(\\d{1,4})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (const auto m = singleRe.match(window); m.hasMatch()) {
        lo = hi = m.captured(1).toInt();
        evidence = m.captured(0).trimmed();
        return true;
    }

    static const QRegularExpression wordedRe(QStringLiteral("\\b([A-Za-z]+)\\s*-\\s*([A-Za-z]+)\\b"));
    if (const auto m = wordedRe.match(window); m.hasMatch()) {
        const int a = wordedNumber(m.captured(1));
        const int b = wordedNumber(m.captured(2));
        if (a != -1 && b != -1) {
            lo = a;
            hi = b;
            evidence = m.captured(0).trimmed();
            return true;
        }
    }

    // A SINGLE worded ordinal immediately after the format token ("Book One" ->
    // 1). Anchored to the window start so it only reads a number-word that
    // actually governs the format token, never a stray word later in the clause
    // (e.g. "Deluxe Two-Face"). Mirrors ComicEditionIdentity::parseOrdinal, which
    // already reads worded ordinals off the edition title — coverage detection on
    // release filenames must read them the same way, or a "Book One" edition can
    // never match a "Book One" torrent.
    static const QRegularExpression wordedSingleRe(QStringLiteral("^\\s*([A-Za-z]+)\\b"));
    if (const auto m = wordedSingleRe.match(window); m.hasMatch()) {
        const int a = wordedNumber(m.captured(1));
        if (a != -1) {
            lo = hi = a;
            evidence = m.captured(0).trimmed();
            return true;
        }
    }

    return false;
}

// Last-resort shorthand: a bare "v01" / "v01-v03" with NO other recognized
// format token anywhere in the clause. Only reachable when `findFormatTokens`
// found nothing, so this never fires inside "TPBs v01-v25" (TPB already
// governs that number). The negative lookbehind keeps this from matching the
// "v" inside an ordinary word (e.g. "Invincible").
bool bareVolumeRangeIn(const QString& clause, int& lo, int& hi, QString& evidence)
{
    static const QRegularExpression re(
        QStringLiteral("(?<![A-Za-z])v0*(\\d{1,4})(?:\\s*-\\s*v?0*(\\d{1,4}))?"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(clause);
    if (!m.hasMatch())
        return false;
    lo = m.captured(1).toInt();
    hi = m.captured(2).isEmpty() ? lo : m.captured(2).toInt();
    evidence = m.captured(0).trimmed();
    return true;
}

} // namespace

QList<ComicCoverageSpan> detectComicCoverage(const QString& text)
{
    QList<ComicCoverageSpan> spans;

    const QStringList clauses = text.split(QRegularExpression(QStringLiteral("[,;()]+")), Qt::SkipEmptyParts);
    for (const QString& rawClause : clauses) {
        const QString clause = rawClause.trimmed();
        if (clause.isEmpty())
            continue;

        const QList<TokenMatch> tokens = findFormatTokens(clause);
        if (tokens.isEmpty()) {
            int lo = -1, hi = -1;
            QString evidence;
            if (bareVolumeRangeIn(clause, lo, hi, evidence)) {
                ComicCoverageSpan span;
                span.format = ComicCollectionFormat::Volume;
                span.lo = lo;
                span.hi = hi;
                span.evidenceText = evidence;
                spans.append(span);
            }
            continue;
        }

        for (int i = 0; i < tokens.size(); ++i) {
            const TokenMatch& tok = tokens[i];
            const int windowEnd = (i + 1 < tokens.size()) ? tokens[i + 1].start : clause.size();
            if (tok.end >= windowEnd)
                continue;
            const QString window = clause.mid(tok.end, windowEnd - tok.end);

            int lo = -1, hi = -1;
            QString evidence;
            if (numericRangeIn(window, lo, hi, evidence)) {
                ComicCoverageSpan span;
                span.format = tok.format;
                span.lo = lo;
                span.hi = hi;
                span.evidenceText = clause;
                spans.append(span);
            }
        }
    }

    return spans;
}

bool coverageCovers(const QList<ComicCoverageSpan>& spans, ComicCollectionFormat format, int ordinal)
{
    for (const auto& span : spans)
        if (span.format == format && span.lo <= ordinal && ordinal <= span.hi)
            return true;
    return false;
}

} // namespace ComicCoverage
