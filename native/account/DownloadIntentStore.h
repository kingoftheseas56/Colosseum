#pragma once

#include "ProfilePaths.h"
#include "SyncAdapter.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class DownloadIntentStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)

public:
    explicit DownloadIntentStore(QObject *parent = nullptr);

    quint64 revision() const { return m_revision; }

    bool activate(
        const ProfilePaths &profile,
        QString *error = nullptr);

    void setLocalRecordProvider(
        std::function<QVariantList()> provider);

    bool refreshFromLocal(
        QString *error = nullptr);

    QVariantList records() const;

    bool exportSnapshot(
        SyncAdapterExport *snapshot,
        QString *error = nullptr) const;

    bool applyRemote(
        const QString &recordKey,
        SyncWireOperation operation,
        const QJsonValue &payload,
        int schemaVersion,
        QString *error = nullptr);

signals:
    void changed();

private:
    static QString recordKey(
        const QVariantMap &record);

    static QVariantMap portableRecord(
        const QVariantMap &record);

    static bool validPortableRecord(
        const QVariantMap &record,
        QString *error = nullptr);

    static QVariantMap portableRecord(
        const QJsonObject &object,
        QString *error = nullptr);

    bool load(QString *error);
    bool save(QString *error) const;
    static bool setError(
        QString *error,
        const QString &message);

    ProfilePaths m_profile = ProfilePaths::sealed();
    QString m_path;
    QHash<QString, QVariantMap> m_records;
    std::function<QVariantList()> m_localRecordProvider;
    quint64 m_revision = 0;
    bool m_active = false;
};
