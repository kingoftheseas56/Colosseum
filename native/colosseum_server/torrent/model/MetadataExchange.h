#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ColosseumServer::Torrent {

class MetadataExchangeAssembler {
public:
    static constexpr qsizetype kPieceSize = 16 * 1024;
    static constexpr qsizetype kMaxMetadataSize = 4 * 1024 * 1024;

    enum class Result {
        Rejected,
        Incomplete,
        Complete,
        HashMismatch,
    };

    MetadataExchangeAssembler(QString expectedInfoHash, qsizetype metadataSize);

    bool valid() const { return m_valid; }
    int pieceCount() const { return m_pieces.size(); }
    Result acceptPiece(int pieceIndex, const QByteArray& payload);
    const QByteArray& metadata() const { return m_metadata; }
    void reset();

private:
    QString m_expectedInfoHash;
    qsizetype m_metadataSize = 0;
    QVector<QByteArray> m_pieces;
    QVector<bool> m_received;
    QByteArray m_metadata;
    bool m_valid = false;
};

} // namespace ColosseumServer::Torrent
