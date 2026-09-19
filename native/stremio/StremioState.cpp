#include "StremioState.h"

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

namespace {
constexpr int kStateVersion = 1;

bool writeState(const QString &path, const QJsonObject &object, QString *error) {
    if (path.trimmed().isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("The Stremio state directory could not be created.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("The Stremio state file could not be opened.");
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("The Stremio state file could not be committed.");
        return false;
    }
    return true;
}

bool validIntent(const StremioPendingIntent &intent) {
    return !intent.operationId.trimmed().isEmpty()
        && !intent.kind.trimmed().isEmpty()
        && intent.attempts >= 0;
}
}

StremioState::StremioState(QObject *parent)
    : QObject(parent) {
    setObjectName(QStringLiteral("stremioState"));
    m_writerObject = new QObject();
    m_writerObject->moveToThread(&m_writerThread);
    m_writerThread.setObjectName(QStringLiteral("stremioStateWriter"));
    m_writerThread.start();
}

StremioState::~StremioState() {
    QString ignored;
    flush(&ignored);
    if (m_writerObject) {
        QObject *worker = m_writerObject;
        if (m_writerThread.isRunning()) {
            QMetaObject::invokeMethod(worker, [worker]() { delete worker; }, Qt::BlockingQueuedConnection);
        } else {
            delete worker;
        }
        m_writerObject = nullptr;
    }
    m_writerThread.quit();
    m_writerThread.wait();
}

std::optional<StremioPersistentState> StremioState::load(
    const QString &path,
    QString *error) const {
    if (path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("The Stremio state path is empty.");
        return std::nullopt;
    }
    QFile file(path);
    if (!file.exists())
        return StremioPersistentState{};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("The Stremio state file could not be opened.");
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("The Stremio state file is malformed.");
        return std::nullopt;
    }
    return decode(document.object(), error);
}

quint64 StremioState::saveAsync(
    const QString &path,
    const StremioPersistentState &state) {
    const quint64 generation = m_nextGeneration++;
    if (!m_writerObject || !m_writerThread.isRunning() || path.trimmed().isEmpty()) {
        const QString message = path.trimmed().isEmpty()
            ? QStringLiteral("The Stremio state path is empty.")
            : QStringLiteral("The Stremio state writer is unavailable.");
        QMutexLocker locker(&m_writerErrorMutex);
        m_lastWriterError = message;
        QMetaObject::invokeMethod(this, [this, generation, message]() {
            emit persistenceFailed(generation, message);
        }, Qt::QueuedConnection);
        return generation;
    }
    const StremioPersistentState copy = state;
    QPointer<StremioState> self(this);
    QMetaObject::invokeMethod(m_writerObject, [self, path, copy, generation]() {
        QString message;
        const bool committed = writeState(path, StremioState::encode(copy), &message);
        if (!self)
            return;
        {
            QMutexLocker locker(&self->m_writerErrorMutex);
            self->m_lastWriterError = committed ? QString() : message;
        }
        QMetaObject::invokeMethod(self, [self, generation, committed, message]() {
            if (!self)
                return;
            if (committed)
                emit self->persistenceCommitted(generation);
            else
                emit self->persistenceFailed(generation, message);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return generation;
}

bool StremioState::flush(QString *error) {
    if (m_writerThread.isRunning() && m_writerObject
        && QThread::currentThread() != &m_writerThread) {
        QMetaObject::invokeMethod(m_writerObject, [] {}, Qt::BlockingQueuedConnection);
    }
    QMutexLocker locker(&m_writerErrorMutex);
    if (error)
        *error = m_lastWriterError;
    return m_lastWriterError.isEmpty();
}

QJsonObject StremioState::encode(const StremioPersistentState &state) {
    QJsonObject result;
    result.insert(QStringLiteral("version"), kStateVersion);
    result.insert(QStringLiteral("profileId"), state.profileId);
    result.insert(QStringLiteral("bindingGeneration"), QString::number(state.bindingGeneration));
    result.insert(QStringLiteral("accountId"), state.accountId);
    result.insert(QStringLiteral("displayName"), state.displayName);
    result.insert(QStringLiteral("acknowledgedBaselines"), state.acknowledgedBaselines);
    result.insert(QStringLiteral("importRedoReceipts"), state.importRedoReceipts);
    result.insert(QStringLiteral("intentionalMembershipDifferences"), state.intentionalMembershipDifferences);
    result.insert(QStringLiteral("lastSuccessAtMs"), QString::number(state.lastSuccessAtMs));
    result.insert(QStringLiteral("firstMergeComplete"), state.firstMergeComplete);
    QJsonArray intents;
    for (const StremioPendingIntent &intent : state.pendingIntents) {
        QJsonObject item;
        item.insert(QStringLiteral("operationId"), intent.operationId);
        item.insert(QStringLiteral("kind"), intent.kind);
        item.insert(QStringLiteral("desired"), intent.desired);
        item.insert(QStringLiteral("remoteAcknowledged"), intent.remoteAcknowledged);
        item.insert(QStringLiteral("localReceiptDurable"), intent.localReceiptDurable);
        item.insert(QStringLiteral("attempts"), intent.attempts);
        item.insert(QStringLiteral("retryAtMs"), QString::number(intent.retryAtMs));
        intents.append(item);
    }
    result.insert(QStringLiteral("pendingIntents"), intents);
    return result;
}

std::optional<StremioPersistentState> StremioState::decode(
    const QJsonObject &object,
    QString *error) {
    if (object.value(QStringLiteral("version")).toInt() != kStateVersion) {
        if (error)
            *error = QStringLiteral("The Stremio state version is unsupported.");
        return std::nullopt;
    }
    bool generationOk = false;
    const quint64 generation = object.value(QStringLiteral("bindingGeneration")).toString().toULongLong(&generationOk);
    bool successOk = false;
    const qint64 lastSuccess = object.value(QStringLiteral("lastSuccessAtMs")).toString().toLongLong(&successOk);
    if (!generationOk || !successOk
        || !object.value(QStringLiteral("acknowledgedBaselines")).isObject()
        || !object.value(QStringLiteral("importRedoReceipts")).isArray()
        || !object.value(QStringLiteral("intentionalMembershipDifferences")).isArray()
        || !object.value(QStringLiteral("pendingIntents")).isArray()) {
        if (error)
            *error = QStringLiteral("The Stremio state file is malformed.");
        return std::nullopt;
    }
    StremioPersistentState result;
    result.profileId = object.value(QStringLiteral("profileId")).toString();
    result.bindingGeneration = generation;
    result.accountId = object.value(QStringLiteral("accountId")).toString();
    result.displayName = object.value(QStringLiteral("displayName")).toString();
    result.acknowledgedBaselines = object.value(QStringLiteral("acknowledgedBaselines")).toObject();
    result.importRedoReceipts = object.value(QStringLiteral("importRedoReceipts")).toArray();
    result.intentionalMembershipDifferences = object.value(QStringLiteral("intentionalMembershipDifferences")).toArray();
    result.lastSuccessAtMs = lastSuccess;
    result.firstMergeComplete = object.value(QStringLiteral("firstMergeComplete")).toBool();
    for (const QJsonValue &value : object.value(QStringLiteral("pendingIntents")).toArray()) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("The Stremio state file is malformed.");
            return std::nullopt;
        }
        const QJsonObject item = value.toObject();
        bool retryOk = false;
        StremioPendingIntent intent;
        intent.operationId = item.value(QStringLiteral("operationId")).toString();
        intent.kind = item.value(QStringLiteral("kind")).toString();
        intent.desired = item.value(QStringLiteral("desired")).toObject();
        intent.remoteAcknowledged = item.value(QStringLiteral("remoteAcknowledged")).toBool();
        intent.localReceiptDurable = item.value(QStringLiteral("localReceiptDurable")).toBool();
        intent.attempts = item.value(QStringLiteral("attempts")).toInt(-1);
        intent.retryAtMs = item.value(QStringLiteral("retryAtMs")).toString().toLongLong(&retryOk);
        if (!retryOk || !validIntent(intent)) {
            if (error)
                *error = QStringLiteral("The Stremio state file is malformed.");
            return std::nullopt;
        }
        result.pendingIntents.append(intent);
    }
    return result;
}
