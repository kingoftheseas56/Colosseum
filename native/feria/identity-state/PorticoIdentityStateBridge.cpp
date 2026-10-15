#include "PorticoIdentityStateBridge.h"

#include "PorticoLegacyFixtures.h"

#include <QMetaType>

#include <iterator>

namespace {

QString legacyIdFrom(const QVariantMap &record)
{
    const QString explicitLegacy = record.value(QStringLiteral("legacyId")).toString();
    if (!explicitLegacy.isEmpty())
        return explicitLegacy;
    return record.value(QStringLiteral("id")).toString();
}

QString providerIdFrom(const QVariantMap &record)
{
    const QString provider = record.value(QStringLiteral("providerId")).toString();
    if (!provider.isEmpty())
        return provider;
    return record.value(QStringLiteral("pk")).toString();
}

} // namespace

PorticoIdentityStateBridge::PorticoIdentityStateBridge(QObject *parent)
    : QObject(parent)
{
}

QStringList PorticoIdentityStateBridge::legacyFixtureIds() const
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(std::size(kPorticoLegacyFixtures)));
    for (const auto &entry : kPorticoLegacyFixtures)
        ids.push_back(QString::fromLatin1(entry.legacyId));
    return ids;
}

bool PorticoIdentityStateBridge::isLegacyFixture(const QString &legacyId) const
{
    for (const auto &entry : kPorticoLegacyFixtures) {
        if (legacyId == QLatin1StringView(entry.legacyId))
            return true;
    }
    return false;
}

QString PorticoIdentityStateBridge::canonicalKeyForLegacy(const QString &legacyId) const
{
    for (const auto &entry : kPorticoLegacyFixtures) {
        if (legacyId != QLatin1StringView(entry.legacyId))
            continue;
        return entry.canonicalKey ? QString::fromLatin1(entry.canonicalKey) : QString();
    }
    return {};
}

QVariantMap PorticoIdentityStateBridge::canonicalRecord(const QString &canonicalKey,
                                                        const QString &providerId,
                                                        const QString &legacyId) const
{
    QVariantMap record;
    QString resolvedCanonical = canonicalKey;
    if (resolvedCanonical.isEmpty() && !legacyId.isEmpty())
        resolvedCanonical = canonicalKeyForLegacy(legacyId);

    record.insert(QStringLiteral("canonicalKey"), resolvedCanonical);
    record.insert(QStringLiteral("providerId"), providerId);
    if (!legacyId.isEmpty())
        record.insert(QStringLiteral("legacyId"), legacyId);
    return record;
}

QVariantMap PorticoIdentityStateBridge::legacyRecord(const QString &legacyId,
                                                     const QString &providerId) const
{
    return canonicalRecord({}, providerId, legacyId);
}

QVariantMap PorticoIdentityStateBridge::normalizeRecord(const QVariantMap &record) const
{
    QVariantMap normalized = record;
    QString canonicalKey = record.value(QStringLiteral("canonicalKey")).toString();
    const QString legacyId = legacyIdFrom(record);
    const QString providerId = providerIdFrom(record);

    if (canonicalKey.isEmpty() && !legacyId.isEmpty())
        canonicalKey = canonicalKeyForLegacy(legacyId);

    normalized.insert(QStringLiteral("canonicalKey"), canonicalKey);
    normalized.insert(QStringLiteral("providerId"), providerId);
    if (!legacyId.isEmpty())
        normalized.insert(QStringLiteral("legacyId"), legacyId);
    else
        normalized.remove(QStringLiteral("legacyId"));
    return normalized;
}

QVariantMap PorticoIdentityStateBridge::promoteLegacy(const QVariantMap &record,
                                                      const QString &canonicalKey,
                                                      const QString &providerId) const
{
    QVariantMap promoted = normalizeRecord(record);
    if (!canonicalKey.isEmpty())
        promoted.insert(QStringLiteral("canonicalKey"), canonicalKey);
    if (!providerId.isEmpty())
        promoted.insert(QStringLiteral("providerId"), providerId);
    return promoted;
}

QVariantList PorticoIdentityStateBridge::migrateSaved(const QVariantList &saved) const
{
    QVariantList migrated;
    migrated.reserve(saved.size());
    for (const QVariant &value : saved) {
        if (value.metaType().id() == QMetaType::QString) {
            migrated.push_back(legacyRecord(value.toString()));
        } else if (value.metaType().id() == QMetaType::QVariantMap) {
            migrated.push_back(normalizeRecord(value.toMap()));
        } else {
            migrated.push_back(value);
        }
    }
    return migrated;
}

QVariantMap PorticoIdentityStateBridge::newHostSession(const QString &canonicalKey,
                                                       const QString &providerId,
                                                       qint64 openedAtMs,
                                                       double durationMinutes,
                                                       const QString &legacyId) const
{
    QVariantMap session = canonicalRecord(canonicalKey, providerId, legacyId);
    session.insert(QStringLiteral("at"), openedAtMs);
    session.insert(QStringLiteral("mins"), durationMinutes);
    session.insert(QStringLiteral("sample"), false);
    return session;
}

QVariantMap PorticoIdentityStateBridge::migrateSession(const QVariantMap &session) const
{
    return normalizeRecord(session);
}

QVariantList PorticoIdentityStateBridge::migrateSessions(const QVariantList &sessions) const
{
    QVariantList migrated;
    migrated.reserve(sessions.size());
    for (const QVariant &value : sessions) {
        if (value.metaType().id() == QMetaType::QVariantMap)
            migrated.push_back(migrateSession(value.toMap()));
        else
            migrated.push_back(value);
    }
    return migrated;
}

QStringList PorticoIdentityStateBridge::lookupKeys(const QVariantMap &record) const
{
    const QVariantMap normalized = normalizeRecord(record);
    const QString canonicalKey = normalized.value(QStringLiteral("canonicalKey")).toString();
    const QString legacyId = normalized.value(QStringLiteral("legacyId")).toString();

    QStringList keys;
    if (!canonicalKey.isEmpty())
        keys.push_back(canonicalKey);
    if (!legacyId.isEmpty() && legacyId != canonicalKey)
        keys.push_back(legacyId);
    return keys;
}

QString PorticoIdentityStateBridge::identityToken(const QVariantMap &record) const
{
    const QVariantMap normalized = normalizeRecord(record);
    const QString canonicalKey = normalized.value(QStringLiteral("canonicalKey")).toString();
    if (!canonicalKey.isEmpty())
        return QStringLiteral("canonical:") + canonicalKey;

    const QString legacyId = normalized.value(QStringLiteral("legacyId")).toString();
    if (!legacyId.isEmpty())
        return QStringLiteral("legacy:") + legacyId;
    return {};
}

bool PorticoIdentityStateBridge::sameIdentity(const QVariantMap &left,
                                              const QVariantMap &right) const
{
    const QString leftToken = identityToken(left);
    return !leftToken.isEmpty() && leftToken == identityToken(right);
}
