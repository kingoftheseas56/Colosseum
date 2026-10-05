// Proves OPF associations, reading order, metadata and safe URI resolution.
// Negative control performed and reverted: use root instead of OPF directory; only subfolder goes red.
#include "reader2/mediaoverlay/MediaOverlayPackage.h"
#include <QtTest>
using namespace MediaOverlay;
static QByteArray package(const QByteArray &manifest, const QByteArray &spine="<itemref idref='c'/>", const QByteArray &meta=
{})
{
    return "<package xmlns='http://www.idpf.org/2007/opf'><metadata>"+meta+"</metadata><manifest>"+manifest+"</manifest><spine>"+spine+"</spine></package>";
}
static const QByteArray chapter="<item id='c' href='text/c.xhtml' media-type='application/xhtml+xml' media-overlay='mo'/>";
static const QByteArray overlay="<item id='mo' href='mo/c.smil' media-type='application/smil+xml'/>";
class tst_media_overlay_package : public QObject
{
    Q_OBJECT
private slots:
    void cases_data()
    {
        QTest::addColumn<QByteArray>("manifest");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("warning");
        QTest::newRow("one-overlay") << (chapter+overlay) << QString("mo/c.smil") << QString();
        QTest::newRow("no-overlay") << QByteArray("<item id='c' href='c.xhtml' media-type='application/xhtml+xml'/>") << QString() << QString();
        QTest::newRow("dangling") << chapter << QString() << QString("missing-overlay");
        QTest::newRow("wrong-type") << (chapter+"<item id='mo' href='a' media-type='audio/mpeg'/>") << QString() << QString("wrong-overlay-type");
    }
    void cases()
    {
        QFETCH(QByteArray,manifest);
        QFETCH(QString,expected);
        QFETCH(QString,warning);
        auto r=MediaOverlayPackage::parse(package(manifest),"book.opf");
        QVERIFY(r.ok);
        QCOMPARE(r.spine.size(),1);
        QCOMPARE(r.spine[0].overlayPath,expected);
        if(warning.isEmpty()) QVERIFY(r.warnings.isEmpty());
        else
        {
            QCOMPARE(r.warnings.size(),1);
            QCOMPARE(r.warnings[0].code,warning);
        }
    }
    void subfolder()
    {
        auto r=MediaOverlayPackage::parse(package("<item id='c' href='../text/c.xhtml' media-type='application/xhtml+xml' media-overlay='mo'/><item id='mo' href='./mo/c.smil' media-type='application/smil+xml'/>"),"OPS/package/book.opf");
        QVERIFY(r.ok);
        QCOMPARE(r.spine.size(),1);
        QCOMPARE(r.spine[0].path,QString("OPS/text/c.xhtml"));
        QCOMPARE(r.spine[0].overlayPath,QString("OPS/package/mo/c.smil"));
        QCOMPARE(r.overlays.value("OPS/text/c.xhtml"),QString("OPS/package/mo/c.smil"));
    }
    void percentEncoded()
    {
        auto r=MediaOverlayPackage::parse(package("<item id='c' href='text/a%20b%C3%A9.xhtml'/>"),"book.opf");
        QVERIFY(r.ok);
        QCOMPARE(r.spine.size(),1);
        QCOMPARE(r.spine[0].path,QString::fromUtf8("text/a b\xC3\xA9.xhtml"));
    }
    void unsafe_data()
    {
        QTest::addColumn<QString>("path");
        for(const auto &s: QStringList
        {
            "../out","%2e%2e/out","/out","https://example.org/a","C:/out","a\\b","a%00b","%ZZ","a?x","a#f","%ff"
        }) QTest::newRow(qPrintable(s)) << s;
    }
    void unsafe()
    {
        QFETCH(QString,path);
        auto xml=package("<item id='c' href='"+path.toUtf8()+"'/>");
        auto r=MediaOverlayPackage::parse(xml,"book.opf");
        QVERIFY(r.ok);
        QVERIFY(r.spine.isEmpty());
        QVERIFY(!r.warnings.isEmpty());
        QCOMPARE(r.warnings[0].code,QString("unsafe-path"));
    }
    void metadata()
    {
        auto r=MediaOverlayPackage::parse(package(chapter+overlay,"<itemref idref='c'/>","<meta property='media:duration'>4.5s</meta><meta property='media:duration' refines='#mo'>2s</meta><meta property='media:active-class'>spoken</meta><meta property='media:playback-active-class'>playing</meta>"),"book.opf");
        QVERIFY(r.ok);
        QCOMPARE(r.durationMs.value_or(-1),qint64(4500));
        QCOMPARE(r.overlayDurations.value("mo"),qint64(2000));
        QCOMPARE(r.activeClass,QString("spoken"));
        QCOMPARE(r.playbackActiveClass,QString("playing"));
    }
    void readingOrderAndPlainBook()
    {
        auto r=MediaOverlayPackage::parse(package("<item id='b' href='b.xhtml'/><item id='c' href='c.xhtml'/>","<itemref idref='c'/><itemref idref='b'/>"),"book.opf");
        QVERIFY(r.ok);
        QVERIFY(r.overlays.isEmpty());
        QCOMPARE(r.spine.size(),2);
        QCOMPARE(r.spine[0].id,QString("c"));
        QCOMPARE(r.spine[1].id,QString("b"));
    }
    void malformed()
    {
        auto r=MediaOverlayPackage::parse("<package xmlns='http://www.idpf.org/2007/opf'><manifest>","book.opf");
        QVERIFY(!r.ok);
        QVERIFY(r.errorLine>0);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.manifest.isEmpty());
    }
    void namespaces()
    {
        QVERIFY(!MediaOverlayPackage::parse("<package xmlns='wrong'/>","book.opf").ok);
        auto r=MediaOverlayPackage::parse(package("<item xmlns='wrong' id='c' href='c'/>",""),"book.opf");
        QVERIFY(r.ok);
        QVERIFY(r.manifest.isEmpty());
    }
};
QTEST_APPLESS_MAIN(tst_media_overlay_package)
#include "tst_media_overlay_package.moc"
