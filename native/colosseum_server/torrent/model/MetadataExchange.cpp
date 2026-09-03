#include "colosseum_server/torrent/model/MetadataExchange.h"

#include <QCryptographicHash>
#include <QRegularExpression>

namespace ColosseumServer::Torrent {

MetadataExchangeAssembler::MetadataExchangeAssembler(QString expectedInfoHash,
                                                     qsizetype metadataSize)
    : m_expectedInfoHash(std::move(expectedInfoHash)), m_metadataSize(metadataSize)
{
    static const QRegularExpression hashRe(QStringLiteral("^[0-9a-fA-F]{40}$"));
    m_expectedInfoHash = m_expectedInfoHash.trimmed().toLower();
    m_valid = hashRe.match(m_expectedInfoHash).hasMatch()
        && m_metadataSize > 0 && m_metadataSize <= kMaxMetadataSize;
    if (!m_valid)
        return;
    const int count = static_cast<int>((m_metadataSize + kPieceSize - 1) / kPieceSize);
    m_pieces.resize(count);
    m_received.fill(false, count);
}

MetadataExchangeAssembler::Result MetadataExchangeAssembler::acceptPiece(
    int pieceIndex, const QByteArray& payload)
{
    if (!m_valid || pieceIndex < 0 || pieceIndex >= m_pieces.size())
        return Result::Rejected;
    m_pieces[pieceIndex] = payload;
    m_received[pieceIndex] = true;
    for (const bool received : m_received) {
        if (!received)
            return Result::Incomplete;
    }

    QByteArray assembled;
    assembled.reserve(m_metadataSize);
    for (const QByteArray& piece : m_pieces)
        assembled += piece;
    const QString actualHash = QString::fromLatin1(
        QCryptographicHash::hash(assembled, QCryptographicHash::Sha1).toHex());
    if (actualHash != m_expectedInfoHash) {
        reset();
        return Result::HashMismatch;
    }

    m_metadata = std::move(assembled);
    return Result::Complete;
}

void MetadataExchangeAssembler::reset()
{
    m_metadata.clear();
    for (QByteArray& piece : m_pieces)
        piece.clear();
    m_received.fill(false, m_received.size());
}

} // namespace ColosseumServer::Torrent
