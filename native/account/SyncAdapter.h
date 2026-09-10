#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncProtocol.h"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>

struct SyncAdapterRecord {
    QString recordKey;
    QJsonValue payload;

    // Optional domain-owned local wall-clock ordering hint used only while
    // materializing local semantic state into fresh HLC mutations. A value
    // <= 0 means "no trustworthy timestamp"; the engine then uses current
    // adjusted time. The hint never becomes conflict authority by itself.
    qint64 localOrderMs = -1;
};

struct SyncAdapterExport {
    quint64 revision = 0;
    QList<SyncAdapterRecord> records;
};

// A validation failure is compatibility evidence only when it comes from a
// typed, side-effect-free adapter preflight. Owner apply failures remain
// hard persistence/lifecycle failures in the registry.
enum class SyncAdapterFailureClass {
    Owner,
    Compatibility
};

struct SyncAdapterValidationError {
    QString code;
    QString detail;
    QString fieldPath;
};

class SyncAdapter : public QObject {
    Q_OBJECT

public:
    explicit SyncAdapter(
        QObject *parent = nullptr)
        : QObject(parent) {}

    ~SyncAdapter() override = default;

    // Immutable for the lifetime of a registry registration.
    virtual QString categoryId() const = 0;
    virtual int schemaVersion() const = 0;

    // Monotonic for the lifetime of this adapter instance.
    virtual quint64 revision() const = 0;

    virtual bool missingRecordsAreDeletes() const {
        return true;
    }

    // Validates a remote mutation against the owner materialization contract
    // without touching owner state. A false result is safe to quarantine as a
    // compatibility rejection; applyRemote() remains the authoritative
    // durable operation and may still fail hard for owner I/O/lifecycle.
    virtual bool validateRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        SyncAdapterValidationError *error = nullptr) const {
        if (error)
            *error = {};
        Q_UNUSED(recordKey)
        Q_UNUSED(operation)
        Q_UNUSED(payload)
        Q_UNUSED(schemaVersion)
        return true;
    }

    // Returns one coherent current semantic record snapshot. Each logical
    // record key appears at most once. The generic engine interprets a key
    // missing from a later snapshot as a local delete relative to its durable
    // semantic mirror.
    virtual bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error = nullptr) const = 0;

    // Applies exactly one remote winner through the authoritative domain
    // owner. Replaying the same semantic winner must be idempotent. Returning
    // false means the authoritative owner was not partially committed.
    //
    // Any queued/asynchronous owner notification caused by this import must be
    // absorbed or coalesced before this method returns so remote state cannot
    // echo back into the local mutation stream.
    virtual bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error = nullptr) = 0;

signals:
    // Emit only for durable local/user-originated semantic mutations.
    void localMutationAvailable(
        quint64 revision);
};
