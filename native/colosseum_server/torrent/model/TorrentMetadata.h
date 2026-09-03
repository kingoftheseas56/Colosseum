#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

namespace ColosseumServer::Torrent {

struct TorrentFile {
    int index = -1;
    QString path;
    QString name;
    qint64 length = 0;
    qint64 offset = 0;
};

struct SeriesHint {
    int season = 0;
    int episode = 0;
};

struct MagnetIdentity {
    QString infoHash;
    QStringList trackers;
};

struct TorrentMetadata {
    QString infoHash;
    QByteArray infoSection;
    QString name;
    bool isPrivate = false;
    qint64 length = 0;
    qint64 pieceLength = 0;
    qint64 lastPieceLength = 0;
    QVector<QByteArray> pieceHashes;
    QVector<TorrentFile> files;
    QStringList announce;
    QStringList urlList;

    std::optional<int> guessFileIndex(std::optional<SeriesHint> hint = std::nullopt) const;
};

class TorrentMetadataCodec {
public:
    static std::optional<MagnetIdentity> parseMagnet(const QString& uri, QString* error = nullptr);
    static std::optional<TorrentMetadata> parseTorrent(const QByteArray& torrentBytes,
                                                       QString* error = nullptr);
    static std::optional<TorrentMetadata> parseInfoSection(const QByteArray& infoSection,
                                                           const QString& expectedInfoHash,
                                                           QString* error = nullptr);
    static QByteArray persistedEnvelope(const QByteArray& infoSection);
    static bool persistInfoSection(const QString& filePath,
                                   const QByteArray& infoSection,
                                   QString* error = nullptr);
    static std::optional<TorrentMetadata> loadPersisted(const QString& filePath,
                                                        const QString& expectedInfoHash,
                                                        QString* error = nullptr);
};

class TorrentMetadataState {
public:
    static std::optional<TorrentMetadataState> fromMagnet(const QString& uri,
                                                          QString* error = nullptr);
    static std::optional<TorrentMetadataState> fromTorrent(const QByteArray& torrentBytes,
                                                           QString* error = nullptr);

    const QString& infoHash() const { return m_infoHash; }
    const QStringList& trackers() const { return m_trackers; }
    bool metadataReady() const { return m_metadata.has_value(); }
    const TorrentMetadata* metadata() const;
    void whenReady(std::function<void(const TorrentMetadata&)> callback);

    bool applyInfoSection(const QByteArray& infoSection, QString* error = nullptr);
    bool restorePersisted(const QByteArray& torrentBytes, QString* error = nullptr);
    QByteArray persistedTorrentBytes() const;

private:
    void releaseReadyCallbacks();

    QString m_infoHash;
    QStringList m_trackers;
    std::optional<TorrentMetadata> m_metadata;
    QList<std::function<void(const TorrentMetadata&)>> m_readyCallbacks;
};

} // namespace ColosseumServer::Torrent
