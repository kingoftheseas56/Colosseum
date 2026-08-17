// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncStateStore.h"

#include "SyncProtocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace {
constexpr int kStateSchemaVersion = 1;

bool parseUnsigned(
    const QJsonValue &value,
    quint64 *result) {
    if (!result)
        return false;

    bool ok = false;
    const quint64 parsed =
        value.toString().toULongLong(
            &ok);
    if (!ok)
        return false;

    *result = parsed;
    return true;
}

bool parseSigned(
    const QJsonValue &value,
    qint64 *result) {
    if (!result)
        return false;

    bool ok = false;
    const qint64 parsed =
        value.toString().toLongLong(
            &ok);
    if (!ok)
        return false;

    *result = parsed;
    return true;
}

bool validCategory(
    const QString &category) {
    return !category.isEmpty()
        && category
            == category.trimmed().toLower();
}

QString normalizedUuid(
    const QString &value) {
    const QUuid parsed(value);
    if (parsed.isNull())
        return QString();

    return parsed.toString(
        QUuid::WithoutBraces)
        .toLower();
}

bool writeStateFile(
    const QString &path,
    const QJsonObject &object,
    QString *error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(
            info.absolutePath())) {
        if (error) {
            *error = QStringLiteral(
                "The sync state directory could not be created.");
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(
            QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral(
                "The sync state file could not be opened.");
        }
        return false;
    }

    const QByteArray bytes =
        QJsonDocument(object)
            .toJson(
                QJsonDocument::Compact);

    if (file.write(bytes)
            != bytes.size()
        || !file.commit()) {
        if (error) {
            *error = QStringLiteral(
                "The sync state file could not be committed.");
        }
        return false;
    }

    return true;
}
}

SyncStateStore::SyncStateStore(
    QObject *parent)
    : QObject(parent) {
    setObjectName(
        QStringLiteral("syncStateStore"));

    m_writerObject =
        new QObject();
    m_writerObject->moveToThread(
        &m_writerThread);

    m_writerThread.setObjectName(
        QStringLiteral(
            "syncStateWriter"));
    m_writerThread.start();
}

SyncStateStore::~SyncStateStore() {
    QString ignored;
    flush(&ignored);

    if (m_writerObject) {
        QObject *worker =
            m_writerObject;

        if (m_writerThread.isRunning()) {
            QMetaObject::invokeMethod(
                worker,
                [worker]() {
                    delete worker;
                },
                Qt::BlockingQueuedConnection);
        } else {
            delete worker;
        }

        m_writerObject = nullptr;
    }

    m_writerThread.quit();
    m_writerThread.wait();
}

std::optional<SyncPersistentState>
SyncStateStore::load(
    const QString &path,
    QString *error) const {
    if (path.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "The sync state path is empty.");
        }
        return std::nullopt;
    }

    QFile file(path);
    if (!file.exists())
        return SyncPersistentState{};

    if (!file.open(
            QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral(
                "The sync state file could not be opened.");
        }
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            file.readAll(),
            &parseError);

    if (parseError.error
            != QJsonParseError::NoError
        || !document.isObject()) {
        if (error) {
            *error = QStringLiteral(
                "The sync state file is malformed.");
        }
        return std::nullopt;
    }

    return decode(
        document.object(),
        error);
}

quint64 SyncStateStore::saveAsync(
    const QString &path,
    const SyncPersistentState &state) {
    const quint64 generation =
        m_nextGeneration++;

    if (path.trimmed().isEmpty()
        || !m_writerObject
        || !m_writerThread.isRunning()) {
        const QString message =
            path.trimmed().isEmpty()
            ? QStringLiteral(
                  "The sync state path is empty.")
            : QStringLiteral(
                  "The sync state writer is unavailable.");

        {
            QMutexLocker locker(
                &m_writerErrorMutex);
            m_lastWriterError =
                message;
        }

        QMetaObject::invokeMethod(
            this,
            [this, generation, message]() {
                emit persistenceFailed(
                    generation,
                    message);
            },
            Qt::QueuedConnection);
        return generation;
    }

    const SyncPersistentState stateCopy =
        state;
    QPointer<SyncStateStore> self(this);

    QMetaObject::invokeMethod(
        m_writerObject,
        [self,
         path,
         stateCopy,
         generation]() {
            const QJsonObject encoded =
                SyncStateStore::encode(
                    stateCopy);

            QString message;
            const bool committed =
                writeStateFile(
                    path,
                    encoded,
                    &message);

            if (!self)
                return;

            {
                QMutexLocker locker(
                    &self->m_writerErrorMutex);
                if (committed)
                    self->m_lastWriterError.clear();
                else
                    self->m_lastWriterError =
                        message;
            }

            QMetaObject::invokeMethod(
                self,
                [self,
                 generation,
                 committed,
                 message]() {
                    if (!self)
                        return;

                    if (committed) {
                        emit self->persistenceCommitted(
                            generation);
                    } else {
                        emit self->persistenceFailed(
                            generation,
                            message);
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);

    return generation;
}

bool SyncStateStore::flush(
    QString *error) {
    if (m_writerThread.isRunning()
        && m_writerObject
        && QThread::currentThread()
            != &m_writerThread) {
        QMetaObject::invokeMethod(
            m_writerObject,
            []() {},
            Qt::BlockingQueuedConnection);
    }

    QMutexLocker locker(
        &m_writerErrorMutex);
    if (error)
        *error = m_lastWriterError;

    return m_lastWriterError.isEmpty();
}

QJsonObject SyncStateStore::encode(
    const SyncPersistentState &state) {
    QJsonObject root;
    root.insert(
        QStringLiteral("schema_version"),
        kStateSchemaVersion);
    root.insert(
        QStringLiteral("cursor"),
        QString::number(state.cursor));
    root.insert(
        QStringLiteral(
            "hlc_physical_ms"),
        QString::number(
            state.hlcPhysicalMs));
    root.insert(
        QStringLiteral("hlc_counter"),
        QString::number(
            state.hlcCounter));
    root.insert(
        QStringLiteral(
            "server_offset_ms"),
        QString::number(
            state.serverOffsetMs));

    QJsonArray outbox;
    for (const SyncWireMutation &mutation :
         state.outbox) {
        outbox.append(
            syncWireMutationToJson(
                mutation));
    }
    root.insert(
        QStringLiteral("outbox"),
        outbox);

    QJsonArray mirrors;
    QStringList mirrorCategories =
        state.mirrors.keys();
    mirrorCategories.sort();

    for (const QString &category :
         mirrorCategories) {
        const auto &records =
            state.mirrors.value(category);
        QStringList recordKeys =
            records.keys();
        recordKeys.sort();

        for (const QString &recordKey :
             recordKeys) {
            const SyncMirrorRecord &record =
                records.value(recordKey);

            QJsonObject object;
            object.insert(
                QStringLiteral("category"),
                category);
            object.insert(
                QStringLiteral("record_key"),
                recordKey);
            object.insert(
                QStringLiteral(
                    "schema_version"),
                record.schemaVersion);
            object.insert(
                QStringLiteral("payload"),
                record.payload);
            mirrors.append(object);
        }
    }
    root.insert(
        QStringLiteral("mirrors"),
        mirrors);

    QJsonArray winners;
    QStringList winnerCategories =
        state.winners.keys();
    winnerCategories.sort();

    for (const QString &category :
         winnerCategories) {
        const auto &records =
            state.winners.value(category);
        QStringList recordKeys =
            records.keys();
        recordKeys.sort();

        for (const QString &recordKey :
             recordKeys) {
            const SyncWinner &winner =
                records.value(recordKey);

            QJsonObject object;
            object.insert(
                QStringLiteral("category"),
                category);
            object.insert(
                QStringLiteral("record_key"),
                recordKey);
            object.insert(
                QStringLiteral(
                    "schema_version"),
                winner.schemaVersion);
            object.insert(
                QStringLiteral(
                    "hlc_physical_ms"),
                QString::number(
                    winner.hlc.physicalMs));
            object.insert(
                QStringLiteral(
                    "hlc_counter"),
                QString::number(
                    winner.hlc.counter));
            object.insert(
                QStringLiteral("device_id"),
                winner.hlc.deviceId);
            object.insert(
                QStringLiteral("operation"),
                syncWireOperationName(
                    winner.operation));
            winners.append(object);
        }
    }
    root.insert(
        QStringLiteral("winners"),
        winners);

    return root;
}

std::optional<SyncPersistentState>
SyncStateStore::decode(
    const QJsonObject &object,
    QString *error) {
    if (object.value(
            QStringLiteral(
                "schema_version"))
            .toInt()
        != kStateSchemaVersion) {
        if (error) {
            *error = QStringLiteral(
                "The sync state schema is unsupported.");
        }
        return std::nullopt;
    }

    SyncPersistentState state;
    if (!parseUnsigned(
            object.value(
                QStringLiteral("cursor")),
            &state.cursor)
        || !parseSigned(
            object.value(
                QStringLiteral(
                    "hlc_physical_ms")),
            &state.hlcPhysicalMs)
        || !parseUnsigned(
            object.value(
                QStringLiteral(
                    "hlc_counter")),
            &state.hlcCounter)
        || !parseSigned(
            object.value(
                QStringLiteral(
                    "server_offset_ms")),
            &state.serverOffsetMs)) {
        if (error) {
            *error = QStringLiteral(
                "The sync state numeric metadata is invalid.");
        }
        return std::nullopt;
    }

    const QJsonValue outboxValue =
        object.value(
            QStringLiteral("outbox"));
    if (!outboxValue.isArray()) {
        if (error) {
            *error = QStringLiteral(
                "The sync outbox is malformed.");
        }
        return std::nullopt;
    }

    QSet<QString> mutationIds;
    for (const QJsonValue &value :
         outboxValue.toArray()) {
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral(
                    "A sync outbox mutation is malformed.");
            }
            return std::nullopt;
        }

        const auto mutation =
            syncWireMutationFromJson(
                value.toObject());
        if (!mutation.has_value()
            || mutationIds.contains(
                mutation->mutationId)) {
            if (error) {
                *error = QStringLiteral(
                    "A sync outbox mutation is invalid or duplicated.");
            }
            return std::nullopt;
        }

        mutationIds.insert(
            mutation->mutationId);
        state.outbox.append(
            *mutation);
    }

    const QJsonValue mirrorsValue =
        object.value(
            QStringLiteral("mirrors"));
    if (!mirrorsValue.isArray()) {
        if (error) {
            *error = QStringLiteral(
                "The sync mirror state is malformed.");
        }
        return std::nullopt;
    }

    for (const QJsonValue &value :
         mirrorsValue.toArray()) {
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral(
                    "A sync mirror record is malformed.");
            }
            return std::nullopt;
        }

        const QJsonObject record =
            value.toObject();
        const QString category =
            record.value(
                QStringLiteral("category"))
                .toString();
        const QString recordKey =
            record.value(
                QStringLiteral("record_key"))
                .toString();
        const int schemaVersion =
            record.value(
                QStringLiteral(
                    "schema_version"))
                .toInt();

        if (!validCategory(category)
            || !isValidSyncWireRecordKey(
                recordKey)
            || schemaVersion <= 0
            || !record.contains(
                QStringLiteral("payload"))
            || state.mirrors
                   .value(category)
                   .contains(recordKey)) {
            if (error) {
                *error = QStringLiteral(
                    "A sync mirror record is invalid or duplicated.");
            }
            return std::nullopt;
        }

        state.mirrors[category].insert(
            recordKey,
            SyncMirrorRecord{
                schemaVersion,
                record.value(
                    QStringLiteral(
                        "payload"))});
    }

    const QJsonValue winnersValue =
        object.value(
            QStringLiteral("winners"));
    if (!winnersValue.isArray()) {
        if (error) {
            *error = QStringLiteral(
                "The sync winner state is malformed.");
        }
        return std::nullopt;
    }

    for (const QJsonValue &value :
         winnersValue.toArray()) {
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral(
                    "A sync winner record is malformed.");
            }
            return std::nullopt;
        }

        const QJsonObject record =
            value.toObject();
        const QString category =
            record.value(
                QStringLiteral("category"))
                .toString();
        const QString recordKey =
            record.value(
                QStringLiteral("record_key"))
                .toString();
        const int schemaVersion =
            record.value(
                QStringLiteral(
                    "schema_version"))
                .toInt();

        qint64 physicalMs = 0;
        quint64 counter = 0;
        const QString deviceId =
            normalizedUuid(
                record.value(
                    QStringLiteral(
                        "device_id"))
                    .toString());
        const auto operation =
            syncWireOperationFromName(
                record.value(
                    QStringLiteral(
                        "operation"))
                    .toString());

        if (!validCategory(category)
            || !isValidSyncWireRecordKey(
                recordKey)
            || schemaVersion <= 0
            || !parseSigned(
                record.value(
                    QStringLiteral(
                        "hlc_physical_ms")),
                &physicalMs)
            || physicalMs < 0
            || !parseUnsigned(
                record.value(
                    QStringLiteral(
                        "hlc_counter")),
                &counter)
            || deviceId.isEmpty()
            || !operation.has_value()
            || state.winners
                   .value(category)
                   .contains(recordKey)) {
            if (error) {
                *error = QStringLiteral(
                    "A sync winner record is invalid or duplicated.");
            }
            return std::nullopt;
        }

        SyncWinner winner;
        winner.hlc = SyncWireHlc{
            physicalMs,
            counter,
            deviceId};
        winner.schemaVersion =
            schemaVersion;
        winner.operation =
            *operation;

        state.winners[category].insert(
            recordKey,
            winner);
    }

    return state;
}
