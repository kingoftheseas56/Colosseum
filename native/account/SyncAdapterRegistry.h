#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncAdapter.h"

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

struct SyncAdapterSnapshot {
    QString categoryId;
    int schemaVersion = 0;
    quint64 revision = 0;
    bool missingRecordsAreDeletes = true;
    QList<SyncAdapterRecord> records;
};

struct SyncAdapterMutation {
    QString categoryId;
    QString recordKey;
    int schemaVersion = 0;
    SyncWireOperation operation = SyncWireOperation::Put;
    QJsonValue payload;
};

struct SyncAdapterRegistryError {
    QString code;
    QString detail;
    QString fieldPath;

    bool isEmpty() const {
        return code.isEmpty();
    }
};

class SyncAdapterRegistry final : public QObject {
    Q_OBJECT

public:
    explicit SyncAdapterRegistry(
        QObject *parent = nullptr);

    bool registerAdapter(
        SyncAdapter *adapter,
        SyncAdapterRegistryError *error = nullptr);

    bool unregisterAdapter(
        const QString &categoryId);

    bool contains(
        const QString &categoryId) const;

    QStringList registeredCategories() const;

    bool exportSnapshot(
        const QString &categoryId,
        SyncAdapterSnapshot *snapshot,
        SyncAdapterRegistryError *error = nullptr) const;

    bool applyRemote(
        const SyncAdapterMutation &mutation,
        SyncAdapterRegistryError *error = nullptr);

signals:
    void adapterRegistered(
        const QString &categoryId);

    void adapterUnregistered(
        const QString &categoryId);

    void localMutationAvailable(
        const QString &categoryId,
        quint64 revision);

    void remoteApplied(
        const QString &categoryId,
        const QString &recordKey,
        quint64 revision);

private:
    struct Entry {
        QString categoryId;
        int schemaVersion = 0;
        SyncAdapter *identity = nullptr;
        QPointer<SyncAdapter> adapter;
        QMetaObject::Connection mutationConnection;
        QMetaObject::Connection destroyedConnection;
    };

    bool registrationAllowed(
        SyncAdapter *adapter,
        SyncAdapterRegistryError *error) const;

    bool identityMatches(
        const Entry &entry,
        SyncAdapterRegistryError *error) const;

    bool validatePutPayload(
        const QString &categoryId,
        const QJsonValue &payload,
        SyncAdapterRegistryError *error) const;

    Entry *entryFor(
        const QString &categoryId);

    const Entry *entryFor(
        const QString &categoryId) const;

    static QString canonicalCategory(
        const QString &categoryId);

    static bool fail(
        SyncAdapterRegistryError *error,
        const QString &code,
        const QString &detail,
        const QString &fieldPath = QString());

    void handleAdapterDestroyed(
        const QString &categoryId,
        QObject *object);

    QHash<QString, Entry> m_entries;
    QHash<QString, int> m_remoteApplyDepth;
};
