#include "ComicTorrentRanker.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>

namespace {
QString normalized(QString value)
{
    value = value.toLower();
    value.replace(QRegularExpression(QStringLiteral("[._\\-]+")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9 ]")), QString());
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value.trimmed();
}

QStringList tokensOf(const QString& normValue)
{
    return normValue.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString digitsOf(const QString& value)
{
    QString out;
    for (const QChar& c : value)
        if (c.isDigit()) out.append(c);
    return out;
}

// The digit sequences appearing in a collected string, e.g. "Saga #1-18" -> ["1","18"].
QStringList collectedNumbers(const QString& collects)
{
    QStringList nums;
    static const QRegularExpression numRe(QStringLiteral("\\d+"));
    auto it = numRe.globalMatch(collects);
    while (it.hasNext())
        nums.append(it.next().captured(0));
    return nums;
}

// True when the candidate's tokens contain the collected numbers as a
// consecutive run, so a lone "1" cannot masquerade as the "1-18" range.
bool containsNumberRun(const QStringList& candidateTokens, const QStringList& nums)
{
    if (nums.isEmpty()) return false;
    for (int i = 0; i + nums.size() <= candidateTokens.size(); ++i) {
        bool all = true;
        for (int j = 0; j < nums.size(); ++j) {
            if (candidateTokens[i + j] != nums[j]) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}

bool allTokensPresent(const QStringList& wanted, const QSet<QString>& have)
{
    if (wanted.isEmpty()) return false;
    for (const QString& t : wanted)
        if (!have.contains(t)) return false;
    return true;
}
} // namespace

int ComicTorrentRanker::matchTier(const QString& query, const QString& candidate)
{
    const QString wanted = normalized(query);
    const QString found = normalized(candidate);
    if (wanted.isEmpty()) return 0;
    if (found == wanted) return 4;
    if (found.startsWith(wanted + QLatin1Char(' '))) return 3;

    const QStringList wantedTokens = wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList foundList = found.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QSet<QString> foundTokens(foundList.cbegin(), foundList.cend());
    bool all = !wantedTokens.isEmpty();
    for (const QString& token : wantedTokens) {
        if (!foundTokens.contains(token)) {
            all = false;
            break;
        }
    }
    if (all) return 2;
    for (const QString& token : wantedTokens)
        if (foundTokens.contains(token)) return 1;
    return 0;
}

bool ComicTorrentRanker::hasComicArchiveHint(const QString& title)
{
    static const QRegularExpression archive(
        QStringLiteral("(?:^|[ ._\\-])(cbr|cbz|cb7|cbt)(?:$|[ ._\\-])"),
        QRegularExpression::CaseInsensitiveOption);
    return archive.match(title).hasMatch();
}

QList<RankedComicTorrent> ComicTorrentRanker::rank(const QString& query,
                                                    const QList<TorrentResult>& raw)
{
    QHash<QString, TorrentResult> bestByHash;
    for (const TorrentResult& result : raw) {
        const QString hash = canonicalizeInfoHash(result.infoHash);
        if (hash.isEmpty()) continue;
        TorrentResult canonical = result;
        canonical.infoHash = hash;
        auto it = bestByHash.find(hash);
        if (it == bestByHash.end() || canonical.seeders > it.value().seeders)
            bestByHash.insert(hash, canonical);
    }

    QList<RankedComicTorrent> ranked;
    for (const TorrentResult& result : bestByHash) {
        ranked.append(RankedComicTorrent{
            result,
            matchTier(query, result.title),
            hasComicArchiveHint(result.title)
        });
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedComicTorrent& a,
                                               const RankedComicTorrent& b) {
        if (a.matchTier != b.matchTier) return a.matchTier > b.matchTier;
        if (a.archiveHint != b.archiveHint) return a.archiveHint;
        return a.src.seeders > b.src.seeders;
    });
    return ranked;
}

TorrentResult ComicTorrentRanker::best(const QString& query,
                                        const QList<TorrentResult>& raw)
{
    const QList<RankedComicTorrent> ranked = rank(query, raw);
    if (ranked.isEmpty() || ranked.first().matchTier < 2) return {};
    return ranked.first().src;
}

QList<RankedComicTorrent> ComicTorrentRanker::rankForEdition(
    const QString& seriesTitle, const QString& editionTitle,
    const QString& isbn, const QString& collects,
    const QList<TorrentResult>& raw)
{
    // Dedup by canonical hash, keeping the highest-seeded representative.
    QHash<QString, TorrentResult> bestByHash;
    for (const TorrentResult& result : raw) {
        const QString hash = canonicalizeInfoHash(result.infoHash);
        if (hash.isEmpty()) continue;
        TorrentResult canonical = result;
        canonical.infoHash = hash;
        auto it = bestByHash.find(hash);
        if (it == bestByHash.end() || canonical.seeders > it.value().seeders)
            bestByHash.insert(hash, canonical);
    }

    const QString normEdition = normalized(editionTitle);
    const QStringList editionTokens = tokensOf(normEdition);
    const QStringList seriesTokens = tokensOf(normalized(seriesTitle));
    const QString isbnDigits = digitsOf(isbn);
    const QStringList collectedNums = collectedNumbers(collects);

    QList<RankedComicTorrent> ranked;
    ranked.reserve(bestByHash.size());
    for (const TorrentResult& result : bestByHash) {
        RankedComicTorrent r;
        r.src = result;
        r.matchTier = matchTier(editionTitle, result.title);
        r.archiveHint = hasComicArchiveHint(result.title);

        const QString normCand = normalized(result.title);
        const QStringList candTokens = tokensOf(normCand);
        const QSet<QString> candSet(candTokens.cbegin(), candTokens.cend());
        const QString candDigits = digitsOf(result.title);

        int score = 0;
        QStringList evidence;

        const bool isbnMatch = !isbnDigits.isEmpty() && candDigits.contains(isbnDigits);
        if (isbnMatch) { score += 1000; evidence << QStringLiteral("ISBN"); }

        // Title evidence ladder: exact > canonical prefix > all significant tokens.
        bool titleExact = false, titlePrefix = false, titleAllTokens = false;
        if (!normEdition.isEmpty()) {
            if (normCand == normEdition)
                titleExact = true;
            else if (normCand.startsWith(normEdition + QLatin1Char(' ')))
                titlePrefix = true;
            else
                titleAllTokens = allTokensPresent(editionTokens, candSet);
        }
        if (titleExact)          { score += 600; evidence << QStringLiteral("TITLE"); }
        else if (titlePrefix)    { score += 400; evidence << QStringLiteral("TITLE"); }
        else if (titleAllTokens) { score += 200; evidence << QStringLiteral("TITLE"); }

        const bool rangeMatch = containsNumberRun(candTokens, collectedNums);
        if (rangeMatch) { score += 100; evidence << QStringLiteral("ISSUES"); }

        if (r.archiveHint) { score += 10; evidence << QStringLiteral("ARCHIVE"); }

        const bool seriesPresent = allTokensPresent(seriesTokens, candSet);

        QString confidence;
        if (isbnMatch || titleExact || (titlePrefix && rangeMatch))
            confidence = QStringLiteral("strong");
        else if (titleAllTokens || (seriesPresent && rangeMatch))
            confidence = QStringLiteral("possible");
        else
            confidence = QStringLiteral("weak");

        r.identityScore = score;
        r.confidence = confidence;
        r.evidence = evidence;
        ranked.append(r);
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedComicTorrent& a,
                                               const RankedComicTorrent& b) {
        if (a.identityScore != b.identityScore) return a.identityScore > b.identityScore;
        if (a.archiveHint != b.archiveHint) return a.archiveHint;
        if (a.src.seeders != b.src.seeders) return a.src.seeders > b.src.seeders;
        return a.src.infoHash < b.src.infoHash;   // stable, deterministic final tiebreak
    });
    return ranked;
}

QVariantList ComicTorrentRanker::toVariantRows(const QList<RankedComicTorrent>& ranked)
{
    QVariantList rows;
    rows.reserve(ranked.size());
    for (const RankedComicTorrent& r : ranked) {
        QVariantMap m;
        m.insert(QStringLiteral("infoHash"), r.src.infoHash);
        m.insert(QStringLiteral("magnetUri"), r.src.magnetUri);
        m.insert(QStringLiteral("title"), r.src.title);
        m.insert(QStringLiteral("sizeBytes"), QVariant::fromValue(r.src.sizeBytes));
        m.insert(QStringLiteral("sizeText"), humanSize(r.src.sizeBytes));
        m.insert(QStringLiteral("seeders"), r.src.seeders);
        m.insert(QStringLiteral("leechers"), r.src.leechers);
        m.insert(QStringLiteral("sourceName"), r.src.sourceName);
        m.insert(QStringLiteral("sourceKey"), r.src.sourceKey);
        m.insert(QStringLiteral("confidence"), r.confidence);
        m.insert(QStringLiteral("matchTier"), r.matchTier);
        m.insert(QStringLiteral("evidence"), r.evidence);
        m.insert(QStringLiteral("archiveHint"), r.archiveHint);
        rows.append(m);
    }
    return rows;
}
