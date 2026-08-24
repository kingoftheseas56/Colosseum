#include "update/UpdateDownload.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace Colosseum::Update {

UpdateDownload::UpdateDownload(QNetworkAccessManager* nam, UpdateCache* cache, QObject* parent)
    : QObject(parent), m_nam(nam), m_cache(cache), m_file(new QFile(this))
{
}

UpdateDownload::UpdateDownload(QNetworkAccessManager* nam,
                               std::unique_ptr<UpdateCache> cache,
                               QObject* parent)
    : UpdateDownload(nam, cache.get(), parent)
{
    m_ownedCache = std::move(cache);
}

bool UpdateDownload::validRequest(const DownloadRequest& request, QString* error) const
{
    if (!m_nam || !m_cache) {
        if (error) *error = QStringLiteral("download_dependencies_missing");
        return false;
    }
    if (request.version.canonical().isEmpty() || request.expectedSize <= 0
        || request.expectedSha256.size() != 32 || !request.url.isValid()
        || request.url.host().isEmpty()
        || (request.url.scheme() != QLatin1String("https")
            && !(request.allowHttpForTests && request.url.scheme() == QLatin1String("http"))) ) {
        if (error) *error = QStringLiteral("invalid_download_request");
        return false;
    }
    return true;
}

void UpdateDownload::start(const DownloadRequest& request)
{
    cancel();
    m_cancelled = false;
    m_finished = false;
    m_request = request;

    QString error;
    if (!validRequest(request, &error)) {
        fail(error, false);
        return;
    }
    m_partPath = m_cache->partPath(request.version, request.assetName, &error);
    m_metadataPath = m_cache->metadataPath(request.version, &error);
    if (m_partPath.isEmpty() || m_metadataPath.isEmpty()) {
        fail(error.isEmpty() ? QStringLiteral("unsafe_cache_path") : error, false);
        return;
    }
    if (!m_cache->ensureVersionDirectory(request.version, &error)) {
        fail(error, false);
        return;
    }

    qint64 existingBytes = 0;
    const auto metadata = m_cache->readMetadata(request.version, nullptr);
    const bool metadataMatches = metadata.has_value()
        && metadata->assetName == request.assetName
        && metadata->expectedSize == request.expectedSize
        && metadata->expectedSha256 == request.expectedSha256
        && metadata->expectedEtag == request.expectedEtag;
    if (metadataMatches && QFile::exists(m_partPath)) {
        existingBytes = QFileInfo(m_partPath).size();
        if (existingBytes < 0 || existingBytes > request.expectedSize) {
            QFile::remove(m_partPath);
            existingBytes = 0;
        }
    } else {
        QFile::remove(m_partPath);
        QFile::remove(m_metadataPath);
    }
    if (!m_cache->preflightSpace(request.expectedSize - existingBytes, &error)) {
        fail(error, true);
        return;
    }

    m_received = existingBytes;
    m_timer.start();
    DownloadMetadata state{request.version, request.assetName, request.expectedSize,
                           request.expectedSha256, request.expectedEtag, m_received};
    if (!m_cache->writeMetadata(state, &error)) {
        fail(error, true);
        return;
    }
    requestNetwork(m_received > 0);
}

void UpdateDownload::requestNetwork(bool resume)
{
    if (m_cancelled || m_finished)
        return;
    m_resume = resume;
    m_requestOffset = resume ? m_received : 0;
    QNetworkRequest request(m_request.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Colosseum/1.1.4");
    if (resume) {
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_requestOffset)
            + "-");
        if (!m_request.expectedEtag.isEmpty())
            request.setRawHeader("If-Range", m_request.expectedEtag.toUtf8());
    }
    m_file->setFileName(m_partPath);
    if (!m_file->open(resume ? (QIODevice::WriteOnly | QIODevice::Append)
                             : (QIODevice::WriteOnly | QIODevice::Truncate))) {
        fail(QStringLiteral("part_open_failed"), true);
        return;
    }

    QNetworkReply* reply = m_nam->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_resume && status == 200) {
            // The server ignored Range. Discard the partial bytes before the first body chunk.
            m_file->resize(0);
            m_received = 0;
            m_resume = false;
            m_requestOffset = 0;
            persistMetadata();
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty())
            return;
        if (m_file->write(bytes) != bytes.size()) {
            reply->abort();
            fail(QStringLiteral("part_write_failed"), true);
            return;
        }
        m_received += bytes.size();
        if (m_received > m_request.expectedSize) {
            reply->abort();
            fail(QStringLiteral("wrong_length"), false);
            return;
        }
        persistMetadata();
        const qint64 elapsed = qMax<qint64>(1, m_timer.elapsed());
        emit progress(m_received, m_request.expectedSize, (m_received * 1000) / elapsed);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_reply == reply)
            m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const qint64 contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const QByteArray responseEtag = reply->rawHeader("ETag");
        if (!reply->isFinished())
            return;
        if (m_cancelled) {
            reply->deleteLater();
            fail(QStringLiteral("cancelled"), true);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            const qint64 expectedResponseBytes = (m_requestOffset > 0 && status == 206)
                ? m_request.expectedSize - m_requestOffset : m_request.expectedSize;
            if (contentLength >= 0 && contentLength != expectedResponseBytes) {
                fail(contentLength < expectedResponseBytes ? QStringLiteral("truncated_response")
                                                            : QStringLiteral("wrong_length"), true);
            } else {
                fail(QStringLiteral("network_error"), true);
            }
            return;
        }
        const bool resumedResponse = m_requestOffset > 0 && status == 206;
        if (m_requestOffset > 0 && status == 206 && !m_request.expectedEtag.isEmpty()
            && !responseEtag.isEmpty() && responseEtag != m_request.expectedEtag) {
            // If-Range should normally produce 200, but a non-conforming server may return a
            // partial body under a new validator. Restart from zero rather than concatenate it.
            m_file->close();
            QFile::remove(m_partPath);
            m_received = 0;
            m_request.expectedEtag = QString::fromUtf8(responseEtag);
            persistMetadata();
            reply->deleteLater();
            requestNetwork(false);
            return;
        }
        const qint64 expectedResponseBytes = resumedResponse
            ? m_request.expectedSize - m_requestOffset : m_request.expectedSize;
        if (contentLength >= 0 && contentLength != expectedResponseBytes) {
            m_file->close();
            reply->deleteLater();
            fail(contentLength < expectedResponseBytes ? QStringLiteral("truncated_response")
                                                        : QStringLiteral("wrong_length"), true);
            return;
        }
        if ((m_requestOffset > 0 && status != 206 && status != 200)
            || (m_requestOffset == 0 && status != 200 && status != 206)) {
            m_file->close();
            reply->deleteLater();
            fail(QStringLiteral("unexpected_http_status"), true);
            return;
        }
        m_file->flush();
        m_file->close();
        reply->deleteLater();
        if (m_received != m_request.expectedSize) {
            fail(QStringLiteral("truncated_response"), true);
            return;
        }
        finishHashVerification();
    });
}

void UpdateDownload::persistMetadata()
{
    if (!m_cache)
        return;
    DownloadMetadata state{m_request.version, m_request.assetName, m_request.expectedSize,
                           m_request.expectedSha256, m_request.expectedEtag, m_received};
    m_cache->writeMetadata(state, nullptr);
}

void UpdateDownload::finishHashVerification()
{
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
            [this, watcher] {
                const QByteArray digest = watcher->result();
                watcher->deleteLater();
                if (m_cancelled) {
                    fail(QStringLiteral("cancelled"), true);
                    return;
                }
                if (digest != m_request.expectedSha256) {
                    fail(QStringLiteral("sha256_mismatch"), false);
                    return;
                }
                QString error;
                QString promotedPath;
                if (!m_cache->promotePart(m_request.version, m_request.assetName,
                                          &promotedPath, &error)) {
                    fail(error.isEmpty() ? QStringLiteral("installer_promotion_failed") : error, true);
                    return;
                }
                QFile::remove(m_metadataPath);
                m_finished = true;
                emit completed(promotedPath);
            });
    const QString path = m_partPath;
    watcher->setFuture(QtConcurrent::run([path] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file))
            return QByteArray{};
        return hash.result();
    }));
}

void UpdateDownload::cancel()
{
    if (m_finished)
        return;
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
}

void UpdateDownload::fail(const QString& errorCode, bool resumable)
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_file && m_file->isOpen())
        m_file->close();
    emit failed(errorCode, resumable);
}

} // namespace Colosseum::Update
