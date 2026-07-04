#include "downloadstore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

DownloadStore::DownloadStore(QObject *parent)
    : QObject(parent)
    , m_defaultDownloadDir(buildDefaultDownloadDir()) {
    m_status.insert(QStringLiteral("kind"), QStringLiteral("idle"));
    loadIndex();
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
    m_activeRequest = request;
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
        recordFinished(m_outputPath, bytes);
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
    // Live jobs carry their identity so aggregators (LocalDownloads) can label
    // the row without reaching into the request.
    const QString kind = m_status.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("preparing") || kind == QStringLiteral("downloading")) {
        if (!m_status.contains(QStringLiteral("title")))
            m_status.insert(QStringLiteral("title"), m_activeRequest.value(QStringLiteral("title")));
        if (!m_status.contains(QStringLiteral("id")))
            m_status.insert(QStringLiteral("id"), m_activeRequest.value(QStringLiteral("id")));
    }
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

// ── downloaded-videos library (<appdata>/videos/index.json, atomic writes) ──

QString DownloadStore::indexPath() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/videos");
    QDir().mkpath(base);
    return base + QStringLiteral("/index.json");
}

void DownloadStore::loadIndex() {
    QFile f(indexPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject rootObj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = rootObj.constBegin(); it != rootObj.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.id = it.key();
        e.kind = o.value(QStringLiteral("kind")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        e.subtitle = o.value(QStringLiteral("subtitle")).toString();
        e.seriesTitle = o.value(QStringLiteral("seriesTitle")).toString();
        e.season = o.value(QStringLiteral("season")).toInt();
        e.episode = o.value(QStringLiteral("episode")).toInt();
        e.path = o.value(QStringLiteral("path")).toString();
        e.bytes = qint64(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = qint64(o.value(QStringLiteral("addedAt")).toDouble());
        if (!e.path.isEmpty())
            m_index.insert(e.id, e);
    }
    qInfo("[download] loaded video index: %d files", int(m_index.size()));
}

void DownloadStore::saveIndex() const {
    QJsonObject rootObj;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry &e = it.value();
        rootObj.insert(it.key(), QJsonObject{
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("title"), e.title},
            {QStringLiteral("subtitle"), e.subtitle},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("season"), e.season},
            {QStringLiteral("episode"), e.episode},
            {QStringLiteral("path"), e.path},
            {QStringLiteral("bytes"), double(e.bytes)},
            {QStringLiteral("addedAt"), double(e.addedAt)}
        });
    }
    QSaveFile f(indexPath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
    f.commit();
}

void DownloadStore::recordFinished(const QString &path, qint64 bytes) {
    Entry e;
    e.id = m_activeRequest.value(QStringLiteral("id")).toString();
    if (e.id.isEmpty())
        e.id = QStringLiteral("video-%1").arg(QDateTime::currentMSecsSinceEpoch());
    e.kind = m_activeRequest.value(QStringLiteral("kind")).toString();
    e.season = m_activeRequest.value(QStringLiteral("season")).toInt();
    e.episode = m_activeRequest.value(QStringLiteral("episode")).toInt();
    if (e.kind.isEmpty())
        e.kind = (e.season > 0 || e.episode > 0) ? QStringLiteral("episode") : QStringLiteral("movie");
    e.title = m_activeRequest.value(QStringLiteral("title")).toString();
    e.subtitle = m_activeRequest.value(QStringLiteral("subtitle")).toString();
    e.seriesTitle = m_activeRequest.value(QStringLiteral("seriesTitle")).toString();
    if (e.seriesTitle.isEmpty() && e.kind == QStringLiteral("episode"))
        e.seriesTitle = e.title;
    e.path = path;
    e.bytes = bytes;
    e.addedAt = QDateTime::currentSecsSinceEpoch();
    m_index.insert(e.id, e);
    saveIndex();
    emit libraryChanged();
}

QVariantList DownloadStore::downloadedVideos() const {
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry &e = it.value();
        out.append(QVariantMap{
            {QStringLiteral("id"), e.id},
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("title"), e.title},
            {QStringLiteral("subtitle"), e.subtitle},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("season"), e.season},
            {QStringLiteral("episode"), e.episode},
            {QStringLiteral("path"), e.path},
            {QStringLiteral("bytes"), e.bytes},
            {QStringLiteral("addedAt"), e.addedAt},
            {QStringLiteral("missing"), !QFile::exists(e.path)}
        });
    }
    return out;
}

void DownloadStore::removeVideo(const QString &id) {
    const auto it = m_index.constFind(id);
    if (it == m_index.constEnd())
        return;
    QFile::remove(it.value().path);
    m_index.remove(id);
    saveIndex();
    emit libraryChanged();
}
