// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "HistoryStore.h"

#include "SyncPayloadFirewall.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QSettings>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr auto kHistoryRecordsKey =
    "history/records";
constexpr auto kHistoryTombstonesKey =
    "history/tombstones";
constexpr auto kHistoryResetGenerationKey =
    "history/resetGeneration";
constexpr auto kHistoryResetBarrierKey =
    "history/resetBarrierAtMs";

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

bool strictPositiveIntegerVariant(
    const QVariant &value,
    qint64 *result,
    bool allowZero = false) {
    if (!value.isValid())
        return false;

    const int type = value.metaType().id();
    if (type == QMetaType::Double
        || type == QMetaType::Float) {
        const double number = value.toDouble();
        if (!std::isfinite(number)
            || (allowZero ? number < 0 : number <= 0)
            || std::floor(number) != number
            || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
            return false;
        }
        const qint64 integer = value.toLongLong();
        if (allowZero ? integer < 0 : integer <= 0)
            return false;
        if (result)
            *result = integer;
        return true;
    }

    if (type == QMetaType::QString) {
        const QString text = value.toString();
        if (text.isEmpty()
            || (text.size() > 1 && text.startsWith(QLatin1Char('0')))) {
            return false;
        }
        for (const QChar character : text) {
            if (!character.isDigit())
                return false;
        }
        bool ok = false;
        const qint64 integer = text.toLongLong(&ok);
        if (!ok || (allowZero ? integer < 0 : integer <= 0))
            return false;
        if (result)
            *result = integer;
        return true;
    }

    if (type != QMetaType::Int
        && type != QMetaType::UInt
        && type != QMetaType::LongLong
        && type != QMetaType::ULongLong
        && type != QMetaType::Short
        && type != QMetaType::UShort) {
        return false;
    }

    bool ok = false;
    const qint64 integer = value.toLongLong(&ok);
    if (!ok || (allowZero ? integer < 0 : integer <= 0))
        return false;
    if (result)
        *result = integer;
    return true;
}

bool strictPositiveJsonInteger(
    const QJsonValue &value,
    qint64 *result) {
    if (!value.isDouble())
        return false;
    return strictPositiveIntegerVariant(value.toVariant(), result);
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
    const OwnerState previous = ownerState();
    const QString key = recordKey(normalizedKind, normalizedId);
    bool tombstoneCleared = false;
    const auto tombstone = m_tombstones.constFind(key);
    if (tombstone != m_tombstones.constEnd() && lastActivityAtMs > tombstone.value()) {
        clearTombstone(key);
        tombstoneCleared = true;
    }
    const QVariantMap current = m_records.value(key).toMap();
    const qint64 existingFirst = current.value(QStringLiteral("firstActivityAt")).toLongLong();
    const qint64 existingLast = current.value(QStringLiteral("lastActivityAt")).toLongLong();
    const QVariantMap normalized = canonicalRecord(
        normalizedKind, normalizedId,
        existingFirst > 0 ? qMin(existingFirst, firstActivityAtMs) : firstActivityAtMs,
        qMax(existingLast, lastActivityAtMs),
        current.value(QStringLiteral("completedAt")).toLongLong());
    if (current == normalized && !tombstoneCleared)
        return true;
    QVariantMap next = m_records;
    next.insert(key, normalized);
    return commit(next, true, previous);
}

bool HistoryStore::clearAll() {
    const qint64 barrier = QDateTime::currentMSecsSinceEpoch();
    const OwnerState previous = ownerState();
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it)
        rememberTombstone(it.key(), barrier);
    ++m_resetGeneration;
    m_resetBarrierAtMs = qMax(m_resetBarrierAtMs, barrier);
    return commit(QVariantMap(), true, previous);
}

bool HistoryStore::clearSyncedAll(qint64 barrierAtMs) {
    const qint64 barrier = barrierAtMs > 0
        ? barrierAtMs
        : QDateTime::currentMSecsSinceEpoch();
    const OwnerState previous = ownerState();
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it)
        rememberTombstone(it.key(), barrier);
    m_resetBarrierAtMs = qMax(m_resetBarrierAtMs, barrier);
    return commit(QVariantMap(), false, previous);
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

    const OwnerState previous = ownerState();
    QVariantMap next =
        m_records;

    const QString key = recordKey(normalizedKind, normalizedId);
    bool tombstoneCleared = false;
    const auto tombstone = m_tombstones.constFind(key);
    if (tombstone != m_tombstones.constEnd() && activityAtMs > tombstone.value()) {
        clearTombstone(key);
        tombstoneCleared = true;
    }

    QVariantMap current =
        next
            .value(
                key)
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

    if (current == normalized && !tombstoneCleared)
        return true;

    next.insert(
        key,
        normalized);

    return commit(
        next,
        true,
        previous);
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

    const OwnerState previous = ownerState();
    QVariantMap next =
        m_records;

    const QString key = recordKey(normalizedKind, normalizedId);
    bool tombstoneCleared = false;
    const auto tombstone = m_tombstones.constFind(key);
    if (tombstone != m_tombstones.constEnd() && completedAtMs > tombstone.value()) {
        clearTombstone(key);
        tombstoneCleared = true;
    }

    const QVariantMap current =
        next
            .value(
                key)
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
            current.value(QStringLiteral("completedAt")).toLongLong() > 0
                ? qMin(current.value(QStringLiteral("completedAt")).toLongLong(), completedAtMs)
                : completedAtMs);

    if (current == normalized && !tombstoneCleared)
        return true;

    next.insert(
        key,
        normalized);

    return commit(
        next,
        true,
        previous);
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

    const OwnerState previous = ownerState();
    const QString key = recordKey(normalizedKind, normalizedId);
    QVariantMap next = m_records;
    next.remove(key);
    rememberTombstone(key, QDateTime::currentMSecsSinceEpoch());

    return commit(
        next,
        true,
        previous);
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

    if (blockedByTombstone(normalized))
        return true;

    const OwnerState previous = ownerState();
    const QVariantMap current = m_records.value(key).toMap();
    const QVariantMap merged = current.isEmpty()
        ? normalized
        : mergeRecords(current, normalized);
    const auto tombstone = m_tombstones.constFind(key);
    const bool tombstoneCleared = tombstone != m_tombstones.constEnd()
        && recordLast(normalized) > tombstone.value();
    if (current == merged && !tombstoneCleared) {
        return true;
    }

    clearTombstone(key);

    QVariantMap next =
        m_records;
    next.insert(
        key,
        merged);

    return commit(
        next,
        false,
        previous);
}

bool HistoryStore::applySyncedReset(
    qint64 generation,
    qint64 resetAtMs) {
    if (generation <= 0 || resetAtMs <= 0 || !healthy())
        return false;
    if (generation < m_resetGeneration
        || (generation == m_resetGeneration
            && resetAtMs <= m_resetBarrierAtMs)) {
        return true;
    }

    const OwnerState previous = ownerState();
    const qint64 effectiveBarrier =
        qMax(m_resetBarrierAtMs, resetAtMs);

    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it)
        rememberTombstone(it.key(), effectiveBarrier);
    m_resetGeneration = qMax(m_resetGeneration, generation);
    m_resetBarrierAtMs = effectiveBarrier;
    return commit(QVariantMap(), false, previous);
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

    const OwnerState previous = ownerState();
    const QString key = recordKey(normalizedKind, normalizedId);
    QVariantMap next = m_records;
    next.remove(key);
    rememberTombstone(key, QDateTime::currentMSecsSinceEpoch());

    return commit(
        next,
        false,
        previous);
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
    m_tombstones.clear();
    m_resetGeneration = 0;
    m_resetBarrierAtMs = 0;
    m_loadError.clear();

    const auto fail = [this](const QString &message) {
        m_records.clear();
        m_tombstones.clear();
        m_resetGeneration = 0;
        m_resetBarrierAtMs = 0;
        m_loadError = message;
    };

    if (m_settings->status() != QSettings::NoError) {
        fail(QStringLiteral("The History settings could not be read."));
        return;
    }

    const QString generationKey =
        QString::fromLatin1(kHistoryResetGenerationKey);
    const QString barrierKey =
        QString::fromLatin1(kHistoryResetBarrierKey);
    const bool hasGeneration = m_settings->contains(generationKey);
    const bool hasBarrier = m_settings->contains(barrierKey);
    qint64 generation = 0;
    qint64 barrier = 0;

    if (hasGeneration) {
        const QVariant raw = m_settings->value(generationKey);
        if (!strictPositiveIntegerVariant(raw, &generation, true)) {
            fail(QStringLiteral("The History reset generation is malformed."));
            return;
        }
    }
    if (hasBarrier) {
        const QVariant raw = m_settings->value(barrierKey);
        if (!strictPositiveIntegerVariant(raw, &barrier, true)) {
            fail(QStringLiteral("The History reset barrier is malformed."));
            return;
        }
    }
    if ((generation == 0) != (barrier == 0)) {
        fail(QStringLiteral("The History reset generation and barrier are incoherent."));
        return;
    }
    m_resetGeneration = generation;
    m_resetBarrierAtMs = barrier;

    const QString tombstonesKey =
        QString::fromLatin1(kHistoryTombstonesKey);
    if (m_settings->contains(tombstonesKey)) {
        const QByteArray tombstonePayload =
            m_settings->value(tombstonesKey).toByteArray();
        if (tombstonePayload.isEmpty()) {
            fail(QStringLiteral("The History tombstone persistence is malformed."));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument tombstoneDocument =
            QJsonDocument::fromJson(tombstonePayload, &parseError);
        if (parseError.error != QJsonParseError::NoError
            || !tombstoneDocument.isObject()) {
            fail(QStringLiteral("The History tombstone persistence is malformed."));
            return;
        }
        const QJsonObject tombstoneObject = tombstoneDocument.object();
        for (auto it = tombstoneObject.constBegin();
             it != tombstoneObject.constEnd(); ++it) {
            const QStringList parts =
                it.key().split(QChar(0x1f), Qt::KeepEmptyParts);
            if (parts.size() != 2
                || !validIdentity(parts.at(0), parts.at(1))
                || recordKey(parts.at(0), parts.at(1)) != it.key()) {
                fail(QStringLiteral("A History tombstone key is invalid."));
                return;
            }
            qint64 atMs = 0;
            if (!strictPositiveJsonInteger(it.value(), &atMs)) {
                fail(QStringLiteral("A History tombstone timestamp is invalid."));
                return;
            }
            m_tombstones.insert(it.key(), atMs);
        }
    }

    const QString recordsKey =
        QString::fromLatin1(kHistoryRecordsKey);
    if (!m_settings->contains(recordsKey))
        return;

    const QByteArray payload = m_settings->value(recordsKey).toByteArray();
    if (payload.isEmpty()) {
        fail(QStringLiteral("The History record persistence is malformed."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        fail(QStringLiteral("The History persistence file is malformed."));
        return;
    }

    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isObject()) {
            fail(QStringLiteral("A persisted History record is malformed."));
            return;
        }

        const QJsonObject recordObject = it.value().toObject();
        if (!recordObject.value(QStringLiteral("kind")).isString()
            || !recordObject.value(QStringLiteral("id")).isString()) {
            fail(QStringLiteral("A persisted History record identity is malformed."));
            return;
        }
        QVariantMap record = recordObject.toVariantMap();

        // Cumulative 4A/4B placeholder compatibility: an old reference record
        // stored only completedAt. Promote that durable fact into a complete
        // first/last/completed record rather than discarding it.
        if (!record.contains(QStringLiteral("firstActivityAt"))
            && !record.contains(QStringLiteral("lastActivityAt"))) {
            qint64 completedAt = 0;
            if (!strictPositiveJsonInteger(
                    recordObject.value(QStringLiteral("completedAt")),
                    &completedAt)) {
                fail(QStringLiteral("A persisted History record has invalid semantic fields."));
                return;
            }
            record.insert(QStringLiteral("firstActivityAt"), completedAt);
            record.insert(QStringLiteral("lastActivityAt"), completedAt);
        } else {
            qint64 ignored = 0;
            if (!strictPositiveJsonInteger(
                    recordObject.value(QStringLiteral("firstActivityAt")),
                    &ignored)
                || !strictPositiveJsonInteger(
                    recordObject.value(QStringLiteral("lastActivityAt")),
                    &ignored)) {
                fail(QStringLiteral("A persisted History record has invalid semantic fields."));
                return;
            }
            if (recordObject.contains(QStringLiteral("completedAt"))
                && !strictPositiveJsonInteger(
                    recordObject.value(QStringLiteral("completedAt")),
                    &ignored)) {
                fail(QStringLiteral("A persisted History record has invalid semantic fields."));
                return;
            }
        }

        QVariantMap normalized;
        if (!normalizeRecord(record, &normalized)) {
            fail(QStringLiteral("A persisted History record has invalid semantic fields."));
            return;
        }

        const QString kind = normalized.value(QStringLiteral("kind")).toString();
        const QString id = normalized.value(QStringLiteral("id")).toString();
        if (recordKey(kind, id) != it.key()) {
            fail(QStringLiteral("A persisted History record key is invalid."));
            return;
        }
        m_records.insert(recordKey(kind, id), normalized);
    }
}

HistoryStore::OwnerState HistoryStore::ownerState() const {
    OwnerState state;
    state.records = m_records;
    state.tombstones = m_tombstones;
    state.resetGeneration = m_resetGeneration;
    state.resetBarrierAtMs = m_resetBarrierAtMs;
    return state;
}

void HistoryStore::restoreOwnerState(const OwnerState &state) {
    m_records = state.records;
    m_tombstones = state.tombstones;
    m_resetGeneration = state.resetGeneration;
    m_resetBarrierAtMs = state.resetBarrierAtMs;
}

bool HistoryStore::commit(
    const QVariantMap &next,
    bool localMutation,
    const OwnerState &previous) {
    if (!m_loadError.isEmpty()) {
        restoreOwnerState(previous);
        return false;
    }

    if (!saveRecords(next)) {
        restoreOwnerState(previous);
        // Restore QSettings' in-memory view as well. If the backing file is
        // writable again, this also repairs a partially staged failed write;
        // when it is not, the owner still retains the exact pre-commit state.
        saveRecords(previous.records);
        return false;
    }

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

    QJsonObject tombstones;
    QStringList tombstoneKeys = m_tombstones.keys();
    tombstoneKeys.sort();
    for (const QString &key : tombstoneKeys)
        tombstones.insert(key, m_tombstones.value(key));
    m_settings->setValue(
        QString::fromLatin1(kHistoryTombstonesKey),
        QJsonDocument(tombstones).toJson(QJsonDocument::Compact));
    m_settings->setValue(
        QString::fromLatin1(kHistoryResetGenerationKey),
        m_resetGeneration);
    m_settings->setValue(
        QString::fromLatin1(kHistoryResetBarrierKey),
        m_resetBarrierAtMs);

    m_settings->sync();
    return m_settings->status()
        == QSettings::NoError;
}

qint64 HistoryStore::recordFirst(const QVariantMap &record) {
    return record.value(QStringLiteral("firstActivityAt")).toLongLong();
}

qint64 HistoryStore::recordLast(const QVariantMap &record) {
    return record.value(QStringLiteral("lastActivityAt")).toLongLong();
}

qint64 HistoryStore::recordCompletion(const QVariantMap &record) {
    return record.value(QStringLiteral("completedAt")).toLongLong();
}

QVariantMap HistoryStore::mergeRecords(const QVariantMap &left,
                                       const QVariantMap &right) {
    const QString kind = left.value(QStringLiteral("kind")).toString();
    const QString id = left.value(QStringLiteral("id")).toString();
    const qint64 leftFirst = recordFirst(left);
    const qint64 rightFirst = recordFirst(right);
    const qint64 leftLast = recordLast(left);
    const qint64 rightLast = recordLast(right);
    const qint64 leftCompletion = recordCompletion(left);
    const qint64 rightCompletion = recordCompletion(right);
    const qint64 first = leftFirst > 0 && rightFirst > 0
        ? qMin(leftFirst, rightFirst)
        : qMax(leftFirst, rightFirst);
    const qint64 last = qMax(leftLast, rightLast);
    qint64 completion = 0;
    if (leftCompletion > 0 && rightCompletion > 0)
        completion = qMin(leftCompletion, rightCompletion);
    else
        completion = qMax(leftCompletion, rightCompletion);
    if (first <= 0 || last < first)
        return left;
    if (completion > 0)
        completion = qBound(first, completion, last);
    return canonicalRecord(kind, id, first, last, completion);
}

bool HistoryStore::blockedByTombstone(const QVariantMap &record) const {
    if (m_resetBarrierAtMs > 0
        && recordLast(record) <= m_resetBarrierAtMs)
        return true;
    const QString key = recordKey(record.value(QStringLiteral("kind")).toString(),
                                  record.value(QStringLiteral("id")).toString());
    const auto tombstone = m_tombstones.constFind(key);
    if (tombstone == m_tombstones.constEnd())
        return false;
    return recordLast(record) <= tombstone.value();
}

void HistoryStore::clearTombstone(const QString &key) {
    m_tombstones.remove(key);
}

void HistoryStore::rememberTombstone(const QString &key, qint64 atMs) {
    if (key.isEmpty() || atMs <= 0)
        return;
    m_tombstones.insert(key, qMax(m_tombstones.value(key), atMs));
}
