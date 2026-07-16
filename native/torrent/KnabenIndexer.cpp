#include "KnabenIndexer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
const char* kEndpoint = "https://api.knaben.org/v1";
}

KnabenIndexer::KnabenIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();
}

QByteArray KnabenIndexer::buildRequestBody(const QString& query, int limit,
                                           const QString& categoryId)
{
    QJsonObject body;
    body[QStringLiteral("search_type")]  = QStringLiteral("score");   // fuzzy relevance match
    body[QStringLiteral("search_field")] = QStringLiteral("title");
    body[QStringLiteral("query")]        = query;
    // Deliberately NO "order_by": knaben's default is relevance. order_by=seeders
    // makes the API ignore the query and return the global top-seeded firehose
    // (SEO-spam cracks). Our ranker applies the seeder tie-break after identity.
    body[QStringLiteral("hide_unsafe")]  = true;    // malware-flagged rows out
    body[QStringLiteral("hide_xxx")]     = true;    // adult out at the source
    body[QStringLiteral("size")]         = qBound(1, limit, 300);
    // Optional source-side category scope (empty by default — see categoryFor()).
    const int cat = categoryId.trimmed().toInt();
    if (cat > 0)
        body[QStringLiteral("categories")] = QJsonArray{ cat };
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void KnabenIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    QNetworkRequest req{ QUrl(QString::fromLatin1(kEndpoint)) };
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Match the sibling indexers' UA (PirateBayIndexer) so the fingerprint we
    // present to Cloudflare is representative of the whole search path.
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(15000);

    startRequestTimer();
    auto* reply = m_nam->post(req, buildRequestBody(query, limit, categoryId));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void KnabenIndexer::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        emit searchError(reply->errorString());
        return;
    }

    const QByteArray raw = reply->readAll();
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        // A JSON API that suddenly returns non-JSON is a Cloudflare challenge
        // page (or an outage). Classify it and surface it, never silently empty.
        markError(reply);
        emit searchError(QStringLiteral("knaben: non-JSON response (Cloudflare wall / outage?)"));
        return;
    }

    markSuccess();
    emit searchFinished(parseHits(raw));
}

QList<TorrentResult> KnabenIndexer::parseHits(const QByteArray& body)
{
    QList<TorrentResult> out;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return out;

    const QJsonArray hits = doc.object().value(QStringLiteral("hits")).toArray();
    out.reserve(hits.size());
    for (const QJsonValue& v : hits) {
        const QJsonObject o = v.toObject();
        const QString title = o.value(QStringLiteral("title")).toString().trimmed();
        const QString ih = canonicalizeInfoHash(o.value(QStringLiteral("hash")).toString());
        if (title.isEmpty() || ih.isEmpty())
            continue;   // no usable 40-hex infohash — never guess

        TorrentResult r;
        r.title    = title;
        r.infoHash = ih;
        // Prefer knaben's own magnet (it carries a tracker list); otherwise
        // synthesize one from the canonical infohash + our default trackers.
        const QString magnet = o.value(QStringLiteral("magnetUrl")).toString().trimmed();
        r.magnetUri = magnet.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)
                        ? magnet : buildMagnet(ih, title);
        r.sizeBytes = o.value(QStringLiteral("bytes")).toVariant().toLongLong();
        r.seeders   = o.value(QStringLiteral("seeders")).toVariant().toInt();
        r.leechers  = o.value(QStringLiteral("peers")).toVariant().toInt();
        r.sourceName = QStringLiteral("Knaben");
        r.sourceKey  = QStringLiteral("knaben");
        // Keep the ORIGIN tracker (e.g. "1337x", "Nyaa.si") visible so a pick's
        // true provenance is honest in the picker, not hidden behind "Knaben".
        const QString tracker = o.value(QStringLiteral("tracker")).toString().trimmed();
        if (!tracker.isEmpty())
            r.category = tracker;
        const QString details = o.value(QStringLiteral("details")).toString().trimmed();
        if (!details.isEmpty())
            r.detailsUrl = details;
        const QString date = o.value(QStringLiteral("date")).toString();
        if (!date.isEmpty()) {
            const QDateTime dt = QDateTime::fromString(date, Qt::ISODate);
            if (dt.isValid())
                r.publishDate = dt;
        }
        out.append(r);
    }
    return out;
}
