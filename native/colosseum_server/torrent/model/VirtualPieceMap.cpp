#include "colosseum_server/torrent/model/VirtualPieceMap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace ColosseumServer::Torrent {
namespace {

qint64 ceilDiv(qint64 value, qint64 divisor)
{
    return divisor > 0 ? (value + divisor - 1) / divisor : 0;
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

} // namespace

VirtualPieceMap::VirtualPieceMap(qint64 totalLength, qint64 originalPieceLength,
                                 bool virtualPieces)
    : m_totalLength(totalLength), m_originalPieceLength(originalPieceLength)
{
    if (m_totalLength <= 0 || m_originalPieceLength <= 0)
        return;
    m_streamPieceLength = m_originalPieceLength;
    // Stremio 4.20.17 module 816 virtualizes only large pieces evenly divisible by 512 KiB.
    if (virtualPieces && m_originalPieceLength > kVirtualPieceLength
        && m_originalPieceLength % kVirtualPieceLength == 0) {
        m_streamPieceLength = kVirtualPieceLength;
    }
    m_originalPieceCount = static_cast<int>(ceilDiv(m_totalLength, m_originalPieceLength));
    m_streamPieceCount = static_cast<int>(ceilDiv(m_totalLength, m_streamPieceLength));
    m_valid = m_originalPieceCount > 0 && m_streamPieceCount > 0;
}

VirtualPieceMap::VirtualPieceMap(const TorrentMetadata& metadata, bool virtualPieces)
    : VirtualPieceMap(metadata.length, metadata.pieceLength, virtualPieces)
{
}

int VirtualPieceMap::originalPieceForStreamPiece(int streamPiece) const
{
    if (!m_valid || streamPiece < 0 || streamPiece >= m_streamPieceCount)
        return -1;
    return static_cast<int>((static_cast<qint64>(streamPiece) * m_streamPieceLength)
                            / m_originalPieceLength);
}

std::pair<int, int> VirtualPieceMap::streamRangeForOriginalPiece(int originalPiece) const
{
    if (!m_valid || originalPiece < 0 || originalPiece >= m_originalPieceCount)
        return {-1, -1};
    const qint64 startByte = static_cast<qint64>(originalPiece) * m_originalPieceLength;
    const qint64 endByte = static_cast<qint64>(originalPiece + 1) * m_originalPieceLength;
    const int start = static_cast<int>(startByte / m_streamPieceLength);
    const int end = qMin(m_streamPieceCount, static_cast<int>(endByte / m_streamPieceLength));
    return {start, end};
}

qint64 VirtualPieceMap::streamPieceSize(int streamPiece) const
{
    if (!m_valid || streamPiece < 0 || streamPiece >= m_streamPieceCount)
        return 0;
    if (streamPiece != m_streamPieceCount - 1)
        return m_streamPieceLength;
    const qint64 remainder = m_totalLength % m_streamPieceLength;
    return remainder == 0 ? m_streamPieceLength : remainder;
}

qint64 VirtualPieceMap::originalPieceSize(int originalPiece) const
{
    if (!m_valid || originalPiece < 0 || originalPiece >= m_originalPieceCount)
        return 0;
    if (originalPiece != m_originalPieceCount - 1)
        return m_originalPieceLength;
    const qint64 remainder = m_totalLength % m_originalPieceLength;
    return remainder == 0 ? m_originalPieceLength : remainder;
}

VerificationBitfield::VerificationBitfield(int bitCount)
    : m_bitCount(qMax(0, bitCount)), m_bytes((m_bitCount + 7) / 8, '\0')
{
}
VerificationBitfield VerificationBitfield::fromBytes(int bitCount, QByteArray bytes)
{
    VerificationBitfield result(bitCount);
    const int required = result.m_bytes.size();
    result.m_bytes = bytes.left(required);
    if (result.m_bytes.size() < required)
        result.m_bytes.resize(required);
    return result;
}

VerificationBitfield VerificationBitfield::load(const QString& path, int bitCount, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return VerificationBitfield(bitCount);
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() && bitCount > 0) {
        setError(error, QStringLiteral("verification bitfield file is empty"));
        return VerificationBitfield(bitCount);
    }
    return fromBytes(bitCount, bytes);
}

bool VerificationBitfield::get(int index) const
{
    if (index < 0 || index >= m_bitCount)
        return false;
    const unsigned char byte = static_cast<unsigned char>(m_bytes.at(index >> 3));
    return (byte & (0x80u >> (index & 7))) != 0;
}
void VerificationBitfield::set(int index, bool value)
{
    if (index < 0 || index >= m_bitCount)
        return;
    const int byteIndex = index >> 3;
    unsigned char byte = static_cast<unsigned char>(m_bytes.at(byteIndex));
    const unsigned char mask = static_cast<unsigned char>(0x80u >> (index & 7));
    byte = value ? static_cast<unsigned char>(byte | mask)
                 : static_cast<unsigned char>(byte & ~mask);
    m_bytes[byteIndex] = static_cast<char>(byte);
}

bool VerificationBitfield::persist(const QString& path, QString* error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(m_bytes) != m_bytes.size() || !file.commit()) {
        setError(error, QStringLiteral("failed to persist verification bitfield"));
        return false;
    }
    return true;
}

void VerificationBitfield::invalidateFile(const TorrentFile& file, qint64 originalPieceLength)
{
    if (originalPieceLength <= 0 || file.length <= 0)
        return;
    const int first = static_cast<int>(file.offset / originalPieceLength);
    const int last = static_cast<int>((file.offset + file.length - 1) / originalPieceLength);
    for (int piece = qMax(0, first); piece <= qMin(m_bitCount - 1, last); ++piece)
        set(piece, false);
}

QByteArray VerificationBitfield::streamCompletionBytes(const VirtualPieceMap& map) const
{
    if (!map.valid())
        return {};
    VerificationBitfield projected(map.streamPieceCount());
    const int originals = qMin(m_bitCount, map.originalPieceCount());
    for (int original = 0; original < originals; ++original) {
        if (!get(original))
            continue;
        const auto [first, end] = map.streamRangeForOriginalPiece(original);
        if (first < 0)
            continue;
        for (int stream = first; stream < end; ++stream)
            projected.set(stream);
    }
    return projected.bytes();
}

} // namespace ColosseumServer::Torrent
