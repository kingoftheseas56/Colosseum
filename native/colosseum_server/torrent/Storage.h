#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace colosseum::server::torrent {

struct StorageFileSpan {
    qint64 offset = 0;
    qint64 length = 0;
};

struct StorageLayout {
    qint64 pieceLength = 0;
    qint64 verificationLength = 0;
    int pieceCount = 0;
    QVector<StorageFileSpan> files;
};

struct StorageHooks {
    std::function<bool(int)> hasPiece;
    std::function<bool(int)> isSelected;
    std::function<bool(int)> isLocked;
    std::function<void(int)> resetPiece;
};

struct VerificationResult {
    bool success = false;
    int start = 0;
    int end = 0;
};

class NormalPieceStore {
public:
    NormalPieceStore(QString root, StorageLayout layout, StorageHooks hooks,
                     qint64 memoryCacheBytes = 0);

    QByteArray read(int index);
    void write(int index, const QByteArray& buffer);
    void commit(int start, int end);
    std::optional<VerificationResult> verify(
        int index, const QVector<QByteArray>& verificationHashes);
    void close();

    void setDestination(int fileIndex, const QString& path);
    QString destination(int fileIndex) const;
    qint64 memoryBufferSize() const;

private:
    struct MemoryEntry { QByteArray bytes; bool free = false; };
    QString m_root;
    StorageLayout m_layout;
    StorageHooks m_hooks;
    qint64 m_memoryCacheBytes = 0;
    QHash<int, MemoryEntry> m_memory;
    QVector<int> m_lru;
    QHash<int, QString> m_destinations;
    mutable QMutex m_mutex;
    QMutex m_commitMutex;

    void bumpLruLocked(int index);
    QByteArray readDiskPiece(int index) const;
    void commitPiece(int index);
};

enum class CircularStoreType { Memory, FileSystem };

struct CircularStoreOptions {
    CircularStoreType type = CircularStoreType::Memory;
    qint64 sizeBytes = 30 * 1024 * 1024;
};

class CircularPieceStore {
public:
    CircularPieceStore(QString root, StorageLayout layout, StorageHooks hooks,
                       CircularStoreOptions options = {});

    QByteArray read(int index);
    void write(int index, const QByteArray& buffer, qint64 nowMs);
    void commit(int start, int end);
    std::optional<VerificationResult> verify(
        int index, const QVector<QByteArray>& verificationHashes) const;
    void close();

private:
    struct Slot {
        int index = -1;
        QByteArray bytes;
        bool occupied = false;
        bool committed = true;
        bool fileBacked = false;
        qint64 atimeMs = 0;
    };

    QString m_root;
    StorageLayout m_layout;
    StorageHooks m_hooks;
    CircularStoreOptions m_options;
    QVector<Slot> m_slots;
    mutable QMutex m_mutex;

    int findSlotLocked(int index) const;
    QString piecePath(int index) const;
};

} // namespace colosseum::server::torrent
