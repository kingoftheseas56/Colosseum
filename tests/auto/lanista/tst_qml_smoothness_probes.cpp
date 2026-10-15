#include <QtTest>
#include <QTemporaryDir>

#include "FrameTimingProbe.h"
#include "net/PosterTimingProbe.h"

class QmlSmoothnessProbesTest : public QObject {
    Q_OBJECT
private slots:
    void posterLifecycleAndCacheMetadata()
    {
        PosterTimingProbe probe(true);
        const QUrl url(QStringLiteral("https://covers.example/poster.jpg"));
        const int id = probe.networkStart(url, QStringLiteral("covers.example"));
        QVERIFY(id > 0);
        probe.requestSent(id);
        probe.responseHeaders(id);
        QCOMPARE(probe.imageSource(url, true, QStringLiteral("source-change"),
                                   QStringLiteral("discoverCard_demo_img")), id);
        probe.networkDone(id, 1234, QStringLiteral("image/jpeg"),
                          QStringLiteral("h2"), QStringLiteral("miss"), 200);
        probe.decodeDone(id);
        probe.visiblePaint(id);
        const QJsonObject row = probe.rows().first().toObject();
        QCOMPARE(row.value(QStringLiteral("host")).toString(), QStringLiteral("covers.example"));
        QCOMPARE(row.value(QStringLiteral("bytes")).toInteger(), qint64(1234));
        QCOMPARE(row.value(QStringLiteral("protocol")).toString(), QStringLiteral("h2"));
        QCOMPARE(row.value(QStringLiteral("cache")).toString(), QStringLiteral("miss"));
        // The Image source notification can arrive after NAM started the request;
        // its current geometry cannot be retroactively called request visibility.
        QVERIFY(row.value(QStringLiteral("onScreenAtRequest")).isNull());
        QVERIFY(row.value(QStringLiteral("visibilityBasis")).toString().isEmpty());
        QCOMPARE(row.value(QStringLiteral("onScreenAtObservation")).toBool(), true);
        QVERIFY(row.value(QStringLiteral("visibleAtEnd")).isNull());
        QCOMPARE(row.value(QStringLiteral("objectName")).toString(), QStringLiteral("discoverCard_demo_img"));
        QVERIFY(row.value(QStringLiteral("sourceMs")).toInteger() >= 0);
        QVERIFY(row.value(QStringLiteral("paintMs")).toInteger() >= row.value(QStringLiteral("decodedMs")).toInteger());
        QVERIFY(row.value(QStringLiteral("replyMs")).toInteger() >= row.value(QStringLiteral("requestMs")).toInteger());
    }

    void firstObservedVisibilityRemainsUnknown()
    {
        PosterTimingProbe probe(true);
        const int id = probe.imageSource(QUrl(QStringLiteral("https://covers.example/late.jpg")),
                                         true, QStringLiteral("first-observed"));
        QVERIFY(id > 0);
        const QJsonObject row = probe.rows().first().toObject();
        QVERIFY(row.value(QStringLiteral("onScreenAtRequest")).isNull());
        QVERIFY(row.value(QStringLiteral("visibilityBasis")).toString().isEmpty());
        QCOMPARE(row.value(QStringLiteral("observation")).toString(), QStringLiteral("first-observed"));
    }

    void framePercentilesAndThresholds()
    {
        const QJsonObject summary = FrameTimingProbe::summarize({8.0, 16.7, 17.0, 34.0, 100.0});
        QCOMPARE(summary.value(QStringLiteral("count")).toInt(), 5);
        QCOMPARE(summary.value(QStringLiteral("p50Ms")).toDouble(), 17.0);
        QCOMPARE(summary.value(QStringLiteral("p95Ms")).toDouble(), 100.0);
        QCOMPARE(summary.value(QStringLiteral("maxMs")).toDouble(), 100.0);
        QCOMPARE(summary.value(QStringLiteral("over16_7")).toInt(), 3);
        QCOMPARE(summary.value(QStringLiteral("over33")).toInt(), 2);
    }

    void negativeControlDisabledMeansNoSamplesOrArtifact()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        PosterTimingProbe posters(false);
        FrameTimingProbe frames(false);
        QCOMPARE(posters.networkStart(QUrl(QStringLiteral("https://covers.example/a.jpg")),
                                      QStringLiteral("covers.example")), 0);
        QCOMPARE(posters.imageSource(QUrl(QStringLiteral("https://covers.example/a.jpg")), true,
                                     QStringLiteral("source-change")), 0);
        frames.recordSwap();
        QCOMPARE(posters.rows().size(), 0);
        QCOMPARE(frames.artifact().value(QStringLiteral("summary")).toObject()
                     .value(QStringLiteral("count")).toInt(), 0);
        QVERIFY(!posters.writeArtifact(dir.path() + QStringLiteral("/posters.json")));
        QVERIFY(!frames.writeArtifact(dir.path() + QStringLiteral("/frames.json")));
        QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/posters.json")));
        QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/frames.json")));
    }
};

QTEST_GUILESS_MAIN(QmlSmoothnessProbesTest)
#include "tst_qml_smoothness_probes.moc"
