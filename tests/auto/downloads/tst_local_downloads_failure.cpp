#include <QtTest>

#include "engine/LocalDownloads.h"
#include "account/DownloadIntentStore.h"
#include "account/ProfilePaths.h"
#include "player/downloadstore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

namespace {

class FakeVolumeOwner final : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void setJobs(const QVariantList& jobs) { m_jobs = jobs; }

    Q_INVOKABLE QVariantList activeVolumeJobs() const { return m_jobs; }

    void fail(const QString& id, const QString& reason)
    {
        emit failed(id, reason);
    }

signals:
    void failed(const QString& id, const QString& reason);

private:
    QVariantList m_jobs;
};

QVariantMap retainedFailure(const QVariantList& rows, const QString& id)
{
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id
                && row.value(QStringLiteral("canDismiss")).toBool()) {
            return row;
        }
    }
    return {};
}

} // namespace

class tst_local_downloads_failure final : public QObject
{
    Q_OBJECT

private slots:
    void failedVolumeUsesHumanTitleFromFailedActiveJob();
    void emptyVolumeTitleNeverLeaksRoutingId();
    void cancelPersistsIntentBeforeVolumeEarlyReturn();
    void cancelPersistenceFailureLeavesIntentOwned();
    void redownloadClearsCancellationBeforeEnqueue();
    void deactivatedIntentStoreCannotBeMutatedByLocalCancel();
};

void tst_local_downloads_failure::failedVolumeUsesHumanTitleFromFailedActiveJob()
{
    const QString id = QStringLiteral("tankoban:01J76XY7E9FNDZ1DBBM6PBJPFK:volume:2");
    FakeVolumeOwner owner;
    owner.setJobs({QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("seriesTitle"), QStringLiteral("Berserk")},
        {QStringLiteral("label"), QStringLiteral("Vol. 2")},
        {QStringLiteral("state"), QStringLiteral("failed")},
        {QStringLiteral("done"), 0.0},
        {QStringLiteral("total"), 0.0}
    }});

    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr, &owner);
    owner.fail(id, QStringLiteral("CBZ validation failed: cannot open CBZ: file open failed"));

    const QVariantMap row = retainedFailure(downloads.activeJobs(), id);
    QVERIFY2(!row.isEmpty(), "the failed volume must be retained as a dismissible failure row");
    QVERIFY(row.value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Berserk")));
    QVERIFY(row.value(QStringLiteral("title")).toString().endsWith(QStringLiteral("Vol. 2")));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral("tankoban:")));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral(":volume:")));
}

void tst_local_downloads_failure::emptyVolumeTitleNeverLeaksRoutingId()
{
    const QString id = QStringLiteral("tankoban:01J76XY7EF75DJNQCV04HTPDZK:volume:1");
    FakeVolumeOwner owner;
    owner.setJobs({QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), QString()},
        {QStringLiteral("state"), QStringLiteral("failed")}
    }});

    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr, &owner);
    owner.fail(id, QStringLiteral("CBZ validation failed"));

    const QVariantMap row = retainedFailure(downloads.activeJobs(), id);
    QVERIFY2(!row.isEmpty(), "the empty-title failure must still be retained");
    QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Vol. 1"));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral("tankoban:")));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral(":volume:")));
}

void tst_local_downloads_failure::cancelPersistsIntentBeforeVolumeEarlyReturn()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    DownloadIntentStore intents;
    QString error;
    QVERIFY2(intents.activate(*profile, &error), qPrintable(error));

    // The volume route has no local backend in this seam. A user cancellation
    // still owns a durable logical DELETE before the volume early return.
    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr);
    downloads.setDownloadIntentStore(&intents);
    downloads.cancel(QStringLiteral("tankoban"),
                     QStringLiteral("tankoban:series:volume:2"));

    SyncAdapterExport snapshot;
    QVERIFY2(intents.exportSnapshot(&snapshot, &error), qPrintable(error));
    QCOMPARE(snapshot.tombstones,
             QList<QString>{QStringLiteral("tankoban/tankoban:series:volume:2")});
    QVERIFY(snapshot.records.isEmpty());
}

void tst_local_downloads_failure::cancelPersistenceFailureLeavesIntentOwned()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};
    DownloadIntentStore intents;
    QString error;
    QVERIFY2(intents.activate(*profile, &error), qPrintable(error));
    QVERIFY2(intents.remember(row, &error), qPrintable(error));

    const QString path = QDir(profile->profileRoot()).filePath(
        QStringLiteral("download-intents.json"));
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkpath(path));

    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr);
    downloads.setDownloadIntentStore(&intents);
    downloads.cancel(QStringLiteral("theatre"), QStringLiteral("movie-42"));

    QCOMPARE(intents.records().size(), 1);
    QCOMPARE(intents.records().first().toMap(), row);

    QVERIFY(QDir().rmdir(path));
    SyncAdapterExport recovered;
    QVERIFY2(intents.exportSnapshot(&recovered, &error), qPrintable(error));
    QCOMPARE(recovered.records.size(), 1);
    QCOMPARE(recovered.tombstones.size(), 0);
}

void tst_local_downloads_failure::redownloadClearsCancellationBeforeEnqueue()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};
    DownloadIntentStore intents;
    QString error;
    QVERIFY2(intents.activate(*profile, &error), qPrintable(error));
    QVERIFY2(intents.cancel(QStringLiteral("theatre/movie-42"), &error),
             qPrintable(error));

    DownloadStore videos;
    LocalDownloads downloads(nullptr, nullptr, nullptr, &videos);
    downloads.setDownloadIntentStore(&intents);
    const QVariantMap result = downloads.redownload(row);
    QVERIFY2(result.value(QStringLiteral("success")).toBool(),
             qPrintable(result.value(QStringLiteral("message")).toString()));

    SyncAdapterExport snapshot;
    QVERIFY2(intents.exportSnapshot(&snapshot, &error), qPrintable(error));
    QCOMPARE(snapshot.tombstones.size(), 0);
    QCOMPARE(snapshot.records.size(), 1);
    QCOMPARE(snapshot.records.first().recordKey,
             QStringLiteral("theatre/movie-42"));
    QCOMPARE(snapshot.records.first().payload.toObject(),
             QJsonObject::fromVariantMap(row));
}

void tst_local_downloads_failure::deactivatedIntentStoreCannotBeMutatedByLocalCancel()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto profile = ProfilePaths::account(
        QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        QDir(temp.path()).filePath(QStringLiteral("appdata")));
    QVERIFY(profile.has_value());
    QVERIFY(QDir().mkpath(profile->profileRoot()));

    const QVariantMap row{
        {QStringLiteral("id"), QStringLiteral("movie-42")},
        {QStringLiteral("world"), QStringLiteral("theatre")},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Fixture Movie")}};
    DownloadIntentStore intents;
    QString error;
    QVERIFY2(intents.activate(*profile, &error), qPrintable(error));
    QVERIFY2(intents.remember(row, &error), qPrintable(error));
    const QString path = QDir(profile->profileRoot()).filePath(
        QStringLiteral("download-intents.json"));
    QFile persisted(path);
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    const QByteArray before = persisted.readAll();
    persisted.close();

    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr);
    downloads.setDownloadIntentStore(&intents);
    intents.deactivate();
    QVERIFY(!intents.active());
    downloads.cancel(QStringLiteral("theatre"), QStringLiteral("movie-42"));

    QVERIFY(persisted.open(QIODevice::ReadOnly));
    QCOMPARE(persisted.readAll(), before);
    persisted.close();

    DownloadIntentStore reopened;
    QVERIFY2(reopened.activate(*profile, &error), qPrintable(error));
    QCOMPARE(reopened.records().size(), 1);
    QCOMPARE(reopened.records().first().toMap(), row);
}

QTEST_GUILESS_MAIN(tst_local_downloads_failure)
#include "tst_local_downloads_failure.moc"
