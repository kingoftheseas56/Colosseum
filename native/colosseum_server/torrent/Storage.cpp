#include "Storage.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace colosseum::server::torrent {
namespace {

void validateLayout(const StorageLayout& layout)
{
    if (layout.pieceLength <= 0 || layout.pieceCount < 0)
        throw std::invalid_argument("invalid torrent storage layout");
}

int verificationRatio(const StorageLayout& layout)
{
    if (layout.verificationLength <= 0) return 1;
    return std::max(1, static_cast<int>(layout.verificationLength / layout.pieceLength));
}

qint64 totalLength(const StorageLayout& layout)
{
    if (layout.files.isEmpty()) return 0;
    const auto& last = layout.files.constLast();
    return last.offset + last.length;
}

QByteArray hashBytes(const QVector<QByteArray>& pieces)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const auto& piece : pieces) hash.addData(piece);
    return hash.result().toHex();
}

void ensureParent(const QString& path)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        throw std::runtime_error("unable to create storage destination directory");
}

} // namespace

// Upstream module 846 (p2p-stream-storage): piece bytes live in memory until
// commit, span torrent files by absolute offsets, and verify original SHA-1 groups.
NormalPieceStore::NormalPieceStore(QString root, StorageLayout layout, StorageHooks hooks,
                                   qint64 memoryCacheBytes)
    : m_root(std::move(root)), m_layout(std::move(layout)), m_hooks(std::move(hooks)),
      m_memoryCacheBytes(memoryCacheBytes)
{
    validateLayout(m_layout);
    if (!QDir().mkpath(m_root))
        throw std::runtime_error("unable to create normal store root");
}

QString NormalPieceStore::destination(int fileIndex) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_destinations.constFind(fileIndex);
    return it == m_destinations.constEnd()
        ? QDir(m_root).filePath(QString::number(fileIndex)) : it.value();
}

void NormalPieceStore::setDestination(int fileIndex, const QString& path)
{
    QMutexLocker lock(&m_mutex);
    m_destinations.insert(fileIndex, path);
}

void NormalPieceStore::bumpLruLocked(int index)
{
    const int at = m_lru.indexOf(index);
    if (at >= 0) m_lru.removeAt(at);
    m_lru.push_back(index);
}

void NormalPieceStore::write(int index, const QByteArray& buffer)
{
    QMutexLocker lock(&m_mutex);
    bumpLruLocked(index);
    m_memory.insert(index, MemoryEntry{buffer, false});

    if (m_memoryCacheBytes <= 0) return;
    const qint64 maxEntries = m_memoryCacheBytes / m_layout.pieceLength;
    while (m_lru.size() > maxEntries) {
        int victimPos = -1;
        for (int i = 0; i < m_lru.size(); ++i) {
            const auto it = m_memory.constFind(m_lru[i]);
            if (it != m_memory.constEnd() && it->free) {
                victimPos = i;
                break;
            }
        }
        if (victimPos < 0) break;
        const int victim = m_lru.takeAt(victimPos);
        m_memory.remove(victim);
    }
}

QByteArray NormalPieceStore::read(int index)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_memory.constFind(index);
        if (it != m_memory.constEnd()) {
            const QByteArray result = it->bytes;
            bumpLruLocked(index);
            return result;
        }
    }
    return readDiskPiece(index);
}

QByteArray NormalPieceStore::readDiskPiece(int index) const
{
    qint64 length = m_layout.pieceLength;
    if (index == m_layout.pieceCount - 1) {
        // Upstream module 846 uses totalLength % pieceLength verbatim here.
        length = std::min(length, totalLength(m_layout) % m_layout.pieceLength);
    }
    if (length <= 0) return {};

    const qint64 byteStart = static_cast<qint64>(index) * m_layout.pieceLength;
    const qint64 byteEnd = byteStart + length;
    QByteArray result(static_cast<qsizetype>(length), '\0');

    for (int fileIndex = 0; fileIndex < m_layout.files.size(); ++fileIndex) {
        const auto& span = m_layout.files[fileIndex];
        const qint64 start = std::max(byteStart, span.offset);
        const qint64 end = std::min(byteEnd, span.offset + span.length);
        if (start >= end) continue;

        const qint64 readStart = std::max<qint64>(0, byteStart - span.offset);
        const qint64 readLength = end - start;
        QFile file(destination(fileIndex));
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
            throw std::runtime_error("piece destination file does not exist");
        if (!file.seek(readStart))
            throw std::runtime_error("unable to seek piece destination file");

        QByteArray chunk(static_cast<qsizetype>(readLength), '\0');
        const qint64 got = file.read(chunk.data(), readLength);
        if (got < 0) throw std::runtime_error("unable to read piece destination file");
        const qint64 resultOffset = std::max<qint64>(0, span.offset - byteStart);
        std::copy_n(chunk.constData(), static_cast<qsizetype>(readLength),
                    result.data() + resultOffset);
    }
    return result;
}

void NormalPieceStore::commit(int start, int end)
{
    QMutexLocker serial(&m_commitMutex);
    for (int index = start; index <= end; ++index) commitPiece(index);
}

void NormalPieceStore::commitPiece(int index)
{
    QByteArray buffer;
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_memory.constFind(index);
        if (it == m_memory.constEnd()) throw std::runtime_error("piece missing from memory store");
        buffer = it->bytes;
    }

    const qint64 byteStart = static_cast<qint64>(index) * m_layout.pieceLength;
    const qint64 byteEnd = byteStart + m_layout.pieceLength;
    for (int fileIndex = 0; fileIndex < m_layout.files.size(); ++fileIndex) {
        const auto& span = m_layout.files[fileIndex];
        const qint64 start = std::max(byteStart, span.offset);
        const qint64 end = std::min(byteEnd, span.offset + span.length);
        if (start >= end) continue;

        const qint64 bufferOffset = std::max<qint64>(start - byteStart, 0);
        const qint64 writeLength = std::min(end - start, m_layout.pieceLength);
        const qint64 fileOffset = std::max<qint64>(0, byteStart - span.offset);
        if (bufferOffset + writeLength > buffer.size())
            throw std::runtime_error("piece buffer range exceeds available bytes");

        const QString path = destination(fileIndex);
        ensureParent(path);
        QFile file(path);
        if (!file.open(QIODevice::ReadWrite))
            throw std::runtime_error("unable to open piece destination for write");
        if (!file.seek(fileOffset))
            throw std::runtime_error("unable to seek piece destination for write");
        if (file.write(buffer.constData() + bufferOffset, writeLength) != writeLength)
            throw std::runtime_error("unable to commit piece bytes");
    }

    QMutexLocker lock(&m_mutex);
    auto it = m_memory.find(index);
    if (it == m_memory.end()) return;
    if (m_memoryCacheBytes > 0) it->free = true;
    else {
        m_memory.erase(it);
        m_lru.removeAll(index);
    }
}

std::optional<VerificationResult> NormalPieceStore::verify(
    int index, const QVector<QByteArray>& verificationHashes)
{
    const int ratio = verificationRatio(m_layout);
    const int real = index / ratio;
    const int start = real * ratio;
    const int end = std::min(m_layout.pieceCount, (real + 1) * ratio);
    if (real < 0 || real >= verificationHashes.size()) return std::nullopt;

    QVector<QByteArray> bytes;
    QMutexLocker lock(&m_mutex);
    for (int piece = start; piece < end; ++piece) {
        if (m_hooks.hasPiece && !m_hooks.hasPiece(piece)) return std::nullopt;
        const auto it = m_memory.constFind(piece);
        if (it == m_memory.constEnd()) return std::nullopt;
        bytes.push_back(it->bytes);
        bumpLruLocked(piece);
    }
    return VerificationResult{hashBytes(bytes) == verificationHashes[real].toLower(), start, end};
}

void NormalPieceStore::close()
{
    QMutexLocker serial(&m_commitMutex);
}

qint64 NormalPieceStore::memoryBufferSize() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<qint64>(m_memory.size()) * m_layout.pieceLength;
}

// Upstream module 847 (p2p-stream-storage-circular): eviction is LRU among
// committed pieces only, while active selection and FileStream locks are protected.
CircularPieceStore::CircularPieceStore(QString root, StorageLayout layout, StorageHooks hooks,
                                       CircularStoreOptions options)
    : m_root(std::move(root)), m_layout(std::move(layout)), m_hooks(std::move(hooks)),
      m_options(options)
{
    validateLayout(m_layout);
    const qint64 slotCount = m_options.sizeBytes / m_layout.pieceLength;
    m_slots.resize(static_cast<qsizetype>(std::max<qint64>(0, slotCount)));
    if (m_options.type == CircularStoreType::FileSystem) {
        if (!QDir().mkpath(QDir(m_root).filePath(QStringLiteral("pieces"))))
            throw std::runtime_error("unable to create circular piece directory");
    }
}

QString CircularPieceStore::piecePath(int index) const
{
    return QDir(m_root).filePath(QStringLiteral("pieces/") + QString::number(index));
}

int CircularPieceStore::findSlotLocked(int index) const
{
    for (int i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].occupied && m_slots[i].index == index) return i;
    }
    return -1;
}

QByteArray CircularPieceStore::read(int index)
{
    QString filePath;
    QByteArray bytes;
    {
        QMutexLocker lock(&m_mutex);
        const int at = findSlotLocked(index);
        if (at < 0) throw std::runtime_error("piece not found in circular buffer");
        auto& slot = m_slots[at];
        slot.atimeMs = QDateTime::currentMSecsSinceEpoch();
        if (!slot.fileBacked) return slot.bytes;
        filePath = piecePath(slot.index);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("unable to read filesystem circular piece");
    return file.readAll();
}
void CircularPieceStore::write(int index, const QByteArray& buffer, qint64 nowMs)
{
    QMutexLocker lock(&m_mutex);
    if (m_slots.isEmpty()) return;

    int at = findSlotLocked(index);
    if (at < 0) {
        for (int i = 0; i < m_slots.size(); ++i) {
            if (!m_slots[i].occupied) {
                at = i;
                break;
            }
        }
    }

    if (at < 0) {
        qint64 oldestAtime = std::numeric_limits<qint64>::max();
        for (int i = 0; i < m_slots.size(); ++i) {
            const auto& candidate = m_slots[i];
            if (!candidate.occupied || !candidate.committed) continue;
            if (m_hooks.isSelected && m_hooks.isSelected(candidate.index)) continue;
            if (m_hooks.isLocked && m_hooks.isLocked(candidate.index)) continue;
            if (candidate.atimeMs < oldestAtime) {
                oldestAtime = candidate.atimeMs;
                at = i;
            }
        }
    }
    if (at < 0)
        throw std::runtime_error("circular buffer is full with no evictable piece");

    auto& slot = m_slots[at];
    if (slot.occupied && slot.index != index) {
        const int evicted = slot.index;
        if (slot.fileBacked) QFile::remove(piecePath(evicted));
        if (m_hooks.resetPiece) m_hooks.resetPiece(evicted);
    }

    slot.index = index;
    slot.bytes = buffer;
    slot.occupied = true;
    slot.committed = false;
    slot.fileBacked = false;
    slot.atimeMs = nowMs;
}

void CircularPieceStore::commit(int start, int end)
{
    QMutexLocker lock(&m_mutex);
    for (int index = start; index <= end; ++index) {
        const int at = findSlotLocked(index);
        if (at < 0) continue;
        auto& slot = m_slots[at];
        slot.committed = true;
        if (m_options.type != CircularStoreType::FileSystem) continue;

        const QString path = piecePath(index);
        ensureParent(path);        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw std::runtime_error("unable to persist filesystem circular piece");
        if (file.write(slot.bytes) != slot.bytes.size())
            throw std::runtime_error("unable to write filesystem circular piece bytes");
        file.close();
        slot.bytes.clear();
        slot.fileBacked = true;
    }
}

std::optional<VerificationResult> CircularPieceStore::verify(
    int index, const QVector<QByteArray>& verificationHashes) const
{
    const int ratio = verificationRatio(m_layout);
    const int real = index / ratio;
    const int start = real * ratio;
    const int end = std::min(m_layout.pieceCount, (real + 1) * ratio);
    if (real < 0 || real >= verificationHashes.size()) return std::nullopt;

    QVector<QByteArray> bytes;
    QMutexLocker lock(&m_mutex);
    for (int piece = start; piece < end; ++piece) {
        if (m_hooks.hasPiece && !m_hooks.hasPiece(piece)) return std::nullopt;
        const int at = findSlotLocked(piece);
        if (at < 0) return std::nullopt;
        const auto& slot = m_slots[at];
        if (!slot.fileBacked) {
            bytes.push_back(slot.bytes);
            continue;
        }
        QFile file(piecePath(piece));
        if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
        bytes.push_back(file.readAll());
    }
    return VerificationResult{hashBytes(bytes) == verificationHashes[real].toLower(), start, end};
}

void CircularPieceStore::close()
{
    QMutexLocker lock(&m_mutex);
    m_slots.clear();
}

} // namespace colosseum::server::torrent
