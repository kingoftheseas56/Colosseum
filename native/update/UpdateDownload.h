#pragma once

#include "update/UpdateCache.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace Colosseum::Update {

struct DownloadRequest {
    Version version;
    QUrl url;
    QString assetName;
    qint64 expectedSize = 0;
    QByteArray expectedSha256;
    QString expectedEtag;
    bool allowHttpForTests = false;
};

class UpdateDownload final : public QObject {
    Q_OBJECT
public:
    UpdateDownload(QNetworkAccessManager* nam, UpdateCache* cache, QObject* parent = nullptr);
    UpdateDownload(QNetworkAccessManager* nam, std::unique_ptr<UpdateCache> cache,
                   QObject* parent = nullptr);

    void start(const DownloadRequest& request);
    void cancel();

signals:
    void progress(qint64 received, qint64 total, qint64 bytesPerSecond);
    void completed(QString verifiedInstallerPath);
    void failed(QString errorCode, bool resumable);

private:
    void requestNetwork(bool resume);
    void persistMetadata();
    void finishHashVerification();
    void fail(const QString& errorCode, bool resumable);
    bool validRequest(const DownloadRequest& request, QString* error) const;

    QNetworkAccessManager* m_nam = nullptr;
    std::unique_ptr<UpdateCache> m_ownedCache;
    UpdateCache* m_cache = nullptr;
    QPointer<QNetworkReply> m_reply;
    DownloadRequest m_request;
    QFile* m_file = nullptr;
    QString m_partPath;
    QString m_metadataPath;
    qint64 m_received = 0;
    qint64 m_requestOffset = 0;
    bool m_resume = false;
    bool m_cancelled = false;
    bool m_finished = false;
    QElapsedTimer m_timer;
};

} // namespace Colosseum::Update
