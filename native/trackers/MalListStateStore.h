#pragma once

#include "MalProtocol.h"
#include "account/ProfilePaths.h"

#include <QList>
#include <QString>

#include <optional>

class MalListStateStore final
{
public:
    explicit MalListStateStore(const ProfilePaths &profile);

    static QString storagePath(const ProfilePaths &profile);

    bool healthy(QString *error = nullptr) const;
    QString remoteAccountId() const { return m_remoteAccountId; }
    QString snapshotId() const { return m_snapshotId; }
    QList<MalListItem> items() const { return m_items; }
    std::optional<MalListItem> item(MalMediaKind kind,
                                    const QString &malId) const;

    bool replaceAll(const QString &remoteAccountId,
                    const QString &snapshotId,
                    const QList<MalListItem> &items,
                    QString *error = nullptr);
    bool clear(QString *error = nullptr);

private:
    bool load();
    bool persist(const QString &remoteAccountId,
                 const QString &snapshotId,
                 const QList<MalListItem> &items,
                 QString *error) const;

    QString m_path;
    QString m_remoteAccountId;
    QString m_snapshotId;
    QList<MalListItem> m_items;
    bool m_healthy = true;
    QString m_error;
};
