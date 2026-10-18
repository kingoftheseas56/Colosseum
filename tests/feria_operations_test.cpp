#include "SessionStore.h"
#include "feria/FeriaBrowserOperations.h"

#include <QCoreApplication>
#include <QtTest>

// Focused native tests for the shared operation/request seam against the real
// SessionStore: no engine, no doubles. Mirrors the review-focus fencing cases
// of the GLM slice-1 brief (duplicate results, stale identities across every
// dimension, sibling retargeting, one-use answers, Stay vs timeout evidence).
class FeriaOperationsTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { m_store.setBrowserProfile(m_tempDir.path() + QStringLiteral("/p") + QString::number(++m_serial)); }
    void testNavigationOwnCommitIsNotInvalidation();
    void testNavigationInvalidatedByHostMove();
    void testPendingCloseSurvivesRetention();
    void testRetiredOperationCannotAnswerConsent();
    void testRequestForForeignOperationRefused();

    void testCloseAdmissionUsesCanonicalAttempt();
    void testExactlyOnceSettlement();
    void testCloseSuccessRequiresEvidenceAndDescriptor();
    void testStaleIdentityCannotSettleOrAnswer();
    void testRequestOneUseAndSiblingRetarget();
    void testStayEvidenceVersusTimeout();
    void testNavigationOperationLifecycle();
    void testInvalidCommandShapes();

private:
    QString openSpace(const QString &url = QStringLiteral("https://example.com/a"));
    bool spaceOpen(const QString &id);
    QTemporaryDir m_tempDir;
    int m_serial = 0;
    SessionStore m_store{this};
};

QString FeriaOperationsTest::openSpace(const QString &url)
{
    const QString id = m_store.openBrowser(QVariantMap{
        {QStringLiteral("providerId"), QStringLiteral("fixture")},
        {QStringLiteral("providerName"), QStringLiteral("Fixture")},
        {QStringLiteral("url"), url},
        {QStringLiteral("startUrl"), url}}, true, false, false);
    return id;
}

bool FeriaOperationsTest::spaceOpen(const QString &id)
{
    return !m_store.get(id).isEmpty();
}

void FeriaOperationsTest::testCloseAdmissionUsesCanonicalAttempt()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QVariantMap admitted = ops.beginOperation(identity, QStringLiteral("webview2"),
                                                    QStringLiteral("request-close"));
    QVERIFY2(admitted.value(QStringLiteral("admitted")).toBool(), "close not admitted");
    const QString operationId = admitted.value(QStringLiteral("operationId")).toString();
    QVERIFY(!operationId.isEmpty());
    // The seam has no second ledger: the operation id IS the canonical attempt.
    QCOMPARE(m_store.get(id).value(QStringLiteral("browserCloseAttempt")).toString(),
             operationId);
    // A second close attempt for the same space is refused by the store.
    const QVariantMap second = ops.beginOperation(identity, QStringLiteral("webview2"),
                                                  QStringLiteral("request-close"));
    QVERIFY(!second.value(QStringLiteral("admitted")).toBool());
    QCOMPARE(second.value(QStringLiteral("reason")).toString(),
             QStringLiteral("close-already-pending"));
}

void FeriaOperationsTest::testExactlyOnceSettlement()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identity, QStringLiteral("qtwebengine"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();
    QVariantMap settled = ops.settleOperation(identity, op, QStringLiteral("cancelled"),
                                              QStringLiteral("user-stay"),
                                              QStringLiteral("explicit-stay-deferral-completed"), {});
    QVERIFY(settled.value(QStringLiteral("settled")).toBool());
    QCOMPARE(settled.value(QStringLiteral("outcome")).toString(), QStringLiteral("cancelled"));
    // Stay cleared the canonical attempt: an immediate retry is admissible.
    QVERIFY(m_store.get(id).value(QStringLiteral("browserCloseAttempt")).toString().isEmpty());
    QVERIFY(ops.beginOperation(m_store.browserIdentity(id), QStringLiteral("qtwebengine"),
                               QStringLiteral("request-close"))
                .value(QStringLiteral("admitted")).toBool());

    // A duplicate terminal result for the first attempt settles nothing more.
    QVariantMap duplicate = ops.settleOperation(identity, op, QStringLiteral("succeeded"),
                                                QStringLiteral("duplicate"),
                                                QStringLiteral("correlated-navigation-departure"),
                                                QVariantMap{{QStringLiteral("descriptor"),
                                    QVariantMap{{QStringLiteral("providerId"), QStringLiteral("fixture")},
                                                {QStringLiteral("url"), QStringLiteral("https://example.com/a")}}}});
    QVERIFY(!duplicate.value(QStringLiteral("settled")).toBool());
    QVERIFY(duplicate.value(QStringLiteral("duplicate")).toBool());
    QCOMPARE(duplicate.value(QStringLiteral("outcome")).toString(), QStringLiteral("cancelled"));
    QVERIFY(spaceOpen(id));
    QCOMPARE(ops.operationResult(op).value(QStringLiteral("outcome")).toString(),
             QStringLiteral("cancelled"));
}

void FeriaOperationsTest::testCloseSuccessRequiresEvidenceAndDescriptor()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identity, QStringLiteral("webview2"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();
    // A timer or dispatch result is not departure evidence.
    QVariantMap timer = ops.settleOperation(identity, op, QStringLiteral("succeeded"),
                                            QStringLiteral("deadline"),
                                            QStringLiteral("timer-elapsed"), {});
    QVERIFY(!timer.value(QStringLiteral("settled")).toBool());
    QCOMPARE(timer.value(QStringLiteral("reason")).toString(),
             QStringLiteral("close-success-evidence-not-accepted"));
    QVERIFY(spaceOpen(id));

    const QVariantMap descriptor{{QStringLiteral("providerId"), QStringLiteral("fixture")},
                                 {QStringLiteral("url"), QStringLiteral("https://example.com/a")},
                                 {QStringLiteral("title"), QStringLiteral("Fixture page")}};
    QVariantMap settled = ops.settleOperation(identity, op, QStringLiteral("succeeded"),
                                              QStringLiteral("leave-departed"),
                                              QStringLiteral("correlated-navigation-departure"),
                                              QVariantMap{{QStringLiteral("descriptor"), descriptor}});
    QVERIFY(settled.value(QStringLiteral("settled")).toBool());
    QCOMPARE(settled.value(QStringLiteral("outcome")).toString(), QStringLiteral("succeeded"));
    QVERIFY(!spaceOpen(id));
    const QVariantList records = m_store.closedBrowserRecords(QStringLiteral("fixture"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().toMap().value(QStringLiteral("descriptor")).toMap()
                 .value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.com/a"));
}

void FeriaOperationsTest::testStaleIdentityCannotSettleOrAnswer()
{
    const QString a = openSpace(QStringLiteral("https://example.com/a"));
    const QString b = openSpace(QStringLiteral("https://example.com/b"));
    const QVariantMap identityA = m_store.browserIdentity(a);
    const QVariantMap identityB = m_store.browserIdentity(b);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identityA, QStringLiteral("webview2"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();

    // A sibling identity cannot settle A's operation.
    QVariantMap sibling = ops.settleOperation(identityB, op, QStringLiteral("cancelled"),
                                              QStringLiteral("sibling"),
                                              QStringLiteral("dialog-rejected"), {});
    QVERIFY(!sibling.value(QStringLiteral("settled")).toBool());
    QCOMPARE(sibling.value(QStringLiteral("reason")).toString(), QStringLiteral("identity-mismatch"));

    // A request captured for A cannot be answered with B's identity.
    const QString request = ops.openRequest(op, identityA, QStringLiteral("app-close"),
                                            QVariantMap{{QStringLiteral("origin"),
                                                         QStringLiteral("https://example.com")}})
                                .value(QStringLiteral("requestId")).toString();
    QVERIFY(!request.isEmpty());
    QVERIFY(!ops.answerRequest(request, identityB).value(QStringLiteral("valid")).toBool());

    // documentGeneration replacement invalidates the captured close.
    m_store.advanceBrowserDocument(a);
    QVERIFY(!ops.identityCurrent(identityA));
    QVariantMap stale = ops.settleOperation(identityA, op, QStringLiteral("succeeded"),
                                            QStringLiteral("late-leave"),
                                            QStringLiteral("correlated-navigation-departure"),
                                            QVariantMap{{QStringLiteral("descriptor"),
                                        QVariantMap{{QStringLiteral("providerId"), QStringLiteral("fixture")},
                                                    {QStringLiteral("url"), QStringLiteral("https://example.com/a")}}}});
    QVERIFY(stale.value(QStringLiteral("settled")).toBool());
    QCOMPARE(stale.value(QStringLiteral("outcome")).toString(), QStringLiteral("invalidated"));
    QVERIFY(spaceOpen(a));
    QVERIFY(m_store.get(a).value(QStringLiteral("browserCloseAttempt")).toString().isEmpty());

    // hostId/hostGeneration replacement invalidates too: the replacement
    // identity itself is refused (it is not the captured one), while the
    // captured identity settles as invalidated without canonical effect.
    const QVariantMap identityBeforeHost = m_store.browserIdentity(a);
    const QString op2 = ops.beginOperation(identityBeforeHost, QStringLiteral("webview2"),
                                           QStringLiteral("request-close"))
                            .value(QStringLiteral("operationId")).toString();
    m_store.setBrowserHost(m_store.browserIdentity(a), QStringLiteral("app-fullsize:1"));
    QVariantMap replacement = ops.settleOperation(m_store.browserIdentity(a), op2,
                                                  QStringLiteral("cancelled"), QStringLiteral("replacement"),
                                                  QStringLiteral("dialog-rejected"), {});
    QVERIFY(!replacement.value(QStringLiteral("settled")).toBool());
    QCOMPARE(replacement.value(QStringLiteral("reason")).toString(), QStringLiteral("identity-mismatch"));
    QVariantMap moved = ops.settleOperation(identityBeforeHost, op2,
                                            QStringLiteral("cancelled"), QStringLiteral("moved"),
                                            QStringLiteral("dialog-rejected"), {});
    QVERIFY(moved.value(QStringLiteral("settled")).toBool());
    QCOMPARE(moved.value(QStringLiteral("outcome")).toString(), QStringLiteral("invalidated"));

    // accountEpoch replacement invalidates old-era identities.
    const QString op3 = ops.beginOperation(m_store.browserIdentity(b), QStringLiteral("qtwebengine"),
                                           QStringLiteral("request-close"))
                            .value(QStringLiteral("operationId")).toString();
    const QVariantMap identityBEra = m_store.browserIdentity(b);
    m_store.setBrowserProfile(m_tempDir.path() + QStringLiteral("/next-era"));
    QVERIFY(!ops.identityCurrent(identityBEra));
    QVariantMap era = ops.settleOperation(identityBEra, op3, QStringLiteral("cancelled"),
                                          QStringLiteral("account-switch"),
                                          QStringLiteral("dialog-rejected"), {});
    QCOMPARE(era.value(QStringLiteral("outcome")).toString(), QStringLiteral("invalidated"));
}

void FeriaOperationsTest::testRequestOneUseAndSiblingRetarget()
{
    const QString a = openSpace(QStringLiteral("https://example.com/a"));
    const QString b = openSpace(QStringLiteral("https://example.com/b"));
    const QVariantMap identityA = m_store.browserIdentity(a);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identityA, QStringLiteral("webview2"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();
    const QString request = ops.openRequest(op, identityA, QStringLiteral("app-close"), {})
                                .value(QStringLiteral("requestId")).toString();
    QVERIFY(!request.isEmpty());
    QCOMPARE(ops.requestState(request).value(QStringLiteral("source")).toMap()
                 .value(QStringLiteral("origin")).toString(), QString());
    QVERIFY(ops.answerRequest(request, identityA).value(QStringLiteral("valid")).toBool());
    // One-use: the second answer is refused even with the right identity.
    QVERIFY(!ops.answerRequest(request, identityA).value(QStringLiteral("valid")).toBool());
    QCOMPARE(ops.answerRequest(request, identityA).value(QStringLiteral("reason")).toString(),
             QStringLiteral("request-already-answered"));

    // External unload consent exists without an app-close operation and never
    // deletes a space by itself.
    const QString external = ops.openRequest(QString(), identityA,
                                             QStringLiteral("external-unload"),
                                             QVariantMap{{QStringLiteral("origin"),
                                                          QStringLiteral("https://example.com")}})
                                 .value(QStringLiteral("requestId")).toString();
    QVERIFY(!external.isEmpty());
    QVERIFY(ops.answerRequest(external, identityA).value(QStringLiteral("valid")).toBool());
    QVERIFY(spaceOpen(a));
    QVERIFY(spaceOpen(b));

    // An app-close request cannot be opened for an unknown operation.
    QVERIFY(!ops.openRequest(QStringLiteral("op-nonexistent"), identityA,
                             QStringLiteral("app-close"), {})
                 .value(QStringLiteral("opened")).toBool());
}

void FeriaOperationsTest::testStayEvidenceVersusTimeout()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    // Navigation operations accept their own evidence vocabulary.
    const QString nav = ops.beginOperation(identity, QStringLiteral("webview2"),
                                           QStringLiteral("navigate"))
                            .value(QStringLiteral("operationId")).toString();
    QVERIFY(!nav.isEmpty());
    // Timeout must not be recorded as cancellation for close operations.
    const QString op = ops.beginOperation(identity, QStringLiteral("qtwebengine"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();
    QVERIFY(!ops.settleOperation(identity, op, QStringLiteral("cancelled"),
                                 QStringLiteral("deadline"),
                                 QStringLiteral("timeout"), {})
                 .value(QStringLiteral("settled")).toBool());
    QVERIFY(!ops.settleOperation(identity, op, QStringLiteral("cancelled"),
                                 QStringLiteral("unknown-ack"),
                                 QStringLiteral("elapsed-time"), {})
                 .value(QStringLiteral("settled")).toBool());
    // The Qt rejection barrier is accepted cancellation evidence.
    QVERIFY(ops.settleOperation(identity, op, QStringLiteral("cancelled"),
                                QStringLiteral("renderer-rejected"),
                                QStringLiteral("window-close-rejected"), {})
                .value(QStringLiteral("settled")).toBool());
}

void FeriaOperationsTest::testNavigationOperationLifecycle()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identity, QStringLiteral("webview2"),
                                          QStringLiteral("back"))
                           .value(QStringLiteral("operationId")).toString();
    QVERIFY(ops.settleOperation(identity, op, QStringLiteral("succeeded"),
                                QStringLiteral("native-history"),
                                QStringLiteral("navigation-completed"),
                                QVariantMap{{QStringLiteral("nativeNavigationId"),
                                             QVariant::fromValue<qulonglong>(42)}})
                .value(QStringLiteral("settled")).toBool());
    const QVariantMap result = ops.operationResult(op);
    QCOMPARE(result.value(QStringLiteral("outcome")).toString(), QStringLiteral("succeeded"));
    QCOMPARE(result.value(QStringLiteral("nativeNavigationId")).toULongLong(), quint64(42));
    QCOMPARE(result.value(QStringLiteral("engineId")).toString(), QStringLiteral("webview2"));}

void FeriaOperationsTest::testInvalidCommandShapes()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    QVERIFY(!ops.beginOperation({}, QStringLiteral("webview2"),
                                QStringLiteral("navigate"))
                 .value(QStringLiteral("admitted")).toBool());
    QVERIFY(!ops.beginOperation(identity, QString(),
                                QStringLiteral("navigate"))
                 .value(QStringLiteral("admitted")).toBool());
    QVERIFY(!ops.beginOperation(identity, QStringLiteral("gecko"),
                                QStringLiteral("navigate"))
                 .value(QStringLiteral("admitted")).toBool());
    QVERIFY(!ops.beginOperation(identity, QStringLiteral("webview2"),
                                QStringLiteral("adopt"))
                 .value(QStringLiteral("admitted")).toBool());
    QVERIFY(!ops.settleOperation(identity, QStringLiteral("op-unknown"),
                                 QStringLiteral("succeeded"), QString(),
                                 QStringLiteral("navigation-completed"), {})
                 .value(QStringLiteral("settled")).toBool());
}

void FeriaOperationsTest::testNavigationOwnCommitIsNotInvalidation()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identity, QStringLiteral("webview2"),
                                          QStringLiteral("navigate"))
                           .value(QStringLiteral("operationId")).toString();
    // The navigation's own commit advances the document generation; its
    // terminal must settle as requested, not be invalidated by itself.
    m_store.advanceBrowserDocument(id);
    QVariantMap settled = ops.settleOperation(identity, op, QStringLiteral("succeeded"),
                                              QStringLiteral("native-history"),
                                              QStringLiteral("navigation-completed"),
                                              QVariantMap{{QStringLiteral("nativeNavigationId"),
                                                           QVariant::fromValue<quint64>(7)}});
    QVERIFY(settled.value(QStringLiteral("settled")).toBool());
    QCOMPARE(settled.value(QStringLiteral("outcome")).toString(), QStringLiteral("succeeded"));
    QCOMPARE(ops.operationResult(op).value(QStringLiteral("nativeNavigationId")).toULongLong(),
             quint64(7));
}

void FeriaOperationsTest::testNavigationInvalidatedByHostMove()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identity, QStringLiteral("qtwebengine"),
                                          QStringLiteral("back"))
                           .value(QStringLiteral("operationId")).toString();
    // A host transfer during a pending navigation is not the operation's own
    // commit: the terminal is invalidated.
    m_store.setBrowserHost(m_store.browserIdentity(id), QStringLiteral("app-fullsize:9"));
    QVariantMap settled = ops.settleOperation(identity, op, QStringLiteral("succeeded"),
                                              QStringLiteral("native-history"),
                                              QStringLiteral("navigation-completed"), {});
    QVERIFY(settled.value(QStringLiteral("settled")).toBool());
    QCOMPARE(settled.value(QStringLiteral("outcome")).toString(), QStringLiteral("invalidated"));
}

void FeriaOperationsTest::testPendingCloseSurvivesRetention()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString close = ops.beginOperation(identity, QStringLiteral("webview2"),
                                             QStringLiteral("request-close"))
                              .value(QStringLiteral("operationId")).toString();
    // Completed-history retention must never evict live ownership: after 256
    // settled navigation operations the pending close still settles.
    for (int i = 0; i < 256; ++i) {
        const QString nav = ops.beginOperation(identity, QStringLiteral("webview2"),
                                               QStringLiteral("reload"))
                                .value(QStringLiteral("operationId")).toString();
        QVERIFY(!nav.isEmpty());
        QVERIFY(ops.settleOperation(identity, nav, QStringLiteral("failed"),
                                    QStringLiteral("retention-check"),
                                    QStringLiteral("receiving-check"), {})
                    .value(QStringLiteral("settled")).toBool());
    }
    QVariantMap settled = ops.settleOperation(identity, close, QStringLiteral("blocked"),
                                              QStringLiteral("retention-check"),
                                              QStringLiteral("receiving-check"), {});
    QVERIFY(settled.value(QStringLiteral("settled")).toBool());
    QCOMPARE(settled.value(QStringLiteral("outcome")).toString(), QStringLiteral("blocked"));
    // The seam's settlement already cleared the canonical attempt.
    QVERIFY(m_store.get(id).value(QStringLiteral("browserCloseAttempt")).toString().isEmpty());
}

void FeriaOperationsTest::testRetiredOperationCannotAnswerConsent()
{
    const QString id = openSpace();
    const QVariantMap identity = m_store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString op = ops.beginOperation(identity, QStringLiteral("webview2"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();
    const QString request = ops.openRequest(op, identity, QStringLiteral("app-close"), {})
                                .value(QStringLiteral("requestId")).toString();
    QVERIFY(!request.isEmpty());
    QVERIFY(ops.settleOperation(identity, op, QStringLiteral("invalidated"),
                                QStringLiteral("retired"), QStringLiteral(""), {})
                .value(QStringLiteral("settled")).toBool());
    QVariantMap answer = ops.answerRequest(request, identity);
    QVERIFY(!answer.value(QStringLiteral("valid")).toBool());
    QCOMPARE(answer.value(QStringLiteral("reason")).toString(),
             QStringLiteral("request-retired"));
    // A retired operation cannot open a fresh request either.
    QVERIFY(!ops.openRequest(op, identity, QStringLiteral("app-close"), {})
                 .value(QStringLiteral("opened")).toBool());
}

void FeriaOperationsTest::testRequestForForeignOperationRefused()
{
    const QString a = openSpace(QStringLiteral("https://example.com/a"));
    const QString b = openSpace(QStringLiteral("https://example.com/b"));
    const QVariantMap identityA = m_store.browserIdentity(a);
    const QVariantMap identityB = m_store.browserIdentity(b);
    FeriaBrowserOperations ops;
    ops.setStore(&m_store);
    const QString opA = ops.beginOperation(identityA, QStringLiteral("webview2"),
                                           QStringLiteral("request-close"))
                            .value(QStringLiteral("operationId")).toString();
    QVERIFY(!opA.isEmpty());
    // B's identity cannot open a consent on A's operation.
    QVERIFY(!ops.openRequest(opA, identityB, QStringLiteral("app-close"), {})
                 .value(QStringLiteral("opened")).toBool());
    // A non-close operation cannot carry an app-close request.
    const QString nav = ops.beginOperation(identityA, QStringLiteral("webview2"),
                                           QStringLiteral("navigate"))
                            .value(QStringLiteral("operationId")).toString();
    QVERIFY(!ops.openRequest(nav, identityA, QStringLiteral("app-close"), {})
                 .value(QStringLiteral("opened")).toBool());
}

// Negative control: with FERIA_UNSAFE_OPERATION_FENCE set, the seam's own
// durable fences are off. The canonical store still validates identities, so
// this control targets the two rules only the seam enforces: a timeout must
// never be recorded as cancellation, and a settled operation must never be
// re-settled. With the fence off both assertions below must FAIL (exit 1 with
// the specific message), proving they test the seam fence, not store luck.
static int runNegativeControl()
{
    SessionStore store;
    store.setBrowserProfile(QCoreApplication::applicationDirPath() + QStringLiteral("/neg-profile"));
    const QString id = store.openBrowser(QVariantMap{
        {QStringLiteral("providerId"), QStringLiteral("fixture")},
        {QStringLiteral("url"), QStringLiteral("https://example.com/a")}}, true, false, false);
    const QVariantMap identity = store.browserIdentity(id);
    FeriaBrowserOperations ops;
    ops.setStore(&store);
    const QString op = ops.beginOperation(identity, QStringLiteral("webview2"),
                                          QStringLiteral("request-close"))
                           .value(QStringLiteral("operationId")).toString();
    ops.settleOperation(identity, op, QStringLiteral("cancelled"),
                        QStringLiteral("deadline"), QStringLiteral("timeout"), {});
    if (ops.operationResult(op).value(QStringLiteral("outcome")).toString()
            != QStringLiteral("cancelled")) {
        fprintf(stderr, "NEGATIVE-CONTROL-FAILED-EARLY: timeout was not recorded as cancellation; fence appears active\n");
        return 1;
    }
    fprintf(stderr, "NEGATIVE-CONTROL-FAILED-AS-EXPECTED: without the seam fence a timeout was accepted as explicit cancellation evidence\n");
    return 1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool negative = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--negative-control") == 0)
            negative = true;
    if (negative)
        return runNegativeControl();
    FeriaOperationsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "feria_operations_test.moc"
