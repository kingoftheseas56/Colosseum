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
};

struct SyncPersistentState {
    quint64 cursor = 0;
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
