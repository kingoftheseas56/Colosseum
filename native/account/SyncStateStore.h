#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncProtocol.h"

#include <QHash>
#include <QJsonValue>
#include <QMutex>
#include <QObject>
#include <QThread>

#include <optional>

struct SyncMirrorRecord {
    int schemaVersion = 0;
    QJsonValue payload;
};

struct SyncWinner {
    SyncWireHlc hlc;
    int schemaVersion = 0;
    SyncWireOperation operation =
        SyncWireOperation::Put;
    // Zero denotes a local winner or legacy state. Remote winners retain the
    // journal sequence so a later canonical payload at the same HLC can be
    // applied without changing the original request identity.
    quint64 serverSeq = 0;
};

struct SyncPausedOverlayRecord {
    SyncWireOperation operation = SyncWireOperation::Put;
    int schemaVersion = 0;
    QJsonValue payload;
    qint64 localOrderMs = -1;
};

struct SyncPausedCategoryState {
    QHash<QString, SyncMirrorRecord> localBaseline;
    QHash<QString, SyncPausedOverlayRecord> localOverlay;
    // Mutations already authored before the category was paused. They remain
    // durable while paused and are restored after the replay barrier.
    QList<SyncWireMutation> pendingMutations;
    bool replaying = false;
};

struct SyncQuarantineEntry {
    quint64 serverSeq = 0;
    bool won = false;
    SyncWireMutation mutation;
    QString code;
    QString message;
};

struct SyncRejectedMutation {
    QString mutationId;
    QString category;
    QString recordKey;
    QString code;
    QString message;
    QString fingerprint;
};

// A winning remote mutation whose owner has not acknowledged durable commit
// yet. The state file is the crash-safe redo record: it is written before the
// owner operation starts and retained until the owner receipt arrives.
struct SyncOwnerRedo {
    quint64 serverSeq = 0;
    bool won = true;
    SyncWireMutation mutation;
    bool replayingHistorical = false;
    bool fromQuarantine = false;
};

struct SyncPersistentState {
    quint64 cursor = 0;
    quint64 historicalReplayCursor = 0;
    // Original normal cursor captured when the one-time historical replay
    // generation began. Replay must never consume newer rows past this
    // boundary; finalization promotes the normal cursor from the replayed
    // position in the same persisted checkpoint.
    quint64 historicalReplayLimit = 0;
    bool historicalReplayPending = false;
    qint64 hlcPhysicalMs = 0;
    quint64 hlcCounter = 0;
    qint64 serverOffsetMs = 0;

    QList<SyncWireMutation> outbox;

    QHash<
        QString,
        QHash<QString, SyncMirrorRecord>>
        mirrors;

    QHash<
        QString,
        QHash<QString, SyncWinner>>
        winners;

    QHash<QString, SyncPausedCategoryState> pausedCategories;

    // A rejected mutation remains in the outbox for repair/retry visibility,
    // while this marker prevents a known poison record from starving later
    // records. Enqueueing a replacement clears the matching marker.
    QHash<QString, SyncRejectedMutation> rejectedMutations;

    // Raw remote entries stay durable until a compatible owner accepts them.
    // Cursor advancement may continue past these entries so unrelated domains
    // remain live, while the retained mutation is available for replay after
    // an adapter/schema repair.
    QList<SyncQuarantineEntry> quarantinedEntries;

    // Remote owner transactions are serialized by SyncEngine. Keeping the
    // full mutation here makes a process death between the sync checkpoint and
    // the owner write recoverable without inferring a local delete from a
    // temporarily empty owner snapshot.
    QList<SyncOwnerRedo> ownerRedos;
};

class SyncStateStore final : public QObject {
    Q_OBJECT

public:
    explicit SyncStateStore(
        QObject *parent = nullptr);
    ~SyncStateStore() override;

    std::optional<SyncPersistentState> load(
        const QString &path,
        QString *error = nullptr) const;

    quint64 saveAsync(
        const QString &path,
        const SyncPersistentState &state);

    bool flush(
        QString *error = nullptr);

    static QJsonObject encode(
        const SyncPersistentState &state);

    static std::optional<SyncPersistentState> decode(
        const QJsonObject &object,
        QString *error = nullptr);

signals:
    void persistenceCommitted(
        quint64 generation);

    void persistenceFailed(
        quint64 generation,
        const QString &message);

private:
    QThread m_writerThread;
    QObject *m_writerObject = nullptr;

    mutable QMutex m_writerErrorMutex;
    QString m_lastWriterError;
    quint64 m_nextGeneration = 1;
};
