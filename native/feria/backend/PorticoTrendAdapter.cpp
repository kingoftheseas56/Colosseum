#include "PorticoTrendAdapter.h"

#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>

PorticoTrendAdapter::PorticoTrendAdapter(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{
    Q_ASSERT(m_network);
}

bool PorticoTrendAdapter::isConfigured(const QVariantMap &) const
{
    return true;
}

QNetworkReply *PorticoTrendAdapter::get(const QUrl &url, const QVariantMap &headers)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Colosseum-Portico-Trends/0.1");
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toByteArray());
    return m_network->get(request);
}

QNetworkReply *PorticoTrendAdapter::postJson(const QUrl &url, const QByteArray &body,
                                              const QVariantMap &headers)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "Colosseum-Portico-Trends/0.1");
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toByteArray());
    return m_network->post(request, body);
}

bool PorticoTrendAdapter::takeReply(QNetworkReply *reply, QByteArray *body, QString *error) const
{
    const QVariant statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusValue.toInt();
    const bool badHttpStatus = statusValue.isValid() && (status < 200 || status >= 300);
    if (reply->error() != QNetworkReply::NoError || badHttpStatus) {
        *error = statusValue.isValid()
            ? QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString())
            : reply->errorString();
        reply->deleteLater();
        return false;
    }
    *body = reply->readAll();
    reply->deleteLater();
    return true;
}

QString PorticoTrendAdapter::decodeHtml(QString value)
{
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    value.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));

    static const QRegularExpression numeric(QStringLiteral("&#(x?[0-9A-Fa-f]+);"));
    auto match = numeric.match(value);
    while (match.hasMatch()) {
        bool ok = false;
        const QString raw = match.captured(1);
        const uint code = raw.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
            ? raw.mid(1).toUInt(&ok, 16) : raw.toUInt(&ok, 10);
        if (ok) {
            const char32_t codePoint = static_cast<char32_t>(code);
            value.replace(match.capturedStart(), match.capturedLength(),
                          QString::fromUcs4(&codePoint, 1));
        }
        match = numeric.match(value);
    }
    return value.trimmed();
}

QString PorticoTrendAdapter::firstNonEmpty(const QStringList &values)
{
    for (const auto &value : values)
        if (!value.trimmed().isEmpty())
            return value.trimmed();
    return {};
}

QStringList PorticoTrendAdapter::jsonStringList(const QJsonValue &value)
{
    QStringList out;
    for (const auto &entry : value.toArray()) {
        if (entry.isString())
            out.push_back(entry.toString());
    }
    return out;
}

QJsonArray PorticoTrendAdapter::findFirstArrayByKey(const QJsonValue &value, const QString &key)
{
    if (value.isObject()) {
        const auto object = value.toObject();
        if (object.value(key).isArray())
            return object.value(key).toArray();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const auto found = findFirstArrayByKey(it.value(), key);
            if (!found.isEmpty())
                return found;
        }
    } else if (value.isArray()) {
        for (const auto &entry : value.toArray()) {
            const auto found = findFirstArrayByKey(entry, key);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

QString PorticoTrendAdapter::lastThumbnailUrl(const QJsonValue &thumbnailValue)
{
    const auto thumbnails = thumbnailValue.toObject().value(QStringLiteral("thumbnails")).toArray();
    for (qsizetype i = thumbnails.size() - 1; i >= 0; --i) {
        const auto thumbnail = thumbnails.at(i);
        if (thumbnail.isObject()) {
            const QString url = thumbnail.toObject().value(QStringLiteral("url")).toString();
            if (!url.isEmpty())
                return url;
        }
        if (thumbnail.isString()) {
            const QString raw = thumbnail.toString();
            const int start = raw.indexOf(QStringLiteral("url="));

            const int end = raw.indexOf(QLatin1Char(';'), start);
            if (start >= 0)
                return raw.mid(start + 4, end < 0 ? -1 : end - start - 4);
        }
    }
    return {};
}

int PorticoTrendAdapter::yearFromIsoDate(const QString &value)
{
    return value.left(4).toInt();
}
