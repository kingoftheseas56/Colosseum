// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "account/DownloadIntentStore.h"
#include "account/DownloadIntentSyncAdapter.h"
#include "account/ProfilePaths.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class tst_download_intent : public QObject {
    Q_OBJECT

private slots:
    void remoteIntentStoresLogicalIdentityWithoutAPath();
    void localDownloadRowsExportWithoutLocalOnlyFields();
};

void tst_download_intent::remoteIntentStoresLogicalIdentityWithoutAPath() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    DownloadIntentStore store;
    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY2(store.applyRemote(
        QStringLiteral("theatre/movie-42"),
        SyncWireOperation::Put,
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("movie-42")},
            {QStringLiteral("world"), QStringLiteral("theatre")},
            {QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Fixture Movie")}},
        1,
        &error), qPrintable(error));

    const QVariantList rows = store.records();
    QCOMPARE(rows.size(), 1);
    const QVariantMap row = rows.first().toMap();
    QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("movie-42"));
    QVERIFY(!row.contains(QStringLiteral("path")));
    QVERIFY(!row.contains(QStringLiteral("url")));
}

void tst_download_intent::localDownloadRowsExportWithoutLocalOnlyFields() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    DownloadIntentStore store;
    store.setLocalRecordProvider([] {
        return QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("movie-42")},
                {QStringLiteral("world"), QStringLiteral("theatre")},
                {QStringLiteral("kind"), QStringLiteral("movie")},
                {QStringLiteral("title"), QStringLiteral("Fixture Movie")},
                {QStringLiteral("path"), QStringLiteral("C:/Users/test/movie.mkv")},
                {QStringLiteral("url"), QStringLiteral("https://source.test/movie.mkv")},
                {QStringLiteral("headers"), QStringLiteral("secret")},
                {QStringLiteral("bytes"), 1234}}};
    });

    QString error;
    QVERIFY2(store.activate(*profile, &error), qPrintable(error));
    QVERIFY2(store.refreshFromLocal(&error), qPrintable(error));

    SyncAdapterExport snapshot;
    DownloadIntentSyncAdapter adapter(&store);
    QVERIFY2(adapter.exportSnapshot(&snapshot, &error), qPrintable(error));
    QCOMPARE(snapshot.records.size(), 1);
    QVERIFY(snapshot.records.first().payload.isObject());
    const QJsonObject payload = snapshot.records.first().payload.toObject();
    QVERIFY(!payload.contains(QStringLiteral("path")));
    QVERIFY(!payload.contains(QStringLiteral("url")));
    QVERIFY(!payload.contains(QStringLiteral("headers")));
    QVERIFY(!payload.contains(QStringLiteral("bytes")));
}

QTEST_MAIN(tst_download_intent)
#include "tst_download_intent.moc"
