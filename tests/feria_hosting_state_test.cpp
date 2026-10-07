#include "SessionStore.h"
#include <QMetaObject>
#include <QtTest>

// Native state assertions complement, rather than replace, real WebEngine tests.
// Dynamic dispatch lets this target compile against the accepted pre-feature owner.
class FeriaHostingStateTest : public QObject {
    Q_OBJECT
    static QVariantMap descriptor(QString provider = "youtube", QString url = "https://example.com/a") {
        return {{"providerId", provider}, {"providerName", provider}, {"url", url},
                {"title", "Fixture"}, {"mode", "watch"}, {"privateBrowsing", false}};
    }
    static QVariantMap identity(SessionStore &s, const QString &id) {
        QVariantMap out;
        QMetaObject::invokeMethod(&s, "browserIdentity", Q_RETURN_ARG(QVariantMap, out), Q_ARG(QString, id));
        return out;
    }
    static QString begin(SessionStore &s, const QVariantMap &origin) {
        QString out;
        QMetaObject::invokeMethod(&s, "beginBrowserClose", Q_RETURN_ARG(QString, out), Q_ARG(QVariantMap, origin));
        return out;
    }
    static bool settle(SessionStore &s, const QVariantMap &origin, const QString &attempt,
                       bool accepted, const QVariantMap &desc = {}) {
        bool out = false;
        QMetaObject::invokeMethod(&s, "settleBrowserClose", Q_RETURN_ARG(bool, out),
            Q_ARG(QVariantMap, origin), Q_ARG(QString, attempt), Q_ARG(bool, accepted), Q_ARG(QVariantMap, desc));
        return out;
    }
    static QVariantMap advance(SessionStore &s, const QString &id) {
        QVariantMap out;
        QMetaObject::invokeMethod(&s, "advanceBrowserDocument", Q_RETURN_ARG(QVariantMap, out), Q_ARG(QString, id));
        return out;
    }
    static QVariantMap host(SessionStore &s, const QVariantMap &origin, const QString &hostId) {
        QVariantMap out;
        QMetaObject::invokeMethod(&s, "setBrowserHost", Q_RETURN_ARG(QVariantMap, out),
            Q_ARG(QVariantMap, origin), Q_ARG(QString, hostId));
        return out;
    }
    static QVariantList records(SessionStore &s, const QString &provider) {
        QVariantList out;
        QMetaObject::invokeMethod(&s, "closedBrowserRecords", Q_RETURN_ARG(QVariantList, out), Q_ARG(QString, provider));
        return out;
    }
    static QVariantMap prepare(SessionStore &s, const QString &provider) {
        QVariantMap out;
        QMetaObject::invokeMethod(&s, "prepareBrowserReopen", Q_RETURN_ARG(QVariantMap, out), Q_ARG(QString, provider));
        return out;
    }
    static bool commit(SessionStore &s, const QString &record, const QString &id) {
        bool out = false;
        QMetaObject::invokeMethod(&s, "commitBrowserReopen", Q_RETURN_ARG(bool, out),
            Q_ARG(QString, record), Q_ARG(QString, id));
        return out;
    }
    static bool accept(SessionStore &s, const QString &id, const QVariantMap &desc = {}) {
        const auto origin = identity(s, id);
        const auto token = begin(s, origin);
        return !token.isEmpty() && settle(s, origin, token, true, desc);
    }
private slots:
    void apiContract() {
        SessionStore s;
        for (const char *signature : {"browserIdentity(QString)", "beginBrowserClose(QVariantMap)",
             "settleBrowserClose(QVariantMap,QString,bool,QVariantMap)", "advanceBrowserDocument(QString)",
             "setBrowserHost(QVariantMap,QString)", "closedBrowserRecords(QString)",
             "prepareBrowserReopen(QString)", "commitBrowserReopen(QString,QString)"})
            QVERIFY2(s.metaObject()->indexOfMethod(signature) >= 0, signature);
    }
    void exactClosePreservesSelectedNeighborAndRejectAllowsRetry() {
        SessionStore s; s.setBrowserProfile("account-a");
        const QString a = s.openBrowser(descriptor(), true);
        const QVariantMap origin = identity(s, a);
        QVERIFY(!origin.isEmpty());
        for (const char *key : {"accountEpoch", "profileId", "spaceId", "pageGeneration",
                               "documentGeneration", "hostId", "hostGeneration"})
            QVERIFY2(origin.contains(key), key);
        QCOMPARE(origin.value("spaceId").toString(), a);
        const QString attempt = begin(s, origin); QVERIFY(!attempt.isEmpty());
        QVERIFY(begin(s, origin).isEmpty()); // No second renderer request for one pending attempt.
        const QString b = s.openBrowser(descriptor(), true);
        QVERIFY(settle(s, origin, attempt, false));
        QVERIFY(!s.get(a).isEmpty()); QCOMPARE(s.activeId(), b);
        QVERIFY(records(s, "youtube").isEmpty());
        QVERIFY(!settle(s, origin, attempt, true));
        const QString retry = begin(s, origin); QVERIFY(!retry.isEmpty()); QVERIFY(retry != attempt);
        QVERIFY(!settle(s, origin, attempt, true));
        QVERIFY(settle(s, origin, retry, true));
        QVERIFY(s.get(a).isEmpty()); QVERIFY(!s.get(b).isEmpty()); QCOMPARE(s.activeId(), b);
        QCOMPARE(records(s, "youtube").size(), 1);
        QVERIFY(!settle(s, origin, retry, true)); QCOMPARE(records(s, "youtube").size(), 1);
    }
    void everyIdentityFieldIsRequiredAndExact() {
        SessionStore s; s.setBrowserProfile("account-a");
        const auto id = s.openBrowser(descriptor(), true);
        const auto origin = identity(s, id); QVERIFY(!origin.isEmpty());
        for (const auto &key : origin.keys()) {
            auto missing = origin; missing.remove(key); QVERIFY(begin(s, missing).isEmpty());
            auto stale = origin; stale[key] = QStringLiteral("wrong"); QVERIFY(begin(s, stale).isEmpty());
        }
        const auto attempt = begin(s, origin); QVERIFY(!attempt.isEmpty());
        auto forged = origin; forged["spaceId"] = "missing";
        QVERIFY(!settle(s, forged, attempt, true));
        QVERIFY(settle(s, origin, attempt, false));
    }
    void navigationAndHostMoveInvalidateConsentWithoutHistory() {
        SessionStore s; s.setBrowserProfile("account-a");
        const auto id = s.openBrowser(descriptor(), true);
        const auto original = identity(s, id); const auto attempt = begin(s, original);
        QVERIFY(!attempt.isEmpty());
        const auto navigated = advance(s, id); QVERIFY(!navigated.isEmpty());
        QVERIFY(navigated.value("documentGeneration") != original.value("documentGeneration"));
        QCOMPARE(navigated.value("pageGeneration"), original.value("pageGeneration"));
        QVERIFY(!settle(s, original, attempt, true));
        const auto retry = begin(s, navigated); QVERIFY(!retry.isEmpty());
        const auto moved = host(s, navigated, "desktop-a"); QVERIFY(!moved.isEmpty());
        QCOMPARE(moved.value("hostId").toString(), QStringLiteral("desktop-a"));
        QVERIFY(moved.value("hostGeneration") != navigated.value("hostGeneration"));
        QVERIFY(!settle(s, navigated, retry, true));
        QVERIFY(host(s, navigated, "desktop-b").isEmpty());
        QVERIFY(!s.get(id).isEmpty()); QVERIFY(records(s, "youtube").isEmpty());
        const auto finalAttempt = begin(s, moved); QVERIFY(!finalAttempt.isEmpty());
        QVERIFY(settle(s, moved, finalAttempt, false));
    }
    void accountRetirementAndRendererTeardownRejectLateResults() {
        SessionStore s; s.setBrowserProfile("account-a");
        const auto local = s.openOrSwitch({{"appType", "biblio"}, {"contentKind", "book"},
            {"target", QVariantMap{{"path", "fixture.epub"}}}});
        const auto id = s.openBrowser(descriptor(), true);
        const auto origin = identity(s, id); const auto attempt = begin(s, origin);
        QVERIFY(!attempt.isEmpty());
        s.close(id); // Renderer death/failed adoption: teardown, never a user-approved closed record.
        QVERIFY(!settle(s, origin, attempt, true)); QVERIFY(records(s, "youtube").isEmpty());
        const auto next = s.openBrowser(descriptor(), true);
        const auto nextOrigin = identity(s, next); const auto nextAttempt = begin(s, nextOrigin);
        QVERIFY(!nextAttempt.isEmpty());
        s.setBrowserProfile("account-b");
        QVERIFY(s.get(next).isEmpty()); QVERIFY(!s.get(local).isEmpty());
        QVERIFY(!settle(s, nextOrigin, nextAttempt, true));
        s.setBrowserProfile("account-a");
        const auto replacement = s.openBrowser(descriptor(), true);
        QVERIFY(identity(s, replacement).value("accountEpoch") != origin.value("accountEpoch"));
        QVERIFY(begin(s, origin).isEmpty()); QVERIFY(prepare(s, "youtube").isEmpty());
    }
    void reopenIsAppScopedFreshAndTransactional() {
        SessionStore s; s.setBrowserProfile("account-a");
        const auto oldExisting = s.openBrowser(descriptor(), true);
        const auto a = s.openBrowser(descriptor("youtube", "https://example.com/first"), true);
        QVERIFY(accept(s, a));
        const auto other = s.openBrowser(descriptor("mangaplus", "https://example.com/manga"), true);
        QVERIFY(accept(s, other));
        const auto b = s.openBrowser(descriptor("youtube", "https://example.com/latest"), true);
        QVERIFY(accept(s, b));
        auto top = prepare(s, "youtube"); QVERIFY(!top.isEmpty());
        QCOMPARE(top.value("descriptor").toMap().value("url").toString(), QStringLiteral("https://example.com/latest"));
        const auto token = top.value("recordId").toString(); QVERIFY(!token.isEmpty());
        QVERIFY(!commit(s, token, "missing")); QVERIFY(!commit(s, token, oldExisting));
        QVERIFY(!commit(s, token, b)); // A retired identity cannot be resurrected.
        const auto wrongUrl = s.openBrowser(descriptor("youtube", "https://example.com/wrong"), true);
        QVERIFY(!commit(s, token, wrongUrl));
        QCOMPARE(prepare(s, "youtube"), top); // Failure is a non-consuming peek.
        const auto wrong = s.openBrowser(descriptor("mangaplus"), true);
        QVERIFY(!commit(s, token, wrong));
        auto privateDesc = top.value("descriptor").toMap(); privateDesc["privateBrowsing"] = true;
        const auto privateId = s.openBrowser(privateDesc, true); QVERIFY(!commit(s, token, privateId));
        const auto restored = s.openBrowser(top.value("descriptor").toMap(), true);
        QVERIFY(restored != a); QVERIFY(restored != b); QVERIFY(commit(s, token, restored));
        QVERIFY(!commit(s, token, restored));
        QCOMPARE(records(s, "youtube").size(), 1); QCOMPARE(records(s, "mangaplus").size(), 1);
        QCOMPARE(prepare(s, "youtube").value("descriptor").toMap().value("url").toString(), QStringLiteral("https://example.com/first"));
    }
    void reopenedRedirectKeepsOriginalTransactionBinding() {
        SessionStore s; s.setBrowserProfile("account-a");
        const auto original = s.openBrowser(descriptor("youtube", "https://example.com/redirect"), true);
        QVERIFY(accept(s, original));
        const auto prepared = prepare(s, "youtube"); QVERIFY(!prepared.isEmpty());
        const auto recordId = prepared.value("recordId").toString();
        const auto reopened = s.openBrowser(prepared.value("descriptor").toMap(), true);
        QVERIFY(!reopened.isEmpty());
        s.updateBrowser(reopened, "https://example.com/final", "Redirected document");
        // Successful policy-allowed navigation cannot invalidate original reconstruction.
        QVERIFY(commit(s, recordId, reopened));
        QCOMPARE(s.get(reopened).value("target").toMap().value("url").toString(),
                 QStringLiteral("https://example.com/final"));
        QVERIFY(records(s, "youtube").isEmpty());
    }
    void forgedReopenAllocationCannotConsumeRecord() {
        SessionStore s; s.setBrowserProfile("account-a");
        const auto original = s.openBrowser(descriptor(), true); QVERIFY(accept(s, original));
        const auto prepared = prepare(s, "youtube"); QVERIFY(!prepared.isEmpty());
        const auto recordId = prepared.value("recordId").toString();
        auto forged = prepared.value("descriptor").toMap(); forged["reopenRecordId"] = "forged-record";
        const auto badToken = s.openBrowser(forged, true); QVERIFY(!commit(s, recordId, badToken));
        auto wrongUrl = prepared.value("descriptor").toMap(); wrongUrl["url"] = "https://example.com/wrong";
        const auto badUrl = s.openBrowser(wrongUrl, true);
        s.updateBrowser(badUrl, "https://example.com/a", "Changed after allocation");
        QVERIFY(!commit(s, recordId, badUrl));
        QCOMPARE(prepare(s, "youtube"), prepared);
    }
    void privateInvalidAndStaleAccountNeverReopen() {
        SessionStore s; s.setBrowserProfile("account-a");
        auto privateDesc = descriptor(); privateDesc["privateBrowsing"] = true;
        const auto privateId = s.openBrowser(privateDesc, true);
        QVERIFY(accept(s, privateId, descriptor())); // Caller cannot turn a private canonical page ordinary.
        QVERIFY(records(s, "youtube").isEmpty());
        const auto id = s.openBrowser(descriptor(), true);
        QVERIFY(accept(s, id)); QVERIFY(!prepare(s, "youtube").isEmpty());
        s.setBrowserProfile("account-b"); QVERIFY(records(s, "youtube").isEmpty());
        s.setBrowserProfile("account-a"); QVERIFY(prepare(s, "youtube").isEmpty());
        QVERIFY(prepare(s, "unknown").isEmpty());
    }
    void snapshotsStripCredentialsAndTransientState() {
        SessionStore s; s.setBrowserProfile("account-a");
        auto desc = descriptor("youtube", "https://user:secret@example.com/path");
        desc["password"] = "secret"; desc["authorization"] = "Bearer secret";
        desc["postData"] = "body"; desc["openerId"] = "old-opener"; desc["hostId"] = "old-host";
        desc["profileId"] = "forged"; desc["accountEpoch"] = 999; desc["pageGeneration"] = 99;
        desc["visitContext"] = QVariantMap{{"token", "secret"}};
        const auto id = s.openBrowser(desc, true); QVERIFY(!id.isEmpty());
        auto forged = desc; forged["providerId"] = "mangaplus"; forged["url"] = "javascript:alert(1)";
        QVERIFY(accept(s, id, forged));
        const auto record = prepare(s, "youtube"); QVERIFY(!record.isEmpty());
        QVERIFY(prepare(s, "mangaplus").isEmpty());
        const auto clean = record.value("descriptor").toMap();
        QCOMPARE(clean.value("providerId").toString(), QStringLiteral("youtube"));
        QCOMPARE(clean.value("url").toString(), QStringLiteral("https://example.com/path"));
        for (const char *key : {"id", "password", "authorization", "postData", "openerId", "hostId",
                               "profileId", "accountEpoch", "pageGeneration", "visitContext"})
            QVERIFY2(!clean.contains(key), key);
    }
    void boundedNewestFirstHistory() {
        SessionStore s; s.setBrowserProfile("account-a");
        for (int i = 0; i < 25; ++i) {
            const auto id = s.openBrowser(descriptor("youtube", QString("https://example.com/%1").arg(i)), true);
            QVERIFY(accept(s, id));
        }
        const auto closed = records(s, "youtube"); QCOMPARE(closed.size(), 10);
        QCOMPARE(closed.first().toMap().value("descriptor").toMap().value("url").toString(), QStringLiteral("https://example.com/24"));
        QCOMPARE(closed.last().toMap().value("descriptor").toMap().value("url").toString(), QStringLiteral("https://example.com/15"));
    }
};
QTEST_GUILESS_MAIN(FeriaHostingStateTest)
#include "feria_hosting_state_test.moc"
