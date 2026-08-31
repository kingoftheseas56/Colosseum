// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "HistoryStore.h"

#include "SyncPayloadFirewall.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtGlobal>

#include <algorithm>

namespace {
constexpr auto kHistoryRecordsKey =
    "history/records";

QVariantMap canonicalRecord(
    const QString &kind,
    const QString &id,
    qint64 firstActivityAt,
    qint64 lastActivityAt,
    qint64 completedAt) {
    QVariantMap record;
    record.insert(
        QStringLiteral("kind"),
        kind);
    record.insert(
        QStringLiteral("id"),
        id);
    record.insert(
        QStringLiteral("firstActivityAt"),
        firstActivityAt);
    record.insert(
        QStringLiteral("lastActivityAt"),
        lastActivityAt);

    if (completedAt > 0) {
        record.insert(
            QStringLiteral("completedAt"),
            completedAt);
    }

    return record;
}
}

HistoryStore::HistoryStore(
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>()) {
    setObjectName(
        QStringLiteral("historyStore"));
    load();
}

HistoryStore::HistoryStore(
    const QString &iniPath,
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>(
              iniPath,
              QSettings::IniFormat)) {
    setObjectName(
        QStringLiteral("historyStore"));
    load();
}

int HistoryStore::revision() const {
    return m_revision;
}

bool HistoryStore::healthy(
    QString *error) const {
    if (error)
        *error = m_loadError;
    return m_loadError.isEmpty();
}

QVariantList HistoryStore::records() const {
    QVariantList result =
        syncEntries();

    std::sort(
        result.begin(),
        result.end(),
        [](const QVariant &left,
           const QVariant &right) {
            const QVariantMap leftMap =
                left.toMap();
            const QVariantMap rightMap =
                right.toMap();

            const qint64 leftLast =
                leftMap
                    .value(
                        QStringLiteral(
                            "lastActivityAt"))
                    .toLongLong();
            const qint64 rightLast =
                rightMap
                    .value(
                        QStringLiteral(
                            "lastActivityAt"))
                    .toLongLong();

            if (leftLast != rightLast)
                return leftLast > rightLast;

            const QString leftKey =
                recordKey(
                    leftMap
                        .value(
                            QStringLiteral("kind"))
                        .toString(),
                    leftMap
                        .value(
                            QStringLiteral("id"))
                        .toString());
            const QString rightKey =
                recordKey(
                    rightMap
                        .value(
                            QStringLiteral("kind"))
                        .toString(),
                    rightMap
                        .value(
                            QStringLiteral("id"))
                        .toString());
            return leftKey < rightKey;
        });

    return result;
}

QVariantMap HistoryStore::get(
    const QString &kind,
    const QString &id) const {
    return m_records
        .value(
            recordKey(kind, id))
        .toMap();
}

bool HistoryStore::completed(const QString &kind, const QString &id) const {
    return get(kind, id).value(QStringLiteral("completedAt")).toLongLong() > 0;
}

bool HistoryStore::recordActivityRange(const QString &kind, const QString &id,
                                       qint64 firstActivityAtMs, qint64 lastActivityAtMs) {
    const QString normalizedKind = kind.trimmed();
    const QString normalizedId = id.trimmed();
    if (!validIdentity(normalizedKind, normalizedId) || firstActivityAtMs <= 0
        || lastActivityAtMs < firstActivityAtMs)
        return false;
    const QString key = recordKey(normalizedKind, normalizedId);
    const QVariantMap current = m_records.value(key).toMap();
    const qint64 existingFirst = current.value(QStringLiteral("firstActivityAt")).toLongLong();
    const qint64 existingLast = current.value(QStringLiteral("lastActivityAt")).toLongLong();
    const QVariantMap normalized = canonicalRecord(
        normalizedKind, normalizedId,
        existingFirst > 0 ? qMin(existingFirst, firstActivityAtMs) : firstActivityAtMs,
        qMax(existingLast, lastActivityAtMs),
        current.value(QStringLiteral("completedAt")).toLongLong());
    if (current == normalized)
        return true;
    QVariantMap next = m_records;
    next.insert(key, normalized);
    return commit(next, true);
}

bool HistoryStore::clearAll() {
    if (m_records.isEmpty())
        return true;
    return commit(QVariantMap(), true);
}

bool HistoryStore::recordActivity(
    const QString &kind,
    const QString &id,
    qint64 activityAtMs) {
    const QString normalizedKind =
        kind.trimmed();
    const QString normalizedId =
        id.trimmed();

    if (!validIdentity(
            normalizedKind,
            normalizedId)
        || activityAtMs <= 0) {
        return false;
    }

    QVariantMap next =
        m_records;

    QVariantMap current =
        next
            .value(
                recordKey(
                    normalizedKind,
                    normalizedId))
            .toMap();

    qint64 firstActivityAt =
        current
            .value(
                QStringLiteral(
                    "firstActivityAt"))
            .toLongLong();
    qint64 lastActivityAt =
        current
            .value(
                QStringLiteral(
                    "lastActivityAt"))
            .toLongLong();
    const qint64 completedAt =
        current
            .value(
                QStringLiteral(
                    "completedAt"))
            .toLongLong();

    if (firstActivityAt <= 0)
        firstActivityAt =
            activityAtMs;
    else
        firstActivityAt =
            qMin(
                firstActivityAt,
                activityAtMs);

    lastActivityAt =
        qMax(
            lastActivityAt,
            activityAtMs);

    const QVariantMap normalized =
        canonicalRecord(
            normalizedKind,
            normalizedId,
            firstActivityAt,
            lastActivityAt,
            completedAt);

    if (current == normalized)
        return true;

    next.insert(
        recordKey(
            normalizedKind,
            normalizedId),
        normalized);

    return commit(
        next,
        true);
}

bool HistoryStore::markCompleted(
    const QString &kind,
    const QString &id,
    qint64 completedAtMs) {
    const QString normalizedKind =
        kind.trimmed();
    const QString normalizedId =
        id.trimmed();

    if (!validIdentity(
            normalizedKind,
            normalizedId)
        || completedAtMs <= 0) {
        return false;
    }

    QVariantMap next =
        m_records;

    const QVariantMap current =
        next
            .value(
                recordKey(
                    normalizedKind,
                    normalizedId))
            .toMap();

    qint64 firstActivityAt =
        current
            .value(
                QStringLiteral(
                    "firstActivityAt"))
            .toLongLong();
    qint64 lastActivityAt =
        current
            .value(
                QStringLiteral(
                    "lastActivityAt"))
            .toLongLong();

    if (firstActivityAt <= 0)
        firstActivityAt =
            completedAtMs;
    else
        firstActivityAt =
            qMin(
                firstActivityAt,
                completedAtMs);

    lastActivityAt =
        qMax(
            lastActivityAt,
            completedAtMs);

    const QVariantMap normalized =
        canonicalRecord(
            normalizedKind,
            normalizedId,
            firstActivityAt,
            lastActivityAt,
            completedAtMs);

    if (current == normalized)
        return true;

    next.insert(
        recordKey(
            normalizedKind,
            normalizedId),
        normalized);

    return commit(
        next,
        true);
}

bool HistoryStore::remove(
    const QString &kind,
    const QString &id) {
    const QString normalizedKind =
        kind.trimmed();
    const QString normalizedId =
        id.trimmed();

    if (!validIdentity(
            normalizedKind,
            normalizedId)) {
        return false;
    }

    QVariantMap next =
        m_records;
    if (!next.remove(
            recordKey(
                normalizedKind,
                normalizedId))) {
        return true;
    }

    return commit(
        next,
        true);
}

QVariantList HistoryStore::syncEntries() const {
    QVariantList result;
    result.reserve(
        m_records.size());

    QStringList keys =
        m_records.keys();
    keys.sort();

    for (const QString &key : keys)
        result.append(
            m_records.value(key));

    return result;
}

bool HistoryStore::applySyncedRecord(
    const QVariantMap &record) {
    QVariantMap normalized;
    if (!normalizeRecord(
            record,
            &normalized)) {
        return false;
    }

    const QString kind =
        normalized
            .value(
                QStringLiteral("kind"))
            .toString();
    const QString id =
        normalized
            .value(
                QStringLiteral("id"))
            .toString();

    const QString key =
        recordKey(kind, id);

    if (m_records
            .value(key)
            .toMap()
        == normalized) {
        return true;
    }

    QVariantMap next =
        m_records;
    next.insert(
        key,
        normalized);

    return commit(
        next,
        false);
}

bool HistoryStore::removeSyncedRecord(
    const QString &kind,
    const QString &id) {
    const QString normalizedKind =
        kind.trimmed();
    const QString normalizedId =
        id.trimmed();

    if (!validIdentity(
            normalizedKind,
            normalizedId)) {
        return false;
    }

    QVariantMap next =
        m_records;
    if (!next.remove(
            recordKey(
                normalizedKind,
                normalizedId))) {
        return true;
    }

    return commit(
        next,
        false);
}

QString HistoryStore::recordKey(
    const QString &kind,
    const QString &id) {
    return kind.trimmed()
        + QChar(0x1f)
        + id.trimmed();
}

bool HistoryStore::normalizeRecord(
    const QVariantMap &input,
    QVariantMap *normalized) {
    if (!normalized)
        return false;

    const QString kind =
        input
            .value(
                QStringLiteral("kind"))
            .toString()
            .trimmed();
    const QString id =
        input
            .value(
                QStringLiteral("id"))
            .toString()
            .trimmed();

    if (!validIdentity(kind, id))
        return false;

    bool firstOk = false;
    bool lastOk = false;
    const qint64 firstActivityAt =
        input
            .value(
                QStringLiteral(
                    "firstActivityAt"))
            .toLongLong(
                &firstOk);
    const qint64 lastActivityAt =
        input
            .value(
                QStringLiteral(
                    "lastActivityAt"))
            .toLongLong(
                &lastOk);

    qint64 completedAt = 0;
    if (input.contains(
            QStringLiteral(
                "completedAt"))) {
        bool completedOk = false;
        completedAt =
            input
                .value(
                    QStringLiteral(
                        "completedAt"))
                .toLongLong(
                    &completedOk);
        if (!completedOk
            || completedAt <= 0) {
            return false;
        }
    }

    if (!firstOk
        || !lastOk
        || firstActivityAt <= 0
        || lastActivityAt
            < firstActivityAt
        || (completedAt > 0
            && (completedAt
                    < firstActivityAt
                || completedAt
                    > lastActivityAt))) {
        return false;
    }

    *normalized =
        canonicalRecord(
            kind,
            id,
            firstActivityAt,
            lastActivityAt,
            completedAt);
    return true;
}

bool HistoryStore::validIdentity(
    const QString &kind,
    const QString &id) {
    if (kind.isEmpty()
        || id.isEmpty()) {
        return false;
    }

    if (SyncPayloadFirewall::
            isFilesystemPathValue(kind)
        || SyncPayloadFirewall::
               isFilesystemPathValue(id)) {
        return false;
    }

    return true;
}

void HistoryStore::load() {
    m_records.clear();

    const QByteArray payload =
        m_settings
            ->value(
                QString::fromLatin1(
                    kHistoryRecordsKey))
            .toByteArray();

    if (payload.isEmpty())
        return;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            payload);

    if (!document.isObject()) {
        m_loadError =
            QStringLiteral(
                "The History persistence file is malformed.");
        return;
    }

    const QJsonObject object =
        document.object();

    for (auto it =
             object.constBegin();
         it != object.constEnd();
         ++it) {
        if (!it.value().isObject()) {
            m_loadError =
                QStringLiteral(
                    "A persisted History record is malformed.");
            continue;
        }

        QVariantMap record =
            it.value()
                .toObject()
                .toVariantMap();

        // Cumulative 4A/4B placeholder compatibility: an old reference record
        // stored only completedAt. Promote that durable fact into a complete
        // first/last/completed record rather than discarding it.
        if (!record.contains(
                QStringLiteral(
                    "firstActivityAt"))
            && !record.contains(
                QStringLiteral(
                    "lastActivityAt"))) {
            const qint64 completedAt =
                record
                    .value(
                        QStringLiteral(
                            "completedAt"))
                    .toLongLong();
            if (completedAt > 0) {
                record.insert(
                    QStringLiteral(
                        "firstActivityAt"),
                    completedAt);
                record.insert(
                    QStringLiteral(
                        "lastActivityAt"),
                    completedAt);
            }
        }

        QVariantMap normalized;
        if (!normalizeRecord(
                record,
                &normalized)) {
            m_loadError =
                QStringLiteral(
                    "A persisted History record has invalid semantic fields.");
            continue;
        }

        const QString key =
            recordKey(
                normalized
                    .value(
                        QStringLiteral("kind"))
                    .toString(),
                normalized
                    .value(
                        QStringLiteral("id"))
                    .toString());

        m_records.insert(
            key,
            normalized);
    }
}

bool HistoryStore::commit(
    const QVariantMap &next,
    bool localMutation) {
    if (!m_loadError.isEmpty())
        return false;

    if (!saveRecords(next))
        return false;

    m_records =
        next;
    ++m_revision;

    emit changed();
    if (localMutation)
        emit syncDirty();

    return true;
}

bool HistoryStore::saveRecords(
    const QVariantMap &records) const {
    QJsonObject object;

    QStringList keys =
        records.keys();
    keys.sort();

    for (const QString &key : keys) {
        object.insert(
            key,
            QJsonObject::fromVariantMap(
                records
                    .value(key)
                    .toMap()));
    }

    m_settings->setValue(
        QString::fromLatin1(
            kHistoryRecordsKey),
        QJsonDocument(object)
            .toJson(
                QJsonDocument::Compact));

    m_settings->sync();
    return m_settings->status()
        == QSettings::NoError;
}
