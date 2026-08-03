#include "BiblioCanonicalizer.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>
#include <climits>

// Layered identity resolution (spec 6.2). Records are unioned when they share a
// strong identity key — an Open Library work key, an ISBN, or a normalized
// title+author (never title alone). Each resulting group becomes one canonical
// work; ordinary format variants nest as editions. Ownership (spec 6.1) is
// honored when a field is filled: Apple governs rating/chart/artwork, Open
// Library governs work identity + earliest-publication evidence, and every field
// keeps the source/sourceId/observedAt it came from.

namespace {

QString slug(const QString &s)
{
    QString t = s;
    t.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QString());
    return t.toLower();
}

// The record's earliest-publication signal (Open Library year first, else its
// edition date). INT_MAX => no reliable date.
int effectiveYear(const BiblioSourceRecord &r)
{
    if (r.firstPublishYear > 0)
        return r.firstPublishYear;
    if (r.published.isValid())
        return r.published.year();
    return INT_MAX;
}

// Disjoint-set forest over record indices.
struct DisjointSet {
    QVector<int> parent;
    void init(int n) { parent.resize(n); for (int i = 0; i < n; ++i) parent[i] = i; }
    int find(int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; }
    void unite(int a, int b) { const int ra = find(a), rb = find(b); if (ra != rb) parent[ra] = rb; }
};

QStringList identityKeys(const BiblioSourceRecord &r)
{
    QStringList keys;
    if (!r.workKey.isEmpty())
        keys << (QStringLiteral("wk:") + r.workKey);              // layer 1: OL work key
    for (const QString &isbn : r.isbns)
        if (!isbn.isEmpty())
            keys << (QStringLiteral("isbn:") + isbn);             // layer 2: ISBN authority id
    // Identity fold lives in BiblioProviders (mirrors qml/BiblioApi.js pairKey);
    // one definition shared with the parsers so edition merging can't silently
    // drift from a divergent copy.
    const QString nt = r.normalizedTitle.isEmpty() ? BiblioProviders::foldTitleAuthor(r.title) : r.normalizedTitle;
    const QString na = r.normalizedAuthor.isEmpty() ? BiblioProviders::foldTitleAuthor(r.author) : r.normalizedAuthor;
    if (!nt.isEmpty() && !na.isEmpty())
        keys << (QStringLiteral("ta:") + nt + QLatin1Char('|') + na); // layer 3: title+author (never title alone)
    return keys;
}

BiblioFieldSource fieldSource(const QString &field, const BiblioSourceRecord &r)
{
    return BiblioFieldSource{field, r.source, r.sourceId, r.observedAt};
}

} // namespace

namespace BiblioCanonicalizer {

QList<BiblioCanonicalWork> merge(const QList<BiblioSourceRecord> &records)
{
    QList<BiblioCanonicalWork> out;
    const int n = records.size();
    if (n == 0)
        return out;

    // ── Union records that share any strong identity key. ──
    DisjointSet dsu;
    dsu.init(n);
    QHash<QString, int> keyOwner;
    for (int i = 0; i < n; ++i) {
        for (const QString &k : identityKeys(records[i])) {
            const auto it = keyOwner.constFind(k);
            if (it != keyOwner.constEnd())
                dsu.unite(i, it.value());
            else
                keyOwner.insert(k, i);
        }
    }

    QHash<int, QVector<int>> groups;
    for (int i = 0; i < n; ++i)
        groups[dsu.find(i)].append(i);

    for (auto git = groups.constBegin(); git != groups.constEnd(); ++git) {
        const QVector<int> &idx = git.value();
        BiblioCanonicalWork cw;
        BiblioWork &w = cw.work;

        // ── canonicalId: strongest identity available (OL work key > ISBN > title+author) ──
        QString minWorkKey, minIsbn, anyNormTitle, anyNormAuthor;
        for (int i : idx) {
            const BiblioSourceRecord &r = records[i];
            if (!r.workKey.isEmpty() && (minWorkKey.isEmpty() || r.workKey < minWorkKey))
                minWorkKey = r.workKey;
            for (const QString &isbn : r.isbns)
                if (!isbn.isEmpty() && (minIsbn.isEmpty() || isbn < minIsbn))
                    minIsbn = isbn;
            if (anyNormTitle.isEmpty() && !r.normalizedTitle.isEmpty()) {
                anyNormTitle = r.normalizedTitle;
                anyNormAuthor = r.normalizedAuthor;
            }
        }
        if (!minWorkKey.isEmpty()) {
            QString key = minWorkKey;
            key.remove(QStringLiteral("/works/"));
            w.canonicalId = slug(key);
        } else if (!minIsbn.isEmpty()) {
            w.canonicalId = QStringLiteral("isbn-") + minIsbn;
        } else {
            w.canonicalId = QStringLiteral("ta-") + slug(anyNormTitle + QLatin1Char('-') + anyNormAuthor);
        }

        // ── originalLanguage: language of the earliest edition (OL first-publish evidence) ──
        int earliestIdx = -1;
        for (int i : idx) {
            if (earliestIdx < 0) { earliestIdx = i; continue; }
            const int ey = effectiveYear(records[i]);
            const int by = effectiveYear(records[earliestIdx]);
            if (ey < by) {
                earliestIdx = i;
            } else if (ey == by) {
                const BiblioSourceRecord &a = records[i];
                const BiblioSourceRecord &b = records[earliestIdx];
                const bool aOl = a.source == QLatin1String("openlibrary");
                const bool bOl = b.source == QLatin1String("openlibrary");
                if (aOl && !bOl) earliestIdx = i;
                else if (aOl == bOl && a.sourceId < b.sourceId) earliestIdx = i;
            }
        }
        if (earliestIdx >= 0 && !records[earliestIdx].language.isEmpty()) {
            w.originalLanguage = records[earliestIdx].language;
            cw.fieldSources.append(fieldSource(QStringLiteral("originalLanguage"), records[earliestIdx]));
        }

        // ── canonicalFirstPublished: earliest Open Library first-publish year ──
        int minYear = INT_MAX, minYearIdx = -1;
        for (int i : idx) {
            const int y = records[i].firstPublishYear;
            if (y > 0 && y < minYear) { minYear = y; minYearIdx = i; }
        }
        if (minYearIdx >= 0) {
            w.canonicalFirstPublished = QDate(minYear, 1, 1);
            cw.fieldSources.append(fieldSource(QStringLiteral("firstPublished"), records[minYearIdx]));
        }

        // ── rating (Apple owns): prefer the ebook storefront, then the largest vote count ──
        int ratingIdx = -1;
        for (int i : idx) {
            const BiblioSourceRecord &r = records[i];
            if (r.source != QLatin1String("apple") || !r.hasRating)
                continue;
            if (ratingIdx < 0) { ratingIdx = i; continue; }
            const BiblioSourceRecord &best = records[ratingIdx];
            const bool iEbook = r.format == QLatin1String("ebook");
            const bool bEbook = best.format == QLatin1String("ebook");
            if (iEbook && !bEbook) ratingIdx = i;
            else if (iEbook == bEbook && r.rating.count > best.rating.count) ratingIdx = i;
        }
        if (ratingIdx >= 0) {
            w.rating = records[ratingIdx].rating;
            cw.fieldSources.append(fieldSource(QStringLiteral("rating"), records[ratingIdx]));
        }

        // ── appleChartScore (Apple owns): the best charting edition's score ──
        int chartIdx = -1;
        for (int i : idx) {
            const BiblioSourceRecord &r = records[i];
            if (r.source == QLatin1String("apple") && r.appleChartScore > 0.0
                && (chartIdx < 0 || r.appleChartScore > records[chartIdx].appleChartScore))
                chartIdx = i;
        }
        if (chartIdx >= 0) {
            w.appleChartScore = records[chartIdx].appleChartScore;
            cw.fieldSources.append(fieldSource(QStringLiteral("appleChartScore"), records[chartIdx]));
        }

        // ── openLibraryPopularity (Open Library owns) ──
        int popIdx = -1;
        for (int i : idx) {
            const BiblioSourceRecord &r = records[i];
            if (r.source == QLatin1String("openlibrary") && r.openLibraryPopularity > 0.0
                && (popIdx < 0 || r.openLibraryPopularity > records[popIdx].openLibraryPopularity))
                popIdx = i;
        }
        if (popIdx >= 0) {
            w.openLibraryPopularity = records[popIdx].openLibraryPopularity;
            cw.fieldSources.append(fieldSource(QStringLiteral("openLibraryPopularity"), records[popIdx]));
        }

        // ── title + publisher: the representative English edition ──
        // Prefer an English-readable edition (discovery is English-readable), then
        // Open Library (identity evidence), then the earliest, then a stable id.
        auto isBetterRep = [&records](int a, int b) -> bool {
            const BiblioSourceRecord &ra = records[a];
            const BiblioSourceRecord &rb = records[b];
            const bool aEng = ra.englishReadable || ra.language == QLatin1String("en");
            const bool bEng = rb.englishReadable || rb.language == QLatin1String("en");
            if (aEng != bEng) return aEng;
            const bool aOl = ra.source == QLatin1String("openlibrary");
            const bool bOl = rb.source == QLatin1String("openlibrary");
            if (aOl != bOl) return aOl;
            const int ay = effectiveYear(ra), by = effectiveYear(rb);
            if (ay != by) return ay < by;
            return ra.sourceId < rb.sourceId;
        };
        int repIdx = -1;
        for (int i : idx) {
            if (records[i].title.isEmpty()) continue;
            if (repIdx < 0 || isBetterRep(i, repIdx)) repIdx = i;
        }
        if (repIdx >= 0) {
            w.title = records[repIdx].title;
            cw.fieldSources.append(fieldSource(QStringLiteral("title"), records[repIdx]));
            if (!records[repIdx].author.isEmpty()) {
                w.author = records[repIdx].author;
                cw.fieldSources.append(fieldSource(QStringLiteral("author"), records[repIdx]));
            }
            if (!records[repIdx].publisher.isEmpty()) {
                w.publisher = records[repIdx].publisher;
                cw.fieldSources.append(fieldSource(QStringLiteral("publisher"), records[repIdx]));
            }
        }

        // ── coverUrl: Apple owns artwork (spec 6.1); fall back to Open Library art ──
        // Pass 1 prefers an Apple cover (ebook edition first, then a stable id);
        // pass 2 falls back to any Open Library cover only if Apple carried none.
        int coverIdx = -1;
        for (int i : idx) {
            const BiblioSourceRecord &r = records[i];
            if (r.source != QLatin1String("apple") || r.artworkUrl.isEmpty())
                continue;
            if (coverIdx < 0) { coverIdx = i; continue; }
            const BiblioSourceRecord &best = records[coverIdx];
            const bool iEbook = r.format == QLatin1String("ebook");
            const bool bEbook = best.format == QLatin1String("ebook");
            if (iEbook && !bEbook) coverIdx = i;
            else if (iEbook == bEbook && r.sourceId < best.sourceId) coverIdx = i;
        }
        if (coverIdx < 0) {
            for (int i : idx) {
                if (records[i].source == QLatin1String("openlibrary") && !records[i].artworkUrl.isEmpty()) {
                    coverIdx = i;
                    break;
                }
            }
        }
        if (coverIdx >= 0) {
            w.coverUrl = records[coverIdx].artworkUrl;
            cw.fieldSources.append(fieldSource(QStringLiteral("coverUrl"), records[coverIdx]));
        }
        if (w.publisher.isEmpty()) { // fall back to any Open Library publisher
            for (int i : idx) {
                if (records[i].source == QLatin1String("openlibrary") && !records[i].publisher.isEmpty()) {
                    w.publisher = records[i].publisher;
                    cw.fieldSources.append(fieldSource(QStringLiteral("publisher"), records[i]));
                    break;
                }
            }
        }

        // ── editions: one per (format, language); ordinary formats nest here ──
        QMap<QString, QVector<int>> byEdition; // QMap keeps a deterministic order
        for (int i : idx) {
            const BiblioSourceRecord &r = records[i];
            const QString fmt = r.format.isEmpty() ? QStringLiteral("ebook") : r.format;
            const QString lang = r.language.isEmpty() ? QStringLiteral("en") : r.language;
            byEdition[fmt + QLatin1Char('|') + lang].append(i);
        }
        for (auto eit = byEdition.constBegin(); eit != byEdition.constEnd(); ++eit) {
            const QStringList parts = eit.key().split(QLatin1Char('|'));
            BiblioEdition ed;
            ed.format = parts.value(0);
            ed.language = parts.value(1);
            ed.editionId = w.canonicalId + QLatin1Char(':') + ed.format + QLatin1Char(':') + ed.language;
            ed.englishReadable = (ed.language == QLatin1String("en"));
            for (int i : eit.value()) {
                const BiblioSourceRecord &r = records[i];
                if (r.englishReadable) ed.englishReadable = true;
                if (r.pageCount > ed.pageCount) ed.pageCount = r.pageCount;
                if (ed.publisher.isEmpty() && !r.publisher.isEmpty()) ed.publisher = r.publisher;
                if (r.published.isValid() && (!ed.published.isValid() || r.published < ed.published))
                    ed.published = r.published;
            }
            w.editions.append(ed);
        }
        std::sort(w.editions.begin(), w.editions.end(), [](const BiblioEdition &a, const BiblioEdition &b) {
            if (a.format != b.format) return a.format < b.format;
            return a.language < b.language;
        });

        out.append(cw);
    }

    std::sort(out.begin(), out.end(), [](const BiblioCanonicalWork &a, const BiblioCanonicalWork &b) {
        return a.work.canonicalId < b.work.canonicalId;
    });
    return out;
}

} // namespace BiblioCanonicalizer
