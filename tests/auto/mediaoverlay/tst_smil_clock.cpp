// Proves all clock forms, strict validation and exact rounding/overflow handling.
// Negative control performed and reverted: changing the min multiplier to 1000 must fail only formats(min).
#include "reader2/mediaoverlay/SmilClock.h"
#include <QtTest>
using namespace MediaOverlay;
class tst_smil_clock : public QObject
{
    Q_OBJECT
private slots:
    void formats_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<qint64>("expected");
        QTest::newRow("full") << QString("0:01:02.5") << qint64(62500);
        QTest::newRow("long-hours") << QString("124:59:36") << qint64(449976000);
        QTest::newRow("partial") << QString("03:24.25") << qint64(204250);
        QTest::newRow("h") << QString("1.5h") << qint64(5400000);
        QTest::newRow("min") << QString("2min") << qint64(120000);
        QTest::newRow("s") << QString("5.5s") << qint64(5500);
        QTest::newRow("ms") << QString("30ms") << qint64(30);
        QTest::newRow("bare") << QString("12") << qint64(12000);
        QTest::newRow("bare-fraction") << QString("12.75") << qint64(12750);
        QTest::newRow("round-half") << QString(".0005s") << qint64(1);
        QTest::newRow("round-below") << QString("0.000499999999999999s") << qint64(0);
        QTest::newRow("zero") << QString("00:00:00") << qint64(0);
        QTest::newRow("trim") << QString(" \t12s\n") << qint64(12000);
        QTest::newRow("max") << QString("9223372036854775807ms") << qint64(9223372036854775807LL);
    }
    void formats()
    {
        QFETCH(QString,text);
        QFETCH(qint64,expected);
        const auto r=SmilClock::parse(text);
        QVERIFY(r.has_value());
        QCOMPARE(*r,expected);
    }
    void invalid_data()
    {
        QTest::addColumn<QString>("text");
        const QStringList values=
        {
            "", "-1", "garbage", "00:60:00", "60:00", "00:00:60", "1day", "1.2.3", "1 s", "1: 20", "1e3", "+1", "NaN", "1:", "1:2:3:4", "9223372036854775808ms", "999999999999999999999999h", "9223372036854775807.5ms"
        };
        const QStringList names=
        {
            "empty","negative","garbage","full-minute-60","partial-minute-60","second-60","unknown-metric","multiple-decimals","inner-space","inner-clock-space","exponent","plus-sign","nan","incomplete","too-many-fields","overflow-ms","overflow-hours","rounding-overflow"
        };
        for(int i=0;i<values.size();++i) QTest::newRow(qPrintable(names[i])) << values[i];
    }
    void invalid()
    {
        QFETCH(QString,text);
        QVERIFY(!SmilClock::parse(text));
    }
};
QTEST_APPLESS_MAIN(tst_smil_clock)
#include "tst_smil_clock.moc"
