#pragma once

#include "colosseum_server/torrent/model/TorrentMetadata.h"

#include <QByteArray>
#include <QString>

#include <utility>

namespace ColosseumServer::Torrent {

class VirtualPieceMap {
public:
    static constexpr qint64 kVirtualPieceLength = 512 * 1024;

    VirtualPieceMap(qint64 totalLength, qint64 originalPieceLength, bool virtualPieces);
    explicit VirtualPieceMap(const TorrentMetadata& metadata, bool virtualPieces = true);

    bool valid() const { return m_valid; }
    bool isVirtualized() const { return m_streamPieceLength != m_originalPieceLength; }
    qint64 totalLength() const { return m_totalLength; }
    qint64 originalPieceLength() const { return m_originalPieceLength; }
    qint64 streamPieceLength() const { return m_streamPieceLength; }
    int originalPieceCount() const { return m_originalPieceCount; }
    int streamPieceCount() const { return m_streamPieceCount; }

    int originalPieceForStreamPiece(int streamPiece) const;
    std::pair<int, int> streamRangeForOriginalPiece(int originalPiece) const;
    qint64 streamPieceSize(int streamPiece) const;
    qint64 originalPieceSize(int originalPiece) const;
private:
    qint64 m_totalLength = 0;
    qint64 m_originalPieceLength = 0;
    qint64 m_streamPieceLength = 0;
    int m_originalPieceCount = 0;
    int m_streamPieceCount = 0;
    bool m_valid = false;
};

class VerificationBitfield {
public:
    explicit VerificationBitfield(int bitCount = 0);

    static VerificationBitfield fromBytes(int bitCount, QByteArray bytes);
    static VerificationBitfield load(const QString& path, int bitCount, QString* error = nullptr);

    int bitCount() const { return m_bitCount; }
    const QByteArray& bytes() const { return m_bytes; }
    bool get(int index) const;
    void set(int index, bool value = true);
    bool persist(const QString& path, QString* error = nullptr) const;

    void invalidateFile(const TorrentFile& file, qint64 originalPieceLength);
    QByteArray streamCompletionBytes(const VirtualPieceMap& map) const;

private:
    int m_bitCount = 0;
    QByteArray m_bytes;
};

} // namespace ColosseumServer::Torrent
