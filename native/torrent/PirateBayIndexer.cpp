#include "PirateBayIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

PirateBayIndexer::PirateBayIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();
}

void PirateBayIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    Q_UNUSED(limit)
    QUrl url("https://apibay.org/q.php");
    QUrlQuery q;
    q.addQueryItem("q", query);
    if (!categoryId.trimmed().isEmpty())
        q.addQueryItem("cat", categoryId.trimmed());
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

void PirateBayIndexer::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        emit searchError(reply->errorString());
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        markError(reply);
        emit searchError("JSON parse error: " + err.errorString());
        return;
    }

    markSuccess();

    QJsonArray arr = doc.array();
    QList<TorrentResult> results;

    for (const auto& val : arr) {
        QJsonObject row = val.toObject();
        QString name = row.value("name").toString().trimmed();
        QString ih = row.value("info_hash").toString().trimmed().toLower();

        if (name.isEmpty() || ih.isEmpty() || ih == "0")
            continue;

        TorrentResult r;
        r.title      = name;
        QStringList trackers = defaultTrackers();
        trackers.prepend("udp://tracker.bittor.pw:1337/announce");
        r.magnetUri  = buildMagnet(ih, name, trackers);
        r.sizeBytes  = row.value("size").toVariant().toLongLong();
        r.seeders    = row.value("seeders").toVariant().toInt();
        r.leechers   = row.value("leechers").toVariant().toInt();
        r.sourceName = "PirateBay";
        r.sourceKey  = "piratebay";
        r.categoryId = row.value("category").toVariant().toString();
        r.category   = categoryLabel(r.categoryId);

        r.infoHash = canonicalizeInfoHash(ih);
        if (r.infoHash.isEmpty())
            qDebug() << "[PirateBayIndexer] infoHash missing for:" << name;

        const qint64 addedSecs = row.value("added").toVariant().toLongLong();
        if (addedSecs > 0)
            r.publishDate = QDateTime::fromSecsSinceEpoch(addedSecs);

        const QString id = row.value("id").toVariant().toString();
        if (!id.isEmpty())
            r.detailsUrl = QStringLiteral("https://thepiratebay.org/description.php?id=%1").arg(id);

        if (!r.magnetUri.isEmpty())
            results.append(r);
    }

    emit searchFinished(results);
}

QString PirateBayIndexer::categoryLabel(const QString& catId)
{
    if (catId.isEmpty()) return {};
    QChar first = catId.at(0);
    if (first == '1') return "Audio";
    if (first == '2') return "Video";
    if (first == '3') return "Applications";
    if (first == '4') return "Games";
    if (first == '5') return "Porn";
    if (first == '6') return "Other";
    return {};
}
