#include "update/UpdateCache.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>

namespace Colosseum::Update {
namespace {

constexpr int kSchemaVersion = 1;
constexpr qint64 kArtworkCapBytes = 8LL * 1024LL * 1024LL;

void fail(QString* error, const QString& message)
{
    if (error) *error = message;
}

bool parseVersionObject(const QJsonObject& object, Version* version)
{
    const auto parsed = Version::parseCanonical(object.value(QStringLiteral("version")).toString());
    if (!parsed)
        return false;
    *version = *parsed;
    return true;
}

} // namespace

UpdateCache::UpdateCache(QString root)
    : m_root(QDir::cleanPath(root.isEmpty() ? productionRoot() : std::move(root)))
{
}

QString UpdateCache::productionRoot()
{
    // Installer payloads are machine-local and can be large; keep the update
    // chronicle beside the Windows local cache rather than roaming it through
    // profile sync. This also matches Lanista's disposable AppLocalData root.
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/updates"));
}

bool UpdateCache::safeAssetName(const QString& assetName) const
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    return pattern.match(assetName).hasMatch();
}

QString UpdateCache::versionDirectory(const Version& version) const
{
    return QDir::cleanPath(m_root + QLatin1Char('/') + version.canonical());
}

bool UpdateCache::validPath(const QString& path) const
{
    const QString root = QDir::cleanPath(m_root);
    const QString child = QDir::cleanPath(path);
    if (child == root)
        return true;
    return child.startsWith(root + QLatin1Char('/'));
}

QString UpdateCache::partPath(const Version& version, const QString& assetName, QString* error) const
{
    if (!safeAssetName(assetName)) {
        fail(error, QStringLiteral("unsafe_asset_name"));
        return {};
    }
    const QString path = versionDirectory(version) + QLatin1Char('/') + assetName
        + QStringLiteral(".part");
    if (!validPath(path)) {
        fail(error, QStringLiteral("unsafe_cache_path"));
        return {};
    }
    return path;
}

QString UpdateCache::installerPath(const Version& version, const QString& assetName,
                                   QString* error) const
{
    if (!safeAssetName(assetName)) {
        fail(error, QStringLiteral("unsafe_asset_name"));
        return {};
    }
    const QString path = versionDirectory(version) + QLatin1Char('/') + assetName;
    if (!validPath(path)) {
        fail(error, QStringLiteral("unsafe_cache_path"));
        return {};
    }
    return path;
}

QString UpdateCache::metadataPath(const Version& version, QString* error) const
{
    const QString path = versionDirectory(version) + QStringLiteral("/download-state.json");
    if (!validPath(path)) {
        fail(error, QStringLiteral("unsafe_cache_path"));
        return {};
    }
    return path;
}

bool UpdateCache::ensureVersionDirectory(const Version& version, QString* error) const
{
    if (m_root.isEmpty() || !QDir().mkpath(versionDirectory(version))) {
        fail(error, QStringLiteral("cache_directory_unavailable"));
        return false;
    }
    return true;
}

bool UpdateCache::writeMetadata(const DownloadMetadata& metadata, QString* error) const
{
    if (metadata.expectedSize <= 0 || metadata.expectedSha256.size() != 32
        || metadata.receivedBytes < 0 || metadata.receivedBytes > metadata.expectedSize
        || !safeAssetName(metadata.assetName) || !ensureVersionDirectory(metadata.version, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("invalid_download_metadata");
        return false;
    }
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    object.insert(QStringLiteral("version"), metadata.version.canonical());
    object.insert(QStringLiteral("asset"), metadata.assetName);
    object.insert(QStringLiteral("expectedSize"), metadata.expectedSize);
    object.insert(QStringLiteral("expectedSha256"), QString::fromLatin1(metadata.expectedSha256.toHex()));
    object.insert(QStringLiteral("expectedEtag"), metadata.expectedEtag);
    object.insert(QStringLiteral("receivedBytes"), metadata.receivedBytes);

    QSaveFile file(metadataPath(metadata.version, error));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        fail(error, QStringLiteral("metadata_write_failed"));
        return false;
    }
    return true;
}

std::optional<DownloadMetadata> UpdateCache::readMetadata(const Version& version,
                                                           QString* error) const
{
    const QString path = metadataPath(version, error);
    if (path.isEmpty())
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("metadata_missing"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, QStringLiteral("metadata_invalid_json"));
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != kSchemaVersion) {
        fail(error, QStringLiteral("metadata_schema_mismatch"));
        return std::nullopt;
    }
    DownloadMetadata metadata;
    if (!parseVersionObject(object, &metadata.version)
        || metadata.version.compare(version) != 0
        || !safeAssetName(object.value(QStringLiteral("asset")).toString())) {
        fail(error, QStringLiteral("metadata_identity_invalid"));
        return std::nullopt;
    }
    metadata.assetName = object.value(QStringLiteral("asset")).toString();
    metadata.expectedSize = object.value(QStringLiteral("expectedSize")).toInteger();
    metadata.expectedSha256 = QByteArray::fromHex(
        object.value(QStringLiteral("expectedSha256")).toString().toLatin1());
    metadata.expectedEtag = object.value(QStringLiteral("expectedEtag")).toString();
    metadata.receivedBytes = object.value(QStringLiteral("receivedBytes")).toInteger();
    if (metadata.expectedSize <= 0 || metadata.expectedSha256.size() != 32
        || metadata.receivedBytes < 0 || metadata.receivedBytes > metadata.expectedSize) {
        fail(error, QStringLiteral("metadata_values_invalid"));
        return std::nullopt;
    }
    return metadata;
}

bool UpdateCache::promotePart(const Version& version, const QString& assetName,
                              QString* promotedPath, QString* error) const
{
    const QString part = partPath(version, assetName, error);
    const QString installer = installerPath(version, assetName, error);
    if (part.isEmpty() || installer.isEmpty())
        return false;
    if (QFile::exists(installer)) {
        fail(error, QStringLiteral("installer_already_exists"));
        return false;
    }
    if (!QFile::rename(part, installer)) {
        fail(error, QStringLiteral("installer_promotion_failed"));
        return false;
    }
    if (promotedPath)
        *promotedPath = installer;
    return true;
}

QString UpdateCache::artworkPath(const QString& assetName, QString* error) const
{
    if (!safeAssetName(assetName)) {
        fail(error, QStringLiteral("unsafe_asset_name"));
        return {};
    }
    const QString path = QDir(m_root).filePath(QStringLiteral("artwork/") + assetName);
    if (!validPath(path)) {
        fail(error, QStringLiteral("unsafe_cache_path"));
        return {};
    }
    return path;
}

bool UpdateCache::writeArtwork(const QString& assetName, const QByteArray& bytes,
                               const QByteArray& expectedSha256, QString* error) const
{
    const QString path = artworkPath(assetName, error);
    if (path.isEmpty() || expectedSha256.size() != 32 || bytes.size() > kArtworkCapBytes) {
        if (error && error->isEmpty())
            *error = QStringLiteral("invalid_artwork");
        return false;
    }
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != expectedSha256) {
        fail(error, QStringLiteral("artwork_sha256_mismatch"));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        fail(error, QStringLiteral("artwork_directory_unavailable"));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        fail(error, QStringLiteral("artwork_write_failed"));
        return false;
    }
    return true;
}

bool UpdateCache::removeSuperseded(const Version& keep, QString* error) const
{
    if (!QDir(m_root).exists())
        return true;
    const QDir root(m_root);
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto version = Version::parseCanonical(name);
        if (!version || version->compare(keep) == 0)
            continue;
        const QFileInfo info(root.filePath(name));
        if (info.isSymLink()) {
            fail(error, QStringLiteral("symlink_cache_entry"));
            return false;
        }
        if (!QDir(info.absoluteFilePath()).removeRecursively()) {
            fail(error, QStringLiteral("superseded_cleanup_failed"));
            return false;
        }
    }
    return true;
}

bool UpdateCache::preflightSpace(qint64 additionalBytes, QString* error) const
{
    if (additionalBytes < 0) {
        fail(error, QStringLiteral("invalid_space_request"));
        return false;
    }
    const qint64 available = m_availableSpaceOverride >= 0
        ? m_availableSpaceOverride
        : QStorageInfo(m_root).bytesAvailable();
    if (available < additionalBytes) {
        fail(error, QStringLiteral("insufficient_space"));
        return false;
    }
    return true;
}

} // namespace Colosseum::Update
