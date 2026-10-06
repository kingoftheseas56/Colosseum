#include "SessionStore.h"
#include "feria/FeriaAccountStore.h"
#include "feria/FeriaBrowserPolicy.h"
#include <QSignalSpy>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QtTest>

class FeriaSessionOwnershipTest : public QObject {
    Q_OBJECT
private slots:
    void browserIdentityAndMru() {
        SessionStore sessions;
        // Invocation keeps the baseline compilable: absent production support
        // fails the contract assertion before checking its behavior.
        QVERIFY(sessions.metaObject()->indexOfMethod("openBrowser(QVariantMap,bool)") >= 0);
        QVERIFY(QMetaObject::invokeMethod(&sessions, "setBrowserProfile",
                                         Q_ARG(QString, QStringLiteral("fixture-profile"))));
        auto open = [&](const QString &url, bool fresh) {
            QString id;
            const QVariantMap desc{{"providerId", "mangaplus"}, {"title", "MANGA Plus"},
                {"logo", "fixture.svg"}, {"url", url}, {"mode", "read"}};
            const bool invoked = QMetaObject::invokeMethod(&sessions, "openBrowser",
                Q_RETURN_ARG(QString, id), Q_ARG(QVariantMap, desc), Q_ARG(bool, fresh));
            return invoked ? id : QString();
        };
        const QString a = open("https://example.invalid/a", false);
        QVERIFY(!a.isEmpty());
        const QString b = open("https://example.invalid/a", true);
        QVERIFY(!b.isEmpty()); QVERIFY(a != b);
        QCOMPARE(open("https://example.invalid/home", false), b);
        sessions.switchTo(a);
        QCOMPARE(open("https://example.invalid/home", false), a);
        QVERIFY(QMetaObject::invokeMethod(&sessions, "updateBrowser",
            Q_ARG(QString, a), Q_ARG(QString, QStringLiteral("https://example.invalid/next")),
            Q_ARG(QString, QStringLiteral("New title"))));
        QCOMPARE(open("https://example.invalid/home", false), a);
        QCOMPARE(sessions.list().size(), 2);
        QCOMPARE(sessions.get(a).value("target").toMap().value("url").toString(),
                 QStringLiteral("https://example.invalid/next"));
        const QString navigated = sessions.openBrowser({{"providerId", "mangaplus"},
            {"url", "https://example.invalid/title"}, {"title", "Explicit title"},
            {"titleId", "title-61"}, {"visitContext", QVariantMap{{"cover", "cover.jpg"}}}}, false, true);
        QCOMPARE(navigated, a);
        QCOMPARE(sessions.get(a).value("target").toMap().value("titleId").toString(), QStringLiteral("title-61"));
        QCOMPARE(open("https://example.invalid/home", false), a);
        QCOMPARE(sessions.get(a).value("target").toMap().value("titleId").toString(), QStringLiteral("title-61"));
        QCOMPARE(sessions.groups().size(), 1);
        QCOMPARE(sessions.groups().first().toMap().value("icon").toString(), QStringLiteral("fixture.svg"));
        sessions.close(a);
        QVERIFY(sessions.activeId().isEmpty());
        QVERIFY(!sessions.get(b).isEmpty());
        const QString book = sessions.openOrSwitch({{"appType", "biblio"}, {"contentKind", "book"},
            {"title", "Local book"}, {"target", QVariantMap{{"path", "fixture.epub"}}}});
        QVERIFY(QMetaObject::invokeMethod(&sessions, "setBrowserProfile",
                                         Q_ARG(QString, QStringLiteral("second-profile"))));
        QVERIFY(sessions.get(b).isEmpty());
        QVERIFY(!sessions.get(book).isEmpty());
    }
    void browserNavigationBoundary() {
        SessionStore sessions;
        sessions.setBrowserProfile(QStringLiteral("fixture"));
        for (const QString &url : {QStringLiteral("file:///private.txt"), QStringLiteral("javascript:alert(1)"),
             QStringLiteral("data:text/html,hello"), QStringLiteral("https:///"), QString()})
            QVERIFY(sessions.openBrowser({{"providerId", "fixture"}, {"url", url}}, true).isEmpty());
        QVERIFY(!sessions.openBrowser({{"providerId", "fixture"}, {"url", "about:blank"}}, true).isEmpty());
    }
    void liveProfilePreventsCookieCleanup() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        FeriaBrowserPolicy policy;
        policy.setStorageRoot(temp.path());
        QVERIFY(policy.metaObject()->indexOfMethod("registerRuntimeProfile(QObject*)") >= 0);
        auto *profile = new QObject;
        QVERIFY(QMetaObject::invokeMethod(&policy, "registerRuntimeProfile", Q_ARG(QObject*, profile)));
        QCOMPARE(policy.property("runtimeProfileCount").toInt(), 1);
        QSignalSpy replies(&policy, &FeriaBrowserPolicy::cookiesCleared);
        const int request = policy.clearQtCookies(temp.path(), {QStringLiteral("example.invalid")});
        QTest::qWait(180);
        QCOMPARE(replies.count(), 0); // No SQLite operation while a context is alive.
        delete profile;
        QCOMPARE(policy.property("runtimeProfileCount").toInt(), 0);
        QTRY_COMPARE(replies.count(), 1);
        QCOMPARE(replies.first().at(0).toInt(), request);
        QVERIFY(replies.first().at(1).toBool());
        auto *old = new QObject;
        QVERIFY(QMetaObject::invokeMethod(&policy, "registerRuntimeProfile", Q_ARG(QObject*, old)));
        replies.clear();
        policy.clearQtCookies(temp.path(), {QStringLiteral("example.invalid")});
        policy.setStorageRoot(temp.filePath("next"));
        delete old;
        QTRY_COMPARE(replies.count(), 1);
        QVERIFY(!replies.first().at(1).toBool()); // Superseded cleanup does no file work.
    }
    void independentVisitsRejectStaleTokens() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        FeriaAccountStore store; store.setStoragePath(temp.filePath("account.json"));
        QVERIFY(store.metaObject()->indexOfMethod("beginSessionVisit(QString,QVariantMap)") >= 0);
        auto begin = [&](const QString &id, const QString &provider) {
            QString token;
            const QVariantMap context{{"pk", provider}};
            QMetaObject::invokeMethod(&store, "beginSessionVisit", Q_RETURN_ARG(QString, token),
                Q_ARG(QString, id), Q_ARG(QVariantMap, context));
            return token;
        };
        auto observe = [&](const QString &id, const QString &token, const QVariantMap &sample) {
            bool ok = false;
            QMetaObject::invokeMethod(&store, "observeSession", Q_RETURN_ARG(bool, ok),
                Q_ARG(QString, id), Q_ARG(QString, token), Q_ARG(QVariantMap, sample));
            return ok;
        };
        const QString music = begin("music", "ytmusic");
        const QString manga = begin("manga", "mangaplus");
        QVERIFY(!music.isEmpty()); QVERIFY(!manga.isEmpty()); QVERIFY(music != manga);
        const QVariantMap audio{{"href", "https://music.youtube.com/watch?v=abcdefghijk"},
            {"title", "Music"}, {"providerMediaId", "abcdefghijk"}, {"kind", "audio"},
            {"position", 15.0}, {"duration", 100.0}, {"paused", false}};
        const QVariantMap page{{"href", "https://mangaplus.shueisha.co.jp/viewer/123"},
            {"title", "Chapter"}, {"kind", "book"}, {"locator", QVariantMap{{"type", "url"}}}};
        QVERIFY(observe("music", music, audio));
        QVERIFY(observe("manga", manga, page));
        QVERIFY(!observe("manga", music, audio));
        QCOMPARE(store.sessions().size(), 2);
        QHash<QString, QString> titles;
        for (const QVariant &row : store.sessions())
            titles.insert(row.toMap().value("pk").toString(), row.toMap().value("title").toString());
        QCOMPARE(titles.value("ytmusic"), QStringLiteral("Music"));
        QCOMPARE(titles.value("mangaplus"), QStringLiteral("Chapter"));
        store.setMediaCaptureBlocked(true, music);
        QVERIFY(!observe("music", music, audio));
        QVERIFY(observe("manga", manga, page));
        store.setMediaCaptureBlocked(false, music);
        QVERIFY(store.setRecording(false));
        QVERIFY(!observe("music", music, audio));
        QVERIFY(store.setRecording(true));
        const QString replacement = begin("music", "ytmusic");
        QVERIFY(!observe("music", music, audio));
        QVERIFY(observe("music", replacement, audio));
        QVERIFY(QMetaObject::invokeMethod(&store, "endSessionVisit", Q_ARG(QString, QStringLiteral("manga"))));
        QVERIFY(!observe("manga", manga, page));
        store.setStoragePath(temp.filePath("other.json"));
        QVERIFY(!observe("music", replacement, audio));
        QVERIFY(store.sessions().isEmpty());
    }
};
QTEST_GUILESS_MAIN(FeriaSessionOwnershipTest)
#include "feria_session_ownership_test.moc"
