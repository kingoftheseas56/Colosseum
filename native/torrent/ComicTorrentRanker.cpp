#include "ComicTorrentRanker.h"

#include "ComicCoverage.h"
#include "ComicUploaderTrust.h"

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

namespace {
using ComicEditionIdentity::ComicCollectionFormat;
using ComicEditionIdentity::ComicEditionTarget;

// Identity-evidence weights, strongest first (design doc "Torrent-level
// ranking"): ISBN > exact title > format-scoped coverage > complete
// collected-issue range > general series/title tokens > archive hint >
// uploader trust. Seeder count is never folded into identityScore — it stays
// a separate, explicitly volatile sort key (see the final std::sort below).
constexpr int kIsbnScore          = 100000;
constexpr int kTitleExactScore    = 50000;
constexpr int kCoverageScore      = 20000;
constexpr int kIssuesScore        = 8000;
constexpr int kTitlePrefixScore   = 3000;
constexpr int kTitleTokensScore   = 1500;
constexpr int kArchiveScore       = 100;
constexpr int kTrustTier1Score    = 50;
constexpr int kTrustTier2Score    = 20;

// One listing's raw identity evidence against `target`, computed BEFORE
// infohash dedup so duplicate rows can be unioned rather than one winner
// silently discarding what another duplicate proved.
struct RowEvidence {
    TorrentResult src;
    bool isbnMatch = false;
    bool titleExact = false;
    bool titlePrefix = false;
    bool titleAllTokens = false;
    bool coverageMatch = false;
    bool rangeMatch = false;
    bool archiveHint = false;
    bool seriesPresent = false;
    int trustTier = 99;
    QString uploaderName;
    bool blocked = false;
};

// Sum of the weighted evidence a single row carries, used only to pick which
// duplicate becomes the DISPLAYED representative (title/magnet/etc.) — never
// the score that decides confidence, which is computed from the union.
int rowWeight(const RowEvidence& e)
{
    int score = 0;
    if (e.isbnMatch) score += kIsbnScore;
    if (e.titleExact) score += kTitleExactScore;
    else if (e.titlePrefix) score += kTitlePrefixScore;
    else if (e.titleAllTokens) score += kTitleTokensScore;
    if (e.coverageMatch) score += kCoverageScore;
    if (e.rangeMatch) score += kIssuesScore;
    if (e.archiveHint) score += kArchiveScore;
    if (e.trustTier == 1) score += kTrustTier1Score;
    else if (e.trustTier == 2) score += kTrustTier2Score;
    return score;
}
} // namespace

QList<RankedComicTorrent> ComicTorrentRanker::rankForEdition(
    const ComicEditionTarget& target, const QList<TorrentResult>& raw)
{
    const QString normEdition = normalized(target.editionTitle);
    const QStringList editionTokens = tokensOf(normEdition);
    const QStringList seriesTokens = tokensOf(normalized(target.seriesTitle));

    // Complete collected-issue evidence is scoped to the target's own lo/hi
    // span (e.g. "Saga #1-18" -> ["1","18"]) — a partial parse never lets a
    // subset of issues masquerade as range coverage.
    QStringList issueRangeTokens;
    if (target.collectedIssuesComplete && !target.collectedIssues.isEmpty()) {
        int lo = target.collectedIssues.first().number;
        int hi = lo;
        for (const ComicEditionIdentity::ComicIssueRef& issue : target.collectedIssues) {
            lo = qMin(lo, issue.number);
            hi = qMax(hi, issue.number);
        }
        issueRangeTokens << QString::number(lo) << QString::number(hi);
    }

    const ComicUploaderTrust::TrustTable trustTable = ComicUploaderTrust::load();

    // Grade one listing's edition-identity evidence (title / ISBN / coverage /
    // issue-range / archive / uploader trust) against `target`.
    const auto evaluateRow = [&](const TorrentResult& result) {
        RowEvidence e;
        e.src = result;

        const QString normCand = normalized(result.title);
        const QStringList candTokens = tokensOf(normCand);
        const QSet<QString> candSet(candTokens.cbegin(), candTokens.cend());
        const QString candDigits = digitsOf(result.title);

        e.isbnMatch = !target.isbnDigits.isEmpty() && candDigits.contains(target.isbnDigits);

        // Title evidence ladder: exact > canonical prefix > all significant tokens.
        if (!normEdition.isEmpty()) {
            if (normCand == normEdition)
                e.titleExact = true;
            else if (normCand.startsWith(normEdition + QLatin1Char(' ')))
                e.titlePrefix = true;
            else
                e.titleAllTokens = allTokensPresent(editionTokens, candSet);
        }

        // Format-scoped coverage: a DIFFERENT format matching the same ordinal
        // is never coverage (ComicCoverage::coverageCovers requires format
        // equality), and an ambiguous target never auto-matches.
        if (!target.formatAmbiguous && target.format != ComicCollectionFormat::Unknown
                && target.ordinal >= 0) {
            const auto spans = ComicCoverage::detectComicCoverage(result.title);
            e.coverageMatch = ComicCoverage::coverageCovers(spans, target.format, target.ordinal);
        }

        e.rangeMatch = !issueRangeTokens.isEmpty() && containsNumberRun(candTokens, issueRangeTokens);
        e.archiveHint = hasComicArchiveHint(result.title);
        e.seriesPresent = allTokensPresent(seriesTokens, candSet);

        const ComicUploaderTrust::UploaderTrust trust =
            ComicUploaderTrust::taggedUploader(result.title, trustTable);
        e.trustTier = trust.tier;
        e.uploaderName = trust.name;
        e.blocked = (trust.tier == -1);
        return e;
    };

    // Union raw rows sharing a canonical infohash BEFORE grading confidence:
    // a high-seed generic-title listing and a low-seed exact/covering one for
    // the SAME release must combine into one canonical row whose evidence is
    // the union of both, not whichever happened to be scored first.
    QHash<QString, QList<RowEvidence>> byHash;
    QStringList hashOrder;   // first-seen order — stable output when scores tie
    for (const TorrentResult& result : raw) {
        const QString hash = canonicalizeInfoHash(result.infoHash);
        if (hash.isEmpty()) continue;
        TorrentResult canonical = result;
        canonical.infoHash = hash;
        if (!byHash.contains(hash)) hashOrder << hash;
        byHash[hash].append(evaluateRow(canonical));
    }

    QList<RankedComicTorrent> ranked;
    for (const QString& hash : hashOrder) {
        const QList<RowEvidence>& rows = byHash.value(hash);

        // A blocked uploader tag on ANY duplicate removes the whole canonical
        // release — trust never bypasses identity safety in the other
        // direction either (a blocked tag can't be laundered by a clean dupe).
        bool anyBlocked = false;
        for (const RowEvidence& e : rows)
            if (e.blocked) { anyBlocked = true; break; }
        if (anyBlocked) continue;

        bool isbnMatch = false, titleExact = false, titlePrefix = false, titleAllTokens = false,
             coverageMatch = false, rangeMatch = false, archiveHint = false, seriesPresent = false;
        int bestTier = 99;
        QString uploaderName;
        int maxSeeders = 0, maxLeechers = 0;
        const RowEvidence* representative = nullptr;

        for (const RowEvidence& e : rows) {
            isbnMatch      |= e.isbnMatch;
            titleExact     |= e.titleExact;
            titlePrefix    |= e.titlePrefix;
            titleAllTokens |= e.titleAllTokens;
            coverageMatch  |= e.coverageMatch;
            rangeMatch     |= e.rangeMatch;
            archiveHint    |= e.archiveHint;
            seriesPresent  |= e.seriesPresent;
            if (e.trustTier < bestTier) { bestTier = e.trustTier; uploaderName = e.uploaderName; }
            maxSeeders  = qMax(maxSeeders, e.src.seeders);
            maxLeechers = qMax(maxLeechers, e.src.leechers);
            // The highest-IDENTITY-weight duplicate supplies the displayed
            // src (title/magnet/etc.); only seeders/leechers are the
            // volatile fields unioned to their highest observed value.
            if (!representative || rowWeight(e) > rowWeight(*representative))
                representative = &e;
        }

        RankedComicTorrent r;
        r.src = representative->src;
        r.src.seeders = maxSeeders;
        r.src.leechers = maxLeechers;
        r.matchTier = matchTier(target.editionTitle, r.src.title);
        r.archiveHint = archiveHint;
        r.coverageMatch = coverageMatch;
        r.uploaderName = uploaderName;
        r.trustTier = bestTier;

        int score = 0;
        QStringList evidence;
        if (isbnMatch)           { score += kIsbnScore;        evidence << QStringLiteral("ISBN"); }
        if (titleExact)          { score += kTitleExactScore;  evidence << QStringLiteral("TITLE"); }
        else if (titlePrefix)    { score += kTitlePrefixScore; evidence << QStringLiteral("TITLE"); }
        else if (titleAllTokens) { score += kTitleTokensScore; evidence << QStringLiteral("TITLE"); }
        if (coverageMatch)       { score += kCoverageScore;    evidence << QStringLiteral("COVERAGE"); }
        if (rangeMatch)          { score += kIssuesScore;      evidence << QStringLiteral("ISSUES"); }
        if (archiveHint)         { score += kArchiveScore;     evidence << QStringLiteral("ARCHIVE"); }
        if (bestTier == 1)       { score += kTrustTier1Score;  evidence << QStringLiteral("UPLOADER"); }
        else if (bestTier == 2)  { score += kTrustTier2Score;  evidence << QStringLiteral("UPLOADER"); }

        // Trust can never rescue a format conflict: coverageMatch is only
        // ever true for a format-equal span (ComicCoverage's own contract),
        // and uploader trust does not appear in the strong condition below —
        // so no trust tier can promote a conflicting-format row to strong.
        QString confidence;
        if (isbnMatch || titleExact || coverageMatch || (titlePrefix && rangeMatch))
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
        m.insert(QStringLiteral("coverage"), r.coverageMatch);
        m.insert(QStringLiteral("uploader"), r.uploaderName);
        m.insert(QStringLiteral("trustTier"), r.trustTier);
        rows.append(m);
    }
    return rows;
}
