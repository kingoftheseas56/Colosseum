#include "FeriaBrowserOperations.h"

#include <QMetaObject>
#include <QStringList>
#include <QUuid>

namespace {

const int kMaxRetained = 256;

// Observed terminal proof kinds. A timer, a dispatch S_OK or controller
// disposal is deliberately absent from these lists: those are never consent,
// rejection or departure evidence (contract: consented-close semantics).
const QStringList kCancellationEvidence{
    QStringLiteral("explicit-stay-deferral-completed"),
    QStringLiteral("dialog-rejected"),
    QStringLiteral("window-close-rejected"),
    QStringLiteral("user-stop-requested"),
    QStringLiteral("superseded-by-navigation"),
};
const QStringList kCloseSuccessEvidence{
    QStringLiteral("window-close-acknowledged"),
    QStringLiteral("correlated-navigation-departure"),
};

const QStringList kOutcomes{
    QStringLiteral("succeeded"), QStringLiteral("cancelled"),
    QStringLiteral("failed"), QStringLiteral("blocked"),
    QStringLiteral("unsupported"), QStringLiteral("invalidated"),
};
const QStringList kKinds{
    QStringLiteral("navigate"), QStringLiteral("back"), QStringLiteral("forward"),
    QStringLiteral("stop"), QStringLiteral("reload"), QStringLiteral("request-close"),
};
const QStringList kPurposes{
    QStringLiteral("app-close"), QStringLiteral("external-unload"),
};
const QStringList kNavigationKinds{
    QStringLiteral("navigate"), QStringLiteral("back"), QStringLiteral("forward"),
    QStringLiteral("stop"), QStringLiteral("reload"),
};

QString newToken(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isLiveOperation(const QVariantMap &record)
{
    return !record.value(QStringLiteral("settled")).toBool();
}

bool isLiveRequest(const QVariantMap &record)
{
    return !record.value(QStringLiteral("answered")).toBool();
}

} // namespace

void FeriaBrowserOperations::retain(QStringList &order, QVariantMap &records,
                                    const QString &id, const QVariantMap &record)
{
    if (!order.contains(id)) order.append(id);
    records.insert(id, record);
    // Only completed history is bounded. Live ownership (pending operations,
    // unanswered requests) is never evicted by retention: losing a pending
    // close ticket would strand the canonical attempt, and losing an
    // unanswered request would strand a native deferral.
    while (order.size() > kMaxRetained) {
        const QString &oldest = order.first();
        if (isLiveOperation(records.value(oldest).toMap())
                || isLiveRequest(records.value(oldest).toMap()))
            break;
        records.remove(order.takeFirst());
    }
}

FeriaBrowserOperations::FeriaBrowserOperations(QObject *parent)
    : QObject(parent)
{
#ifdef FERIA_TEST_CONTROLS
    // Test-build-only RED control for the integration negative check: with
    // this variable set to 1, the shared fences are switched off so a
    // deliberately unsafe adapter decision can be shown to fail the
    // assertions the fences normally guarantee. Production builds compile
    // this out entirely.
    m_fenceDisabled = qEnvironmentVariable("FERIA_UNSAFE_OPERATION_FENCE")
                      == QStringLiteral("1");
#endif
}

void FeriaBrowserOperations::setStore(QObject *store)
{
    if (m_store.data() == store)
        return;
    m_store = store;
    emit storeChanged();
}

QVariantMap FeriaBrowserOperations::canonicalIdentity(const QString &spaceId) const
{
    if (m_store.isNull() || spaceId.isEmpty())
        return {};
    QVariantMap identity;
    QMetaObject::invokeMethod(const_cast<QObject *>(m_store.data()),
                              "browserIdentity",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(QVariantMap, identity),
                              Q_ARG(QString, spaceId));
    return identity;
}

bool FeriaBrowserOperations::identityCurrent(const QVariantMap &identity) const
{
    if (m_fenceDisabled)
        return true;
    if (identity.isEmpty())
        return false;
    const QVariantMap canonical = canonicalIdentity(
        identity.value(QStringLiteral("spaceId")).toString());
    // Exact map equality: stale values, missing or extraneous fields all fail,
    // matching the canonical store's own identity index rule.
    return !canonical.isEmpty() && canonical == identity;
}

bool FeriaBrowserOperations::navigationIdentityAlive(const QVariantMap &identity) const
{
    // A navigation operation commits the replacement document itself, so its
    // own terminal naturally trails a documentGeneration advance. The settle
    // is still fenced: the same logical page (account, profile, space, page
    // and host binding) must still own the operation. A host transfer or any
    // other identity move keeps invalidating it.
    if (m_fenceDisabled || identity.isEmpty())
        return true;
    const QVariantMap canonical = canonicalIdentity(
        identity.value(QStringLiteral("spaceId")).toString());
    if (canonical.isEmpty())
        return false;
    const QStringList stable{
        QStringLiteral("accountEpoch"), QStringLiteral("profileId"),
        QStringLiteral("spaceId"), QStringLiteral("pageGeneration"),
        QStringLiteral("hostId"), QStringLiteral("hostGeneration"),
    };
    for (const QString &key : stable)
        if (canonical.value(key) != identity.value(key))
            return false;
    return true;
}

QString FeriaBrowserOperations::beginStoreClose(const QVariantMap &identity) const
{
    if (m_store.isNull())
        return {};
    QString attempt;
    QMetaObject::invokeMethod(const_cast<QObject *>(m_store.data()),
                              "beginBrowserClose",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(QString, attempt),
                              Q_ARG(QVariantMap, identity));
    return attempt;
}

bool FeriaBrowserOperations::settleStoreClose(const QVariantMap &identity,
                                              const QString &attempt,
                                              bool accepted,
                                              const QVariantMap &descriptor) const
{
    if (m_store.isNull())
        return false;
    bool settled = false;
    QMetaObject::invokeMethod(const_cast<QObject *>(m_store.data()),
                              "settleBrowserClose",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, settled),
                              Q_ARG(QVariantMap, identity),
                              Q_ARG(QString, attempt),
                              Q_ARG(bool, accepted),
                              Q_ARG(QVariantMap, descriptor));
    return settled;
}

void FeriaBrowserOperations::retireRequestsForOperation(const QString &operationId)
{
    // Settling an operation retires its unanswered requests: a consent answer
    // can no longer authorize anything for a retired close intent.
    for (const QString &requestId : m_requestOrder) {
        QVariantMap record = m_requests.value(requestId).toMap();
        if (record.isEmpty() || record.value(QStringLiteral("answered")).toBool())
            continue;
        if (record.value(QStringLiteral("operationId")).toString() != operationId)
            continue;
        record.insert(QStringLiteral("answered"), true);
        record.insert(QStringLiteral("retired"), true);
        m_requests.insert(requestId, record);
    }
}

QVariantMap FeriaBrowserOperations::beginOperation(const QVariantMap &identity,
                                                   const QString &engineId,
                                                   const QString &kind)
{
    QVariantMap result{{QStringLiteral("admitted"), false},
                       {QStringLiteral("operationId"), QString()},
                       {QStringLiteral("reason"), QString()}};
    const bool validShape = !identity.isEmpty() && !engineId.isEmpty()
        && (engineId == QStringLiteral("qtwebengine")
            || engineId == QStringLiteral("webview2"))
        && kKinds.contains(kind);
    if (!validShape) {
        result.insert(QStringLiteral("reason"), QStringLiteral("invalid-command"));
        return result;
    }
    if (!identityCurrent(identity)) {
        result.insert(QStringLiteral("reason"), QStringLiteral("stale-identity"));
        return result;
    }
    QString operationId;
    if (kind == QStringLiteral("request-close")) {
        // The canonical close attempt IS the operation id: one pending close
        // per space, owned by SessionStore, never a second ledger here.
        operationId = beginStoreClose(identity);
        if (operationId.isEmpty()) {
            result.insert(QStringLiteral("reason"), QStringLiteral("close-already-pending"));
            return result;
        }
    } else {
        operationId = newToken(QStringLiteral("op-"));
    }
    QVariantMap record;
    record.insert(QStringLiteral("identity"), identity);
    record.insert(QStringLiteral("engineId"), engineId);
    record.insert(QStringLiteral("kind"), kind);
    record.insert(QStringLiteral("settled"), false);
    retain(m_operationOrder, m_operations, operationId, record);
    result.insert(QStringLiteral("admitted"), true);
    result.insert(QStringLiteral("operationId"), operationId);
    return result;
}

QVariantMap FeriaBrowserOperations::openRequest(const QString &operationId,
                                                const QVariantMap &identity,
                                                const QString &purpose,
                                                const QVariantMap &source)
{
    QVariantMap result{{QStringLiteral("opened"), false},
                       {QStringLiteral("requestId"), QString()},
                       {QStringLiteral("reason"), QString()}};
    if (!kPurposes.contains(purpose) || identity.isEmpty()) {
        result.insert(QStringLiteral("reason"), QStringLiteral("invalid-request"));
        return result;
    }
    if (purpose == QStringLiteral("app-close")) {
        // A consent request is bound to a LIVE matching close operation of
        // the same identity: retired or foreign operations cannot open one.
        const QVariantMap operation = m_operations.value(operationId).toMap();
        if (operationId.isEmpty() || operation.isEmpty()) {
            result.insert(QStringLiteral("reason"), QStringLiteral("unknown-operation"));
            return result;
        }
        if (operation.value(QStringLiteral("kind")).toString()
                != QStringLiteral("request-close")
                || operation.value(QStringLiteral("settled")).toBool()
                || operation.value(QStringLiteral("identity")).toMap() != identity) {
            result.insert(QStringLiteral("reason"),
                          QStringLiteral("operation-not-pending-close"));
            return result;
        }
    }
    if (!identityCurrent(identity)) {
        result.insert(QStringLiteral("reason"), QStringLiteral("stale-identity"));
        return result;
    }
    const QString requestId = newToken(QStringLiteral("req-"));
    QVariantMap record;
    record.insert(QStringLiteral("operationId"), operationId);
    record.insert(QStringLiteral("identity"), identity);
    record.insert(QStringLiteral("purpose"), purpose);
    QVariantMap provenance = source;
    // Missing provenance stays explicit; decisions that require frame/origin
    // evidence the API could not supply must inspect this, not guess.
    if (!provenance.contains(QStringLiteral("origin")))
        provenance.insert(QStringLiteral("origin"), QString());
    record.insert(QStringLiteral("source"), provenance);
    record.insert(QStringLiteral("answered"), false);
    retain(m_requestOrder, m_requests, requestId, record);
    result.insert(QStringLiteral("opened"), true);
    result.insert(QStringLiteral("requestId"), requestId);
    return result;
}

QVariantMap FeriaBrowserOperations::answerRequest(const QString &requestId,
                                                  const QVariantMap &identity)
{
    QVariantMap result{{QStringLiteral("valid"), false},
                       {QStringLiteral("reason"), QString()},
                       {QStringLiteral("purpose"), QString()},
                       {QStringLiteral("operationId"), QString()}};
    const QVariantMap record = m_requests.value(requestId).toMap();
    if (record.isEmpty()) {
        result.insert(QStringLiteral("reason"), QStringLiteral("unknown-request"));
        return result;
    }
    result.insert(QStringLiteral("purpose"), record.value(QStringLiteral("purpose")));
    result.insert(QStringLiteral("operationId"), record.value(QStringLiteral("operationId")));
    if (!m_fenceDisabled) {
        if (record.value(QStringLiteral("answered")).toBool()) {
            result.insert(QStringLiteral("reason"),
                          record.value(QStringLiteral("retired")).toBool()
                              ? QStringLiteral("request-retired")
                              : QStringLiteral("request-already-answered"));
            return result;
        }
        // Only the exact captured identity may answer; a sibling or a
        // replacement space cannot retarget this request.
        if (identity != record.value(QStringLiteral("identity"))
            || !identityCurrent(identity)) {
            result.insert(QStringLiteral("reason"), QStringLiteral("stale-identity"));
            return result;
        }
        // A request bound to an operation cannot answer once that operation
        // has retired: consent never authorizes a settled close intent.
        const QString boundOperation = record.value(QStringLiteral("operationId")).toString();
        if (!boundOperation.isEmpty()) {
            const QVariantMap operation = m_operations.value(boundOperation).toMap();
            if (operation.isEmpty()
                    || operation.value(QStringLiteral("settled")).toBool()) {
                result.insert(QStringLiteral("reason"),
                              QStringLiteral("operation-retired"));
                return result;
            }
        }
    }
    QVariantMap answered = record;
    answered.insert(QStringLiteral("answered"), true);
    m_requests.insert(requestId, answered);
    result.insert(QStringLiteral("valid"), true);
    return result;
}

QVariantMap FeriaBrowserOperations::settleOperation(const QVariantMap &identity,
                                                    const QString &operationId,
                                                    const QString &outcome,
                                                    const QString &reason,
                                                    const QString &evidence,
                                                    const QVariantMap &extra)
{
    QVariantMap result{{QStringLiteral("settled"), false},
                       {QStringLiteral("outcome"), outcome},
                       {QStringLiteral("duplicate"), false},
                       {QStringLiteral("reason"), QString()}};
    QVariantMap record = m_operations.value(operationId).toMap();
    if (record.isEmpty()) {
        result.insert(QStringLiteral("reason"), QStringLiteral("unknown-operation"));
        return result;
    }
    const QString kind = record.value(QStringLiteral("kind")).toString();
    if (record.value(QStringLiteral("settled")).toBool() && !m_fenceDisabled) {
        // Exactly one terminal outcome per admitted operation.
        result.insert(QStringLiteral("duplicate"), true);
        result.insert(QStringLiteral("outcome"),
                      record.value(QStringLiteral("outcome")));
        result.insert(QStringLiteral("reason"), QStringLiteral("already-settled"));
        return result;
    }
    if (!kOutcomes.contains(outcome)) {
        result.insert(QStringLiteral("reason"), QStringLiteral("invalid-outcome"));
        return result;
    }
    QString effectiveOutcome = outcome;
    if (!m_fenceDisabled) {
        if (identity != record.value(QStringLiteral("identity"))) {
            result.insert(QStringLiteral("reason"), QStringLiteral("identity-mismatch"));
            return result;
        }
        // A late settle from an old identity is invalidated, never applied:
        // the canonical record it captured has moved on. A navigation
        // operation's own commit is the exception — it replaces the document
        // itself, so only the stable page binding must survive.
        const bool ownCommitAllowed = kNavigationKinds.contains(kind)
            && navigationIdentityAlive(identity);
        if (!identityCurrent(identity) && !ownCommitAllowed
                && outcome != QStringLiteral("invalidated"))
            effectiveOutcome = QStringLiteral("invalidated");
        if (outcome == QStringLiteral("cancelled")
            && !kCancellationEvidence.contains(evidence)) {
            result.insert(QStringLiteral("reason"),
                          QStringLiteral("cancellation-evidence-not-accepted"));
            return result;
        }
        if (outcome == QStringLiteral("succeeded")
            && kind == QStringLiteral("request-close")
            && !kCloseSuccessEvidence.contains(evidence)) {
            result.insert(QStringLiteral("reason"),
                          QStringLiteral("close-success-evidence-not-accepted"));
            return result;
        }
        if (evidence.isEmpty() && outcome != QStringLiteral("invalidated")) {
            result.insert(QStringLiteral("reason"), QStringLiteral("evidence-required"));
            return result;
        }
    }

    if (kind == QStringLiteral("request-close")) {
        const bool accepted = effectiveOutcome == QStringLiteral("succeeded");
        if (accepted) {
            const QVariantMap descriptor = extra.value(QStringLiteral("descriptor")).toMap();
            if (!settleStoreClose(identity, operationId, true, descriptor)) {
                // The store refused the success (attempt already consumed or
                // identity moved underneath us): never claim the space closed.
                effectiveOutcome = QStringLiteral("invalidated");
                result.insert(QStringLiteral("reason"), QStringLiteral("canonical-close-refused"));
            }
        } else if (identityCurrent(identity)) {
            // Cancelled/failed/blocked/invalidated with the captured identity
            // still current: clear the canonical attempt so a fresh close is
            // admissible while the space stays open. A stale identity already
            // had its attempt retired by the store's own identity moves.
            settleStoreClose(identity, operationId, false, {});
        }
    }

    record.insert(QStringLiteral("settled"), true);
    record.insert(QStringLiteral("outcome"), effectiveOutcome);
    record.insert(QStringLiteral("reason"), reason);
    record.insert(QStringLiteral("evidence"), evidence);
    record.insert(QStringLiteral("nativeNavigationId"),
                  extra.value(QStringLiteral("nativeNavigationId")));
    m_operations.insert(operationId, record);
    // Retire unanswered requests bound to this operation; native drain
    // ownership stays with the adapter, but consent authorization ends here.
    retireRequestsForOperation(operationId);
    result.insert(QStringLiteral("settled"), true);
    result.insert(QStringLiteral("outcome"), effectiveOutcome);
    emit operationSettled(operationId, effectiveOutcome);
    return result;
}

QVariantMap FeriaBrowserOperations::operationResult(const QString &operationId) const
{
    QVariantMap record = m_operations.value(operationId).toMap();
    if (record.isEmpty() || !record.value(QStringLiteral("settled")).toBool())
        return {};
    QVariantMap result;
    result.insert(QStringLiteral("operationId"), operationId);
    result.insert(QStringLiteral("kind"), record.value(QStringLiteral("kind")));
    result.insert(QStringLiteral("engineId"), record.value(QStringLiteral("engineId")));
    result.insert(QStringLiteral("identity"), record.value(QStringLiteral("identity")));
    result.insert(QStringLiteral("outcome"), record.value(QStringLiteral("outcome")));
    result.insert(QStringLiteral("reason"), record.value(QStringLiteral("reason")));
    result.insert(QStringLiteral("evidence"), record.value(QStringLiteral("evidence")));
    result.insert(QStringLiteral("nativeNavigationId"),
                  record.value(QStringLiteral("nativeNavigationId")));
    return result;
}

QVariantMap FeriaBrowserOperations::pendingOperation(const QString &spaceId) const
{
    // Newest live navigation operation for a space, if any: adapters use this
    // to correlate a native attempt with the work that admitted it.
    for (int i = m_operationOrder.size() - 1; i >= 0; --i) {
        const QVariantMap record = m_operations.value(m_operationOrder.at(i)).toMap();
        if (record.isEmpty() || !isLiveOperation(record))
            continue;
        if (!kNavigationKinds.contains(record.value(QStringLiteral("kind")).toString()))
            continue;
        if (record.value(QStringLiteral("identity")).toMap()
                .value(QStringLiteral("spaceId")).toString() != spaceId)
            continue;
        QVariantMap result;
        result.insert(QStringLiteral("operationId"), m_operationOrder.at(i));
        result.insert(QStringLiteral("kind"), record.value(QStringLiteral("kind")));
        return result;
    }
    return {};
}

QVariantMap FeriaBrowserOperations::requestState(const QString &requestId) const
{
    QVariantMap result{{QStringLiteral("present"), false}};
    const QVariantMap record = m_requests.value(requestId).toMap();
    if (record.isEmpty())
        return result;
    result.insert(QStringLiteral("present"), true);
    result.insert(QStringLiteral("answered"), record.value(QStringLiteral("answered")));
    result.insert(QStringLiteral("retired"), record.value(QStringLiteral("retired")));
    result.insert(QStringLiteral("purpose"), record.value(QStringLiteral("purpose")));
    result.insert(QStringLiteral("operationId"), record.value(QStringLiteral("operationId")));
    result.insert(QStringLiteral("identity"), record.value(QStringLiteral("identity")));
    result.insert(QStringLiteral("source"), record.value(QStringLiteral("source")));
    return result;
}
