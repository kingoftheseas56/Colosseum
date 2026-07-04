#include "downloadstore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

DownloadStore::DownloadStore(QObject *parent)
    : QObject(parent)
    , m_defaultDownloadDir(buildDefaultDownloadDir()) {
    m_status.insert(QStringLiteral("kind"), QStringLiteral("idle"));
}

DownloadStore::~DownloadStore() {
    cleanupActiveReply();
}

void DownloadStore::startDownload(const QVariantMap &request) {
    if (m_reply)
        return;

    const QString url = request.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        failDownload(QStringLiteral("No video URL available to download."));
        return;
    }

    setStatus({{QStringLiteral("kind"), QStringLiteral("preparing")}});
    m_outputPath = buildOutputPath(request);
    m_partPath = m_outputPath + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(m_outputPath).absolutePath());

    delete m_file;
    m_file = new QFile(m_partPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString message = QStringLiteral("open: %1").arg(m_file->errorString());
        cleanupActiveReply();
        failDownload(message);
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum"));
    m_reply = m_network.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_reply)
            m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const double ratio = total > 0 ? qBound(0.0, double(received) / double(total), 1.0) : 0.0;
        setStatus({
            {QStringLiteral("kind"), QStringLiteral("downloading")},
            {QStringLiteral("receivedBytes"), received},
            {QStringLiteral("totalBytes"), total > 0 ? QVariant(total) : QVariant()},
            {QStringLiteral("ratio"), ratio},
            {QStringLiteral("path"), m_outputPath}
        });
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply)
            return;
        const QNetworkReply::NetworkError err = m_reply->error();
        const QString errText = m_reply->errorString();
        if (m_file) {
            m_file->write(m_reply->readAll());
            m_file->flush();
            m_file->close();
        }
        m_reply->deleteLater();
        m_reply = nullptr;
        delete m_file;
        m_file = nullptr;

        if (err != QNetworkReply::NoError) {
            QFile::remove(m_partPath);
            failDownload(errText);
            return;
        }
        QFile::remove(m_outputPath);
        if (!QFile::rename(m_partPath, m_outputPath)) {
            QFile::remove(m_partPath);
            failDownload(QStringLiteral("rename failed"));
            return;
        }
        const qint64 bytes = QFileInfo(m_outputPath).size();
        setStatus({
            {QStringLiteral("kind"), QStringLiteral("done")},
            {QStringLiteral("path"), m_outputPath},
            {QStringLiteral("receivedBytes"), bytes},
            {QStringLiteral("totalBytes"), bytes},
            {QStringLiteral("ratio"), 1.0}
        });
        revealDownload();
    });
}

void DownloadStore::cancelDownload() {
    if (!m_reply)
        return;
    cleanupActiveReply();
    QFile::remove(m_partPath);
    setStatus({{QStringLiteral("kind"), QStringLiteral("idle")}});
}

void DownloadStore::revealDownload() {
    const QString path = m_status.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return;
    const QString folder = QFileInfo(path).absoluteDir().absolutePath();
    if (!folder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void DownloadStore::resetDownload() {
    if (m_reply)
        return;
    setStatus({{QStringLiteral("kind"), QStringLiteral("idle")}});
}

QString DownloadStore::buildDefaultDownloadDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    const QString dir = QDir(base).filePath(QStringLiteral("Colosseum"));
    QDir().mkpath(dir);
    return QDir::toNativeSeparators(dir);
}

QString DownloadStore::buildOutputPath(const QVariantMap &request) const {
    const QString requested = request.value(QStringLiteral("outputPath")).toString();
    if (!requested.isEmpty())
        return QDir::toNativeSeparators(requested);

    QString stem = sanitizeFilePart(request.value(QStringLiteral("title")).toString());
    if (stem.isEmpty())
        stem = QStringLiteral("video");
    const QString subtitle = sanitizeFilePart(request.value(QStringLiteral("subtitle")).toString());
    if (!subtitle.isEmpty())
        stem += QStringLiteral(" - ") + subtitle;
    stem += QStringLiteral(" - ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
    return QDir::toNativeSeparators(QDir(m_defaultDownloadDir).filePath(stem + QStringLiteral(".") + extensionFromUrl(request.value(QStringLiteral("url")).toString())));
}

QString DownloadStore::sanitizeFilePart(const QString &value) const {
    QString out = value.simplified();
    out.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("_"));
    out = out.trimmed();
    if (out.size() > 90)
        out = out.left(90).trimmed();
    return out;
}

QString DownloadStore::extensionFromUrl(const QString &url) const {
    const QString path = QUrl(url).path().toLower();
    const QString suffix = QFileInfo(path).suffix();
    if (suffix == QStringLiteral("mkv") || suffix == QStringLiteral("mp4") || suffix == QStringLiteral("webm") || suffix == QStringLiteral("mov"))
        return suffix;
    return QStringLiteral("mp4");
}

void DownloadStore::setStatus(const QVariantMap &status) {
    m_status = status;
    emit changed();
}

void DownloadStore::failDownload(const QString &message) {
    setStatus({
        {QStringLiteral("kind"), QStringLiteral("error")},
        {QStringLiteral("message"), message.isEmpty() ? QStringLiteral("Download failed") : message}
    });
}

void DownloadStore::cleanupActiveReply() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}
