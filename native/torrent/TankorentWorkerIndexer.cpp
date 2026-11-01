#include "TankorentWorkerIndexer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <cmath>
#include <limits>

namespace {
constexpr int kMaxQueryLength = 512;
constexpr int kMaxCategoryLength = 128;
constexpr int kMaxResults = 300;
constexpr int kMaxResponseBytes = 1024 * 1024;
constexpr int kMaxTitleLength = 512;
constexpr int kMaxMagnetLength = 8192;
constexpr int kMaxDetailsUrlLength = 2048;
constexpr int kMaxCategoryValueLength = 128;
constexpr int kMaxRemoteErrorLength = 512;
constexpr int kTransferTimeoutMs = 25000;
constexpr qint64 kMaxJsonSafeInteger = 9007199254740991LL;

QString displayNameForSource(const QString &sourceId)
{
    if (sourceId == QLatin1String("piratebay")) return QStringLiteral("PirateBay");
    if (sourceId == QLatin1String("exttorrents")) return QStringLiteral("ExtraTorrents");
    if (sourceId == QLatin1String("torrentscsv")) return QStringLiteral("Torrents-CSV");
    if (sourceId == QLatin1String("knaben")) return QStringLiteral("Knaben");
    return {};
}

bool hasControlCharacters(const QString &value)
{
    for (const QChar ch : value) {
        if (ch.isNull() || ch.category() == QChar::Other_Control)
            return true;
    }
    return false;
}

bool readString(const QJsonObject &object, const QString &key, QString &out,
                int maximumLength, bool required, bool allowEmpty = true)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        out.clear();
        return !required;
    }
    if (!value.isString())
        return false;

    out = value.toString();
    return out.size() <= maximumLength
        && !hasControlCharacters(out)
        && (allowEmpty || !out.trimmed().isEmpty());
}

bool readInteger(const QJsonValue &value, qint64 minimum, qint64 maximum,
                 qint64 &out)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(minimum)
        || number > static_cast<double>(maximum))
        return false;
    out = static_cast<qint64>(number);
    return true;
}

bool validDetailsUrl(const QString &raw)
{
    if (raw.isEmpty())
        return true;
    if (raw.size() > kMaxDetailsUrlLength || hasControlCharacters(raw))
        return false;

    const QUrl url(raw, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    return url.isValid()
        && (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}

bool validMagnet(const QString &raw, const QString &infoHash)
{
    if (raw.isEmpty() || raw.size() > kMaxMagnetLength || hasControlCharacters(raw))
        return false;

    const QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QLatin1String("magnet"), Qt::CaseInsensitive) != 0
        || !url.host().isEmpty() || !url.userInfo().isEmpty())
        return false;

    const QUrlQuery query(url);
    const auto items = query.queryItems(QUrl::FullyDecoded);
    int xtCount = 0;
    int trackerCount = 0;
    for (const auto &item : items) {
        const QString key = item.first.toLower();
        if (key == QLatin1String("xt")) {
            ++xtCount;
            const QString value = item.second;
            const QString prefix = QStringLiteral("urn:btih:");
            if (!value.startsWith(prefix, Qt::CaseInsensitive)
                || canonicalizeInfoHash(value.mid(prefix.size())) != infoHash)
                return false;
        } else if (key == QLatin1String("tr")) {
            if (++trackerCount > 64)
                return false;
            const QUrl tracker(item.second, QUrl::StrictMode);
            const QString scheme = tracker.scheme().toLower();
            if (!tracker.isValid() || tracker.host().isEmpty() || !tracker.userInfo().isEmpty()
                || (scheme != QLatin1String("udp") && scheme != QLatin1String("http")
                    && scheme != QLatin1String("https")))
                return false;
        } else if (key != QLatin1String("dn")) {
            // Do not pass unreviewed magnet extensions such as web-seed URLs
            // from a remote search response into the native torrent engine.
            return false;
        }
    }
    return xtCount == 1;
}

bool readPublishDate(const QJsonObject &object, QDateTime &out)
{
    const QJsonValue value = object.value(QStringLiteral("publishDate"));
    if (value.isNull()) {
        out = {};
        return true;
    }
    if (!value.isString())
        return false;
    const QString text = value.toString();
    if (text.isEmpty()) {
        out = {};
        return true;
    }
    if (text.size() > 64 || hasControlCharacters(text))
        return false;

    QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!date.isValid())
        date = QDateTime::fromString(text, Qt::ISODate);
    if (!date.isValid())
        return false;
    out = date;
    return true;
}

bool parseResult(const QJsonObject &object, const QString &sourceId,
                 const QString &sourceName, TorrentResult &result)
{
    QString title;
    QString magnet;
    QString responseSourceName;
    QString responseSourceKey;
    QString categoryId;
    QString category;
    QString infoHash;
    QString detailsUrl;
    qint64 sizeBytes = 0;
    qint64 seeders = 0;
    qint64 leechers = 0;

    if (!readString(object, QStringLiteral("title"), title, kMaxTitleLength,
                    true, false)
        || !readString(object, QStringLiteral("magnetUri"), magnet,
                       kMaxMagnetLength, true, false)
        || !readString(object, QStringLiteral("sourceName"), responseSourceName,
                       64, true, false)
        || !readString(object, QStringLiteral("sourceKey"), responseSourceKey,
                       64, true, false)
        || !readString(object, QStringLiteral("categoryId"), categoryId,
                       kMaxCategoryValueLength, false)
        || !readString(object, QStringLiteral("category"), category,
                       kMaxCategoryValueLength, false)
        || !readString(object, QStringLiteral("infoHash"), infoHash, 40,
                       true, false)
        || !readString(object, QStringLiteral("detailsUrl"), detailsUrl,
                       kMaxDetailsUrlLength, false)
        || responseSourceKey != sourceId || responseSourceName != sourceName
        || !readInteger(object.value(QStringLiteral("sizeBytes")), 0,
                        kMaxJsonSafeInteger, sizeBytes)
        || !readInteger(object.value(QStringLiteral("seeders")), 0,
                        std::numeric_limits<int>::max(), seeders)
        || !readInteger(object.value(QStringLiteral("leechers")), 0,
                        std::numeric_limits<int>::max(), leechers))
        return false;

    infoHash = canonicalizeInfoHash(infoHash);
    if (infoHash.isEmpty() || !validMagnet(magnet, infoHash)
        || !validDetailsUrl(detailsUrl))
        return false;

    QDateTime publishDate;
    if (!object.contains(QStringLiteral("publishDate"))
        || !readPublishDate(object, publishDate))
        return false;

    result.title = title.trimmed();
    result.magnetUri = magnet;
    result.infoHash = infoHash;
    result.sizeBytes = sizeBytes;
    result.seeders = static_cast<int>(seeders);
    result.leechers = static_cast<int>(leechers);
    // Use the locally-owned names after validating the worker's provenance.
    result.sourceName = sourceName;
    result.sourceKey = sourceId;
    result.categoryId = categoryId;
    result.category = category;
    result.publishDate = publishDate;
    result.detailsUrl = detailsUrl;
    return true;
}

bool readContractVersion(const QJsonObject &object)
{
    qint64 version = 0;
    return readInteger(object.value(QStringLiteral("contractVersion")), 1, 1, version);
}

IndexerHealth healthFromString(const QString &health)
{
    if (health == QLatin1String("auth_required")) return IndexerHealth::AuthRequired;
    if (health == QLatin1String("cloudflare_blocked")) return IndexerHealth::CloudflareBlocked;
    if (health == QLatin1String("rate_limited")) return IndexerHealth::RateLimited;
    if (health == QLatin1String("unreachable")) return IndexerHealth::Unreachable;
    return IndexerHealth::Unknown;
}

QString safeRemoteError(QString error)
{
    error = error.trimmed();
    error.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")),
                  QStringLiteral(" "));
    if (error.size() > 240)
        error.truncate(240);
    return error;
}
} // namespace

TankorentWorkerIndexer::TankorentWorkerIndexer(QNetworkAccessManager *nam,
                                               QString sourceId,
                                               QObject *parent)
    : TorrentIndexer(parent), m_nam(nam), m_sourceId(std::move(sourceId))
{
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout, this, [this] {
        QPointer<QNetworkReply> reply = m_reply;
        if (!reply)
            return;
        m_timedOut = true;
        reply->abort();
        // QNetworkReply::abort() should finish the reply. Handle unusual reply
        // implementations too, while the identity check prevents a reentrant
        // finished signal from being processed twice.
        if (m_reply == reply)
            onReplyFinished(reply);
    });
    if (supportsSource(m_sourceId))
        loadPersistedHealth();
}

TankorentWorkerIndexer::~TankorentWorkerIndexer()
{
    cancel();
}

QString TankorentWorkerIndexer::displayName() const
{
    return displayNameForSource(m_sourceId);
}

QString TankorentWorkerIndexer::categoryFor(const QString &mediaType) const
{
    if (m_sourceId != QLatin1String("piratebay"))
        return {};
    if (mediaType == QLatin1String("books"))
        return QStringLiteral("601");
    if (mediaType == QLatin1String("audiobooks"))
        return QStringLiteral("102");
    return {};
}

bool TankorentWorkerIndexer::supportsSource(const QString &sourceId)
{
    return sourceId == QLatin1String("piratebay")
        || sourceId == QLatin1String("exttorrents")
        || sourceId == QLatin1String("torrentscsv")
        || sourceId == QLatin1String("knaben");
}

QUrl TankorentWorkerIndexer::endpointForSource(const QString &sourceId)
{
    if (!supportsSource(sourceId))
        return {};
    return QUrl(QStringLiteral("https://colosseum-tankorent.colosseum-watchparty-relay.workers.dev/v1/%1/search")
                    .arg(sourceId));
}

QByteArray TankorentWorkerIndexer::buildRequestBody(const QString &query, int limit,
                                                    const QString &categoryId)
{
    QJsonObject body;
    body.insert(QStringLiteral("query"), query.left(kMaxQueryLength));
    body.insert(QStringLiteral("limit"), qBound(1, limit, kMaxResults));
    body.insert(QStringLiteral("categoryId"), categoryId.left(kMaxCategoryLength));
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void TankorentWorkerIndexer::search(const QString &query, int limit,
                                   const QString &categoryId)
{
    m_cancelled = false;
    if (!supportsSource(m_sourceId)) {
        QTimer::singleShot(0, this, [this] { emit searchError(QStringLiteral("Unsupported Tankorent source")); });
        return;
    }
    if (!m_nam) {
        QTimer::singleShot(0, this, [this] { emit searchError(QStringLiteral("Tankorent network manager is unavailable")); });
        return;
    }
    if (m_reply) {
        QTimer::singleShot(0, this, [this] { emit searchError(QStringLiteral("Tankorent search is already active")); });
        return;
    }
    if (query.trimmed().isEmpty() || query.size() > kMaxQueryLength
        || categoryId.size() > kMaxCategoryLength
        || hasControlCharacters(query) || hasControlCharacters(categoryId)) {
        QTimer::singleShot(0, this, [this] {
            if (!m_cancelled)
                emit searchError(QStringLiteral("Tankorent search fields are invalid or exceed their size limits"));
        });
        return;
    }

    m_requestedLimit = qBound(1, limit, kMaxResults);
    m_responseBody.clear();
    m_responseTooLarge = false;
    m_timedOut = false;
    const QUrl endpoint = endpointForSource(m_sourceId);
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Colosseum-Tankorent/1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    startRequestTimer();
    QNetworkReply *reply = m_nam->post(
        request, buildRequestBody(query, m_requestedLimit, categoryId));
    m_reply = reply;
    m_deadlineTimer.start(kTransferTimeoutMs);
    reply->setReadBufferSize(kMaxResponseBytes + 1);
    connect(reply, &QIODevice::readyRead, this,
            [this, reply] { readAvailable(reply); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onReplyFinished(reply); });

    bool contentLengthOk = false;
    const qlonglong contentLength = reply->header(QNetworkRequest::ContentLengthHeader)
                                        .toLongLong(&contentLengthOk);
    if (contentLengthOk && contentLength > kMaxResponseBytes) {
        m_responseTooLarge = true;
        reply->abort();
    }
}

void TankorentWorkerIndexer::readAvailable(QNetworkReply *reply)
{
    if (!reply || m_reply != reply || m_responseTooLarge || m_cancelled)
        return;
    const qint64 remaining = qint64(kMaxResponseBytes) + 1 - m_responseBody.size();
    const QByteArray chunk = reply->read(qMax<qint64>(1, remaining));
    m_responseBody.append(chunk);
    if (m_responseBody.size() > kMaxResponseBytes) {
        m_responseTooLarge = true;
        reply->abort();
    }
}

void TankorentWorkerIndexer::onReplyFinished(QNetworkReply *reply)
{
    if (!reply || m_reply != reply || m_cancelled)
        return;
    readAvailable(reply);
    // abort() may emit finished synchronously when the body cap is reached.
    // Its nested completion owns the result; the outer call must stop here.
    if (m_reply != reply || m_cancelled)
        return;
    if (m_timedOut) {
        finishError(reply, QStringLiteral("Tankorent search exceeded the 25 second deadline"));
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_responseTooLarge) {
        finishError(reply, QStringLiteral("Tankorent response exceeds the 1 MiB limit"));
        return;
    }
    if (status >= 300 && status < 400) {
        finishError(reply, QStringLiteral("Tankorent redirects are not followed (HTTP %1)").arg(status));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(m_responseBody, &parseError);
    if (!document.isObject()) {
        const QString detail = parseError.error == QJsonParseError::NoError
            ? QStringLiteral("response is not a JSON object")
            : QStringLiteral("invalid JSON response");
        finishError(reply, QStringLiteral("Tankorent returned an %1").arg(detail));
        return;
    }
    const QJsonObject object = document.object();
    if (!readContractVersion(object)
        || object.value(QStringLiteral("sourceId")).toString() != m_sourceId) {
        finishError(reply, QStringLiteral("Tankorent response contract version or source id is invalid"));
        return;
    }

    if (status < 200 || status >= 300) {
        QString error;
        QString healthText;
        if (!readString(object, QStringLiteral("error"), error,
                        kMaxRemoteErrorLength, true, false)) {
            finishError(reply, QStringLiteral("Tankorent error response schema is invalid"));
            return;
        }
        const QJsonValue healthValue = object.value(QStringLiteral("health"));
        IndexerHealth healthOverride = IndexerHealth::Unknown;
        if (!healthValue.isUndefined() && !healthValue.isNull()) {
            if (!healthValue.isString()) {
                finishError(reply, QStringLiteral("Tankorent error health schema is invalid"));
                return;
            }
            healthText = healthValue.toString();
            healthOverride = healthFromString(healthText);
            if (healthOverride == IndexerHealth::Unknown) {
                finishError(reply, QStringLiteral("Tankorent error health value is invalid"));
                return;
            }
        }
        const QString cleanError = safeRemoteError(error);
        finishError(reply,
                    QStringLiteral("Tankorent search failed (HTTP %1): %2")
                        .arg(status).arg(cleanError.isEmpty() ? QStringLiteral("remote error") : cleanError),
                    healthOverride);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        finishError(reply, reply->errorString());
        return;
    }
    const QJsonValue resultValue = object.value(QStringLiteral("result"));
    if (!resultValue.isArray()) {
        finishError(reply, QStringLiteral("Tankorent success response has no result array"));
        return;
    }

    const QJsonArray rows = resultValue.toArray();
    if (rows.size() > m_requestedLimit) {
        finishError(reply, QStringLiteral("Tankorent returned more results than requested"));
        return;
    }
    QList<TorrentResult> results;
    results.reserve(rows.size());
    for (const QJsonValue &rowValue : rows) {
        if (!rowValue.isObject()) {
            finishError(reply, QStringLiteral("Tankorent result row is not an object"));
            return;
        }
        TorrentResult result;
        if (!parseResult(rowValue.toObject(), m_sourceId, displayName(), result)) {
            finishError(reply, QStringLiteral("Tankorent result row failed validation"));
            return;
        }
        results.append(result);
    }

    finishSuccess(reply, results);
}

void TankorentWorkerIndexer::finishError(QNetworkReply *reply,
                                         const QString &message,
                                         IndexerHealth healthOverride)
{
    markError(reply);
    m_lastError = message.left(512);
    if (healthOverride != IndexerHealth::Unknown)
        m_health = healthOverride;
    savePersistedHealth();
    releaseReply(reply);
    emit searchError(m_lastError);
}

void TankorentWorkerIndexer::finishSuccess(QNetworkReply *reply,
                                           const QList<TorrentResult> &results)
{
    markSuccess();
    releaseReply(reply);
    emit searchFinished(results);
}

void TankorentWorkerIndexer::releaseReply(QNetworkReply *reply)
{
    m_deadlineTimer.stop();
    if (reply) {
        disconnect(reply, nullptr, this, nullptr);
        reply->deleteLater();
    }
    if (m_reply == reply)
        m_reply.clear();
    m_responseBody.clear();
    m_responseTooLarge = false;
    m_timedOut = false;
}

void TankorentWorkerIndexer::cancel()
{
    m_cancelled = true;
    m_deadlineTimer.stop();
    m_timedOut = false;
    QNetworkReply *reply = m_reply.data();
    if (!reply)
        return;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    m_reply.clear();
    m_responseBody.clear();
    m_responseTooLarge = false;
}
