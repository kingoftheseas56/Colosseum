#include "AccountLifecycleCoordinator.h"

#include "AccountController.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kExportPageLimit = 100;
constexpr int kMaximumExportPages = 100;
constexpr int kMaximumExportItems = 10000;
constexpr qsizetype kMaximumExportBytes = 64 * 1024 * 1024;

QString replyMessage(const AccountTransportReply &reply,
                     const QString &fallback) {
    const QString message = reply.body.value(QStringLiteral("message")).toString().trimmed();
    return message.isEmpty() ? fallback : message;
}

QDateTime parseServerDateTime(QString value) {
    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (parsed.isValid())
        return parsed;
    const qsizetype decimal = value.indexOf(QLatin1Char('.'));
    if (decimal >= 0) {
        qsizetype zone = value.indexOf(QLatin1Char('Z'), decimal);
        const qsizetype plus = value.indexOf(QLatin1Char('+'), decimal);
        const qsizetype minus = value.indexOf(QLatin1Char('-'), decimal);
        if (zone < 0 || (plus >= 0 && plus < zone))
            zone = plus;
        if (zone < 0 || (minus >= 0 && minus < zone))
            zone = minus;
        if (zone > decimal + 4)
            value.remove(decimal + 4, zone - (decimal + 4));
    }
    parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid())
        parsed = QDateTime::fromString(value, Qt::ISODate);
    return parsed;
}
}

AccountLifecycleCoordinator::AccountLifecycleCoordinator(
    AccountClient *client,
    AccountCredentialStore *credentialStore,
    AccountController *controller,
    QObject *parent)
    : QObject(parent),
      m_client(client),
      m_credentialStore(credentialStore),
      m_controller(controller) {
    Q_ASSERT(m_client);
    Q_ASSERT(m_credentialStore);
    Q_ASSERT(m_controller);
    connect(m_client, &AccountClient::completed,
            this, &AccountLifecycleCoordinator::handleCompleted);
}

bool AccountLifecycleCoordinator::exportBusy() const {
    return m_exportRequestId != 0 || m_exportFile != nullptr;
}

bool AccountLifecycleCoordinator::deletionBusy() const {
    return m_deletionRequestId != 0;
}

int AccountLifecycleCoordinator::exportItemCount() const {
    return m_exportItems.size();
}

bool AccountLifecycleCoordinator::hasPendingDeletion() const {
    return !m_credentialStore->pendingDeletions().isEmpty();
}

void AccountLifecycleCoordinator::exportAccountData(const QUrl &destination) {
    if (exportBusy())
        return;
    if (m_controller->accountId().isEmpty()
        || m_controller->modeValue() != AccountController::Mode::SignedIn) {
        emit exportFailed(QStringLiteral("Sign in and connect before exporting account data."));
        return;
    }
    const QString path = destination.isLocalFile()
        ? destination.toLocalFile() : destination.toString();
    if (path.trimmed().isEmpty()) {
        emit exportFailed(QStringLiteral("Choose a local destination for the export."));
        return;
    }

    m_exportFile = std::make_unique<QSaveFile>(path);
    if (!m_exportFile->open(QIODevice::WriteOnly)) {
        m_exportFile.reset();
        emit exportFailed(QStringLiteral("The export destination could not be opened."));
        return;
    }
    m_exportPath = QFileInfo(path).absoluteFilePath();
    m_exportAccountId = m_controller->accountId();
    m_snapshotId.clear();
    m_expectedCursor.clear();
    m_format.clear();
    m_schemaVersion = 0;
    m_highWater = 0;
    m_exportPages = 0;
    m_exportItems = {};
    m_exportKeys.clear();
    m_exportTokenGeneration = m_client->accessTokenGeneration();
    emit exportBusyChanged();
    emit exportProgressChanged();
    requestExportPage(QString());
}

void AccountLifecycleCoordinator::cancelExport() {
    if (!exportBusy())
        return;
    m_exportRequestId = 0;
    if (m_exportFile)
        m_exportFile->cancelWriting();
    m_exportFile.reset();
    m_exportItems = {};
    m_exportKeys.clear();
    emit exportBusyChanged();
    emit exportProgressChanged();
}

void AccountLifecycleCoordinator::requestExportPage(const QString &cursor) {
    m_expectedCursor = cursor;
    m_exportRequestId = m_client->pullAccountExport(cursor, kExportPageLimit);
}

void AccountLifecycleCoordinator::handleCompleted(
    quint64 requestId,
    AccountOperation operation,
    quint64 accessTokenGeneration,
    const AccountTransportReply &reply) {
    if (operation == AccountOperation::AccountExport
        && requestId == m_exportRequestId) {
        m_exportRequestId = 0;
        if (!m_exportFile)
            return;
        if (accessTokenGeneration != m_exportTokenGeneration
            || m_controller->accountId() != m_exportAccountId) {
            failExport(QStringLiteral("The active account changed before the export completed."));
            return;
        }
        if (!success(reply)) {
            failExport(replyMessage(reply, QStringLiteral("The account export could not be downloaded.")));
            return;
        }
        const auto page = syncWireExportPageFromJson(reply.body);
        QString validationError;
        if (!page.has_value() || !acceptExportPage(*page, &validationError)) {
            failExport(validationError.isEmpty()
                ? QStringLiteral("The account export response was invalid.")
                : validationError);
            return;
        }
        emit exportProgressChanged();
        if (page->hasMore) {
            requestExportPage(page->nextCursor);
            return;
        }
        finishExport();
        return;
    }

    if ((operation == AccountOperation::AccountDelete
         || operation == AccountOperation::AccountDeletionRetry)
        && requestId == m_deletionRequestId) {
        m_deletionRequestId = 0;
        finishDeletion(reply);
    }
}

bool AccountLifecycleCoordinator::acceptExportPage(
    const SyncWireExportPage &page,
    QString *error) {
    if (++m_exportPages > kMaximumExportPages
        || page.items.size() > kExportPageLimit
        || m_exportItems.size() + page.items.size() > kMaximumExportItems) {
        if (error) *error = QStringLiteral("The account export exceeded its safety limits.");
        return false;
    }
    if (m_snapshotId.isEmpty()) {
        m_snapshotId = page.snapshotId;
        m_format = page.format;
        m_schemaVersion = page.schemaVersion;
        m_highWater = page.highWaterServerSeq;
    } else if (page.snapshotId != m_snapshotId || page.format != m_format
               || page.schemaVersion != m_schemaVersion
               || page.highWaterServerSeq != m_highWater) {
        if (error) *error = QStringLiteral("The account export changed between pages.");
        return false;
    }
    if ((!m_expectedCursor.isEmpty() && page.cursor != m_expectedCursor)
        || (page.hasMore && page.nextCursor == page.cursor)
        || (!page.hasMore && !page.nextCursor.isEmpty())) {
        if (error) *error = QStringLiteral("The account export page sequence was invalid.");
        return false;
    }
    for (const QJsonValue &value : page.items) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("The account export contained an invalid item.");
            return false;
        }
        const QJsonObject item = value.toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        const QString category = item.value(QStringLiteral("category")).toString();
        const QString key = item.value(QStringLiteral("key")).toString();
        const bool accountMetadata = kind == QLatin1String("account_metadata")
            && category == QLatin1String("account_metadata")
            && key == QLatin1String("profile");
        if ((kind != QLatin1String("sync_record")
             && kind != QLatin1String("activity_fact")
             && !accountMetadata)
            || category.isEmpty() || key.isEmpty()
            || !item.contains(QStringLiteral("payload"))) {
            if (error) *error = QStringLiteral("The account export contained an invalid item.");
            return false;
        }
        const QString identity = kind + QChar(0x1f) + category + QChar(0x1f) + key;
        if (m_exportKeys.contains(identity)) {
            if (error) *error = QStringLiteral("The account export repeated an item.");
            return false;
        }
        m_exportKeys.insert(identity);
        m_exportItems.append(item);
    }
    if (QJsonDocument(m_exportItems).toJson(QJsonDocument::Compact).size()
        > kMaximumExportBytes) {
        if (error) *error = QStringLiteral("The account export exceeded its safety limits.");
        return false;
    }
    return true;
}

void AccountLifecycleCoordinator::finishExport() {
    QJsonObject document;
    document.insert(QStringLiteral("format"), m_format);
    document.insert(QStringLiteral("schema_version"), m_schemaVersion);
    document.insert(QStringLiteral("snapshot_id"), m_snapshotId);
    document.insert(QStringLiteral("high_water_server_seq"),
                    QString::number(m_highWater));
    document.insert(QStringLiteral("items"), m_exportItems);
    const QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (bytes.size() > kMaximumExportBytes
        || m_exportFile->write(bytes) != bytes.size()
        || !m_exportFile->commit()) {
        failExport(QStringLiteral("The account export could not be saved atomically."));
        return;
    }
    const QString path = m_exportPath;
    const int itemCount = m_exportItems.size();
    m_exportFile.reset();
    m_exportRequestId = 0;
    emit exportBusyChanged();
    emit exportSucceeded(path, itemCount);
}

void AccountLifecycleCoordinator::failExport(const QString &message) {
    if (m_exportFile)
        m_exportFile->cancelWriting();
    m_exportFile.reset();
    m_exportRequestId = 0;
    m_exportItems = {};
    m_exportKeys.clear();
    emit exportBusyChanged();
    emit exportProgressChanged();
    emit exportFailed(message);
}

QByteArray AccountLifecycleCoordinator::randomCapability() {
    QByteArray capability(32, Qt::Uninitialized);
    auto *generator = QRandomGenerator::system();
    for (qsizetype offset = 0; offset < capability.size(); offset += 4) {
        const quint32 value = generator->generate();
        const qsizetype remaining = qMin<qsizetype>(4, capability.size() - offset);
        memcpy(capability.data() + offset, &value, static_cast<size_t>(remaining));
    }
    return capability;
}

void AccountLifecycleCoordinator::deleteAccount(const QString &currentPassword) {
    if (deletionBusy())
        return;
    if (m_controller->modeValue() != AccountController::Mode::SignedIn
        || m_controller->accountId().isEmpty()) {
        emit deletionFailed(QStringLiteral("Sign in and connect before deleting the account."), false);
        return;
    }
    if (currentPassword.isEmpty()) {
        emit deletionFailed(QStringLiteral("Enter the current password to delete the account."), false);
        return;
    }

    const QString accountId = m_controller->accountId();
    const QList<StoredAccountDeletion> pending = m_credentialStore->pendingDeletions();
    auto existing = std::find_if(pending.cbegin(), pending.cend(),
        [&accountId](const StoredAccountDeletion &item) {
            return item.accountId == accountId;
        });
    StoredAccountDeletion deletion;
    if (existing != pending.cend()) {
        deletion = *existing;
    } else {
        deletion.accountId = accountId;
        deletion.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        deletion.retryCapability = randomCapability();
        if (!m_credentialStore->savePendingDeletion(deletion)) {
            emit deletionFailed(QStringLiteral("Deletion recovery could not be stored securely."), false);
            return;
        }
    }
    sendDeletion(deletion, currentPassword, false);
}

void AccountLifecycleCoordinator::resumePendingDeletion() {
    if (deletionBusy())
        return;
    const QList<StoredAccountDeletion> pending = m_credentialStore->pendingDeletions();
    if (pending.isEmpty())
        return;
    sendDeletion(pending.first(), QString(), true);
}

void AccountLifecycleCoordinator::sendDeletion(
    const StoredAccountDeletion &deletion,
    const QString &password,
    bool retry) {
    m_deletion = deletion;
    m_deletionRetry = retry;
    m_deletionRequestId = retry
        ? m_client->retryAccountDeletion(deletion.requestId, deletion.retryCapability)
        : m_client->deleteAccount(deletion.requestId, deletion.retryCapability, password);
    emit deletionBusyChanged();
}

void AccountLifecycleCoordinator::finishDeletion(const AccountTransportReply &reply) {
    emit deletionBusyChanged();
    const QDateTime receiptExpiry = parseServerDateTime(
        reply.body.value(QStringLiteral("receipt_expires_at")).toString());
    if (!success(reply)
        || reply.body.value(QStringLiteral("status")).toString()
               != QLatin1String("completed")
        || !receiptExpiry.isValid()
        || receiptExpiry.toUTC() <= QDateTime::currentDateTimeUtc()) {
        emit deletionFailed(replyMessage(reply,
            m_deletionRetry
                ? QStringLiteral("Colosseum could not confirm the saved deletion request yet.")
                : QStringLiteral("The account could not be deleted.")), true);
        return;
    }
    QString cleanupError;
    if (!m_controller->finalizeDeletedAccount(m_deletion.accountId, &cleanupError)) {
        emit deletionFailed(cleanupError, true);
        return;
    }
    if (!m_credentialStore->removePendingDeletion(m_deletion.requestId)) {
        emit deletionFailed(QStringLiteral(
            "The account was deleted, but its local recovery receipt could not be cleared."), true);
        return;
    }
    m_deletion.retryCapability.fill('\0');
    m_deletion = {};
    emit deletionSucceeded();
}

bool AccountLifecycleCoordinator::success(const AccountTransportReply &reply) {
    return !reply.networkError && reply.statusCode >= 200 && reply.statusCode < 300;
}
