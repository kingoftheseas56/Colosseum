#pragma once

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QUuid>

#include <optional>

class NuvioNextUp final {
    struct Edit {
        bool dismissed = false;
        QUuid id;
    };

public:
    struct Push {
        QJsonObject settings;
        QJsonValue expectedUpdatedAt;
        bool changes = false;

    private:
        friend class NuvioNextUp;
        QUuid receipt;
        QMap<QString, Edit> sent;
        QStringList dismissed;
    };

    // The local directory belongs to a single Colosseum profile. Each Nuvio
    // account and remote profile pair has a separate file within it.
    bool activate(const QString &directory, const QString &accountId,
                  const QString &profileId, int profileIndex);
    static QString key(const QString &contentId, int season, int episode);

    bool isDismissed(const QString &key) const;
    QStringList dismissedKeys() const;
    bool setDismissed(const QString &key, bool dismissed);
    bool setDismissed(const QStringList &keys, bool dismissed);
    bool hasPending() const { return !m_pending.isEmpty(); }

    // Pull validates the server row before caching its dismissals or preparing
    // a guarded write. A zero-row response uses JSON null as its CAS timestamp.
    bool applyPull(const QJsonValue &rows, std::optional<Push> *push);
    bool acknowledge(const Push &push, const QJsonValue &updatedAt);

private:
    bool persist(const QStringList &remote, const QMap<QString, Edit> &pending) const;

    QString m_path;
    QString m_scope;
    int m_profileIndex = 0;
    QStringList m_remote;
    QMap<QString, Edit> m_pending;
    QUuid m_receipt;
};
