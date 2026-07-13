#include "ComicTorrentRanker.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

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
