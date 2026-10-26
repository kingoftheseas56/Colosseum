#include "SessionStore.h"
#include "feria/FeriaBrowserOperations.h"
#include "feria/FeriaBrowserPolicy.h"
#include "feria/FeriaBrowserRuntime.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

// Native contract tests for FeriaBrowserRuntime against the real SessionStore,
// FeriaBrowserOperations and FeriaBrowserPolicy. Engine views are plain
// QObjects here: these cases prove the fencing rules, not QtWebEngine. The
// real-engine cases live in tests/feria_runtime_probe.

class EngineAdapter final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    bool accept = true;
    int shutdowns = 0;
    QVariantList commands;
    Q_INVOKABLE QVariant runBrowserCommand(const QVariant &command)
    {
        commands.append(command);
        return accept;
    }
    Q_INVOKABLE void shutdownEngines() { ++shutdowns; }
};

class FeriaRuntimeTest final : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void testIdentitySurvivesNavigationAndAllowsDuplicateUrls();
    void testLifecyclePhasesFollowEngineAndCanonicalClose();
    void testWorkIsBoundToDocumentAndRedeemsOnce();
    void testRendererLossStartsNewEngineGeneration();
    void testAccountSwitchRejectsOldWorkAndCommands();
    void testReplacementInstanceFencesPreviousView();
    void testViewIsNeverReboundAndActivityHasOneView();
    void testCommandsRequireCurrentLeaseAndPhase();
    void testNavigateIsValidatedBeforeDispatch();
    void testCloseWithoutEngineUsesTheCloseLedger();
    void testOpenIsValidatedAndKeepsOpenOrSwitch();
    void testViewsRetireBeforeProfiles();
    void testShutdownRefusesWorkAndRunsAdapterOnce();
    void testAdmissionIsRefusedOffTheOwnerThread();
    void testDisabledFenceLetsStaleWorkThrough();

private:
    QString openSpace(const QString &url = QStringLiteral("https://example.com/a"),
                      bool fresh = true, bool privateBrowsing = false);
    QObject *makeView(QObject *profile = nullptr);
    FeriaBrowserRuntime *runtime() const { return m_policy->runtime(); }

    QTemporaryDir m_tempDir;
    int m_serial = 0;
    SessionStore *m_store = nullptr;
    FeriaBrowserPolicy *m_policy = nullptr;
    EngineAdapter *m_adapter = nullptr;
};

void FeriaRuntimeTest::init()
{
    m_store = new SessionStore(this);
    m_policy = new FeriaBrowserPolicy(this);
    m_adapter = new EngineAdapter(this);
    m_store->setBrowserProfile(m_tempDir.path() + QStringLiteral("/p") + QString::number(++m_serial));
    QVERIFY(runtime()->attach(m_adapter, m_store));
}

void FeriaRuntimeTest::cleanup()
{
    delete m_policy; m_policy = nullptr;
    delete m_adapter; m_adapter = nullptr;
    delete m_store; m_store = nullptr;
}

QString FeriaRuntimeTest::openSpace(const QString &url, bool fresh, bool privateBrowsing)
{
    return m_store->openBrowser(QVariantMap{
        {QStringLiteral("providerId"), QStringLiteral("fixture")},
        {QStringLiteral("providerName"), QStringLiteral("Fixture")},
        {QStringLiteral("url"), url}, {QStringLiteral("startUrl"), url},
        {QStringLiteral("privateBrowsing"), privateBrowsing}}, fresh, false, false);
}

QObject *FeriaRuntimeTest::makeView(QObject *profile)
{
    auto *view = new QObject(this);
    view->setProperty("profile", QVariant::fromValue(profile));
    return view;
}

void FeriaRuntimeTest::testIdentitySurvivesNavigationAndAllowsDuplicateUrls()
{
    const QString first = openSpace();
    const QString second = openSpace(); // explicit New, same URL
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    QVERIFY(first != second);
    const FeriaBrowser::PageKey before = FeriaBrowser::PageKey::fromIdentity(m_store->browserIdentity(first));
    QVERIFY(before.isValid());
    const QVariantMap lease = runtime()->lease(first);

    m_store->updateBrowser(first, QStringLiteral("https://example.com/redirected"), QStringLiteral("New title"));
    m_store->advanceBrowserDocument(first);

    QCOMPARE(m_store->get(first).value(QStringLiteral("id")).toString(), first);
    QVERIFY(FeriaBrowser::PageKey::fromIdentity(m_store->browserIdentity(first)) == before);
    QVERIFY(runtime()->leaseCurrent(lease)); // a lease survives navigation
    // The sibling at the same original URL is a different page allocation.
    QVERIFY(FeriaBrowser::PageKey::fromIdentity(m_store->browserIdentity(second)) != before);
    QCOMPARE(m_store->browsers().size(), 2);
}

void FeriaRuntimeTest::testLifecyclePhasesFollowEngineAndCanonicalClose()
{
    QSignalSpy phases(runtime(), &FeriaBrowserRuntime::phaseChanged);
    const QString id = openSpace();
    QCOMPARE(runtime()->phase(id), QStringLiteral("absent"));
    QVERIFY(!runtime()->beginInstance(QStringLiteral("browser-unknown")));
    QVERIFY(runtime()->beginInstance(id));
    QCOMPARE(runtime()->phase(id), QStringLiteral("creating"));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    QCOMPARE(runtime()->phase(id), QStringLiteral("creating")); // an object is not readiness
    runtime()->markReady(view);
    QCOMPARE(runtime()->phase(id), QStringLiteral("ready"));

    // A pending close is read from SessionStore's own attempt, and its
    // cancellation returns the instance to ready.
    FeriaBrowserOperations *ledger = m_store->browserOperations();
    const QVariantMap identity = m_store->browserIdentity(id);
    const QVariantMap admitted = ledger->beginOperation(identity, QStringLiteral("qtwebengine"),
                                                        QStringLiteral("request-close"));
    QVERIFY(admitted.value(QStringLiteral("admitted")).toBool());
    QCOMPARE(runtime()->phase(id), QStringLiteral("close-requested"));
    ledger->settleOperation(identity, admitted.value(QStringLiteral("operationId")).toString(),
        QStringLiteral("cancelled"), QStringLiteral("stay"), QStringLiteral("dialog-rejected"), {});
    QCOMPARE(runtime()->phase(id), QStringLiteral("ready"));

    m_store->close(id); // canonical record gone -> the instance retires
    QCOMPARE(runtime()->phase(id), QStringLiteral("retiring"));
    QCOMPARE(runtime()->instance(id).value(QStringLiteral("reason")).toString(), QStringLiteral("record-closed"));
    delete view; // destruction is the only way out of retiring
    QCOMPARE(runtime()->phase(id), QStringLiteral("absent"));

    QStringList seen;
    for (const auto &row : phases) seen.append(row.at(1).toString());
    QCOMPARE(seen, (QStringList{QStringLiteral("creating"), QStringLiteral("ready"),
        QStringLiteral("close-requested"), QStringLiteral("ready"),
        QStringLiteral("retiring"), QStringLiteral("absent")}));
}

void FeriaRuntimeTest::testWorkIsBoundToDocumentAndRedeemsOnce()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    runtime()->markReady(view);

    const qint64 script = runtime()->admitWork(view, QStringLiteral("script"));
    QVERIFY(script > 0);
    QCOMPARE(runtime()->pendingWork(view), 1);
    QVERIFY(runtime()->workValid(script));
    QVERIFY(runtime()->redeemWork(script));
    QVERIFY(!runtime()->redeemWork(script)); // exactly once
    QCOMPARE(runtime()->pendingWork(view), 0);

    // A callback that returns after the document was replaced stays
    // outstanding until it physically returns, but can no longer act.
    const qint64 stale = runtime()->admitWork(view, QStringLiteral("script"));
    const qint64 prompt = runtime()->admitWork(view, QStringLiteral("request"));
    const qint64 adoption = runtime()->admitWork(view, QStringLiteral("adoption"));
    runtime()->documentChanged(view);
    QCOMPARE(runtime()->pendingWork(view), 3);
    QVERIFY(!runtime()->drained(view));
    QVERIFY(!runtime()->redeemWork(stale));
    QVERIFY(!runtime()->redeemWork(prompt));
    QVERIFY(runtime()->redeemWork(adoption)); // page-scoped: its first document is expected
    QVERIFY(runtime()->drained(view));
    QCOMPARE(runtime()->admitWork(view, QStringLiteral("download")), 0); // not a view kind
    QCOMPARE(runtime()->admitWork(makeView(), QStringLiteral("script")), 0); // unregistered view

    // Retirement keeps dispatched scripts outstanding (the engine still owes
    // their callbacks) and drops prompts that can no longer be answered.
    const qint64 draining = runtime()->admitWork(view, QStringLiteral("script"));
    const qint64 unanswered = runtime()->admitWork(view, QStringLiteral("request"));
    runtime()->beginRetirement(view);
    QCOMPARE(runtime()->phase(id), QStringLiteral("retiring"));
    QCOMPARE(runtime()->pendingWork(view), 1);
    QVERIFY(!runtime()->workValid(unanswered));
    QCOMPARE(runtime()->admitWork(view, QStringLiteral("script")), 0);
    QVERIFY(!runtime()->redeemWork(draining)); // returned, but must not act
    QVERIFY(runtime()->drained(view));
}

void FeriaRuntimeTest::testRendererLossStartsNewEngineGeneration()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    runtime()->markReady(view);
    const quint64 generation = runtime()->instance(id).value(QStringLiteral("engineGeneration")).toULongLong();
    const QVariantMap identity = m_store->browserIdentity(id);
    const qint64 late = runtime()->admitWork(view, QStringLiteral("script"));

    runtime()->rendererLost(view);
    QCOMPARE(runtime()->phase(id), QStringLiteral("failed"));
    QCOMPARE(runtime()->instance(id).value(QStringLiteral("reason")).toString(), QStringLiteral("renderer-lost"));
    QCOMPARE(runtime()->pendingWork(view), 0); // a dead renderer owes nothing
    QVERIFY(!runtime()->redeemWork(late));
    QVERIFY(runtime()->instance(id).value(QStringLiteral("engineGeneration")).toULongLong() > generation);
    // The activity itself is untouched: same record, same page key.
    QCOMPARE(m_store->browserIdentity(id), identity);

    // Only back/forward/stop need a live document; reload is the recovery.
    QVERIFY(!runtime()->command(id, QStringLiteral("back")).value(QStringLiteral("accepted")).toBool());
    QVERIFY(runtime()->command(id, QStringLiteral("reload")).value(QStringLiteral("accepted")).toBool());
    runtime()->markReady(view); // the adapter observed the new document start
    QCOMPARE(runtime()->phase(id), QStringLiteral("ready"));
    const qint64 fresh = runtime()->admitWork(view, QStringLiteral("script"));
    QVERIFY(fresh > 0 && runtime()->redeemWork(fresh));
}

void FeriaRuntimeTest::testAccountSwitchRejectsOldWorkAndCommands()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    runtime()->markReady(view);
    const QVariantMap lease = runtime()->lease(id);
    const qint64 script = runtime()->admitWork(view, QStringLiteral("script"));
    const qint64 adoption = runtime()->admitWork(view, QStringLiteral("adoption"));
    QVERIFY(script > 0 && adoption > 0);

    m_store->setBrowserProfile(m_tempDir.path() + QStringLiteral("/other-account"));

    QCOMPARE(runtime()->phase(id), QStringLiteral("retiring"));
    QVERIFY(!runtime()->workValid(script));
    QVERIFY(!runtime()->redeemWork(script));
    QVERIFY(!runtime()->redeemWork(adoption));
    QCOMPARE(runtime()->admitWork(view, QStringLiteral("script")), 0); // old owner admits nothing
    QVERIFY(!runtime()->leaseCurrent(lease));
    m_adapter->commands.clear();
    for (const QString &name : {QStringLiteral("navigate"), QStringLiteral("back"), QStringLiteral("reload"),
                                QStringLiteral("stop"), QStringLiteral("close"), QStringLiteral("activate")}) {
        const QVariantMap result = runtime()->execute(lease, name,
            {{QStringLiteral("url"), QStringLiteral("https://example.com/b")}});
        QVERIFY2(!result.value(QStringLiteral("accepted")).toBool(), qPrintable(name));
        QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("unknown-space"));
    }
    QVERIFY(m_adapter->commands.isEmpty()); // nothing reached the engine

    // The new account works immediately and its lease is its own.
    const QString next = openSpace();
    QVERIFY(!next.isEmpty());
    QVERIFY(runtime()->leaseCurrent(runtime()->lease(next)));
    // A forged lease carrying the new activity id but the old account epoch
    // is stale, not merely unknown.
    QVariantMap forged = runtime()->lease(next);
    forged.insert(QStringLiteral("accountEpoch"), lease.value(QStringLiteral("accountEpoch")));
    QCOMPARE(runtime()->execute(forged, QStringLiteral("close")).value(QStringLiteral("reason")).toString(),
             QStringLiteral("stale-lease"));
    QVERIFY(!m_store->get(next).isEmpty());
}

void FeriaRuntimeTest::testReplacementInstanceFencesPreviousView()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    QObject *first = makeView();
    QVERIFY(runtime()->bindView(id, first));
    runtime()->markReady(first);
    const qint64 old = runtime()->admitWork(first, QStringLiteral("script"));
    const quint64 generation = runtime()->instance(id).value(QStringLiteral("engineGeneration")).toULongLong();

    QVERIFY(runtime()->beginInstance(id)); // recreate the engine for the same activity
    QCOMPARE(runtime()->phase(id), QStringLiteral("creating"));
    QObject *second = makeView();
    QVERIFY(runtime()->bindView(id, second));
    runtime()->markReady(second);

    QVERIFY(runtime()->instance(id).value(QStringLiteral("engineGeneration")).toULongLong() > generation);
    QVERIFY(!runtime()->redeemWork(old)); // cannot act on the replacement
    QCOMPARE(runtime()->admitWork(first, QStringLiteral("script")), 0);
    runtime()->markReady(first); // a trailing event from the old view changes nothing
    delete first;
    QCOMPARE(runtime()->phase(id), QStringLiteral("ready"));
    const qint64 fresh = runtime()->admitWork(second, QStringLiteral("script"));
    QVERIFY(fresh > 0 && runtime()->redeemWork(fresh));
}

void FeriaRuntimeTest::testViewIsNeverReboundAndActivityHasOneView()
{
    const QString a = openSpace(), b = openSpace();
    QVERIFY(runtime()->beginInstance(a));
    QVERIFY(runtime()->beginInstance(b));
    QObject *viewA = makeView();
    QVERIFY(runtime()->bindView(a, viewA));
    QVERIFY(!runtime()->bindView(b, viewA));      // a view serves one activity for life
    QVERIFY(!runtime()->bindView(a, makeView())); // an activity has one live view
    QVERIFY(runtime()->bindView(a, viewA));       // idempotent
    QCOMPARE(runtime()->instance(b).value(QStringLiteral("bound")).toBool(), false);
    // An unexpected destruction of the only view is surfaced, not hidden.
    delete viewA;
    QCOMPARE(runtime()->phase(a), QStringLiteral("failed"));
    QCOMPARE(runtime()->instance(a).value(QStringLiteral("reason")).toString(), QStringLiteral("view-destroyed"));
}

void FeriaRuntimeTest::testCommandsRequireCurrentLeaseAndPhase()
{
    const QString id = openSpace();
    auto reason = [this](const QVariantMap &result) { return result.value(QStringLiteral("reason")).toString(); };
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("explode"))), QStringLiteral("unknown-command"));
    QCOMPARE(reason(runtime()->command(QStringLiteral("browser-none"), QStringLiteral("back"))), QStringLiteral("unknown-space"));
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("back"))), QStringLiteral("no-engine-instance"));

    QVERIFY(runtime()->beginInstance(id));
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("back"))), QStringLiteral("not-ready"));
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("reload"))), QStringLiteral("not-ready"));
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("recover"))), QStringLiteral("not-failed"));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    runtime()->markReady(view);

    QSignalSpy finished(runtime(), &FeriaBrowserRuntime::commandFinished);
    for (const QString &name : {QStringLiteral("back"), QStringLiteral("forward"),
                                QStringLiteral("reload"), QStringLiteral("stop"), QStringLiteral("close")})
        QVERIFY2(runtime()->command(id, name).value(QStringLiteral("accepted")).toBool(), qPrintable(name));
    QCOMPARE(m_adapter->commands.size(), 5);
    QCOMPARE(m_adapter->commands.first().toMap().value(QStringLiteral("spaceId")).toString(), id);
    QCOMPARE(finished.count(), 5);
    QVERIFY(!m_store->get(id).isEmpty()); // close with a live view is the engine's to finish

    m_adapter->accept = false;
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("back"))), QStringLiteral("adapter-refused"));
    m_adapter->accept = true;

    // A host lease dies with its host placement; a plain lease does not.
    const QVariantMap hostLease = runtime()->lease(id);
    QVariantMap plain = hostLease;
    plain.remove(QStringLiteral("hostId")); plain.remove(QStringLiteral("hostGeneration"));
    QVERIFY(!m_store->setBrowserHost(hostLease, QStringLiteral("desktop:1")).isEmpty());
    QCOMPARE(reason(runtime()->execute(hostLease, QStringLiteral("reload"))), QStringLiteral("stale-lease"));
    QVERIFY(runtime()->execute(plain, QStringLiteral("reload")).value(QStringLiteral("accepted")).toBool());

    QCOMPARE(m_store->activeId(), QString());
    QVERIFY(runtime()->command(id, QStringLiteral("activate")).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(m_store->activeId(), id);

    runtime()->retireInstance(id);
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("reload"))), QStringLiteral("retiring"));
    QCOMPARE(reason(runtime()->command(id, QStringLiteral("close"))), QStringLiteral("retiring"));
}

void FeriaRuntimeTest::testNavigateIsValidatedBeforeDispatch()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    auto navigate = [this, id](const QVariantMap &arguments) {
        return runtime()->command(id, QStringLiteral("navigate"), arguments);
    };
    for (const QString &url : {QStringLiteral("file:///C:/Windows/win.ini"), QStringLiteral("javascript:alert(1)"),
                               QStringLiteral("chrome://settings"), QStringLiteral("")})
        QCOMPARE(navigate({{QStringLiteral("url"), url}}).value(QStringLiteral("reason")).toString(),
                 QStringLiteral("navigation-not-allowed"));
    QVERIFY(m_adapter->commands.isEmpty());

    // Navigation is admissible while the view is still being created: the
    // adapter queues it. Typed input goes through the shared omnibox policy.
    QVERIFY(navigate({{QStringLiteral("url"), QStringLiteral("https://example.com/b")}}).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(m_adapter->commands.last().toMap().value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/b"));
    QVERIFY(navigate({{QStringLiteral("text"), QStringLiteral("two words")}}).value(QStringLiteral("accepted")).toBool());
    QVERIFY(m_adapter->commands.last().toMap().value(QStringLiteral("url")).toString()
                .startsWith(QStringLiteral("https://duckduckgo.com/?q=")));
    QVERIFY(navigate({{QStringLiteral("text"), QStringLiteral("localhost:8123/x")}}).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(m_adapter->commands.last().toMap().value(QStringLiteral("url")).toString(), QStringLiteral("http://localhost:8123/x"));
}

void FeriaRuntimeTest::testCloseWithoutEngineUsesTheCloseLedger()
{
    // Failed creation must not orphan the canonical activity.
    const QString id = openSpace(QStringLiteral("https://example.com/failed"));
    QVERIFY(runtime()->beginInstance(id));
    runtime()->failInstance(id, QStringLiteral("creation-failed"));
    QCOMPARE(runtime()->phase(id), QStringLiteral("failed"));
    QVERIFY(runtime()->command(id, QStringLiteral("recover")).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(m_adapter->commands.last().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("recover"));
    m_adapter->commands.clear();

    QSignalSpy settled(m_store->browserOperations(), &FeriaBrowserOperations::operationSettled);
    QVERIFY(runtime()->command(id, QStringLiteral("close")).value(QStringLiteral("accepted")).toBool());
    QVERIFY(m_adapter->commands.isEmpty()); // no engine to ask
    QVERIFY(m_store->get(id).isEmpty());
    QCOMPARE(settled.count(), 1);
    const QVariantMap result = m_store->browserOperations()->operationResult(settled.first().first().toString());
    QCOMPARE(result.value(QStringLiteral("outcome")).toString(), QStringLiteral("succeeded"));
    QCOMPARE(result.value(QStringLiteral("evidence")).toString(), QStringLiteral("no-engine-instance"));
    // It remains reopenable like any other closed ordinary space.
    QCOMPARE(m_store->closedBrowserRecords(QStringLiteral("fixture")).size(), 1);
    QCOMPARE(runtime()->phase(id), QStringLiteral("absent")); // nothing left to drain

    // A private space closes the same way and leaves no reopen record.
    const QString secret = openSpace(QStringLiteral("https://example.com/secret"), true, true);
    QVERIFY(m_store->browserIdentity(secret).value(QStringLiteral("profileId")).toString().endsWith(QStringLiteral("/private")));
    QVERIFY(runtime()->command(secret, QStringLiteral("close")).value(QStringLiteral("accepted")).toBool());
    QVERIFY(m_store->get(secret).isEmpty());
    QCOMPARE(m_store->closedBrowserRecords(QStringLiteral("fixture")).size(), 1);
}

void FeriaRuntimeTest::testOpenIsValidatedAndKeepsOpenOrSwitch()
{
    const QVariantMap descriptor{{QStringLiteral("providerId"), QStringLiteral("fixture")},
        {QStringLiteral("providerName"), QStringLiteral("Fixture")},
        {QStringLiteral("url"), QStringLiteral("https://example.com/a")}};
    QVariantMap hostile = descriptor;
    hostile.insert(QStringLiteral("url"), QStringLiteral("file:///C:/secret.txt"));
    QCOMPARE(runtime()->open(hostile).value(QStringLiteral("reason")).toString(), QStringLiteral("navigation-not-allowed"));
    QCOMPARE(m_store->browsers().size(), 0);

    const QString first = runtime()->open(descriptor, {{QStringLiteral("fresh"), true}}).value(QStringLiteral("spaceId")).toString();
    const QString second = runtime()->open(descriptor, {{QStringLiteral("fresh"), true},
        {QStringLiteral("activate"), false}}).value(QStringLiteral("spaceId")).toString();
    QVERIFY(!first.isEmpty() && !second.isEmpty() && first != second); // explicit New, same URL
    QCOMPARE(m_store->activeId(), first); // background open does not steal selection
    // Without fresh, the provider's most recently used space is reused.
    QCOMPARE(runtime()->open(descriptor).value(QStringLiteral("spaceId")).toString(), first);
    QCOMPARE(m_store->browsers().size(), 2);

    m_store->setBrowserProfile(QString()); // signed out: no account, no browser
    const QVariantMap refused = runtime()->open(descriptor, {{QStringLiteral("fresh"), true}});
    QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(refused.value(QStringLiteral("reason")).toString(), QStringLiteral("store-refused"));
}

void FeriaRuntimeTest::testViewsRetireBeforeProfiles()
{
    auto *profile = new QObject(this);
    runtime()->registerProfile(profile);
    QObject *view = makeView(profile), *popup = makeView(profile);
    runtime()->registerView(view);
    runtime()->registerView(popup); // unbound views count as well
    QCOMPARE(runtime()->viewsForProfile(profile), 2);
    QCOMPARE(m_policy->runtimeViewCount(), 2); // the policy's counters are the runtime's
    QVERIFY(!runtime()->profileReleasable(profile));
    delete view;
    QVERIFY(!runtime()->profileReleasable(profile));
    delete popup;
    QVERIFY(runtime()->profileReleasable(profile));

    // Work admitted against the profile itself (a download outlives its tab)
    // also holds the profile.
    QCOMPARE(runtime()->admitProfileWork(new QObject(this), QStringLiteral("download")), 0);
    const qint64 download = runtime()->admitProfileWork(profile, QStringLiteral("download"));
    QVERIFY(download > 0 && runtime()->workValid(download));
    QVERIFY(!runtime()->profileReleasable(profile));
    QCOMPARE(runtime()->retireProfileWork(profile), 1);
    QVERIFY(!runtime()->workValid(download));
    QVERIFY(runtime()->profileReleasable(profile));
    const qint64 late = runtime()->admitProfileWork(profile, QStringLiteral("download"));
    delete profile;
    QVERIFY(!runtime()->redeemWork(late)); // the profile it belonged to is gone
    QCOMPARE(m_policy->runtimeProfileCount(), 0);
}

void FeriaRuntimeTest::testShutdownRefusesWorkAndRunsAdapterOnce()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    runtime()->markReady(view);
    const qint64 script = runtime()->admitWork(view, QStringLiteral("script"));

    runtime()->shutdown();
    runtime()->shutdown();
    QCOMPARE(m_adapter->shutdowns, 1);
    QVERIFY(runtime()->shuttingDown());
    QVERIFY(!runtime()->redeemWork(script));
    QCOMPARE(runtime()->admitWork(view, QStringLiteral("script")), 0);
    QCOMPARE(runtime()->phase(id), QStringLiteral("retiring"));
    QCOMPARE(runtime()->command(id, QStringLiteral("reload")).value(QStringLiteral("reason")).toString(),
             QStringLiteral("shutting-down"));
    QCOMPARE(runtime()->open({{QStringLiteral("url"), QStringLiteral("https://example.com")}})
                 .value(QStringLiteral("reason")).toString(), QStringLiteral("shutting-down"));
    QVERIFY(!runtime()->beginInstance(id));
    // Shutdown never closes canonical activities: they are what gets restored.
    QVERIFY(!m_store->get(id).isEmpty());
}

void FeriaRuntimeTest::testAdmissionIsRefusedOffTheOwnerThread()
{
    const QString id = openSpace();
    QVERIFY(runtime()->beginInstance(id));
    QObject *view = makeView();
    QVERIFY(runtime()->bindView(id, view));
    runtime()->markReady(view);
    auto *profile = new QObject(this);
    runtime()->registerProfile(profile);

    qint64 work = -1, download = -1;
    QVariantMap command, opened;
    FeriaBrowserRuntime *target = runtime();
    QThread *worker = QThread::create([&] {
        work = target->admitWork(view, QStringLiteral("script"));
        download = target->admitProfileWork(profile, QStringLiteral("download"));
        command = target->command(id, QStringLiteral("reload"));
        opened = target->open({{QStringLiteral("url"), QStringLiteral("https://example.com/x")}});
    });
    worker->start();
    QVERIFY(worker->wait(5000));
    delete worker;

    QCOMPARE(work, 0);
    QCOMPARE(download, 0);
    QCOMPARE(command.value(QStringLiteral("reason")).toString(), QStringLiteral("wrong-thread"));
    QCOMPARE(opened.value(QStringLiteral("reason")).toString(), QStringLiteral("wrong-thread"));
    QVERIFY(m_adapter->commands.isEmpty());
    QCOMPARE(m_store->browsers().size(), 1);
    // The owner thread is unaffected.
    QVERIFY(runtime()->admitWork(view, QStringLiteral("script")) > 0);
}

void FeriaRuntimeTest::testDisabledFenceLetsStaleWorkThrough()
{
    // RED control. With the fence compiled in but switched off, exactly the
    // stale callback and stale lease the other cases reject are accepted.
    qputenv("FERIA_UNSAFE_RUNTIME_FENCE", "1");
    FeriaBrowserPolicy unsafePolicy;
    qunsetenv("FERIA_UNSAFE_RUNTIME_FENCE");
    FeriaBrowserRuntime *unsafe = unsafePolicy.runtime();
    QVERIFY(unsafe->attach(m_adapter, m_store));
    const QString id = openSpace();
    QVERIFY(unsafe->beginInstance(id));
    QObject *view = makeView();
    QVERIFY(unsafe->bindView(id, view));
    unsafe->markReady(view);
    const qint64 stale = unsafe->admitWork(view, QStringLiteral("script"));
    unsafe->documentChanged(view);
    QVERIFY2(unsafe->redeemWork(stale), "the disabled fence must let the stale callback through");
    QVariantMap forged = unsafe->lease(id);
    forged.insert(QStringLiteral("accountEpoch"), 987654);
    QVERIFY2(unsafe->leaseCurrent(forged), "the disabled fence must accept the forged lease");
    // The production-configured runtime rejects the same forged lease.
    QVERIFY(!runtime()->leaseCurrent(forged));
}

QTEST_GUILESS_MAIN(FeriaRuntimeTest)
#include "feria_runtime_test.moc"
