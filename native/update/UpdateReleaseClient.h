#pragma once

#include "update/UpdateManifest.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace Colosseum::Update {

struct ReleaseCheckResult {
    enum class Status { Valid, NotModified, Rejected, NetworkError } status = Status::Rejected;
    Manifest manifest;
    QHash<QString, QUrl> assetUrls;
    QString etag;
    QString errorCode;
    // Exact verified bytes retained for the service chronicle.  The service
    // re-verifies these bytes before loading them after a restart.
    QByteArray verifiedManifestBytes;
    QByteArray verifiedSignatureBytes;
};

struct ReleaseClientConfig {
    QUrl latestReleaseUrl;
    QString repository = QStringLiteral("kingoftheseas56/Colosseum");
    QByteArray publicKey;
    bool allowHttpForTests = false;
    int timeoutMs = 15000;
};

class UpdateReleaseClient final : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(ReleaseCheckResult)>;

    UpdateReleaseClient(QNetworkAccessManager* nam, ReleaseClientConfig config,
                        QObject* parent = nullptr);

    void checkLatest(const QString& priorEtag, Callback done);
    void cancel();

private:
    struct FetchResult {
        bool transportOk = false;
        bool bodyTooLarge = false;
        bool unsafeRedirect = false;
        int httpStatus = 0;
        QByteArray body;
        QString etag;
        QString errorCode;
    };

    void fetch(const QUrl& url, qint64 cap, const QByteArray& priorEtag,
               std::function<void(const FetchResult&)> done);
    void finish(ReleaseCheckResult result);
    bool allowedUrl(const QUrl& url) const;

    QNetworkAccessManager* m_nam = nullptr;
    ReleaseClientConfig m_config;
    QPointer<QNetworkReply> m_reply;
    Callback m_done;
    bool m_cancelled = false;
};

} // namespace Colosseum::Update
