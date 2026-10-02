#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PorticoIdentityStateBridge final : public QObject
{
    Q_OBJECT

public:
    explicit PorticoIdentityStateBridge(QObject *parent = nullptr);

    Q_INVOKABLE QStringList legacyFixtureIds() const;
    Q_INVOKABLE bool isLegacyFixture(const QString &legacyId) const;
    Q_INVOKABLE QString canonicalKeyForLegacy(const QString &legacyId) const;

    Q_INVOKABLE QVariantMap canonicalRecord(const QString &canonicalKey,
                                            const QString &providerId = {},
                                            const QString &legacyId = {}) const;
    Q_INVOKABLE QVariantMap legacyRecord(const QString &legacyId,
                                         const QString &providerId = {}) const;
    Q_INVOKABLE QVariantMap normalizeRecord(const QVariantMap &record) const;
    Q_INVOKABLE QVariantMap promoteLegacy(const QVariantMap &record,
                                          const QString &canonicalKey,
                                          const QString &providerId = {}) const;

    Q_INVOKABLE QVariantList migrateSaved(const QVariantList &saved) const;

    Q_INVOKABLE QVariantMap newHostSession(const QString &canonicalKey,
                                           const QString &providerId,
                                           qint64 openedAtMs,
                                           double durationMinutes,
                                           const QString &legacyId = {}) const;
    Q_INVOKABLE QVariantMap migrateSession(const QVariantMap &session) const;
    Q_INVOKABLE QVariantList migrateSessions(const QVariantList &sessions) const;

    Q_INVOKABLE QStringList lookupKeys(const QVariantMap &record) const;
    Q_INVOKABLE QString identityToken(const QVariantMap &record) const;
    Q_INVOKABLE bool sameIdentity(const QVariantMap &left,
                                  const QVariantMap &right) const;
};
