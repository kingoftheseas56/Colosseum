#include <QtTest>

#include "engine/LocalDownloadsProjection.h"
#include "engine/TankoyomiIdentity.h"

#include <QVariantList>
#include <QVariantMap>

class tst_local_downloads_projection final : public QObject
{
    Q_OBJECT

private slots:
    void mixedMangaSeriesCountsChaptersAndVolumesHonestly();
    void legacyMangaRowsInferChapterVersusVolume();
    void onlyChapterRowsAreDirectlyRedownloadable();
    void qualifiedChapterIdentityRestoresHumanLabel();
};

void tst_local_downloads_projection::mixedMangaSeriesCountsChaptersAndVolumesHonestly()
{
    QVariantList items;
    items.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("tankoyomi:es:fixture:chapter:v1:e30")},
        {QStringLiteral("world"), QStringLiteral("tankoban")},
        {QStringLiteral("kind"), QStringLiteral("manga")},
        {QStringLiteral("itemKind"), QStringLiteral("chapter")},
        {QStringLiteral("seriesKey"), QStringLiteral("manga:mal:16498")},
        {QStringLiteral("seriesTitle"), QStringLiteral("Shingeki no Kyojin")},
        {QStringLiteral("title"), QStringLiteral("Capítulo 1")},
        {QStringLiteral("art"), QString()}
    });
    for (int volume = 1; volume <= 4; ++volume) {
        items.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("tankoban:16498:volume:%1").arg(volume)},
            {QStringLiteral("world"), QStringLiteral("tankoban")},
            {QStringLiteral("kind"), QStringLiteral("manga")},
            {QStringLiteral("itemKind"), QStringLiteral("volume")},
            {QStringLiteral("seriesKey"), QStringLiteral("manga:mal:16498")},
            {QStringLiteral("seriesTitle"), QStringLiteral("Shingeki no Kyojin")},
            {QStringLiteral("title"), QStringLiteral("Vol. %1").arg(volume)},
            {QStringLiteral("art"), volume == 1
                ? QStringLiteral("file:///covers/volume-1.jpg") : QString()}
        });
    }

    const QVariantList series = LocalDownloadsProjection::aggregateSeries(
        items, QStringLiteral("tankoban"));
    QCOMPARE(series.size(), 1);
    const QVariantMap row = series.first().toMap();
    QCOMPARE(row.value(QStringLiteral("itemCount")).toInt(), 5);
    QCOMPARE(row.value(QStringLiteral("chapterCount")).toInt(), 1);
    QCOMPARE(row.value(QStringLiteral("volumeCount")).toInt(), 4);
    QCOMPARE(row.value(QStringLiteral("unitText")).toString(),
             QStringLiteral("1 chapter · 4 volumes"));
    QCOMPARE(row.value(QStringLiteral("art")).toString(),
             QStringLiteral("file:///covers/volume-1.jpg"));
}

void tst_local_downloads_projection::legacyMangaRowsInferChapterVersusVolume()
{
    QCOMPARE(LocalDownloadsProjection::itemKind(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("tankoban:16498:volume:2")},
        {QStringLiteral("kind"), QStringLiteral("manga")}
    }), QStringLiteral("volume"));
    QCOMPARE(LocalDownloadsProjection::itemKind(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("tankoyomi:es:zonatmo:chapter:v1:e30")},
        {QStringLiteral("kind"), QStringLiteral("manga")}
    }), QStringLiteral("chapter"));
}

void tst_local_downloads_projection::onlyChapterRowsAreDirectlyRedownloadable()
{
    QVERIFY(LocalDownloadsProjection::canRedownload(QVariantMap{
        {QStringLiteral("world"), QStringLiteral("tankoban")},
        {QStringLiteral("kind"), QStringLiteral("manga")},
        {QStringLiteral("itemKind"), QStringLiteral("chapter")}
    }));
    QVERIFY(!LocalDownloadsProjection::canRedownload(QVariantMap{
        {QStringLiteral("world"), QStringLiteral("tankoban")},
        {QStringLiteral("kind"), QStringLiteral("manga")},
        {QStringLiteral("itemKind"), QStringLiteral("volume")}
    }));
    QVERIFY(LocalDownloadsProjection::canRedownload(QVariantMap{
        {QStringLiteral("world"), QStringLiteral("biblio")}
    }));
}

void tst_local_downloads_projection::qualifiedChapterIdentityRestoresHumanLabel()
{
    const QVariantMap chapter{
        {QStringLiteral("id"), QStringLiteral("123")},
        {QStringLiteral("title"), QStringLiteral("Capítulo 1")},
        {QStringLiteral("number"), 1}
    };
    const QString qualified = TankoyomiIdentity::qualifyChapter(
        QStringLiteral("es"), QStringLiteral("zonatmo"), chapter);

    QCOMPARE(LocalDownloadsProjection::chapterLabel(qualified, QStringLiteral("Chapter")),
             QStringLiteral("Capítulo 1"));
    QCOMPARE(LocalDownloadsProjection::chapterLabel(qualified, QStringLiteral("The Fall of Shiganshina")),
             QStringLiteral("The Fall of Shiganshina"));
}

QTEST_GUILESS_MAIN(tst_local_downloads_projection)
#include "tst_local_downloads_projection.moc"
