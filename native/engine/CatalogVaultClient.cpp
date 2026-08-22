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

CatalogVaultClient::CatalogVaultClient(QNetworkAccessManager* nam, const QString& vaultDir,
                                       const QString& apiBaseUrl, QObject* parent)
    : QObject(parent), m_nam(nam), m_vaultDir(vaultDir), m_apiBaseUrl(apiBaseUrl)
{
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

bool CatalogVaultClient::allFourFilesPresent() const
{
    for (const QString& name : knownAssets()) {
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
    for (const QString& name : knownAssets()) {
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
    if (m_queueIndex >= m_queue.size()) {
        writeState(m_pendingTag, m_finalSizes);
        setCurrentTag(m_pendingTag);
        setFetching(false);
        emit allFresh(m_pendingTag);
        return;
    }

    const AssetInfo asset = m_queue.at(m_queueIndex);
    const QString tmpPath = m_vaultDir + QLatin1Char('/') + asset.name + QStringLiteral(".downloading");
    QFile::remove(tmpPath); // stale leftover from a previous crash/kill

    QDir().mkpath(m_vaultDir);
    m_downloadFile = new QFile(tmpPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        setFetching(false);
        emit fetchFailed(asset.name, QStringLiteral("cannot open temp file"));
        return;
    }

    QNetworkRequest request{asset.url};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(request);
    m_downloadReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_downloadFile)
            m_downloadFile->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, &CatalogVaultClient::onDownloadFinished);
}

void CatalogVaultClient::onDownloadFinished()
{
    QNetworkReply* reply = m_downloadReply;
    if (!reply)
        return;
    reply->deleteLater();
    m_downloadReply = nullptr;

    const AssetInfo asset = m_queue.at(m_queueIndex);
    const QString tmpPath = m_vaultDir + QLatin1Char('/') + asset.name + QStringLiteral(".downloading");
    const QString targetPath = m_vaultDir + QLatin1Char('/') + asset.name;

    if (m_downloadFile) {
        m_downloadFile->write(reply->readAll());
        m_downloadFile->close();
    }
    const qint64 bytesWritten = m_downloadFile ? QFileInfo(tmpPath).size() : -1;
    delete m_downloadFile;
    m_downloadFile = nullptr;

    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const bool sizeOk = asset.size <= 0 || bytesWritten == asset.size;

    if (!networkOk || !sizeOk) {
        QFile::remove(tmpPath);
        setFetching(false);
        emit fetchFailed(asset.name,
                        networkOk ? QStringLiteral("truncated download") : reply->errorString());
        return; // abort the rest of this pass — state.json stays unchanged
    }

    if (!landDownload(asset.name, tmpPath, targetPath)) {
        setFetching(false);
        return; // landDownload already emitted fetchFailed; .downloading left in place
    }

    m_finalSizes.insert(asset.name, bytesWritten);
    emit databaseUpdated(asset.name, targetPath);
    ++m_queueIndex;
    downloadNext();
}

bool CatalogVaultClient::landDownload(const QString& name, const QString& tmpPath,
                                      const QString& targetPath)
{
    if (QFile::exists(targetPath)) {
        // Synchronous — direct-connected slot may close its SQLite handle right here.
        emit aboutToReplace(name);
        if (QFile::exists(targetPath) && !QFile::remove(targetPath)) {
            emit fetchFailed(name, QStringLiteral("target locked"));
            return false;
        }
    }
    if (!QFile::rename(tmpPath, targetPath)) {
        emit fetchFailed(name, QStringLiteral("target locked"));
        return false;
    }
    return true;
}
