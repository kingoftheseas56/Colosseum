#include "account/ProfilePreferencesStore.h"

#include <QDir>
#include <QMetaProperty>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class tst_manga_series_language_preferences : public QObject {
    Q_OBJECT
private slots:
    void qmlFacingMethodsAreInvokable();
    void persistsByCanonicalSeriesAndProfile();
    void rejectsBadIdentityAndLanguage();
    void retainsUninstalledLanguageAndNormalizesPortugueseAlias();
    void corruptedSnapshotDoesNotOverrideCleanState();
    void failedDiskWriteDoesNotClaimSuccess();
};

void tst_manga_series_language_preferences::qmlFacingMethodsAreInvokable() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ProfilePreferencesStore store(temp.filePath(QStringLiteral("preferences.ini")));
    const int revisionIndex = store.metaObject()->indexOfProperty("mangaLanguageRevision");
    QVERIFY(revisionIndex >= 0);
    QVERIFY(store.metaObject()->property(revisionIndex).hasNotifySignal());
    bool saved = false;
    QVERIFY(QMetaObject::invokeMethod(&store, "setMangaLanguageForSeries",
        Qt::DirectConnection, Q_RETURN_ARG(bool, saved),
        Q_ARG(QString, QStringLiteral("mal:1")), Q_ARG(QString, QStringLiteral("pt"))));
    QVERIFY(saved);
    QString chosen;
    QVERIFY(QMetaObject::invokeMethod(&store, "mangaLanguageForSeries",
        Qt::DirectConnection, Q_RETURN_ARG(QString, chosen),
        Q_ARG(QString, QStringLiteral("mal:1"))));
    QCOMPARE(chosen, QStringLiteral("pt"));
}

void tst_manga_series_language_preferences::persistsByCanonicalSeriesAndProfile() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pathA = temp.filePath(QStringLiteral("account-a.ini"));
    const QString pathB = temp.filePath(QStringLiteral("account-b.ini"));
    {
        ProfilePreferencesStore a(pathA);
        QSignalSpy revisions(&a, &ProfilePreferencesStore::mangaLanguagesChanged);
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
        QVERIFY(a.setMangaLanguageForSeries(QStringLiteral("mal:1"), QStringLiteral("pt")));
        QVERIFY(a.setMangaLanguageForSeries(QStringLiteral("mal:13:color"), QStringLiteral("es")));
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("pt"));
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:13:color")), QStringLiteral("es"));
        QCOMPARE(a.mangaLanguageRevision(), 2);
        QCOMPARE(revisions.size(), 2);
        QVERIFY(a.setMangaLanguageForSeries(QStringLiteral("mal:1"), QStringLiteral("pt")));
        QCOMPARE(revisions.size(), 2); // idempotent write
    }
    {
        ProfilePreferencesStore a(pathA);
        ProfilePreferencesStore b(pathB);
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("pt"));
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:13:color")), QStringLiteral("es"));
        QCOMPARE(b.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
        QVERIFY(b.setMangaLanguageForSeries(QStringLiteral("mal:1"), QStringLiteral("fr")));
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("pt"));
        QVERIFY(a.clearMangaLanguageForSeries(QStringLiteral("mal:1")));
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
        QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:13:color")), QStringLiteral("es"));
    }
    ProfilePreferencesStore a(pathA);
    ProfilePreferencesStore b(pathB);
    QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
    QCOMPARE(a.mangaLanguageForSeries(QStringLiteral("mal:13:color")), QStringLiteral("es"));
    QCOMPARE(b.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("fr"));
}

void tst_manga_series_language_preferences::rejectsBadIdentityAndLanguage() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ProfilePreferencesStore store(temp.filePath(QStringLiteral("preferences.ini")));
    for (const QString &id : {QString(), QStringLiteral("Monster"), QStringLiteral("mal:0"),
                              QStringLiteral("mal:-1"), QStringLiteral("vault:c:/file"),
                              QStringLiteral("mal:1/../../prefs")}) {
        QVERIFY2(!store.setMangaLanguageForSeries(id, QStringLiteral("pt")),
                 qPrintable(QStringLiteral("Accepted unsafe identity: ") + id));
        QVERIFY(!store.clearMangaLanguageForSeries(id));
    }
    for (const QString &language : {QString(), QStringLiteral("wrong language"),
                                    QStringLiteral("../pt"), QStringLiteral("pt\nxx"),
                                    QStringLiteral("a")}) {
        QVERIFY(!store.setMangaLanguageForSeries(QStringLiteral("mal:1"), language));
    }
    QCOMPARE(store.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
}

void tst_manga_series_language_preferences::retainsUninstalledLanguageAndNormalizesPortugueseAlias() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("prefs.ini"));
    {
        ProfilePreferencesStore store(path);
        QVERIFY(store.setMangaLanguageForSeries(QStringLiteral("mal:1"), QStringLiteral("pt-BR")));
        QCOMPARE(store.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("pt"));
        QVERIFY(store.setMangaLanguageForSeries(QStringLiteral("mal:2"), QStringLiteral("zz")));
    }
    ProfilePreferencesStore reloaded(path);
    QCOMPARE(reloaded.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("pt"));
    QCOMPARE(reloaded.mangaLanguageForSeries(QStringLiteral("mal:2")), QStringLiteral("zz"));
    // The UI must surface zz as unavailable rather than changing the stored opinion.
}

void tst_manga_series_language_preferences::corruptedSnapshotDoesNotOverrideCleanState() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("prefs.ini"));
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("manga/seriesLanguageOverrides"), QByteArray("{bad json"));
        raw.sync();
        QCOMPARE(raw.status(), QSettings::NoError);
    }
    ProfilePreferencesStore store(path);
    QCOMPARE(store.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
    QVERIFY(store.setMangaLanguageForSeries(QStringLiteral("mal:1"), QStringLiteral("pt")));
    ProfilePreferencesStore reloaded(path);
    QCOMPARE(reloaded.mangaLanguageForSeries(QStringLiteral("mal:1")), QStringLiteral("pt"));
}

void tst_manga_series_language_preferences::failedDiskWriteDoesNotClaimSuccess() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("locked.ini"));
    QVERIFY(QDir().mkpath(path)); // QSettings may not save a file over a directory.
    ProfilePreferencesStore store(path);
    QSignalSpy spy(&store, &ProfilePreferencesStore::mangaLanguagesChanged);
    QVERIFY(!store.setMangaLanguageForSeries(QStringLiteral("mal:1"), QStringLiteral("pt")));
    QCOMPARE(store.mangaLanguageForSeries(QStringLiteral("mal:1")), QString());
    QCOMPARE(store.mangaLanguageRevision(), 0);
    QCOMPARE(spy.size(), 0);
}

QTEST_GUILESS_MAIN(tst_manga_series_language_preferences)
#include "tst_manga_series_language_preferences.moc"
