#include "NuvioNetwork.h"

#include "engine/TankoyomiNetworkPolicy.h"

#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace NuvioNetwork {
namespace {
bool publicAddress(const QHostAddress &address)
{
    return TankoyomiNetworkPolicy::resolvedAddressAllowed(address, QStringLiteral("public-https"));
}

class RepositoryRequest final : public QObject
{
public:
    RepositoryRequest(qsizetype maxBytes, QObject *parent, FetchCompletion complete)
        : QObject(parent), m_maxBytes(maxBytes), m_complete(std::move(complete))
    {
        m_manager.setProxy(QNetworkProxy::NoProxy);
        m_deadline.setSingleShot(true);
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            finish({}, {}, tr("The repository request timed out."));
        });
    }

    void start(const QUrl &url)
    {
        m_deadline.start(15000);
        fetch(url, 0);
    }

private:
    void fetch(const QUrl &url, int redirects)
    {
        if (!publicUrl(url, true)) {
            finish({}, {}, tr("Use a public HTTPS repository address."));
            return;
        }
        resolve(url, this, [this, url, redirects](const QHostAddress &address, const QString &error) {
            if (!m_complete)
                return;
            if (!error.isEmpty()) {
                finish({}, {}, tr("The repository host could not be reached safely."));
                return;
            }

            QNetworkRequest request(url);
            pin(request, address);
            request.setDecompressedSafetyCheckThreshold(m_maxBytes);
            request.setRawHeader("User-Agent", "Colosseum/Nuvio");
            request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
            QNetworkReply *reply = m_manager.get(request);
            reply->setReadBufferSize(m_maxBytes + 1);
            connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
                if (reply->bytesAvailable() > m_maxBytes)
                    finish({}, {}, tr("The repository response is too large."));
            });
            connect(reply, &QNetworkReply::finished, this, [this, reply, url, redirects] {
                reply->deleteLater();
                if (!m_complete)
                    return;
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                if (status >= 300 && status < 400 && !location.isEmpty()) {
                    if (redirects >= 5) {
                        finish({}, {}, tr("The repository redirected too many times."));
                        return;
                    }
                    const auto target = url.resolved(location);
                    if (target.host() != url.host() || target.port(443) != url.port(443)) {
                        finish({}, {}, tr("The repository redirected to a different host."));
                        return;
                    }
                    fetch(target, redirects + 1);
                    return;
                }
                if (reply->error() != QNetworkReply::NoError || status != 200) {
                    finish({}, {}, tr("The repository could not be downloaded."));
                    return;
                }
                if (reply->bytesAvailable() > m_maxBytes) {
                    finish({}, {}, tr("The repository response is too large."));
                    return;
                }
                finish(reply->read(m_maxBytes), url, {});
            });
        });
    }

    void finish(const QByteArray &body, const QUrl &url, const QString &error)
    {
        m_deadline.stop();
        auto complete = std::exchange(m_complete, {});
        deleteLater();
        if (complete)
            complete(body, url, error);
    }

    qsizetype m_maxBytes;
    FetchCompletion m_complete;
    QNetworkAccessManager m_manager;
    QTimer m_deadline;
};
}

bool publicUrl(const QUrl &url, bool httpsOnly)
{
    const QString host = url.host();
    const bool https = url.scheme() == QLatin1String("https");
    if (!url.isValid() || url.isRelative() || host.isEmpty()
        || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        || (!https && (httpsOnly || url.scheme() != QLatin1String("http")))
        || (url.port() != -1 && url.port() != (https ? 443 : 80))) {
        return false;
    }
    const QHostAddress literal(host);
    if (!literal.isNull())
        return publicAddress(literal);
    return host.contains(QLatin1Char('.')) && !host.endsWith(QLatin1Char('.'))
        && !host.endsWith(QLatin1String(".localhost"))
        && !host.endsWith(QLatin1String(".local"))
        && !host.endsWith(QLatin1String(".internal"));
}

void resolve(const QUrl &url, QObject *context, Resolution complete)
{
    if (!publicUrl(url)) {
        QTimer::singleShot(0, context, [complete = std::move(complete)] {
            complete({}, QStringLiteral("network_url_blocked"));
        });
        return;
    }
    const QHostAddress literal(url.host());
    if (!literal.isNull()) {
        QTimer::singleShot(0, context, [literal, complete = std::move(complete)] {
            complete(literal, {});
        });
        return;
    }
    QHostInfo::lookupHost(url.host(), context, [complete = std::move(complete)](const QHostInfo &info) {
        const auto addresses = info.addresses();
        if (addresses.isEmpty()) {
            complete({}, QStringLiteral("upstream_dns"));
            return;
        }
        if (!std::all_of(addresses.cbegin(), addresses.cend(), publicAddress)) {
            complete({}, QStringLiteral("network_address_blocked"));
            return;
        }
        const auto ipv4 = std::find_if(addresses.cbegin(), addresses.cend(), [](const auto &address) {
            return address.protocol() == QAbstractSocket::IPv4Protocol;
        });
        complete(ipv4 == addresses.cend() ? addresses.first() : *ipv4, {});
    });
}

void pin(QNetworkRequest &request, const QHostAddress &address)
{
    const QUrl original = request.url();
    auto pinned = original;
    pinned.setHost(address.toString());
    request.setUrl(pinned);
    request.setPeerVerifyName(original.host());
    request.setRawHeader("Host", original.authority(QUrl::FullyEncoded).toLatin1());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
}

void getRepositoryAsset(const QUrl &url, qsizetype maxBytes, QObject *context,
                        FetchCompletion complete)
{
    auto *job = new RepositoryRequest(maxBytes, context, std::move(complete));
    QTimer::singleShot(0, job, [job, url] { job->start(url); });
}
}
