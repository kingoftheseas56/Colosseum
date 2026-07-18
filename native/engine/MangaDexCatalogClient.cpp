// MangaDexCatalogClient.cpp — see header for the pipeline story.

#include "MangaDexCatalogClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tankoban::manga::mangadex {

namespace {

constexpr const char* kApiBase     = "https://api.mangadex.org";
constexpr const char* kCoverBase   = "https://uploads.mangadex.org/covers";
// MangaDex API policy asks clients to identify themselves with a real UA.
constexpr const char* kUserAgent   = "TankobanColosseum/1.0 (Qt; desktop reader)";
constexpr int         kTimeoutMs   = 20000;
constexpr int         kCoverPage   = 100;   // API max per request
constexpr int         kCoverPagesMax = 4;   // 400 covers is beyond any real series

void applyHeaders(QNetworkRequest& req) {
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
}

// lowercase alphanumerics only — the same spirit as the old sitemap key, minus
// MangaFire's doubled-letter artifact.
QString matchKey(const QString& raw) {
    QString out;
    out.reserve(raw.size());
    for (QChar c : raw.toLower()) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) out.append(c);
    }
    return out;
}

// Parse "1", "1.5", "001" — anything else (null, "", "none") -> NaN.
double volumeNumber(const QJsonValue& v) {
    bool ok = false;
    const double n = v.toString().toDouble(&ok);
    return ok ? n : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

struct MangaDexCatalogClient::PendingFetch {
    QString title;
    QString mangaId;
    // volume number -> cover URL (prefer locale "ja" — the original tankōbon art)
    QMap<double, QString> covers;
    QMap<double, bool>    coverIsJa;
    // volume number -> [first, last] chapter seen in the aggregate
    QMap<double, QPair<double, double>> ranges;
};

MangaDexCatalogClient::MangaDexCatalogClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

MangaDexCatalogClient::~MangaDexCatalogClient() = default;

void MangaDexCatalogClient::fetchByTitle(const QString& title)
{
    auto pending = std::make_shared<PendingFetch>();
    pending->title = title;
    stepSearch(pending);
}

void MangaDexCatalogClient::emitFailure(PendingFetchPtr pending, const QString& reason)
{
    emit catalogFailed(pending->title, reason);
}

// ---- step 1: title -> manga id -------------------------------------------

void MangaDexCatalogClient::stepSearch(PendingFetchPtr pending)
{
    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/manga"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), pending->title);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
    q.addQueryItem(QStringLiteral("order[relevance]"), QStringLiteral("desc"));
    url.setQuery(q);

    QNetworkRequest req(url);
    applyHeaders(req);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending, QStringLiteral("manga search: ") + reply->errorString());
            return;
        }
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll())
                                     .object().value(QLatin1String("data")).toArray();
        if (items.isEmpty()) {
            emitFailure(pending, QStringLiteral("no MangaDex match for title"));
            return;
        }

        // Best match: exact normalized hit on any title/altTitle value beats
        // relevance order (relevance alone ranks "One Piece Academy" traps high).
        const QString want = matchKey(pending->title);
        QString bestId = items.first().toObject().value(QLatin1String("id")).toString();
        for (const QJsonValue& iv : items) {
            const QJsonObject item = iv.toObject();
            const QJsonObject attrs = item.value(QLatin1String("attributes")).toObject();
            QStringList names;
            const QJsonObject titleObj = attrs.value(QLatin1String("title")).toObject();
            for (auto it = titleObj.begin(); it != titleObj.end(); ++it)
                names << it.value().toString();
            const QJsonArray alts = attrs.value(QLatin1String("altTitles")).toArray();
            for (const QJsonValue& av : alts) {
                const QJsonObject alt = av.toObject();
                for (auto it = alt.begin(); it != alt.end(); ++it)
                    names << it.value().toString();
            }
            const bool exact = std::any_of(names.cbegin(), names.cend(),
                [&want](const QString& n) { return matchKey(n) == want; });
            if (exact) {
                bestId = item.value(QLatin1String("id")).toString();
                break;
            }
        }
        if (bestId.isEmpty()) {
            emitFailure(pending, QStringLiteral("MangaDex match had no id"));
            return;
        }
        pending->mangaId = bestId;
        stepCovers(pending, 0);
    });
}

// ---- step 2: manga id -> per-volume covers (paginated) --------------------

void MangaDexCatalogClient::stepCovers(PendingFetchPtr pending, int offset)
{
    QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/cover"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("manga[]"), pending->mangaId);
    q.addQueryItem(QStringLiteral("limit"), QString::number(kCoverPage));
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    q.addQueryItem(QStringLiteral("order[volume]"), QStringLiteral("asc"));
    url.setQuery(q);

    QNetworkRequest req(url);
    applyHeaders(req);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending, offset]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending, QStringLiteral("cover list: ") + reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray items = root.value(QLatin1String("data")).toArray();
        for (const QJsonValue& iv : items) {
            const QJsonObject attrs = iv.toObject().value(QLatin1String("attributes")).toObject();
            const double vol = volumeNumber(attrs.value(QLatin1String("volume")));
            if (std::isnan(vol)) continue;                       // covers with no volume tag
            const QString file = attrs.value(QLatin1String("fileName")).toString();
            if (file.isEmpty()) continue;
            const bool ja = attrs.value(QLatin1String("locale")).toString()
                                == QLatin1String("ja");
            // keep the first cover per volume, except a ja original upgrades a non-ja
            if (pending->covers.contains(vol) && (pending->coverIsJa.value(vol) || !ja))
                continue;
            // .512.jpg = MangaDex's pre-scaled thumbnail — shelf tiles are 116px wide.
            pending->covers.insert(vol, QString::fromLatin1(kCoverBase) + QLatin1Char('/')
                                            + pending->mangaId + QLatin1Char('/')
                                            + file + QStringLiteral(".512.jpg"));
            pending->coverIsJa.insert(vol, ja);
        }
        const int total = root.value(QLatin1String("total")).toInt();
        const int next = offset + kCoverPage;
        if (next < total && next < kCoverPage * kCoverPagesMax) {
            stepCovers(pending, next);
            return;
        }
        stepAggregate(pending);
    });
}

// ---- step 3: manga id -> volume->chapter ranges where MangaDex knows them --

void MangaDexCatalogClient::stepAggregate(PendingFetchPtr pending)
{
    // No language filter: the widest chapter DB gives the most range anchors.
    const QUrl url(QString::fromLatin1(kApiBase) + QStringLiteral("/manga/")
                   + pending->mangaId + QStringLiteral("/aggregate"));
    QNetworkRequest req(url);
    applyHeaders(req);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        // Aggregate is best-effort: covers alone still carry the shelf.
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject vols = QJsonDocument::fromJson(reply->readAll())
                                         .object().value(QLatin1String("volumes")).toObject();
            for (auto vit = vols.begin(); vit != vols.end(); ++vit) {
                const double vol = volumeNumber(vit.key());
                if (std::isnan(vol)) continue;                   // the "none" bucket
                const QJsonObject chapters = vit.value().toObject()
                                                 .value(QLatin1String("chapters")).toObject();
                double lo = std::numeric_limits<double>::quiet_NaN();
                double hi = std::numeric_limits<double>::quiet_NaN();
                for (auto cit = chapters.begin(); cit != chapters.end(); ++cit) {
                    const double ch = volumeNumber(cit.key());
                    if (std::isnan(ch)) continue;
                    if (std::isnan(lo) || ch < lo) lo = ch;
                    if (std::isnan(hi) || ch > hi) hi = ch;
                }
                if (!std::isnan(lo)) pending->ranges.insert(vol, {lo, hi});
            }
        }
        finish(pending);
    });
}

// See the header note: phantom decimal cover keys fold into their base volume;
// a decimal survives only when the chapter aggregate anchors it. (Berserk: 58 of
// 101 keys were variant-cover phantoms; systemic across ~half the sampled
// catalogue, probe 2026-07-18. The aggregate was clean in every probe.)
void foldPhantomCoverVolumes(QMap<double, QString>& covers,
                             const QSet<double>& chapterAnchored)
{
    QMap<double, QString> donations;
    for (auto it = covers.begin(); it != covers.end();) {
        const double vol = it.key();
        const bool integral = (vol == std::floor(vol));
        if (integral || chapterAnchored.contains(vol)) { ++it; continue; }
        const double base = std::floor(vol);
        if (!donations.contains(base))
            donations.insert(base, it.value());   // lowest variant donates (map is ascending)
        it = covers.erase(it);
    }
    for (auto it = donations.cbegin(); it != donations.cend(); ++it) {
        if (!covers.contains(it.key()))
            covers.insert(it.key(), it.value());
    }
}

void MangaDexCatalogClient::finish(PendingFetchPtr pending)
{
    QSet<double> anchored;
    for (auto it = pending->ranges.cbegin(); it != pending->ranges.cend(); ++it)
        anchored.insert(it.key());
    foldPhantomCoverVolumes(pending->covers, anchored);

    // Union of cover volumes and range-only volumes, ascending (QMap keeps order).
    QMap<double, bool> all;
    for (auto it = pending->covers.cbegin(); it != pending->covers.cend(); ++it)
        all.insert(it.key(), true);
    for (auto it = pending->ranges.cbegin(); it != pending->ranges.cend(); ++it)
        all.insert(it.key(), true);
    if (all.isEmpty()) {
        emitFailure(pending, QStringLiteral("MangaDex has no volume covers or ranges"));
        return;
    }

    QVariantList volumes;
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        const double vol = it.key();
        QString start, end;
        if (pending->ranges.contains(vol)) {
            const auto range = pending->ranges.value(vol);
            start = QString::number(range.first);
            end   = QString::number(range.second);
        }
        volumes.append(QVariantMap{{QStringLiteral("number"), vol},
                                   {QStringLiteral("cover"), pending->covers.value(vol)},
                                   {QStringLiteral("chapterStart"), start},
                                   {QStringLiteral("chapterEnd"), end}});
    }
    emit catalogReady(pending->title, volumes);
}

} // namespace tankoban::manga::mangadex
