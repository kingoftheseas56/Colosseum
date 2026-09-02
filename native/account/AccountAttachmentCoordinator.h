#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.
//
// Arc 36 Wave 4B lane N-16: the attachment orchestrator. It turns the
// existing-account attachment contract (N-12 server lifecycle, N-13 wire
// client, N-14 crash-safe receipt, N-15 engine attachment mode) into one
// resumable, crash-safe flow:
//
//   capture/validate source identity and digests
//     -> write the initial receipt (durable before any server work)
//     -> server begin (idempotent, one lowercase-UUID id reused forever)
//     -> engine attachment mode (the engine pages the stable snapshot,
//        applies it union/merge, then drains the outbox as attached pushes)
//     -> verify the applied canonical state locally (verifier callback)
//     -> server commit (idempotent)
//     -> receipt markSourceRetired (only after verified cloud state)
//     -> clear receipt.
//
// The coordinator owns WHEN, not HOW: every server interaction goes through
// the N-13 AccountClient operations and every engine interaction goes through
// the N-15 public attachment-mode API. It never touches SyncEngine internals,
// adapters, or stores directly — the cloud-state verification gate is a
// callback the embedder (N-17) wires to the adapters' projections.
//
// Crash safety: the receipt is the durable source of attachment identity. A
// restart with a pending receipt resumes from the recorded state instead of
// re-beginning: the server's attachment state (open/uploaded/committed) plus
// the receipt's retirement flag derive the resume point. Failures never
// retire the local source, leave the receipt failing closed, and keep the
// server attachment resumable (the frozen wire has no abort route, so an
// abandoned attachment simply stays open/uploaded on the server).

#include "AccountAttachmentReceipt.h"
#include "AccountClient.h"
#include "ProfilePaths.h"

#include <QString>

#include <functional>

class SyncEngine;

class AccountAttachmentCoordinator final
    : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Preparing,
        Beginning,
        EngineBootstrapping,
        Verifying,
        Committing,
        Retiring,
        Completed,
        Failed
    };
    Q_ENUM(State)

    // The local source being attached. The digests come from the local
    // attach step (FirstAccountProfileCoordinator's merge) — this
    // coordinator never re-derives them.
    struct SourceIdentity {
        QString sourceKind;      // legacy_local | local_only
        QString sourceProfileId;
        QString sourceSemanticDigest;
        QString sourceActivityDigest; // empty = no Activity ledger
    };

    // The local verification gate: reconstructs the expected merged
    // projection from the applied canonical state (adapters' projections /
    // Activity facts) and reports whether the account now holds it. The
    // source may only be retired after this returns true.
    using CloudStateVerifier =
        std::function<bool(QString *error)>;

    AccountAttachmentCoordinator(
        AccountClient *client,
        SyncEngine *engine,
        const ProfilePaths &profile,
        QObject *parent = nullptr);
    ~AccountAttachmentCoordinator() override;

    // Starts the attachment flow for the given source. The attachment id
    // must be a canonical lowercase UUID and is reused across every retry
    // and restart of the same attachment. If a pending receipt already
    // exists for this exact identity, the flow resumes from the recorded
    // state instead of restarting. Returns false only for immediate
    // validation/busy refusals; the flow outcome arrives via finished().
    bool start(const QString &attachmentId,
               const SourceIdentity &source,
               QString *error = nullptr);

    // Resumes a pending flow from the durable receipt after a crash or
    // restart. With no pending receipt this is a no-op success. With a
    // receipt whose source is already retired, only the receipt clear
    // remains. Returns false only for immediate refusals.
    bool resumePending(QString *error = nullptr);

    bool hasPendingReceipt() const;

    State state() const;
    QString attachmentId() const;
    QString lastErrorCode() const;
    QString lastErrorMessage() const;

    void setCloudStateVerifier(
        CloudStateVerifier verifier);

signals:
    // Emitted on every step transition; suitable for progress UI (N-17).
    void progress(
        AccountAttachmentCoordinator::State state);

    // Emitted exactly once per attempt when the flow reaches a terminal
    // state (Completed or Failed). A Failed flow keeps the receipt and the
    // local source and can be resumed later.
    void finished(bool succeeded,
                  const QString &errorCode,
                  const QString &errorMessage);

private:
    void handleClientCompleted(
        quint64 requestId,
        AccountOperation operation,
        quint64 accessTokenGeneration,
        const AccountTransportReply &reply);

    void beginOrResume(
        const AccountAttachmentReceiptData &receipt);
    void sendBegin(
        const AccountAttachmentReceiptData &receipt);
    void sendGet();
    void sendCommit();
    void handleBeginReply(
        const AccountTransportReply &reply);
    void handleGetReply(
        const AccountTransportReply &reply);
    void handleCommitReply(
        const AccountTransportReply &reply);

    void ensureEngineAttachmentMode();
    void evaluateBootstrap();
    void enterVerifying();
    void finishCommit();

    bool endEngineAttachmentMode(
        QString *error) const;

    bool validateInputs(
        const QString &attachmentId,
        const SourceIdentity &source,
        QString *error) const;

    bool clearRetiredReceipt(
        QString *error);

    bool isFlowActive() const;

    static bool sameIdentity(
        const AccountAttachmentReceiptData &receipt,
        const SourceIdentity &source);

    void setState(State state);
    void fail(const QString &errorCode,
              const QString &errorMessage);
    bool fail(QString *error,
              const QString &errorCode,
              const QString &errorMessage);
    void succeed();

    AccountClient *m_client = nullptr;
    SyncEngine *m_engine = nullptr;
    ProfilePaths m_profile;
    CloudStateVerifier m_verifier;

    State m_state = State::Idle;
    AccountAttachmentReceiptData m_receipt;

    // Mutable: const validation records the refusal reason.
    mutable QString m_lastErrorCode;
    mutable QString m_lastErrorMessage;

    quint64 m_beginRequestId = 0;
    quint64 m_getRequestId = 0;
    quint64 m_commitRequestId = 0;

    // Durable-proof latch for "the engine's attachment bootstrap has
    // completed its stable snapshot": observed in this run via the final
    // snapshot page, an attached push (the engine gates pushes on the
    // snapshot), or a server state that already proves it (uploaded or
    // committed).
    bool m_snapshotWitness = false;
};
