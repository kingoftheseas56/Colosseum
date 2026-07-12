#include "TorrentsCsvIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

TorrentsCsvIndexer::TorrentsCsvIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();
}

void TorrentsCsvIndexer::search(const QString& query, int limit, const QString& /*categoryId*/)
{
    QUrl url("https://torrents-csv.com/service/search");
    QUrlQuery q;
    q.addQueryItem("q", query);
    q.addQueryItem("size", QString::number(qBound(1, limit, 100)));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(15000);

    startRequestTimer();
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void TorrentsCsvIndexer::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        emit searchError(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        markError(reply);
        emit searchError("JSON parse error: " + err.errorString());
        return;
    }

    markSuccess();

    QJsonArray torrents;
    if (doc.isObject())
        torrents = doc.object().value("torrents").toArray();
    else if (doc.isArray())
        torrents = doc.array();

    QList<TorrentResult> results;
    for (const auto& val : torrents) {
        QJsonObject row = val.toObject();
        QString ih = row.value("infohash").toString().trimmed().toLower();
        QString name = row.value("name").toString().trimmed();
        if (ih.isEmpty() || name.isEmpty())
            continue;

        TorrentResult r;
        r.title     = name;
        r.magnetUri = buildMagnet(ih, name);
        r.sizeBytes = row.value("size_bytes").toInteger(0);
        r.seeders   = row.value("seeders").toInt(0);
        r.leechers  = row.value("leechers").toInt(0);
        r.sourceName = "Torrents-CSV";
        r.sourceKey  = "torrentscsv";

        r.infoHash = canonicalizeInfoHash(ih);
        if (r.infoHash.isEmpty())
            qDebug() << "[TorrentsCsvIndexer] infoHash missing for:" << name;

        const qint64 createdSecs = row.value("created_unix").toVariant().toLongLong();
        if (createdSecs > 0)
            r.publishDate = QDateTime::fromSecsSinceEpoch(createdSecs);

        if (!r.magnetUri.isEmpty())
            results.append(r);
    }

    emit searchFinished(results);
}
