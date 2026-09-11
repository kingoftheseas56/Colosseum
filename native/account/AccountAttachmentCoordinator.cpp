// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountAttachmentCoordinator.h"

#include "SyncEngine.h"
#include "SyncProtocol.h"

#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <utility>

namespace {
bool isReplySuccess(
    const AccountTransportReply &reply) {
    return !reply.networkError
        && reply.statusCode >= 200
        && reply.statusCode < 300;
}

bool isReplyNotFound(
    const AccountTransportReply &reply) {
    return reply.statusCode == 404
        || reply.errorCode
            == QLatin1String(
                "attachment_not_found");
}

QString describeReply(
    const AccountTransportReply &reply) {
    if (!reply.errorMessage.isEmpty())
        return reply.errorMessage;
    if (!reply.errorCode.isEmpty())
        return reply.errorCode;
    return QStringLiteral(
        "The account service replied with status %1.")
        .arg(reply.statusCode);
}
}

AccountAttachmentCoordinator::
    AccountAttachmentCoordinator(
        AccountClient *client,
        SyncEngine *engine,
        const ProfilePaths &profile,
        QObject *parent)
    : QObject(parent),
      m_client(client),
      m_engine(engine),
      m_profile(profile) {
    // The coordinator watches the same reply stream the engine dispatches
    // on (request-id matching for its own requests, operation matching for
    // the engine's bootstrap traffic), plus the engine's observation
    // changes as extra evaluation points.
    connect(
        m_client,
        &AccountClient::completed,
        this,
        &AccountAttachmentCoordinator::
            handleClientCompleted);
    connect(
        m_engine,
        &SyncEngine::observationChanged,
        this,
        [this](SyncEngine::State, int) {
            evaluateBootstrap();
        });
}

AccountAttachmentCoordinator::
    ~AccountAttachmentCoordinator() = default;

bool AccountAttachmentCoordinator::start(
    const QString &attachmentId,
    const SourceIdentity &source,
    QString *error) {
    if (isFlowActive()) {
        if (error) {
            *error = QStringLiteral(
                "An attachment flow is already running on this coordinator.");
        }
        return false;
    }

    if (!validateInputs(
            attachmentId,
            source,
            error)) {
        return fail(
            error,
            m_lastErrorCode,
            m_lastErrorMessage);
    }

    if (m_profile.kind()
        != ProfilePaths::Kind::Account) {
        if (error) {
            *error = QStringLiteral(
                "The attachment coordinator requires an account profile.");
        }
        return fail(
            error,
            QStringLiteral(
                "profile_not_account"),
            QStringLiteral(
                "The attachment coordinator requires an account profile."));
    }

    const AccountAttachmentReceipt::ReadResult
        existing =
            AccountAttachmentReceipt::read(
                m_profile);

    if (existing.status
        == AccountAttachmentReceipt::
            ReadStatus::Invalid) {
        if (error) {
            *error = existing.error;
        }
        return fail(
            error,
            QStringLiteral(
                "receipt_invalid"),
            existing.error);
    }

    if (existing.status
        == AccountAttachmentReceipt::
            ReadStatus::Ok) {
        // One attachment at a time, and one identity per attachment: a
        // pending receipt for anything else fails closed and is left
        // untouched.
        if (existing.data.attachmentId
                != attachmentId
            || !sameIdentity(
                existing.data,
                source)) {
            const QString message =
                QStringLiteral(
                    "A pending cloud attachment receipt exists for a different attachment.");
            if (error) {
                *error = message;
            }
            return fail(
                error,
                QStringLiteral(
                    "receipt_mismatch"),
                message);
        }

        if (existing.data.sourceRetired) {
            // The cloud state was verified and committed before the
            // crash; only the explicit receipt clear remains.
            return clearRetiredReceipt(
                error);
        }

        beginOrResume(
            existing.data);
        return true;
    }

    // Fresh flow: the receipt is durable before any server work, so a
    // crash at any later point resumes with the same attachment id.
    AccountAttachmentReceiptData data;
    data.version = 1;
    data.attachmentId = attachmentId;
    data.sourceKind = source.sourceKind;
    data.sourceProfileId =
        source.sourceProfileId;
    data.sourceSemanticDigest =
        source.sourceSemanticDigest;
    data.sourceActivityDigest =
        source.sourceActivityDigest;
    data.sourceRetired = false;

    setState(State::Preparing);

    QString writeError;
    if (!AccountAttachmentReceipt::save(
            m_profile,
            data,
            &writeError)) {
        if (error) {
            *error = writeError;
        }
        return fail(
            error,
            QStringLiteral(
                "receipt_write_failed"),
            writeError);
    }

    m_receipt = data;
    m_snapshotWitness = false;

    sendBegin(m_receipt);
    return true;
}

bool AccountAttachmentCoordinator::resumePending(
    QString *error) {
    if (isFlowActive()) {
        if (error) {
            *error = QStringLiteral(
                "An attachment flow is already running on this coordinator.");
        }
        return false;
    }

    const AccountAttachmentReceipt::ReadResult
        existing =
            AccountAttachmentReceipt::read(
                m_profile);

    if (existing.status
        == AccountAttachmentReceipt::
            ReadStatus::Missing) {
        // No cloud attachment is pending; resuming is a no-op.
        return true;
    }

    if (existing.status
        == AccountAttachmentReceipt::
            ReadStatus::Invalid) {
        if (error) {
            *error = existing.error;
        }
        return fail(
            error,
            QStringLiteral(
                "receipt_invalid"),
            existing.error);
    }

    if (existing.data.sourceRetired) {
        return clearRetiredReceipt(
            error);
    }

    beginOrResume(existing.data);
    return true;
}

bool AccountAttachmentCoordinator::hasPendingReceipt() const {
    return AccountAttachmentReceipt::read(
               m_profile)
               .status
        != AccountAttachmentReceipt::
        ReadStatus::Missing;
}

AccountAttachmentCoordinator::State
AccountAttachmentCoordinator::state() const {
    return m_state;
}

QString AccountAttachmentCoordinator::attachmentId() const {
    return m_receipt.attachmentId;
}

QString AccountAttachmentCoordinator::lastErrorCode() const {
    return m_lastErrorCode;
}

QString AccountAttachmentCoordinator::lastErrorMessage() const {
    return m_lastErrorMessage;
}

void AccountAttachmentCoordinator::setCloudStateVerifier(
    CloudStateVerifier verifier) {
    m_verifier = std::move(verifier);
}

void AccountAttachmentCoordinator::setCanonicalExportVerifier(
    CanonicalExportVerifier verifier) {
    m_canonicalExportVerifier = std::move(verifier);
}

void AccountAttachmentCoordinator::setAttachmentDispositionVerifier(
    AttachmentDispositionVerifier verifier) {
    m_attachmentDispositionVerifier = std::move(verifier);
}

void AccountAttachmentCoordinator::setSourceLifecycle(
    SourceProbe probe,
    SourceRetirer retirer) {
    m_sourceProbe = std::move(probe);
    m_sourceRetirer = std::move(retirer);
}

void AccountAttachmentCoordinator::beginOrResume(
    const AccountAttachmentReceiptData &receipt) {
    m_receipt = receipt;
    m_snapshotWitness = false;

    setState(State::Preparing);

    // The server's durable attachment state plus the receipt's retirement
    // flag derive the resume point, so the flow never re-begins an
    // attachment the server already holds.
    sendGet();
}

void AccountAttachmentCoordinator::sendBegin(
    const AccountAttachmentReceiptData &receipt) {
    m_receipt = receipt;

    setState(State::Beginning);
    m_beginRequestId =
        m_client->beginProfileAttachment(
            m_receipt.attachmentId,
            m_receipt.sourceKind,
            m_receipt.sourceProfileId,
            m_receipt.sourceSemanticDigest,
            m_receipt.sourceActivityDigest,
            m_receipt.manifestDigest,
            m_receipt.manifest);
}

void AccountAttachmentCoordinator::sendGet() {
    m_getRequestId =
        m_client->getProfileAttachment(
            m_receipt.attachmentId);
}

void AccountAttachmentCoordinator::sendCommit() {
    setState(State::Committing);
    m_commitRequestId =
        m_client->commitProfileAttachment(
            m_receipt.attachmentId);
}

void AccountAttachmentCoordinator::handleClientCompleted(
    quint64 requestId,
    AccountOperation operation,
    quint64 accessTokenGeneration,
    const AccountTransportReply &reply) {
    if (operation
            == AccountOperation::
                BeginProfileAttachment
        && requestId == m_beginRequestId
        && m_beginRequestId != 0) {
        m_beginRequestId = 0;
        handleBeginReply(reply);
        return;
    }

    if (operation
            == AccountOperation::
                GetProfileAttachment
        && requestId == m_getRequestId
        && m_getRequestId != 0) {
        m_getRequestId = 0;
        handleGetReply(reply);
        return;
    }

    if (operation
            == AccountOperation::
                CommitProfileAttachment
        && requestId == m_commitRequestId
        && m_commitRequestId != 0) {
        m_commitRequestId = 0;
        handleCommitReply(reply);
        return;
    }

    if (operation
            == AccountOperation::AccountExport
        && requestId == m_exportRequestId
        && m_exportRequestId != 0) {
        m_exportRequestId = 0;
        handleFreshExportReply(
            reply,
            accessTokenGeneration);
        return;
    }

    if (m_state
        != State::EngineBootstrapping)
        return;

    if (!m_engine->attachmentModeActive()
        || m_engine->attachmentId()
            != m_receipt.attachmentId)
        return;

    if (operation
        == AccountOperation::SyncSnapshot) {
        // The final page of the stable snapshot is the durable proof the
        // bootstrap's snapshot half completed. Malformed pages never
        // witness anything: the engine itself fails closed on them.
        if (isReplySuccess(reply)) {
            const QJsonValue hasMore =
                reply.body.value(
                    QStringLiteral(
                        "has_more"));
            if (hasMore.isBool()
                && !hasMore.toBool())
                m_snapshotWitness = true;
        }
        evaluateBootstrap();
        return;
    }

    if (operation
        == AccountOperation::SyncPush) {
        // The engine gates pushes on the stable snapshot, and every push
        // in attachment mode is attached.  A push that was already in flight
        // before attachment mode began can complete after the mode flip, so
        // only a response observed after the engine's durable snapshot flag
        // is set may witness the bootstrap.
        if (m_engine->attachmentSnapshotComplete())
            m_snapshotWitness = true;
        evaluateBootstrap();
        return;
    }
}

void AccountAttachmentCoordinator::handleBeginReply(
    const AccountTransportReply &reply) {
    if (!isReplySuccess(reply)) {
        if (reply.errorCode
            == QLatin1String(
                "attachment_conflict")) {
            fail(nullptr,
                 QStringLiteral(
                     "attachment_conflict"),
                 describeReply(reply));
            return;
        }
        fail(nullptr,
             QStringLiteral(
                 "server_begin_failed"),
             describeReply(reply));
        return;
    }

    const auto response =
        syncWireAttachmentResponseFromJson(
            reply.body);
    if (!response.has_value()
        || response->attachmentId
            != m_receipt.attachmentId) {
        fail(nullptr,
             QStringLiteral(
                 "attachment_protocol_error"),
             QStringLiteral(
                 "The account service returned an invalid attachment response."));
        return;
    }

    if (response->state
        == SyncWireAttachmentState::
            Aborted) {
        fail(nullptr,
             QStringLiteral(
                 "attachment_aborted"),
             QStringLiteral(
                 "The profile attachment was aborted on the server."));
        return;
    }

    if (response->state
        == SyncWireAttachmentState::
            Committed) {
        enterVerifying();
        return;
    }

    if (response->state
        == SyncWireAttachmentState::
            Uploaded) {
        // An uploaded attachment already accepted attached pushes, which
        // the engine only sends after its stable snapshot completed.
        m_snapshotWitness = true;
    }

    ensureEngineAttachmentMode();
}

void AccountAttachmentCoordinator::handleGetReply(
    const AccountTransportReply &reply) {
    if (isReplyNotFound(reply)) {
        // The begin never reached the server (or the attachment was
        // removed): (re)issue it with the receipt's id. The server's
        // begin is idempotent for the same identity.
        sendBegin(m_receipt);
        return;
    }

    if (!isReplySuccess(reply)) {
        fail(nullptr,
             QStringLiteral(
                 "attachment_state_query_failed"),
             describeReply(reply));
        return;
    }

    const auto response =
        syncWireAttachmentResponseFromJson(
            reply.body);
    if (!response.has_value()
        || response->attachmentId
            != m_receipt.attachmentId) {
        fail(nullptr,
             QStringLiteral(
                 "attachment_protocol_error"),
             QStringLiteral(
                 "The account service returned an invalid attachment response."));
        return;
    }

    if (response->state
        == SyncWireAttachmentState::
            Aborted) {
        fail(nullptr,
             QStringLiteral(
                 "attachment_aborted"),
             QStringLiteral(
                 "The profile attachment was aborted on the server."));
        return;
    }

    if (response->state
        == SyncWireAttachmentState::
            Committed) {
        // Already committed: verify locally again (the verification fact
        // is not durable), then retire through the idempotent commit.
        enterVerifying();
        return;
    }

    if (response->state
        == SyncWireAttachmentState::
            Uploaded) {
        m_snapshotWitness = true;
    }

    ensureEngineAttachmentMode();
}

void AccountAttachmentCoordinator::handleCommitReply(
    const AccountTransportReply &reply) {
    if (!isReplySuccess(reply)) {
        if (reply.errorCode
            == QLatin1String(
                "attachment_not_active")) {
            fail(nullptr,
                 QStringLiteral(
                     "attachment_aborted"),
                 describeReply(reply));
            return;
        }
        fail(nullptr,
             QStringLiteral(
                 "commit_failed"),
             describeReply(reply));
        return;
    }

    const auto response =
        syncWireAttachmentResponseFromJson(
            reply.body);
    if (!response.has_value()
        || response->attachmentId
            != m_receipt.attachmentId
        || response->state
            != SyncWireAttachmentState::
                Committed) {
        fail(nullptr,
             QStringLiteral(
                 "attachment_protocol_error"),
             QStringLiteral(
                 "The account service returned an invalid commit response."));
        return;
    }

    if (!response->freshExportCursor.isEmpty()) {
        m_commitResponse = *response;
        sendFreshExport(*response);
        return;
    }

    m_commitResponse = *response;
    finishCommit();
}

void AccountAttachmentCoordinator::sendFreshExport(
    const SyncWireAttachmentResponse &response) {
    m_freshExportSnapshotId = response.freshExportSnapshotId;
    m_freshExportCursor = response.freshExportCursor;
    m_freshExportHighWaterSeq = response.freshExportHighWaterSeq;
    m_freshExportPages.clear();

    setState(State::Verifying);
    m_exportAccessTokenGeneration =
        m_client->accessTokenGeneration();
    m_exportRequestId = m_client->pullAccountExport(
        m_freshExportCursor);
}

void AccountAttachmentCoordinator::handleFreshExportReply(
    const AccountTransportReply &reply,
    quint64 accessTokenGeneration) {
    if (accessTokenGeneration != m_exportAccessTokenGeneration) {
        fail(nullptr,
             QStringLiteral("session_generation_changed"),
             QStringLiteral(
                 "The account session changed while the attachment export was being fetched."));
        return;
    }

    if (!isReplySuccess(reply)) {
        fail(nullptr,
             QStringLiteral("fresh_export_fetch_failed"),
             describeReply(reply));
        return;
    }

    const auto page = syncWireExportPageFromJson(reply.body);
    if (!page.has_value()) {
        fail(nullptr,
             QStringLiteral("fresh_export_protocol_error"),
             QStringLiteral(
                 "The account service returned an invalid canonical export page."));
        return;
    }

    QString validationError;
    if (!validateFreshExportPage(*page, &validationError)) {
        fail(nullptr,
             QStringLiteral("fresh_export_protocol_error"),
             validationError);
        return;
    }

    m_freshExportPages.append(*page);
    if (page->hasMore) {
        m_exportAccessTokenGeneration =
            m_client->accessTokenGeneration();
        m_exportRequestId = m_client->pullAccountExport(
            page->nextCursor);
        return;
    }

    verifyFreshExport();
}

bool AccountAttachmentCoordinator::validateFreshExportPage(
    const SyncWireExportPage &page,
    QString *error) const {
    constexpr int kMaximumExportPages = 1000;
    constexpr int kMaximumExportItemsPerPage = 100;
    if (m_freshExportPages.size() >= kMaximumExportPages
        || page.items.size() > kMaximumExportItemsPerPage) {
        if (error)
            *error = QStringLiteral(
                "The canonical export page exceeds the attachment bounds.");
        return false;
    }

    const QUuid snapshotUuid(page.snapshotId);
    if (page.format != QLatin1String("colosseum.account.export")
        || page.schemaVersion != 1
        || snapshotUuid.isNull()
        || snapshotUuid.toString(QUuid::WithoutBraces)
               != page.snapshotId
        || page.snapshotId != m_freshExportSnapshotId
        || page.highWaterServerSeq != m_freshExportHighWaterSeq
        || page.cursor.isEmpty()
        || (m_freshExportPages.isEmpty()
            && page.cursor != m_freshExportCursor)) {
        if (error)
            *error = QStringLiteral(
                "The canonical export page does not match the sealed attachment snapshot.");
        return false;
    }

    QSet<QString> seenKeys;
    for (const SyncWireExportPage &previous :
         std::as_const(m_freshExportPages)) {
        for (const QJsonValue &value : previous.items) {
            if (value.isObject())
                seenKeys.insert(
                    value.toObject().value(
                        QStringLiteral("category"))
                        .toString()
                    + QChar(0x1f)
                    + value.toObject().value(
                          QStringLiteral("key"))
                          .toString());
        }
    }

    for (const QJsonValue &value : page.items) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral(
                    "The canonical export contains a non-object item.");
            return false;
        }
        const QJsonObject item = value.toObject();
        const QString kind = item.value(
            QStringLiteral("kind")).toString();
        const QString category = item.value(
            QStringLiteral("category")).toString();
        const QString key = item.value(
            QStringLiteral("key")).toString();
        const QJsonValue payload = item.value(
            QStringLiteral("payload"));
        const QString identity = category + QChar(0x1f) + key;
        if (kind.isEmpty()
            || category.isEmpty()
            || key.isEmpty()
            || payload.isUndefined()
            || seenKeys.contains(identity)
            || key.startsWith(QChar('/'))
            || QJsonDocument(payload.toObject()).toJson(
                   QJsonDocument::Compact).size()
                > 256 * 1024) {
            if (error)
                *error = QStringLiteral(
                    "The canonical export contains an invalid or duplicate item.");
            return false;
        }
        seenKeys.insert(identity);
    }

    return true;
}

void AccountAttachmentCoordinator::verifyFreshExport() {
    if (!m_attachmentDispositionVerifier && !m_canonicalExportVerifier) {
        fail(nullptr,
             QStringLiteral("verification_unavailable"),
             QStringLiteral(
                 "The canonical attachment export cannot be verified without a semantic verifier."));
        return;
    }

    QString verifyError;
    const bool verified = m_attachmentDispositionVerifier
        ? m_attachmentDispositionVerifier(
              m_commitResponse,
              m_freshExportPages,
              &verifyError)
        : m_canonicalExportVerifier(
              m_freshExportPages,
              &verifyError);
    if (!verified) {
        fail(nullptr,
             QStringLiteral("cloud_state_verification_failed"),
             verifyError.isEmpty()
                 ? QStringLiteral(
                       "The fresh canonical export did not absorb the source contribution.")
                 : verifyError);
        return;
    }

    finishCommit();
}

void AccountAttachmentCoordinator::ensureEngineAttachmentMode() {
    if (m_engine->attachmentModeActive()
        && m_engine->attachmentId()
            != m_receipt.attachmentId) {
        fail(nullptr,
             QStringLiteral(
                 "engine_mode_mismatch"),
             QStringLiteral(
                 "The sync engine is already attached to a different attachment."));
        return;
    }

    if (!m_engine->attachmentModeActive()) {
        QString error;
        if (!m_engine->beginAttachmentMode(
                m_receipt.attachmentId,
                &error)) {
            fail(nullptr,
                 QStringLiteral(
                     "engine_begin_failed"),
                 error);
            return;
        }

        if (!persistAndEnqueueManifest(&error)) {
            fail(nullptr,
                 QStringLiteral("manifest_persist_failed"),
                 error);
            return;
        }

        setState(
            State::EngineBootstrapping);
        evaluateBootstrap();
        return;
    }

    // The engine restored our attachment mode across the restart.
    if (m_engine->pendingAttachmentOutboxCount()
        > 0) {
        QString error;
        if (!persistAndEnqueueManifest(&error)) {
            fail(nullptr,
                 QStringLiteral("manifest_persist_failed"),
                 error);
            return;
        }
        // Attached mutations are still draining; the engine resumes from
        // its own durable phase (snapshot pages or pushes) once kicked.
        setState(
            State::EngineBootstrapping);
        m_engine->requestImmediateSync();
        evaluateBootstrap();
        return;
    }

    if (m_snapshotWitness) {
        QString error;
        if (!persistAndEnqueueManifest(&error)) {
            fail(nullptr,
                 QStringLiteral("manifest_persist_failed"),
                 error);
            return;
        }
        // The server state already proves the bootstrap's snapshot
        // completed durably (uploaded/committed); nothing pending remains.
        setState(
            State::EngineBootstrapping);
        m_engine->requestImmediateSync();
        evaluateBootstrap();
        return;
    }

    // In mode, outbox empty, and no witness this run: the stable snapshot
    // may already be done with no traffic left to prove it (a zero-push
    // attachment). Exit and re-enter the mode so the frozen-cursor
    // snapshot re-runs observably; the re-read is idempotent (union
    // merge, no inferred deletes) and bounded.
    QString error;
    if (!endEngineAttachmentMode(
            &error)) {
        fail(nullptr,
             QStringLiteral(
                 "engine_end_failed"),
             error);
        return;
    }

    if (!m_engine->beginAttachmentMode(
            m_receipt.attachmentId,
            &error)) {
        fail(nullptr,
             QStringLiteral(
                 "engine_begin_failed"),
             error);
        return;
    }

    if (!persistAndEnqueueManifest(&error)) {
        fail(nullptr,
             QStringLiteral("manifest_persist_failed"),
             error);
        return;
    }

    setState(State::EngineBootstrapping);
    evaluateBootstrap();
}

bool AccountAttachmentCoordinator::persistAndEnqueueManifest(
    QString *error) {
    if (!m_engine->attachmentModeActive()
        || m_engine->attachmentId() != m_receipt.attachmentId) {
        if (error)
            *error = QStringLiteral(
                "Attachment manifest requires the active recorded attachment mode.");
        return false;
    }

    if (m_receipt.manifest.isEmpty()) {
        QString manifestError;
        const QSet<QString> preexisting(
            m_receipt.preexistingMutationIds.cbegin(),
            m_receipt.preexistingMutationIds.cend());
        const QList<SyncWireAttachmentManifestItem> manifest =
            m_engine->attachmentManifest(preexisting, &manifestError);
        if (!manifestError.isEmpty()) {
            if (error)
                *error = manifestError;
            return false;
        }
        m_receipt.manifest = manifest;
        if (!AccountAttachmentReceipt::save(
                m_profile,
                m_receipt,
                error)) {
            return false;
        }
    }

    if (m_receipt.manifest.isEmpty())
        return true;

    QStringList accepted;
    if (!m_engine->enqueueAttachmentMutations(
            m_receipt.manifest,
            &accepted,
            error)) {
        return false;
    }
    if (accepted.size() != m_receipt.manifest.size()) {
        if (error)
            *error = QStringLiteral(
                "The sync engine did not accept the complete attachment manifest.");
        return false;
    }
    return true;
}

void AccountAttachmentCoordinator::evaluateBootstrap() {
    if (m_state
        != State::EngineBootstrapping)
        return;

    if (!m_engine->attachmentModeActive()
        || m_engine->attachmentId()
            != m_receipt.attachmentId) {
        fail(nullptr,
             QStringLiteral(
                 "engine_mode_mismatch"),
             QStringLiteral(
                 "The sync engine left the attachment mode unexpectedly."));
        return;
    }

    if (!m_snapshotWitness)
        return;

    // A server-side uploaded state proves that an earlier process completed
    // its frozen snapshot before pushing.  This process must still finish its
    // own durable snapshot replay before it may verify and retire the source.
    if (!m_engine->attachmentSnapshotComplete())
        return;

    if (m_engine->pendingAttachmentOutboxCount()
        != 0)
        return;

    if (m_engine->state()
        != SyncEngine::State::Idle)
        return;

    enterVerifying();
}

void AccountAttachmentCoordinator::enterVerifying() {
    setState(State::Verifying);

    if (!m_verifier) {
        // No verifier means the cloud state cannot be verified, so the
        // source must not be retired: fail closed.
        fail(nullptr,
             QStringLiteral(
                 "verification_unavailable"),
             QStringLiteral(
                 "The cloud state cannot be verified without a verifier; the local source stays active."));
        return;
    }

    QString verifyError;
    if (!m_verifier(
            &verifyError)) {
        fail(nullptr,
             QStringLiteral(
                 "cloud_state_verification_failed"),
             verifyError.isEmpty()
                 ? QStringLiteral(
                       "The verified cloud state did not reconstruct the expected merged projection.")
                 : verifyError);
        return;
    }

    sendCommit();
}

void AccountAttachmentCoordinator::finishCommit() {
    setState(State::Retiring);

    QString error;
    if (!endEngineAttachmentMode(
            &error)) {
        fail(nullptr,
             QStringLiteral(
                 "engine_end_failed"),
             error);
        return;
    }

    const QSet<QString> preexisting(
        m_receipt.preexistingMutationIds.cbegin(),
        m_receipt.preexistingMutationIds.cend());
    QString manifestError;
    const QList<SyncWireAttachmentManifestItem> nextManifest =
        m_engine->attachmentManifest(preexisting, &manifestError);
    if (!manifestError.isEmpty()) {
        fail(nullptr,
             QStringLiteral("manifest_capture_failed"),
             manifestError);
        return;
    }
    if (!nextManifest.isEmpty()) {
        AccountAttachmentReceiptData next = m_receipt;
        next.attachmentId = QUuid::createUuid()
            .toString(QUuid::WithoutBraces)
            .toLower();
        next.manifest = nextManifest;
        next.manifestDigest.clear();
        next.sourceRetired = false;
        next.retirementPhase =
            AccountAttachmentReceipt::retirementPhasePending();
        if (!AccountAttachmentReceipt::save(m_profile, next, &error)) {
            fail(nullptr,
                 QStringLiteral("receipt_write_failed"),
                 error);
            return;
        }
        const AccountAttachmentReceipt::ReadResult persisted =
            AccountAttachmentReceipt::read(m_profile);
        if (persisted.status != AccountAttachmentReceipt::ReadStatus::Ok) {
            fail(nullptr,
                 QStringLiteral("receipt_invalid"),
                 persisted.error.isEmpty()
                     ? QStringLiteral("The next attachment receipt could not be verified.")
                     : persisted.error);
            return;
        }
        m_receipt = persisted.data;
        m_snapshotWitness = false;
        m_commitResponse = {};
        m_freshExportPages.clear();
        m_freshExportSnapshotId.clear();
        m_freshExportCursor.clear();
        m_freshExportHighWaterSeq = 0;
        sendBegin(m_receipt);
        return;
    }

    if (m_sourceProbe || m_sourceRetirer) {
        if (!m_sourceProbe || !m_sourceRetirer) {
            fail(nullptr,
                 QStringLiteral("source_lifecycle_unavailable"),
                 QStringLiteral(
                     "The attachment source lifecycle is only partially configured."));
            return;
        }

        if (!AccountAttachmentReceipt::markRetirementStarted(
                m_profile,
                &error)) {
            fail(nullptr,
                 QStringLiteral("receipt_retire_failed"),
                 error);
            return;
        }
        m_receipt.retirementPhase =
            AccountAttachmentReceipt::retirementPhaseStarted();
        m_receipt.sourceRetired = false;

        const SourceState sourceState =
            m_sourceProbe(m_receipt, &error);
        if (sourceState == SourceState::Matching) {
            if (!m_sourceRetirer(m_receipt, &error)) {
                fail(nullptr,
                     QStringLiteral("source_retire_failed"),
                     error.isEmpty()
                         ? QStringLiteral(
                               "The recorded attachment source could not be retired safely.")
                         : error);
                return;
            }
        } else if (sourceState == SourceState::Changed
                   || sourceState == SourceState::Ambiguous) {
            fail(nullptr,
                 QStringLiteral("source_retire_refused"),
                 error.isEmpty()
                     ? QStringLiteral(
                           "The recorded attachment source changed or could not be identified safely.")
                     : error);
            return;
        } else if (sourceState != SourceState::Empty) {
            fail(nullptr,
                 QStringLiteral("source_probe_failed"),
                 error.isEmpty()
                     ? QStringLiteral(
                           "The recorded attachment source could not be inspected safely.")
                     : error);
            return;
        }

        QString verifyError;
        const SourceState afterRetire =
            m_sourceProbe(m_receipt, &verifyError);
        if (afterRetire != SourceState::Empty) {
            fail(nullptr,
                 QStringLiteral("source_retire_unverified"),
                 verifyError.isEmpty()
                     ? QStringLiteral(
                           "The recorded attachment source was not empty after retirement.")
                     : verifyError);
            return;
        }

        if (!AccountAttachmentReceipt::markSourceRetired(
                m_profile,
                &error)) {
            fail(nullptr,
                 QStringLiteral("receipt_retire_failed"),
                 error);
            return;
        }
        m_receipt.sourceRetired = true;
        m_receipt.retirementPhase =
            AccountAttachmentReceipt::retirementPhaseSourceRetired();
    } else {
        // Compatibility for the coordinator's existing isolated test
        // harnesses.  Production AccountRuntime always installs the source
        // lifecycle above before resuming a receipt.
        if (!AccountAttachmentReceipt::markSourceRetired(
                m_profile,
                &error)) {
            fail(nullptr,
                 QStringLiteral("receipt_retire_failed"),
                 error);
            return;
        }
        m_receipt.sourceRetired = true;
        m_receipt.retirementPhase =
            AccountAttachmentReceipt::retirementPhaseSourceRetired();
    }

    if (!AccountAttachmentReceipt::clear(
            m_profile,
            &error)) {
        fail(nullptr,
             QStringLiteral(
                 "receipt_clear_failed"),
             error);
        return;
    }

    succeed();
}

bool AccountAttachmentCoordinator::endEngineAttachmentMode(
    QString *error) const {
    if (!m_engine->active()
        || !m_engine
               ->attachmentModeActive())
        return true;
    return m_engine->endAttachmentMode(
        error);
}

bool AccountAttachmentCoordinator::clearRetiredReceipt(
    QString *error) {
    m_receipt =
        AccountAttachmentReceiptData();
    m_snapshotWitness = false;

    setState(State::Preparing);

    QString clearError;
    if (!AccountAttachmentReceipt::clear(
            m_profile,
            &clearError)) {
        if (error) {
            *error = clearError;
        }
        return fail(
            error,
            QStringLiteral(
                "receipt_clear_failed"),
            clearError);
    }

    succeed();
    return true;
}

bool AccountAttachmentCoordinator::validateInputs(
    const QString &attachmentId,
    const SourceIdentity &source,
    QString *error) const {
    const QUuid parsed(attachmentId);
    if (parsed.isNull()
        || parsed.toString(
               QUuid::WithoutBraces)
            != attachmentId) {
        m_lastErrorCode =
            QStringLiteral(
                "invalid_attachment_id");
        m_lastErrorMessage =
            QStringLiteral(
                "The attachment identity must be a canonical lowercase UUID.");
        if (error) {
            *error =
                m_lastErrorMessage;
        }
        return false;
    }

    if (source.sourceKind
            != AccountAttachmentReceipt::
                sourceKindLegacyLocal()
        && source.sourceKind
            != AccountAttachmentReceipt::
                sourceKindLocalOnly()) {
        m_lastErrorCode =
            QStringLiteral(
                "invalid_source_kind");
        m_lastErrorMessage =
            QStringLiteral(
                "The attachment source kind must be legacy_local or local_only.");
        if (error) {
            *error =
                m_lastErrorMessage;
        }
        return false;
    }

    if (source.sourceProfileId
            .trimmed()
            .isEmpty()) {
        m_lastErrorCode =
            QStringLiteral(
                "invalid_source_profile");
        m_lastErrorMessage =
            QStringLiteral(
                "The attachment source profile is required.");
        if (error) {
            *error =
                m_lastErrorMessage;
        }
        return false;
    }

    if (source.sourceSemanticDigest
            .trimmed()
            .isEmpty()) {
        m_lastErrorCode =
            QStringLiteral(
                "invalid_source_digest");
        m_lastErrorMessage =
            QStringLiteral(
                "The attachment source semantic digest is required.");
        if (error) {
            *error =
                m_lastErrorMessage;
        }
        return false;
    }

    // sourceActivityDigest has no constraint: the empty string is the
    // valid "source had no durable Activity ledger" sentinel.
    return true;
}

bool AccountAttachmentCoordinator::sameIdentity(
    const AccountAttachmentReceiptData &receipt,
    const SourceIdentity &source) {
    return receipt.sourceKind
            == source.sourceKind
        && receipt.sourceProfileId
            == source.sourceProfileId
        && receipt.sourceSemanticDigest
            == source.sourceSemanticDigest
        && receipt.sourceActivityDigest
            == source.sourceActivityDigest;
}

bool AccountAttachmentCoordinator::isFlowActive() const {
    return m_state
            == State::Preparing
        || m_state
            == State::Beginning
        || m_state
            == State::EngineBootstrapping
        || m_state == State::Verifying
        || m_state
            == State::Committing
        || m_state
            == State::Retiring;
}

void AccountAttachmentCoordinator::setState(
    State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit progress(m_state);
}

void AccountAttachmentCoordinator::fail(
    const QString &errorCode,
    const QString &errorMessage) {
    m_lastErrorCode = errorCode;
    m_lastErrorMessage =
        errorMessage;
    setState(State::Failed);
    emit finished(
        false,
        m_lastErrorCode,
        m_lastErrorMessage);
}

bool AccountAttachmentCoordinator::fail(
    QString *error,
    const QString &errorCode,
    const QString &errorMessage) {
    if (error
        && !errorMessage.isEmpty())
        *error = errorMessage;
    fail(errorCode,
         errorMessage);
    return false;
}

void AccountAttachmentCoordinator::succeed() {
    m_lastErrorCode.clear();
    m_lastErrorMessage.clear();
    setState(State::Completed);
    emit finished(
        true,
        QString(),
        QString());
}
