#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>


class HistoryStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        int revision
        READ revision
        NOTIFY changed)

public:
    explicit HistoryStore(
        QObject *parent = nullptr);

    explicit HistoryStore(
        const QString &iniPath,
        QObject *parent = nullptr);

    int revision() const;

    bool healthy(
        QString *error = nullptr) const;

    // Presentation/read seam. Sorted by most recent activity descending.
    Q_INVOKABLE QVariantList records() const;

    Q_INVOKABLE QVariantMap get(
        const QString &kind,
        const QString &id) const;

    // Local durable history facts. These are intentionally independent from
    // Continue/progress ownership.
    Q_INVOKABLE bool recordActivity(
        const QString &kind,
        const QString &id,
        qint64 activityAtMs);

    Q_INVOKABLE bool completed(const QString &kind, const QString &id) const;

    bool recordActivityRange(const QString &kind, const QString &id,
                             qint64 firstActivityAtMs, qint64 lastActivityAtMs);

    Q_INVOKABLE bool markCompleted(
        const QString &kind,
        const QString &id,
        qint64 completedAtMs);

    // Explicit user history deletion. Removing Continue/progress does not call
    // this method.
    Q_INVOKABLE bool remove(
        const QString &kind,
        const QString &id);

    Q_INVOKABLE bool clearAll();

    // Native sync/export seam.
    QVariantList syncEntries() const;

    // Exact remote winner application. These deliberately do not emit
    // syncDirty(); SyncAdapterRegistry suppresses synchronous owner echo and
    // the owner itself never manufactures a local mutation for remote state.
    bool applySyncedRecord(
        const QVariantMap &record);

    bool removeSyncedRecord(
        const QString &kind,
        const QString &id);

signals:
    void changed();
    void syncDirty();

private:
    static QString recordKey(
        const QString &kind,
        const QString &id);

    static bool normalizeRecord(
        const QVariantMap &input,
        QVariantMap *normalized);

    static bool validIdentity(
        const QString &kind,
        const QString &id);

    void load();

    bool commit(
        const QVariantMap &next,
        bool localMutation);

    bool saveRecords(
        const QVariantMap &records) const;

    std::unique_ptr<QSettings> m_settings;
    QVariantMap m_records;
    QString m_loadError;
    int m_revision = 0;
};
