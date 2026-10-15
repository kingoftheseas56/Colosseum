#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "tools/LanistaTiming.h"

class LanistaTimingTest : public QObject {
    Q_OBJECT

private slots:
    void milestoneAndStepShapesAreStable()
    {
        const QJsonObject milestone = lanista::timingMilestone(QStringLiteral("ready"), 125);
        QCOMPARE(milestone.value(QStringLiteral("name")).toString(), QStringLiteral("ready"));
        QCOMPARE(milestone.value(QStringLiteral("atMs")).toInteger(), qint64(125));

        const QJsonObject step = lanista::timingStep(2, QStringLiteral("open details"), 31, true);
        QCOMPARE(step.keys(), QStringList({QStringLiteral("durationMs"), QStringLiteral("index"),
                                           QStringLiteral("label"), QStringLiteral("pass")}));
        QCOMPARE(step.value(QStringLiteral("index")).toInt(), 2);
        QCOMPARE(step.value(QStringLiteral("label")).toString(), QStringLiteral("open details"));
        QCOMPARE(step.value(QStringLiteral("durationMs")).toInteger(), qint64(31));
        QVERIFY(step.value(QStringLiteral("pass")).toBool());
    }

    void documentPreservesMilestoneAndStepOrder()
    {
        const QJsonArray milestones{
            lanista::timingMilestone(QStringLiteral("launch"), 0),
            lanista::timingMilestone(QStringLiteral("ready"), 125),
        };
        const QJsonArray steps{
            lanista::timingStep(1, QStringLiteral("first"), 8, true),
            lanista::timingStep(2, QStringLiteral("second"), 19, false),
        };
        const QJsonObject document = lanista::timingDocument(QStringLiteral("session-1"), milestones, steps);
        QCOMPARE(document.value(QStringLiteral("schema")).toString(),
                 QStringLiteral("colosseum.lanista.timings.v1"));
        QCOMPARE(document.value(QStringLiteral("sessionId")).toString(), QStringLiteral("session-1"));
        QCOMPARE(document.value(QStringLiteral("milestones")).toArray().at(1)
                     .toObject().value(QStringLiteral("name")).toString(), QStringLiteral("ready"));
        QCOMPARE(document.value(QStringLiteral("steps")).toArray().at(0)
                     .toObject().value(QStringLiteral("index")).toInt(), 1);
        QCOMPARE(document.value(QStringLiteral("steps")).toArray().at(1)
                     .toObject().value(QStringLiteral("pass")).toBool(), false);
    }
};

QTEST_MAIN(LanistaTimingTest)
#include "tst_lanista_timing.moc"
