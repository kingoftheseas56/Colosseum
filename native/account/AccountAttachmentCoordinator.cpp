// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountAttachmentCoordinator.h"

#include "SyncEngine.h"
#include "SyncProtocol.h"

#include <QUuid>

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
            m_receipt.sourceSemanticDigest);
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
    Q_UNUSED(accessTokenGeneration)

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
        // in attachment mode is attached: any push reply while our mode is
        // active proves the snapshot gate opened.
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

        setState(
            State::EngineBootstrapping);
        evaluateBootstrap();
        return;
    }

    // The engine restored our attachment mode across the restart.
    if (m_engine->pendingOutboxCount()
        > 0) {
        // Attached mutations are still draining; the engine resumes from
        // its own durable phase (snapshot pages or pushes) once kicked.
        setState(
            State::EngineBootstrapping);
        m_engine->requestImmediateSync();
        evaluateBootstrap();
        return;
    }

    if (m_snapshotWitness) {
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

    setState(State::EngineBootstrapping);
    evaluateBootstrap();
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

    if (m_engine->pendingOutboxCount()
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

    // Retirement is durable before the receipt may disappear: a crash
    // here resumes into the clear-only tail.
    if (!AccountAttachmentReceipt::
            markSourceRetired(
                m_profile,
                &error)) {
        fail(nullptr,
             QStringLiteral(
                 "receipt_retire_failed"),
             error);
        return;
    }
    m_receipt.sourceRetired = true;

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
