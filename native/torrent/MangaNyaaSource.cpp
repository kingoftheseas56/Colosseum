// native/torrent/MangaNyaaSource.cpp
#include "torrent/MangaNyaaSource.h"

#include "engine/MangaTankobanLogic.h" // MangaTankoban::volumeId
#include "torrent/BookTorrentMagnet.h" // buildMagnet() — tracker-bearing magnet for a bare infohash
#include "torrent/MangaTorrentDiscovery.h" // Arc 18 M2 alias-aware bounded query family
#include "torrent/MangaVolumeIdentity.h" // the ONE shared volume-identity grammar (Arc 18 M1)

#include <QDateTime>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>

namespace MangaTankoban {
namespace {

constexpr const char* kNyaaRssEndpoint = "https://nyaa.si";
constexpr const char* kTrustResource = ":/tankoban/manga_uploader_trust.json";

QString norm(const QString& s)
{
    return s.trimmed().toLower();
}

// Fold to a whitespace-separated lower-case token stream: every non-alphanumeric
// character becomes a space, runs collapse. Robust series/alias containment check
// that ignores punctuation, casing and separator noise in Nyaa titles.
QString foldWords(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        // Drop apostrophes entirely so "JoJo's" and "JoJos" fold to one token.
        if (c == QLatin1Char('\'') || c == QChar(0x2019)) // ' and ’
            continue;
        out.append(c.isLetterOrNumber() ? c.toLower() : QLatin1Char(' '));
    }
    return out.simplified();
}

// Parse a Nyaa "36.4 MiB" size string into bytes. Best-effort; 0 when absent.
qint64 parseSize(const QString& raw)
{
    static const QRegularExpression re(
        QStringLiteral(R"(([0-9]*\.?[0-9]+)\s*(TiB|GiB|MiB|KiB|B))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(raw);
    if (!m.hasMatch())
        return 0;
    const double value = m.captured(1).toDouble();
    const QString unit = m.captured(2).toLower();
    double mult = 1.0;
    if (unit == QLatin1String("kib")) mult = 1024.0;
    else if (unit == QLatin1String("mib")) mult = 1024.0 * 1024.0;
    else if (unit == QLatin1String("gib")) mult = 1024.0 * 1024.0 * 1024.0;
    else if (unit == QLatin1String("tib")) mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    return static_cast<qint64>(value * mult);
}

// Volume-coverage detection for release titles lives in the ONE shared owner,
// MangaVolumeIdentity (Arc 18 M1) — the local integer-era grammar this file
// used to carry is gone so search-time and file-time identity cannot drift.
// The shared grammar keeps every pinned rule: explicit v / vol / volume /
// volumes markers only (a bare number is never coverage, which is what keeps
// "Chapter 2" out of the volume model), inclusive ranges with an optional
// repeated prefix on the upper bound, zero-stripped canonical decimal strings —
// now extended to fractional forms ("v1.5") the int grammar could not see.

bool titleHasDigitalHint(const QString& title)
{
    const QString l = title.toLower();
    if (l.contains(QLatin1String("digital")) || l.contains(QLatin1String("official")))
        return true;
    static const QRegularExpression viz(QStringLiteral(R"(\bviz\b)"),
                                        QRegularExpression::CaseInsensitiveOption);
    return title.contains(viz);
}

// A chapter release (Chapter / Ch. / chapters + a number) must never be read as a
// volume, however trusted its uploader.
bool isChapterPack(const QString& title)
{
    static const QRegularExpression re(QStringLiteral(R"(\b(?:chapters?|ch)\b\.?\s*[0-9])"),
                                       QRegularExpression::CaseInsensitiveOption);
    return title.contains(re);
}

// True when `s` uses the standalone word "raw".
bool hasRawWord(const QString& s)
{
    static const QRegularExpression rawWord(QStringLiteral(R"(\braw\b)"),
                                            QRegularExpression::CaseInsensitiveOption);
    return s.contains(rawWord);
}

// A clearly-raw / untranslated Japanese release. The bare-"raw" word check is
// suppressed when the series is itself legitimately named with "Raw" (e.g. "Raw
// Hero"), so a real volume of that series is never mistaken for a raw scan; the
// explicit (Japanese) / untranslated / 日本語 markers stay unconditional.
bool isRaw(const QString& title, bool seriesHasRawWord)
{
    if (!seriesHasRawWord && hasRawWord(title))
        return true;
    const QString l = title.toLower();
    if (l.contains(QLatin1String("(japanese)")) || l.contains(QLatin1String("untranslated")))
        return true;
    if (title.contains(QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"))) // 日本語
        return true;
    return false;
}

// The title must strongly match the series title or one of its aliases.
bool strongSeriesMatch(const QString& title, const SeriesSnapshot& series)
{
    const QString hay = foldWords(title);
    QStringList needles;
    if (!series.title.isEmpty())
        needles << series.title;
    needles << series.aliases;
    for (const QString& n : needles) {
        const QString fn = foldWords(n);
        if (!fn.isEmpty() && hay.contains(fn))
            return true;
    }
    return false;
}

bool matchesRequiredTitleMarker(const QString& title, const QStringList& requiredMarkers)
{
    if (requiredMarkers.isEmpty())
        return true;
    const QString hay = foldWords(title);
    for (const QString& marker : requiredMarkers) {
        const QString needle = foldWords(marker);
        if (!needle.isEmpty() && hay.contains(needle))
            return true;
    }
    return false;
}

// Does the [lo,hi] coverage include the exact target volume? Rebuilt as a
// VolumeCoverage and answered by the shared grammar: exact decimal-string
// comparison for singles (numeric by value, named by folded text — never a
// double round-trip) and inclusive numeric bounds for ranges.
bool coverageIncludesTarget(const QString& lo, const QString& hi, const QString& target)
{
    if (lo.isEmpty() || hi.isEmpty())
        return false;
    MangaVolumeIdentity::VolumeCoverage coverage;
    coverage.lo = MangaVolumeIdentity::makeLabel(lo);
    coverage.hi = MangaVolumeIdentity::makeLabel(hi);
    if (!coverage.lo.isNumeric() || !coverage.hi.isNumeric()) {
        // Named coverage (lo == hi textual): only an exact named match covers.
        coverage.kind = MangaVolumeIdentity::CoverageKind::Single;
        return coverage.lo.isNamed() && coverage.lo.canonical == coverage.hi.canonical
            && MangaVolumeIdentity::coversTarget(coverage, target);
    }
    coverage.kind = (MangaVolumeIdentity::numericCompare(coverage.lo.canonical,
                                                         coverage.hi.canonical) == 0)
        ? MangaVolumeIdentity::CoverageKind::Single
        : MangaVolumeIdentity::CoverageKind::Range;
    return MangaVolumeIdentity::coversTarget(coverage, target);
}

int tierForUploader(const QString& uploader, const TrustTable& trust)
{
    const QString u = norm(uploader);
    if (trust.blocked.contains(u)) return -1; // blocked → drop entirely
    if (trust.tier1.contains(u)) return 1;
    if (trust.tier2.contains(u)) return 2;
    return 99;
}

// When the RSS row carries no uploader, infer one by scanning the title for a
// known trusted uploader (tier1 then tier2), mirroring TB2.
QString inferUploaderFromTitle(const QString& title, const TrustTable& trust)
{
    const QString hay = norm(title);
    for (const QString& u : trust.tier1)
        if (hay.contains(u)) return u;
    for (const QString& u : trust.tier2)
        if (hay.contains(u)) return u;
    return QString();
}

} // namespace

QStringList queryVariants(const QString& seriesTitle, const QString& volumeNumber)
{
    QStringList out;
    const auto add = [&out](const QString& q) {
        const QString t = q.simplified();
        if (!t.isEmpty() && !out.contains(t))
            out.append(t);
    };

    bool ok = false;
    const int n = volumeNumber.trimmed().toInt(&ok);
    if (ok) {
        add(QStringLiteral("%1 %2").arg(seriesTitle).arg(n));
        add(QStringLiteral("%1 %2").arg(seriesTitle).arg(n, 2, 10, QChar('0')));
        add(QStringLiteral("%1 %2").arg(seriesTitle).arg(n, 3, 10, QChar('0')));
        add(QStringLiteral("%1 Vol %2").arg(seriesTitle).arg(n));
    } else {
        // Named / fractional volume — carry the raw token, no zero-pad math.
        const QString v = volumeNumber.trimmed();
        add(QStringLiteral("%1 %2").arg(seriesTitle, v));
        add(QStringLiteral("%1 Vol %2").arg(seriesTitle, v));
    }
    add(seriesTitle);
    return out;
}

QList<MangaNyaaCandidate> MangaNyaaSource::parseRss(const QByteArray& payload)
{
    QList<MangaNyaaCandidate> out;
    QXmlStreamReader xml(payload);
    MangaNyaaCandidate cur;
    QString sizeText;
    QString tag;
    bool inItem = false;

    // QXmlStreamReader has namespace processing on by default, so xml.name()
    // yields the LOCAL name ("infoHash", not "nyaa:infoHash"). Element compares
    // below use local names so the nyaa: extension elements still match.
    while (!xml.atEnd() && !xml.hasError()) {
        const auto t = xml.readNext();
        if (t == QXmlStreamReader::StartElement) {
            tag = xml.name().toString();
            if (tag == QStringLiteral("item")) {
                cur = MangaNyaaCandidate{};
                sizeText.clear();
                inItem = true;
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (xml.name() == QStringLiteral("item") && inItem) {
                cur.sizeBytes = parseSize(sizeText);
                const auto coverage = MangaVolumeIdentity::detectCoverage(
                    cur.title, MangaVolumeIdentity::EvidenceSource::ReleaseTitle);
                if (coverage.has()) {
                    cur.coverageLo = coverage.lo.canonical;
                    cur.coverageHi = coverage.hi.canonical;
                    cur.standalone = coverage.isSingle();
                }
                cur.digitalHint = titleHasDigitalHint(cur.title);
                out.append(cur);
                inItem = false;
            }
            tag.clear();
        } else if (t == QXmlStreamReader::Characters && inItem && !xml.isWhitespace()) {
            const QString text = xml.text().toString();
            if (tag == QStringLiteral("title")) {
                cur.title += text; // characters may arrive in chunks
            } else if (tag == QStringLiteral("infoHash")) {
                cur.infoHash = text.trimmed().toLower();
                // Nyaa's RSS exposes no trackers (its <link> is the .torrent file
                // URL), so a bare magnet here would ride DHT alone — and on this
                // network DHT is unreliable, leaving downloads stuck at
                // "resolving" forever (2026-08-16). Persist the tracker-bearing
                // form so ledger rows are self-sufficient; TB2's comics mode gets
                // the same effect by capturing Nyaa's full HTML magnet href.
                cur.magnetUri = BookTorrentMagnet::buildMagnet(cur.infoHash);
            } else if (tag == QStringLiteral("seeders")) {
                cur.seeders = text.toInt();
            } else if (tag == QStringLiteral("leechers")) {
                cur.leechers = text.toInt();
            } else if (tag == QStringLiteral("size")) {
                sizeText += text;
            } else if (tag == QStringLiteral("uploader") || tag == QStringLiteral("author")) {
                // Prefer the namespaced <nyaa:uploader>; <author> is the fallback.
                // First-write-wins preserves the preferred value.
                if (cur.uploader.isEmpty())
                    cur.uploader = text.trimmed();
            } else if (tag == QStringLiteral("link")) {
                // Nyaa's RSS <link> is the .torrent metainfo URL, not a magnet.
                // Retained (Arc 18 M2) so the indexer can enumerate real file
                // identity without payload bytes; first-write-wins.
                if (cur.torrentUrl.isEmpty())
                    cur.torrentUrl = text.trimmed();
            }
        }
    }
    return out;
}

QList<MangaNyaaCandidate> MangaNyaaSource::filterAndRank(const SeriesSnapshot& series,
                                                        const QString& targetVolume,
                                                        const QList<MangaNyaaCandidate>& parsed,
                                                        const TrustTable& trust,
                                                        bool seriesMode)
{
    QList<MangaNyaaCandidate> kept;
    QSet<QString> seenInfoHashes;

    // A series legitimately named with "Raw" (e.g. "Raw Hero") must not have its
    // volumes rejected by the bare-"raw" scan filter.
    QString seriesText = series.title;
    for (const QString& a : series.aliases)
        seriesText += QLatin1Char(' ') + a;
    const bool seriesHasRawWord = hasRawWord(seriesText);

    for (MangaNyaaCandidate c : parsed) {
        if (c.infoHash.isEmpty())
            continue;                       // no infohash → unusable
        // Chapter-pack reject applies ONLY when there is no volume marker at all.
        // An annotated volume ("v02 (Ch. 8-15)") carries coverage and survives; a
        // pure chapter release has empty coverage and is dropped by the coverage
        // check below, so gating here loses no protection.
        if (c.coverageLo.isEmpty() && isChapterPack(c.title))
            continue;
        if (isRaw(c.title, seriesHasRawWord))
            continue;                       // raw / untranslated Japanese release
        if (!strongSeriesMatch(c.title, series))
            continue;                       // weak series-title / alias match
        if (!matchesRequiredTitleMarker(c.title, series.requiredTitleMarkers))
            continue;                       // wrong edition (e.g. B&W result for Color)
        // Series mode has no volume to target — every strongly-matched, kept
        // release is in scope regardless of what it covers.
        if (!seriesMode && !coverageIncludesTarget(c.coverageLo, c.coverageHi, targetVolume))
            continue;                       // wrong target or no volume coverage

        QString uploader = c.uploader;
        if (uploader.trimmed().isEmpty())
            uploader = inferUploaderFromTitle(c.title, trust);
        const int tier = tierForUploader(uploader, trust);
        if (tier < 0)
            continue;                       // blocked uploader

        if (seenInfoHashes.contains(c.infoHash))
            continue;                       // dedup by infohash (document order kept)
        seenInfoHashes.insert(c.infoHash);

        c.uploader = uploader;
        c.tier = tier;
        kept.append(c);
    }

    // Advisory ordering only — NEVER an auto-pick. stable_sort so equal keys keep
    // document order.
    std::stable_sort(kept.begin(), kept.end(),
                     [](const MangaNyaaCandidate& a, const MangaNyaaCandidate& b) {
                         if (a.tier != b.tier) return a.tier < b.tier;             // trusted first
                         if (a.standalone != b.standalone) return a.standalone;    // volume before pack
                         if (a.digitalHint != b.digitalHint) return a.digitalHint; // digital/official first
                         if (a.seeders != b.seeders) return a.seeders > b.seeders; // seeders desc
                         return a.title.toCaseFolded() < b.title.toCaseFolded();   // case-folded title
                     });
    return kept;
}

MangaNyaaSource::MangaNyaaSource(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    qRegisterMetaType<QList<MangaTankoban::MangaNyaaCandidate>>(
        "QList<MangaTankoban::MangaNyaaCandidate>");
    loadTrustResource();
}

MangaNyaaSource::~MangaNyaaSource() = default;

void MangaNyaaSource::loadTrustResource()
{
    QFile f(QString::fromLatin1(kTrustResource));
    if (!f.open(QIODevice::ReadOnly))
        return; // degrade to all-untrusted; no uploader filter
    QJsonParseError err{};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &err).object();
    if (err.error != QJsonParseError::NoError)
        return;
    const auto fill = [](const QJsonArray& arr, QSet<QString>& set) {
        for (const auto& v : arr) {
            const QString u = v.toString().trimmed().toLower();
            if (!u.isEmpty())
                set.insert(u);
        }
    };
    fill(root.value(QStringLiteral("tier1")).toArray(), m_trust.tier1);
    fill(root.value(QStringLiteral("tier2")).toArray(), m_trust.tier2);
    fill(root.value(QStringLiteral("blocked")).toArray(), m_trust.blocked);
}

void MangaNyaaSource::search(const SeriesSnapshot& series, const QString& targetVolume)
{
    startSearch(volumeId(series.seriesId, targetVolume), series, targetVolume, /*seriesMode=*/false);
}

void MangaNyaaSource::searchSeries(const SeriesSnapshot& series)
{
    // The caller (the façade) already hands us the opaque key it wants results
    // grouped under — series.seriesId here is that key, not a raw malId/title.
    startSearch(series.seriesId, series, QString(), /*seriesMode=*/true);
}

void MangaNyaaSource::startSearch(const QString& vid, const SeriesSnapshot& series,
                                  const QString& targetVolume, bool seriesMode)
{
    if (!m_nam) {
        emit searchFailed(vid, QStringLiteral("network manager unavailable"));
        return;
    }
    // Re-entrancy guard: a search for this key is already in flight. Overwriting
    // m_pending[vid] would reset the reply counter and mismerge the two batches, so
    // ignore the duplicate call.
    if (m_pending.contains(vid))
        return;
    // Arc 18 M2: the query family is planned by MangaTorrentDiscovery —
    // canonical-title variants first (byte-identical to the pre-Arc-18
    // queryVariants family when a series has no aliases), then alias variants,
    // deduped and capped so aliases are now discovery INPUTS, not just
    // validation needles.
    const QStringList queries =
        MangaTorrentDiscovery::queryFamily(series, targetVolume);
    PendingSearch pending;
    pending.volumeId = vid;
    pending.series = series;
    pending.targetVolume = targetVolume;
    pending.seriesMode = seriesMode;
    pending.pendingReplies = queries.size();
    m_pending.insert(vid, pending);

    for (const QString& query : queries) {
        QUrl url(QString::fromLatin1(kNyaaRssEndpoint));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("page"), QStringLiteral("rss"));
        q.addQueryItem(QStringLiteral("c"), QStringLiteral("3_1")); // Literature - English-translated
        q.addQueryItem(QStringLiteral("s"), QStringLiteral("seeders"));
        q.addQueryItem(QStringLiteral("o"), QStringLiteral("desc"));
        q.addQueryItem(QStringLiteral("q"), query);
        url.setQuery(q);

        // Bounded network (Arc 18 M2): a dead/stalled query must not hang the
        // whole merged search — one transfer deadline per request, then it
        // lands in the batch's error list while sibling results survive.
        // (Qt 6 follows same-scheme redirects by default; no attribute needed.)
        QNetworkRequest request(url);
        request.setTransferTimeout(15000);

        auto* reply = m_nam->get(request);
        reply->setProperty("nyaa_volumeId", vid);
        reply->setProperty("nyaa_query", query);
        connect(reply, &QNetworkReply::finished, this, &MangaNyaaSource::onReplyFinished);
    }
}

void MangaNyaaSource::onReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    finishReply(reply);
}

void MangaNyaaSource::finishReply(QNetworkReply* reply)
{
    const QString vid = reply->property("nyaa_volumeId").toString();
    auto it = m_pending.find(vid);
    if (it == m_pending.end())
        return;

    if (reply->error() != QNetworkReply::NoError) {
        it->errors.append(QStringLiteral("%1: %2")
                              .arg(reply->property("nyaa_query").toString(), reply->errorString()));
    } else {
        // Stamp provenance (Arc 18 M2): which query found each candidate and
        // when, so the indexer can explain and audit its discovery evidence.
        // parseRss stays pure; the stamp happens at merge time.
        const QString query = reply->property("nyaa_query").toString();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<MangaNyaaCandidate> batch = parseRss(reply->readAll());
        for (MangaNyaaCandidate& c : batch) {
            if (c.query.isEmpty())
                c.query = query;
            if (c.discoveredAt == 0)
                c.discoveredAt = now;
        }
        it->parsed.append(batch);
    }

    if (--it->pendingReplies > 0)
        return;

    const SeriesSnapshot series = it->series;
    const QString target = it->targetVolume;
    const bool seriesMode = it->seriesMode;
    const QList<MangaNyaaCandidate> parsed = it->parsed;
    const QStringList errors = it->errors;
    m_pending.erase(it);

    const QList<MangaNyaaCandidate> ranked = filterAndRank(series, target, parsed, m_trust, seriesMode);
    if (ranked.isEmpty() && !errors.isEmpty()) {
        emit searchFailed(vid, errors.join(QStringLiteral("; ")));
        return;
    }
    emit searchSucceeded(vid, ranked);
}

} // namespace MangaTankoban
