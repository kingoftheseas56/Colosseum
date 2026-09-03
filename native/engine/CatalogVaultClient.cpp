#include "engine/CatalogVaultClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThread>

namespace {

constexpr auto kUserAgent = "Colosseum/CatalogVault";

const QStringList& knownAssets()
{
    static const QStringList names{
        QStringLiteral("mal_catalog.db"),
        QStringLiteral("tankoban_catalog.db"),
        QStringLiteral("comics_catalog.db"),
        QStringLiteral("imdb_catalog.db"),
    };
    return names;
}

} // namespace

class CatalogVaultIoWorker final : public QObject {
public:
    void prepare(int queueIndex, const QString& tmpPath, QPointer<CatalogVaultClient> client)
    {
        closeActive();
        m_error.clear();
        QFile::remove(tmpPath);
        QDir().mkpath(QFileInfo(tmpPath).absolutePath());
        m_file = new QFile(tmpPath);
        const bool ok = m_file->open(QIODevice::WriteOnly);
        const QString error = ok ? QString() : QStringLiteral("cannot open temp file");
        if (!ok) closeActive();
        post(client, [queueIndex, ok, error](CatalogVaultClient* c) {
            c->onIoPrepared(queueIndex, ok, error);
        });
    }

    void append(const QByteArray& bytes)
    {
        if (!m_file || bytes.isEmpty() || !m_error.isEmpty()) return;
        if (m_file->write(bytes) != bytes.size())
            m_error = QStringLiteral("temp file write failed");
    }

    void finish(int queueIndex, const QString& tmpPath, qint64 expectedSize,
                const QString& networkError, QPointer<CatalogVaultClient> client)
    {
        QString error = m_error;
        if (m_file) {
            if (!m_file->flush() && error.isEmpty())
                error = QStringLiteral("temp file flush failed");
            m_file->close();
        }
        const qint64 bytesWritten = QFileInfo(tmpPath).size();
        closeActive();
        if (error.isEmpty() && !networkError.isEmpty()) error = networkError;
        if (error.isEmpty() && expectedSize > 0 && bytesWritten != expectedSize)
            error = QStringLiteral("truncated download");
        if (!error.isEmpty()) QFile::remove(tmpPath);
        post(client, [queueIndex, bytesWritten, error](CatalogVaultClient* c) {
            c->onIoDownloadClosed(queueIndex, bytesWritten, error);
        });
    }

    void land(int queueIndex, const QString& tmpPath, const QString& targetPath,
              qint64 bytesWritten, QPointer<CatalogVaultClient> client)
    {
        QString error;
        if (QFile::exists(targetPath) && !QFile::remove(targetPath))
            error = QStringLiteral("target locked");
        if (error.isEmpty() && !QFile::rename(tmpPath, targetPath))
            error = QStringLiteral("target locked");
        post(client, [queueIndex, bytesWritten, error](CatalogVaultClient* c) {
            c->onIoLanded(queueIndex, bytesWritten, error);
        });
    }

    void shutdown() { closeActive(); }

private:
    template <typename Fn>
    static void post(QPointer<CatalogVaultClient> client, Fn fn)
    {
        if (!client) return;
        CatalogVaultClient* target = client.data();
        QMetaObject::invokeMethod(target, [client, fn] {
            if (client) fn(client.data());
        }, Qt::QueuedConnection);
    }

    void closeActive()
    {
        if (!m_file) return;
        if (m_file->isOpen()) m_file->close();
        delete m_file;
        m_file = nullptr;
        m_error.clear();
    }

    QFile* m_file = nullptr;
    QString m_error;
};

CatalogVaultClient::CatalogVaultClient(QNetworkAccessManager* nam, const QString& vaultDir,
                                       const QString& apiBaseUrl, QObject* parent)
    : QObject(parent), m_nam(nam), m_vaultDir(vaultDir), m_apiBaseUrl(apiBaseUrl)
{
    m_ioThread = new QThread(this);
    m_ioThread->setObjectName(QStringLiteral("CatalogVaultIo"));
    m_ioWorker = new CatalogVaultIoWorker();
    m_ioWorker->moveToThread(m_ioThread);
    connect(m_ioThread, &QThread::finished, m_ioWorker, &QObject::deleteLater);
    m_ioThread->start();
}

CatalogVaultClient::~CatalogVaultClient()
{
    if (m_downloadReply) {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    if (m_ioWorker && m_ioThread && m_ioThread->isRunning()) {
        QMetaObject::invokeMethod(m_ioWorker, [worker = m_ioWorker] { worker->shutdown(); },
                                  Qt::BlockingQueuedConnection);
        m_ioThread->quit();
        m_ioThread->wait();
    }
}

void CatalogVaultClient::setForegroundPressure(int pressure)
{
    const int next = qBound(0, pressure, 2);
    if (m_foregroundPressure == next)
        return;
    m_foregroundPressure = next;
    if (m_foregroundPressure != 0)
        return;

    if (m_deferredLanding) {
        const int queueIndex = m_deferredLandingQueueIndex;
        const qint64 bytesWritten = m_deferredLandingBytes;
        m_deferredLanding = false;
        m_deferredLandingQueueIndex = -1;
        m_deferredLandingBytes = -1;
        continueLanding(queueIndex, bytesWritten);
        return;
    }
    if (m_deferredDownloadStart) {
        m_deferredDownloadStart = false;
        downloadNext();
        return;
    }
    if (m_deferredCheck) {
        m_deferredCheck = false;
        checkAndFetch();
    }
}

void CatalogVaultClient::setFetching(bool value)
{
    if (m_fetching == value)
        return;
    m_fetching = value;
    emit fetchingChanged();
}

void CatalogVaultClient::setCurrentTag(const QString& tag)
{
    if (m_currentTag == tag)
        return;
    m_currentTag = tag;
    emit tagChanged();
}

void CatalogVaultClient::setManagedNames(const QStringList& names)
{
    QStringList filtered;
    for (const QString& name : names) {
        if (knownAssets().contains(name) && !filtered.contains(name))
            filtered.append(name);
    }
    m_managedNames = filtered;
    m_managedNamesSet = true;
}

QStringList CatalogVaultClient::managedAssetNames() const
{
    return m_managedNamesSet ? m_managedNames : knownAssets();
}

bool CatalogVaultClient::allFourFilesPresent() const
{
    for (const QString& name : managedAssetNames()) {
        if (!QFile::exists(m_vaultDir + QLatin1Char('/') + name))
            return false;
    }
    return true;
}

bool CatalogVaultClient::readState(QString* tag, QDateTime* fetchedAt,
                                   QHash<QString, qint64>* sizes) const
{
    QFile file(m_vaultDir + QStringLiteral("/state.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (tag)
        *tag = root.value(QStringLiteral("tag")).toString();
    if (fetchedAt)
        *fetchedAt = QDateTime::fromString(root.value(QStringLiteral("fetchedAt")).toString(),
                                           Qt::ISODate);
    if (sizes) {
        sizes->clear();
        const QJsonObject assets = root.value(QStringLiteral("assets")).toObject();
        for (auto it = assets.constBegin(); it != assets.constEnd(); ++it)
            sizes->insert(it.key(),
                          it.value().toObject().value(QStringLiteral("size")).toVariant().toLongLong());
    }
    return true;
}

void CatalogVaultClient::writeState(const QString& tag, const QHash<QString, qint64>& sizes) const
{
    QJsonObject assets;
    for (auto it = sizes.constBegin(); it != sizes.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("size"), it.value());
        assets.insert(it.key(), entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("tag"), tag);
    root.insert(QStringLiteral("fetchedAt"),
               QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("assets"), assets);

    QDir().mkpath(m_vaultDir);
    QFile file(m_vaultDir + QStringLiteral("/state.json"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void CatalogVaultClient::checkAndFetch()
{
    if (m_foregroundPressure != 0) {
        m_deferredCheck = true;
        return;
    }
    m_deferredCheck = false;
    if (managedAssetNames().isEmpty()) {
        // Every asset the caller cares about resolved to a local dev override —
        // nothing for this client to manage. Zero network, zero work.
        emit allFresh(m_currentTag);
        return;
    }
    QString tag;
    QDateTime fetchedAt;
    QHash<QString, qint64> sizes;
    const bool hasState = readState(&tag, &fetchedAt, &sizes);
    if (hasState && fetchedAt.isValid()
        && fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600
        && allFourFilesPresent()) {
        setCurrentTag(tag);
        emit allFresh(tag);
        return;
    }
    fetchManifest();
}

void CatalogVaultClient::fetchManifest()
{
    setFetching(true);
    QNetworkRequest request{QUrl(m_apiBaseUrl + QStringLiteral("/releases/latest"))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", kUserAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onManifestReply(reply); });
}

void CatalogVaultClient::onManifestReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const QByteArray body = reply->readAll();

    const auto failManifest = [this](const QString& error) {
        if (allFourFilesPresent()) {
            // Cached copies cover every catalogue — stay quiet, keep serving them.
            qInfo("CatalogVaultClient: manifest fetch failed (%s), keeping cached databases",
                 qUtf8Printable(error));
            setFetching(false);
            return;
        }
        setFetching(false);
        emit fetchFailed(QStringLiteral("manifest"), error);
    };

    if (!networkOk) {
        failManifest(reply->errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        failManifest(QStringLiteral("invalid manifest json"));
        return;
    }
    const QJsonObject root = doc.object();
    const QString tag = root.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        failManifest(QStringLiteral("missing tag_name"));
        return;
    }

    QHash<QString, AssetInfo> byName;
    for (const QJsonValue& value : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!knownAssets().contains(name))
            continue;
        AssetInfo info;
        info.name = name;
        info.url = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        info.size = asset.value(QStringLiteral("size")).toVariant().toLongLong();
        byName.insert(name, info);
    }

    QString priorTag;
    readState(&priorTag, nullptr, nullptr);
    const bool tagChangedFlag = tag != priorTag;

    QVector<AssetInfo> toDownload;
    QHash<QString, qint64> finalSizes;
    for (const QString& name : managedAssetNames()) {
        const QString path = m_vaultDir + QLatin1Char('/') + name;
        const bool existsLocally = QFile::exists(path);
        finalSizes.insert(name, existsLocally ? QFileInfo(path).size() : 0);
        if (!byName.contains(name))
            continue; // release doesn't offer this asset this round — leave local copy as-is
        if (tagChangedFlag || !existsLocally)
            toDownload.append(byName.value(name));
    }

    if (toDownload.isEmpty()) {
        writeState(tag, finalSizes);
        setCurrentTag(tag);
        setFetching(false);
        emit allFresh(tag);
        return;
    }

    beginDownloads(tag, toDownload, finalSizes);
}

void CatalogVaultClient::beginDownloads(const QString& newTag, const QVector<AssetInfo>& toDownload,
                                        const QHash<QString, qint64>& finalSizes)
{
    m_pendingTag = newTag;
    m_queue = toDownload;
    m_finalSizes = finalSizes;
    m_queueIndex = 0;
    downloadNext();
}

void CatalogVaultClient::downloadNext()
{
    if (m_foregroundPressure != 0 && m_queueIndex < m_queue.size()) {
        m_deferredDownloadStart = true;
        return;
    }
    m_deferredDownloadStart = false;
    if (m_queueIndex >= m_queue.size()) {
        writeState(m_pendingTag, m_finalSizes);
        setCurrentTag(m_pendingTag);
        setFetching(false);
        emit allFresh(m_pendingTag);
        return;
    }

    const AssetInfo asset = m_queue.at(m_queueIndex);
    const QString tmpPath = m_vaultDir + QLatin1Char('/') + asset.name + QStringLiteral(".downloading");
    const int queueIndex = m_queueIndex;
    QPointer<CatalogVaultClient> self(this);
    QMetaObject::invokeMethod(m_ioWorker, [worker = m_ioWorker, queueIndex, tmpPath, self] {
        worker->prepare(queueIndex, tmpPath, self);
    }, Qt::QueuedConnection);
}

void CatalogVaultClient::onIoPrepared(int queueIndex, bool ok, const QString& error)
{
    if (queueIndex != m_queueIndex || !m_fetching) return;
    if (!ok) {
        setFetching(false);
        emit fetchFailed(m_queue.at(queueIndex).name, error);
        return;
    }
    startNetworkDownload(queueIndex);
}

void CatalogVaultClient::startNetworkDownload(int queueIndex)
{
    if (queueIndex != m_queueIndex || !m_fetching) return;
    const AssetInfo asset = m_queue.at(queueIndex);
    QNetworkRequest request{asset.url};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(request);
    m_downloadReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty() || !m_ioWorker) return;
        QMetaObject::invokeMethod(m_ioWorker, [worker = m_ioWorker, chunk] {
            worker->append(chunk);
        }, Qt::QueuedConnection);
    });
    connect(reply, &QNetworkReply::finished, this, &CatalogVaultClient::onDownloadFinished);
}

void CatalogVaultClient::onDownloadFinished()
{
    QNetworkReply* reply = m_downloadReply;
    if (!reply) return;
    const int queueIndex = m_queueIndex;
    const AssetInfo asset = m_queue.at(queueIndex);
    const QString tmpPath = m_vaultDir + QLatin1Char('/') + asset.name + QStringLiteral(".downloading");

    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty()) {
        QMetaObject::invokeMethod(m_ioWorker, [worker = m_ioWorker, tail] {
            worker->append(tail);
        }, Qt::QueuedConnection);
    }
    const QString networkError = reply->error() == QNetworkReply::NoError
        ? QString() : reply->errorString();
    reply->deleteLater();
    m_downloadReply = nullptr;

    QPointer<CatalogVaultClient> self(this);
    QMetaObject::invokeMethod(m_ioWorker,
        [worker = m_ioWorker, queueIndex, tmpPath, expectedSize = asset.size, networkError, self] {
            worker->finish(queueIndex, tmpPath, expectedSize, networkError, self);
        }, Qt::QueuedConnection);
}

void CatalogVaultClient::onIoDownloadClosed(int queueIndex, qint64 bytesWritten,
                                            const QString& error)
{
    if (queueIndex != m_queueIndex || !m_fetching) return;
    const AssetInfo asset = m_queue.at(queueIndex);
    if (!error.isEmpty()) {
        setFetching(false);
        emit fetchFailed(asset.name, error);
        return;
    }
    if (m_foregroundPressure != 0) {
        m_deferredLanding = true;
        m_deferredLandingQueueIndex = queueIndex;
        m_deferredLandingBytes = bytesWritten;
        return;
    }
    continueLanding(queueIndex, bytesWritten);
}

void CatalogVaultClient::continueLanding(int queueIndex, qint64 bytesWritten)
{
    if (queueIndex != m_queueIndex || !m_fetching) return;
    const AssetInfo asset = m_queue.at(queueIndex);
    const QString tmpPath = m_vaultDir + QLatin1Char('/') + asset.name + QStringLiteral(".downloading");
    const QString targetPath = m_vaultDir + QLatin1Char('/') + asset.name;
    if (QFile::exists(targetPath))
        emit aboutToReplace(asset.name);

    QPointer<CatalogVaultClient> self(this);
    QMetaObject::invokeMethod(m_ioWorker,
        [worker = m_ioWorker, queueIndex, tmpPath, targetPath, bytesWritten, self] {
            worker->land(queueIndex, tmpPath, targetPath, bytesWritten, self);
        }, Qt::QueuedConnection);
}

void CatalogVaultClient::onIoLanded(int queueIndex, qint64 bytesWritten, const QString& error)
{
    if (queueIndex != m_queueIndex || !m_fetching) return;
    const AssetInfo asset = m_queue.at(queueIndex);
    if (!error.isEmpty()) {
        setFetching(false);
        emit fetchFailed(asset.name, error);
        return;
    }

    const QString targetPath = m_vaultDir + QLatin1Char('/') + asset.name;
    m_finalSizes.insert(asset.name, bytesWritten);
    emit databaseUpdated(asset.name, targetPath);
    ++m_queueIndex;
    downloadNext();
}
