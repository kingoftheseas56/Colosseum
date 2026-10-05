// Proves namespace-aware ordered SMIL parsing and transactional failure.
// Negative control performed and reverted: reverse the output vector; flatOrder and nestedOrder go red.
#include "reader2/mediaoverlay/MediaOverlayDocument.h"
#include <QtTest>
using namespace MediaOverlay;
static QByteArray doc(const QByteArray &body)
{
    return "<smil xmlns='http://www.w3.org/ns/SMIL' xmlns:e='http://www.idpf.org/2007/ops'><body>"+body+"</body></smil>";
}
static QByteArray par(const QByteArray &id)
{
    return "<par id='"+id+"'><text src='../text/c.xhtml#"+id+"'/><audio src='../audio/a.mp3' clipBegin='1s' clipEnd='2s'/></par>";
}
class tst_media_overlay_document : public QObject
{
    Q_OBJECT
private slots:
    void flatOrder()
    {
        auto r=MediaOverlayDocument::parse(doc(par("a")+par("b")));
        QVERIFY(r.ok);
        QCOMPARE(r.entries.size(),2);
        QCOMPARE(r.entries[0].id,QString("a"));
        QCOMPARE(r.entries[1].id,QString("b"));
        QCOMPARE(r.entries[0].textPath,QString("../text/c.xhtml"));
        QCOMPARE(r.entries[0].fragmentId,QString("a"));
        QCOMPARE(r.entries[0].audioPath,QString("../audio/a.mp3"));
    }
    void nestedOrder()
    {
        auto r=MediaOverlayDocument::parse(doc("<seq id='outer' e:type='chapter' e:textref='../text/c.xhtml'><seq id='inner' e:type='section'>"+par("a")+"</seq>"+par("b")+"</seq>"));
        QVERIFY(r.ok);
        QCOMPARE(r.entries.size(),2);
        QCOMPARE(r.entries[0].id,QString("a"));
        QCOMPARE(r.entries[1].id,QString("b"));
        QCOMPARE(r.entries[0].sequences.size(),2);
        QCOMPARE(r.entries[0].sequences[0].id,QString("outer"));
        QCOMPARE(r.entries[0].sequences[0].types,QStringList
        {
            "chapter"
        });
        QCOMPARE(r.entries[0].sequences[0].textRef,QString("../text/c.xhtml"));
        QCOMPARE(r.entries[1].sequences.size(),1);
    }
    void cases_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("warning");
        QTest::newRow("missing-begin") << QByteArray("<par><text src='c#x'/><audio src='a' clipEnd='2'/></par>") << 1 << QString();
        QTest::newRow("open-end") << QByteArray("<par><text src='c#x'/><audio src='a'/></par>") << 1 << QString();
        QTest::newRow("text-only") << QByteArray("<par><text src='c#x'/></par>") << 1 << QString();
        QTest::newRow("reversed") << (par("ok")+"<par><text src='c#x'/><audio src='a' clipBegin='3' clipEnd='2'/></par>") << 1 << QString("invalid-clip");
        QTest::newRow("negative") << QByteArray("<par><text src='c#x'/><audio src='a' clipBegin='-1'/></par>") << 0 << QString("invalid-clip");
        QTest::newRow("bad-clock") << QByteArray("<par><text src='c#x'/><audio src='a' clipEnd='whoops'/></par>") << 0 << QString("invalid-clip");
        QTest::newRow("equal") << QByteArray("<par><text src='c#x'/><audio src='a' clipEnd='0'/></par>") << 0 << QString("invalid-clip");
        QTest::newRow("no-text") << QByteArray("<par><audio src='a'/></par>") << 0 << QString("missing-text");
        QTest::newRow("empty-body") << QByteArray() << 0 << QString();
        QTest::newRow("foreign-par") << QByteArray("<par xmlns='urn:foreign'><text src='c'/></par>") << 0 << QString();
        QTest::newRow("foreign-text") << QByteArray("<par><text xmlns='urn:foreign' src='c'/></par>") << 0 << QString("missing-text");
    }
    void cases()
    {
        QFETCH(QByteArray,body);
        QFETCH(int,count);
        QFETCH(QString,warning);
        const auto r=MediaOverlayDocument::parse(doc(body));
        QVERIFY(r.ok);
        QCOMPARE(r.entries.size(),count);
        if(!warning.isEmpty())
        {
            QCOMPARE(r.warnings.size(),1);
            QCOMPARE(r.warnings[0].code,warning);
            QVERIFY(r.warnings[0].line>0);
            QVERIFY(r.warnings[0].position>0);
        }
        else QVERIFY(r.warnings.isEmpty());
        if(QByteArray(QTest::currentDataTag())=="missing-begin") QCOMPARE(r.entries[0].beginMs,qint64(0));
        if(QByteArray(QTest::currentDataTag())=="open-end") QVERIFY(!r.entries[0].endMs);
        if(QByteArray(QTest::currentDataTag())=="text-only") QVERIFY(r.entries[0].textOnly);
    }
    void alternatePrefix()
    {
        auto r=MediaOverlayDocument::parse(doc("<seq e:type='chapter section' e:textref='c#s'><par e:type='sentence'><text src='c#p'/></par></seq>"));
        QVERIFY(r.ok);
        QCOMPARE(r.entries[0].types,QStringList
        {
            "sentence"
        });
        QCOMPARE(r.entries[0].sequences[0].types,(QStringList
        {
            "chapter","section"
        }));
    }
    void malformed()
    {
        auto r=MediaOverlayDocument::parse(doc(par("a")+"<seq>"));
        QVERIFY(!r.ok);
        QVERIFY(r.entries.isEmpty());
        QVERIFY(r.errorLine>0);
        QVERIFY(!r.error.isEmpty());
    }
    void wrongRoot()
    {
        QVERIFY(!MediaOverlayDocument::parse("<smil xmlns='urn:wrong'><body/></smil>").ok);
    }
};
QTEST_APPLESS_MAIN(tst_media_overlay_document)
#include "tst_media_overlay_document.moc"
