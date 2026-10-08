#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

// Engine-neutral operation/request seam shared by the QtWebEngine and WebView2
// adapters (feria-browser-engine-contract-v1, GLM slice 1).
//
// The canonical SessionStore keeps owning close state: its browserCloseAttempt
// token IS the operation id of a request-close operation, so this seam keeps no
// second close ledger. What it owns instead is the typed fencing the contract
// requires: every admitted operation settles exactly once with one terminal
// outcome, late/stale identities cannot settle or answer anything, consent
// requests are one-use, and success/cancellation evidence is validated instead
// of trusted.
//
// The seam never sees engine request objects (COM or Qt): adapters pass typed
// values only. The store is referenced as a plain QObject and driven through
// its existing Q_INVOKABLE surface, so the header-only SessionStore needs no
// reverse include and any owner implementing that surface can be fenced.
class FeriaBrowserOperations final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *store READ store WRITE setStore NOTIFY storeChanged)
public:
    explicit FeriaBrowserOperations(QObject *parent = nullptr);

    QObject *store() const { return m_store.data(); }
    void setStore(QObject *store);

    // True when identity still names the canonical record byte-for-byte
    // (same rule as SessionStore's private browserIdentityIndex: exact map
    // equality, so a stale, partial or extraneous map is rejected).
    Q_INVOKABLE bool identityCurrent(const QVariantMap &identity) const;

    // Admit an operation for an engine adapter. kind is one of
    // navigate/back/forward/stop/reload/request-close. For request-close the
    // operation id is the canonical close attempt allocated by the store, so
    // only one close operation can be pending per space. Returns
    // {admitted, operationId, reason}.
    Q_INVOKABLE QVariantMap beginOperation(const QVariantMap &identity,
                                           const QString &engineId,
                                           const QString &kind);

    // Open a one-use consent request bound to an operation and identity.
    // purpose is "app-close" (operation required) or "external-unload"
    // (site-initiated unload consent; no operation, never deletes a space).
    // source carries the provenance the native request actually exposed;
    // missing provenance stays explicit ({origin: ""}).
    // Returns {opened, requestId, reason}.
    Q_INVOKABLE QVariantMap openRequest(const QString &operationId,
                                        const QVariantMap &identity,
                                        const QString &purpose,
                                        const QVariantMap &source);

    // Consume a request's one answer. The request must be unanswered and its
    // captured identity must still be current. Returns {valid, reason,
    // purpose, operationId}.
    Q_INVOKABLE QVariantMap answerRequest(const QString &requestId,
                                          const QVariantMap &identity);

    // Settle an operation exactly once. outcome is one of
    // succeeded/cancelled/failed/blocked/unsupported/invalidated; evidence is
    // the observed terminal proof kind and extra may carry descriptor (for
    // close success) and nativeNavigationId. Cancellation and close success
    // require whitelisted engine evidence — a timer, S_OK or disposal is not
    // accepted. A stale identity forces outcome "invalidated" and touches no
    // canonical state. For request-close operations the canonical attempt is
    // settled through the store (accepted only for succeeded with a valid
    // descriptor). Returns {settled, outcome, duplicate, reason}.
    Q_INVOKABLE QVariantMap settleOperation(const QVariantMap &identity,
                                            const QString &operationId,
                                            const QString &outcome,
                                            const QString &reason,
                                            const QString &evidence,
                                            const QVariantMap &extra);

    // Recorded terminal result of an operation (empty before settlement).
    // {operationId, kind, engineId, identity, outcome, reason, evidence,
    // nativeNavigationId}.
    Q_INVOKABLE QVariantMap operationResult(const QString &operationId) const;

    // State of a consent request: {present, answered, purpose, operationId,
    // identity, source}.
    Q_INVOKABLE QVariantMap requestState(const QString &requestId) const;

signals:
    void storeChanged();
    void operationSettled(const QString &operationId, const QString &outcome);

private:
    struct Operation
    {
        QVariantMap identity;
        QString engineId;
        QString kind;
        QString outcome;
        QString reason;
        QString evidence;
        QVariantMap extra;
        bool settled = false;
    };
    struct Request
    {
        QString operationId;
        QVariantMap identity;
        QString purpose;
        QVariantMap source;
        bool answered = false;
    };

    QString beginStoreClose(const QVariantMap &identity) const;
    bool settleStoreClose(const QVariantMap &identity, const QString &attempt,
                          bool accepted, const QVariantMap &descriptor) const;
    QVariantMap canonicalIdentity(const QString &spaceId) const;
    void retain(QStringList &order, QVariantMap &records,
                const QString &id, const QVariantMap &record);

    QPointer<QObject> m_store;
    QVariantMap m_operations; // operationId -> Operation, as variant maps
    QVariantMap m_requests;   // requestId -> Request
    QStringList m_operationOrder;
    QStringList m_requestOrder;
    bool m_fenceDisabled = false; // test-only RED control (env-gated)
};
