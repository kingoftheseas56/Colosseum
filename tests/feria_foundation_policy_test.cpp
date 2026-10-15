#include "SessionStore.h"
#include "feria/FeriaBrowserPolicy.h"
#include <QtTest>
#include <QUrlQuery>

class FoundationPolicyTest : public QObject {
    Q_OBJECT
private slots:
    void independentAdoptionRequiresPatchedRegisteredRuntime() {
        FeriaBrowserPolicy policy;
        QObject view;
        bool repaired = true;
        QVERIFY(QMetaObject::invokeMethod(&policy, "independentAdoptionAvailable",
            Q_RETURN_ARG(bool, repaired), Q_ARG(QObject *, &view)));
        QVERIFY(!repaired);
        policy.registerRuntimeView(&view);
        QVERIFY(QMetaObject::invokeMethod(&policy, "independentAdoptionAvailable",
            Q_RETURN_ARG(bool, repaired), Q_ARG(QObject *, &view)));
        QVERIFY(!repaired);
        view.setProperty("_colosseumAdoptionInputRebound", "true");
        QMetaObject::invokeMethod(&policy, "independentAdoptionAvailable",
            Q_RETURN_ARG(bool, repaired), Q_ARG(QObject *, &view));
        QVERIFY(!repaired);
        view.setProperty("_colosseumAdoptionInputRebound", true);
        QMetaObject::invokeMethod(&policy, "independentAdoptionAvailable",
            Q_RETURN_ARG(bool, repaired), Q_ARG(QObject *, &view));
        QVERIFY(repaired);
    }
    void addresses_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("host");
        QTest::newRow("hostname") << "example.org/path" << "example.org";
        QTest::newRow("uppercase") << "HTTPS://EXAMPLE.ORG/a" << "example.org";
        QTest::newRow("localhost-port") << "localhost:8123/a" << "localhost";
        QTest::newRow("ipv6") << "[::1]:8123/a" << "::1";
        QTest::newRow("idn") << QString::fromUtf8("https://bücher.de") << QString::fromUtf8("bücher.de");
    }
    void addresses() {
        QFETCH(QString, input); QFETCH(QString, host);
        FeriaBrowserPolicy policy;
        const auto url = policy.omniboxUrl(input);
        QVERIFY(url.isValid()); QCOMPARE(url.host(), host);
    }
    void localhostBoundaries_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("path") << "localhost/account?code=private-token" << "http://localhost/account?code=private-token";
        QTest::newRow("query") << "localhost?code=private-token" << "http://localhost?code=private-token";
        QTest::newRow("fragment") << "localhost#section" << "http://localhost#section";
        QTest::newRow("case") << "LOCALHOST/account" << "http://localhost/account";
        QTest::newRow("word") << "localhostish" << "https://duckduckgo.com/?q=localhostish";
    }
    void localhostBoundaries() {
        QFETCH(QString, input); QFETCH(QString, expected);
        FeriaBrowserPolicy policy;
        QCOMPARE(policy.omniboxUrl(input).toString(), expected);
    }
    void searchEscaping() {
        FeriaBrowserPolicy policy;
        const QString text = QString::fromUtf8("music & words # + 🎵 Telugu");
        const QUrl url = policy.omniboxUrl(text);
        QCOMPARE(url.host(), "duckduckgo.com");
        QCOMPARE(QUrlQuery(url).queryItemValue("q", QUrl::FullyDecoded), text);
        QVERIFY(url.fragment().isEmpty());
        QVERIFY(url.toEncoded().contains("%2B"));
        QCOMPARE(QUrlQuery(url).queryItems().size(), 1);
        QCOMPARE(QUrlQuery(policy.omniboxUrl("httpfoo")).queryItemValue("q"), "httpfoo");
    }
    void rejectedInput_data() {
        QTest::addColumn<QString>("input");
        for (const auto *text : {"javascript:alert(1)", "file:///tmp/test", "data:text/html,test",
             "about:config", "chrome://settings", "https://", "https://user:secret@example.org",
             "C:\\private\\book.pdf", "/tmp/private.pdf"})
            QTest::newRow(text) << QString(text);
        QTest::newRow("control") << QString("https://example.org/\nsecret");
        QTest::newRow("too-long") << QString(8193, 'x');
    }
    void rejectedInput() {
        QFETCH(QString, input); FeriaBrowserPolicy policy;
        QVERIFY2(policy.omniboxUrl(input).isEmpty(), qPrintable(input));
    }
    void canonicalSpaces() {
        SessionStore store; store.setBrowserProfile("fixture");
        QVariantMap d{{"providerId", "youtube"}, {"providerName", "YouTube"},
            {"url", "https://youtube.com/"}, {"mode", "watch"}};
        const QString a = store.openBrowser(d, true);
        const QString b = store.openBrowser(d, true, false, false);
        QVERIFY(!a.isEmpty()); QVERIFY(a != b);
        QCOMPARE(store.activeId(), a);
        QCOMPARE(store.browserApps().size(), 1);
        QCOMPARE(store.browserSpaces("youtube").size(), 2);
        store.updateBrowser(a, "https://accounts.google.com/login", "Another title");
        QCOMPARE(store.browserApps().first().toMap().value("providerId").toString(), "youtube");
        QCOMPARE(store.browserApps().first().toMap().value("selectedSpaceId").toString(), a);
        QVERIFY(store.activateBrowserApp("youtube")); QCOMPARE(store.activeId(), "");
        QVERIFY(store.activateBrowserApp("youtube")); QCOMPARE(store.activeId(), a);
        store.close(a);
        QVERIFY(store.activeId().isEmpty());
        QVERIFY(store.activateBrowserApp("youtube")); QCOMPARE(store.activeId(), b);
        store.setBrowserProfile("another-profile");
        QVERIFY(store.browserApps().isEmpty());
        QVERIFY(!store.activateBrowserApp("youtube"));
    }
};
QTEST_GUILESS_MAIN(FoundationPolicyTest)
#include "feria_foundation_policy_test.moc"
