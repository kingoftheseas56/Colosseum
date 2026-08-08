#pragma once

#include "update/UpdateVersion.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace Colosseum::Update {

struct DownloadMetadata {
    Version version;
    QString assetName;
    qint64 expectedSize = 0;
    QByteArray expectedSha256;
    QString expectedEtag;
    qint64 receivedBytes = 0;
};

class UpdateCache final {
public:
    explicit UpdateCache(QString root = {});

    static QString productionRoot();

    QString rootPath() const { return m_root; }
    QString partPath(const Version& version, const QString& assetName,
                     QString* error = nullptr) const;
    QString installerPath(const Version& version, const QString& assetName,
                          QString* error = nullptr) const;
    QString metadataPath(const Version& version, QString* error = nullptr) const;

    bool ensureVersionDirectory(const Version& version, QString* error = nullptr) const;
    bool writeMetadata(const DownloadMetadata& metadata, QString* error = nullptr) const;
    std::optional<DownloadMetadata> readMetadata(const Version& version,
                                                 QString* error = nullptr) const;
    bool promotePart(const Version& version, const QString& assetName,
                     QString* promotedPath, QString* error = nullptr) const;
    QString artworkPath(const QString& assetName, QString* error = nullptr) const;
    bool writeArtwork(const QString& assetName, const QByteArray& bytes,
                      const QByteArray& expectedSha256, QString* error = nullptr) const;
    bool removeSuperseded(const Version& keep, QString* error = nullptr) const;
    bool preflightSpace(qint64 additionalBytes, QString* error = nullptr) const;

    // Deterministic test seam; -1 restores the real QStorageInfo reading.
    void setAvailableSpaceOverrideForTests(qint64 bytes) { m_availableSpaceOverride = bytes; }

private:
    bool safeAssetName(const QString& assetName) const;
    QString versionDirectory(const Version& version) const;
    bool validPath(const QString& path) const;

    QString m_root;
    qint64 m_availableSpaceOverride = -1;
};

} // namespace Colosseum::Update
