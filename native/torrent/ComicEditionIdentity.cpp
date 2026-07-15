#include "ComicEditionIdentity.h"

#include <QHash>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QVector>

#include <algorithm>

namespace ComicEditionIdentity {
namespace {

QString collapsedWhitespace(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value.trimmed();
}

QString digitsOf(const QString& value)
{
    QString out;
    for (const QChar& c : value)
        if (c.isDigit()) out.append(c);
    return out;
}

// Closed alias table (spec: "Canonical edition identity" / format normalization).
// Order matches the design doc; the canonical name is alias[0].
const QVector<QPair<ComicCollectionFormat, QStringList>>& formatTable()
{
    static const QVector<QPair<ComicCollectionFormat, QStringList>> table = {
        { ComicCollectionFormat::Compendium,      { QStringLiteral("compendium"), QStringLiteral("compendiums") } },
        { ComicCollectionFormat::Omnibus,         { QStringLiteral("omnibus"), QStringLiteral("omnibuses"), QStringLiteral("omni") } },
        { ComicCollectionFormat::TradePaperback,  { QStringLiteral("tpb"), QStringLiteral("trade"), QStringLiteral("trade paperback") } },
        { ComicCollectionFormat::Deluxe,          { QStringLiteral("deluxe"), QStringLiteral("deluxe edition") } },
        { ComicCollectionFormat::Absolute,        { QStringLiteral("absolute"), QStringLiteral("absolute edition") } },
        { ComicCollectionFormat::Hardcover,       { QStringLiteral("hc"), QStringLiteral("hardcover"), QStringLiteral("hard cover") } },
        { ComicCollectionFormat::Collection,      { QStringLiteral("collection"), QStringLiteral("collected edition") } },
        { ComicCollectionFormat::Volume,          { QStringLiteral("vol"), QStringLiteral("volume"), QStringLiteral("v") } },
        { ComicCollectionFormat::Book,            { QStringLiteral("book") } },
    };
    return table;
}

// Aliases for one format, longest phrase (most words, then most characters)
// first so a multi-word alias is preferred over a shorter alias that is one
// of its leading words (e.g. "deluxe edition" before "deluxe").
QStringList aliasesFor(ComicCollectionFormat format)
{
    QStringList aliases;
    for (const auto& entry : formatTable())
        if (entry.first == format) aliases = entry.second;
    std::sort(aliases.begin(), aliases.end(), [](const QString& a, const QString& b) {
        const int wordsA = a.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        const int wordsB = b.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        if (wordsA != wordsB) return wordsA > wordsB;
        return a.size() > b.size();
    });
    return aliases;
}

QRegularExpression wordBoundaryPattern(const QString& alias)
{
    const QStringList words = alias.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList escaped;
    escaped.reserve(words.size());
    for (const QString& w : words)
        escaped << QRegularExpression::escape(w);
    const QString pattern = QStringLiteral("\\b") + escaped.join(QStringLiteral("\\s+")) + QStringLiteral("\\b");
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

// Worded ordinals "One".."Twenty", scoped to the token that precedes them.
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

// Roman numerals I..XX, longest token first so "XIX"/"XX" aren't cut short by "X".
int romanNumber(const QString& token)
{
    static const QVector<QPair<QString, int>> table = [] {
        QVector<QPair<QString, int>> t = {
            { QStringLiteral("I"), 1 },     { QStringLiteral("II"), 2 },
            { QStringLiteral("III"), 3 },   { QStringLiteral("IV"), 4 },
            { QStringLiteral("V"), 5 },     { QStringLiteral("VI"), 6 },
            { QStringLiteral("VII"), 7 },   { QStringLiteral("VIII"), 8 },
            { QStringLiteral("IX"), 9 },    { QStringLiteral("X"), 10 },
            { QStringLiteral("XI"), 11 },   { QStringLiteral("XII"), 12 },
            { QStringLiteral("XIII"), 13 }, { QStringLiteral("XIV"), 14 },
            { QStringLiteral("XV"), 15 },   { QStringLiteral("XVI"), 16 },
            { QStringLiteral("XVII"), 17 }, { QStringLiteral("XVIII"), 18 },
            { QStringLiteral("XIX"), 19 },  { QStringLiteral("XX"), 20 },
        };
        std::sort(t.begin(), t.end(), [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
            return a.first.size() > b.first.size();
        });
        return t;
    }();
    const QString upper = token.toUpper();
    for (const auto& entry : table)
        if (upper == entry.first) return entry.second;
    return -1;
}

// Attempts to read an ordinal starting at `rest` (already trimmed of the
// leading token + whitespace). Only a well-formed prefix is required — text
// after the parsed ordinal is ignored.
int ordinalFromRest(const QString& rest)
{
    if (rest.isEmpty()) return -1;

    static const QRegularExpression hashRe(QStringLiteral("^#\\s*(\\d{1,4})\\b"));
    if (const auto m = hashRe.match(rest); m.hasMatch())
        return m.captured(1).toInt();

    static const QRegularExpression noRe(QStringLiteral("^No\\.?\\s*(\\d{1,4})\\b"),
                                          QRegularExpression::CaseInsensitiveOption);
    if (const auto m = noRe.match(rest); m.hasMatch())
        return m.captured(1).toInt();

    static const QRegularExpression digitsRe(QStringLiteral("^(\\d{1,4})\\b"));
    if (const auto m = digitsRe.match(rest); m.hasMatch())
        return m.captured(1).toInt();

    static const QRegularExpression romanRe(QStringLiteral("^([IVXivx]+)\\b"));
    if (const auto m = romanRe.match(rest); m.hasMatch()) {
        const int value = romanNumber(m.captured(1));
        if (value != -1) return value;
    }

    static const QRegularExpression wordRe(QStringLiteral("^([A-Za-z]+)\\b"));
    if (const auto m = wordRe.match(rest); m.hasMatch()) {
        const int value = wordedNumber(m.captured(1));
        if (value != -1) return value;
    }

    return -1;
}

} // namespace

ComicCollectionFormat parseFormat(const QString& text)
{
    const QString norm = collapsedWhitespace(text).toLower();
    if (norm.isEmpty()) return ComicCollectionFormat::Unknown;
    for (const auto& entry : formatTable())
        for (const QString& alias : entry.second)
            if (norm == alias) return entry.first;
    return ComicCollectionFormat::Unknown;
}

QString formatName(ComicCollectionFormat format)
{
    switch (format) {
    case ComicCollectionFormat::Compendium:     return QStringLiteral("Compendium");
    case ComicCollectionFormat::Omnibus:        return QStringLiteral("Omnibus");
    case ComicCollectionFormat::TradePaperback: return QStringLiteral("TradePaperback");
    case ComicCollectionFormat::Deluxe:         return QStringLiteral("Deluxe");
    case ComicCollectionFormat::Absolute:       return QStringLiteral("Absolute");
    case ComicCollectionFormat::Hardcover:      return QStringLiteral("Hardcover");
    case ComicCollectionFormat::Collection:     return QStringLiteral("Collection");
    case ComicCollectionFormat::Volume:         return QStringLiteral("Volume");
    case ComicCollectionFormat::Book:           return QStringLiteral("Book");
    case ComicCollectionFormat::Unknown:        break;
    }
    return QStringLiteral("Unknown");
}

int parseOrdinal(const QString& title, ComicCollectionFormat format)
{
    if (format == ComicCollectionFormat::Unknown) return -1;

    for (const QString& alias : aliasesFor(format)) {
        const QRegularExpression re = wordBoundaryPattern(alias);
        auto it = re.globalMatch(title);
        while (it.hasNext()) {
            const auto m = it.next();
            int i = m.capturedEnd();
            while (i < title.size() && title.at(i).isSpace()) ++i;
            const int ordinal = ordinalFromRest(title.mid(i));
            if (ordinal != -1) return ordinal;
        }
    }
    return -1;
}

namespace {

// Detects which format (if any) is mentioned anywhere in a title, preferring
// the earliest (leftmost) recognized token so reading order decides ties.
ComicCollectionFormat detectFormatInTitle(const QString& title)
{
    ComicCollectionFormat best = ComicCollectionFormat::Unknown;
    int bestPos = -1;
    for (const auto& entry : formatTable()) {
        for (const QString& alias : entry.second) {
            const QRegularExpression re = wordBoundaryPattern(alias);
            const auto m = re.match(title);
            if (!m.hasMatch()) continue;
            if (bestPos == -1 || m.capturedStart() < bestPos) {
                bestPos = m.capturedStart();
                best = entry.first;
            }
        }
    }
    return best;
}

// One collected-issue fragment: an optional leading series name followed by
// "#n" or "#a-b". Anchored at both ends — trailing prose (e.g. "plus bonus
// material") fails the match so the fragment surfaces as a diagnostic
// instead of silently dropping part of the range.
const QRegularExpression& issueFragmentPattern()
{
    static const QRegularExpression re(
        QStringLiteral("^(.*?)\\s*#\\s*(\\d+)(?:\\s*-\\s*#?\\s*(\\d+))?\\s*$"));
    return re;
}

} // namespace

CollectedIssues parseCollectedIssues(const QString& seriesTitle, const QString& collects)
{
    CollectedIssues result;
    QString currentSeries = seriesTitle;

    QSet<QString> seen;
    const QStringList fragments = collects.split(QRegularExpression(QStringLiteral("[,;]")));
    for (const QString& rawFragment : fragments) {
        const QString fragment = rawFragment.trimmed();
        if (fragment.isEmpty()) continue;

        const auto m = issueFragmentPattern().match(fragment);
        if (!m.hasMatch()) {
            result.diagnostics << fragment;
            continue;
        }

        const QString seriesPart = m.captured(1).trimmed();
        if (!seriesPart.isEmpty()) currentSeries = seriesPart;

        const int lo = m.captured(2).toInt();
        const int hi = m.captured(3).isEmpty() ? lo : m.captured(3).toInt();
        if (hi < lo) {
            result.diagnostics << fragment;
            continue;
        }

        for (int n = lo; n <= hi; ++n) {
            const QString key = currentSeries + QLatin1Char('#') + QString::number(n);
            if (seen.contains(key)) continue;
            seen.insert(key);
            result.issues.append(ComicIssueRef{ currentSeries, n });
        }
    }

    result.complete = result.diagnostics.isEmpty();
    return result;
}

ComicEditionTarget buildTarget(const QString& editionId,
                                const QString& seriesId,
                                const QString& seriesTitle,
                                const QString& editionTitle,
                                const QString& catalogFormat,
                                const QString& isbn,
                                const QString& collects)
{
    ComicEditionTarget target;
    target.editionId = editionId;
    target.seriesId = seriesId;
    target.seriesTitle = seriesTitle;
    target.editionTitle = editionTitle;
    target.isbnDigits = digitsOf(isbn);

    const ComicCollectionFormat explicitFormat = parseFormat(catalogFormat);
    const ComicCollectionFormat titleFormat = detectFormatInTitle(editionTitle);

    if (explicitFormat != ComicCollectionFormat::Unknown) {
        target.format = explicitFormat;
        if (titleFormat != ComicCollectionFormat::Unknown && titleFormat != explicitFormat)
            target.formatAmbiguous = true;
    } else {
        target.format = titleFormat;
    }

    target.ordinal = parseOrdinal(editionTitle, target.format);

    const CollectedIssues collected = parseCollectedIssues(seriesTitle, collects);
    target.collectedIssues = collected.issues;
    target.collectedIssuesComplete = collected.complete;

    return target;
}

} // namespace ComicEditionIdentity
