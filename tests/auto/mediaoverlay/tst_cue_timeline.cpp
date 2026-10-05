// Proves format-neutral cue identity, range boundaries, gaps and reading-order traversal.
// Negative control performed and reverted: inclusive end in cueAt must fail only endBoundary.
#include "reader2/mediaoverlay/CueTimeline.h"
#include <QtTest>
using namespace MediaOverlay;
static Cue cue(QString doc, QString fragment, QString audio, qint64 begin, std::optional<qint64> end)
{
    Cue c;
    c.textPath=doc;
    c.fragmentId=fragment;
    c.audioPath=audio;
    c.beginMs=begin;
    c.endMs=end;
    return c;
}
static CueTimeline book()
{
    CueTimeline t;
    t.append(cue("one","a","a.mp3",100,200));
    t.append(cue("one","b","a.mp3",300,400));
    t.append(cue("three","a","b.mp3",0,100));
    return t;
}
class tst_cue_timeline : public QObject
{
    Q_OBJECT
private slots:
    void lookup_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<int>("index");
        QTest::newRow("begin")<<qint64(100)<<0;
        QTest::newRow("middle")<<qint64(150)<<0;
        QTest::newRow("last-ms")<<qint64(199)<<0;
        QTest::newRow("gap")<<qint64(250)<<0;
        QTest::newRow("before")<<qint64(99)<<-1;
        QTest::newRow("after")<<qint64(500)<<-1;
    }
    void lookup()
    {
        QFETCH(qint64,ms);
        QFETCH(int,index);
        auto t=book();
        auto p=t.cueAt("a.mp3",ms);
        if(index<0) QVERIFY(!p);
        else
        {
            QVERIFY(p);
            QCOMPARE(p->order,qsizetype(index));
        }
    }
    void endBoundary()
    {
        CueTimeline t;
        QVERIFY(t.append(cue("c","a","a",0,100)));
        QVERIFY(t.append(cue("c","b","a",100,200)));
        auto p=t.cueAt("a",100);
        QVERIFY(p);
        QCOMPARE(p->fragmentId,QString("b"));
        QVERIFY(!t.cueAt("a",200));
    }
    void openEnd()
    {
        CueTimeline t;
        QVERIFY(t.append(cue("c","a","a",10,
        {})));
        QVERIFY(t.append(cue("c","b","b",0,50)));
        QVERIFY(t.append(cue("c","c","a",30,
        {})));
        QVERIFY(t.cueAt("a",29));
        QCOMPARE(t.cueAt("a",29)->fragmentId,QString("a"));
        QVERIFY(t.cueAt("a",9999));
        QCOMPARE(t.cueAt("a",9999)->fragmentId,QString("c"));
        QVERIFY(!t.cueAt("a",9));
    }
    void readingOrder()
    {
        auto t=book();
        QCOMPARE(t.cues().size(),3);
        QVERIFY(!t.firstCueOf("two"));
        auto a=t.cueFor("one","b");
        QVERIFY(a);
        auto b=t.next(*a);
        QVERIFY(b);
        QCOMPARE(b->textPath,QString("three"));
        QCOMPARE(t.previous(*b)->identity,a->identity);
        QVERIFY(!t.previous(t.cues().first()));
        QVERIFY(!t.next(t.cues().last()));
        QCOMPARE(t.firstCueOf("one")->fragmentId,QString("a"));
        QCOMPARE(t.cuesOf("a.mp3").size(),2);
        QVERIFY(t.cuesOf("absent").isEmpty());
    }
    void identity()
    {
        auto a=book(),b=book();
        QCOMPARE(a.cues().size(),3);
        for(int i=0;i<3;++i) QCOMPARE(a.cues()[i].identity,b.cues()[i].identity);
        QVERIFY(a.cues()[0].identity!=a.cues()[2].identity);
        QCOMPARE(a.cueFor("three","a")->order,qsizetype(2));
        QVERIFY(!a.cueFor("missing","a"));
    }
    void textOnly()
    {
        CueTimeline t;
        QVERIFY(t.append(cue("c","a","a",0,10)));
        QVERIFY(t.append(cue("c","text","",0,
        {})));
        QVERIFY(t.append(cue("c","b","b",0,10)));
        QVERIFY(t.next(t.cues()[0]));
        QCOMPARE(t.next(t.cues()[0])->fragmentId,QString("text"));
        QCOMPARE(t.previous(t.cues()[2])->fragmentId,QString("text"));
        QVERIFY(!t.cueAt("",1));
        QVERIFY(t.cuesOf("").isEmpty());
        QCOMPARE(t.cueFor("c","text")->order,qsizetype(1));
    }
    void overlap()
    {
        CueTimeline t;
        QVERIFY(t.append(cue("c","a","a",0,200)));
        QVERIFY(t.append(cue("c","b","a",50,100)));
        QCOMPARE(t.cueAt("a",75)->fragmentId,QString("a"));
        QCOMPARE(t.cueAt("a",150)->fragmentId,QString("a"));
    }
    void invalid()
    {
        CueTimeline t;
        QVERIFY(!t.append(cue("c","a","a",-1,20)));
        QVERIFY(!t.append(cue("c","a","a",10,10)));
        QVERIFY(!t.next(Cue
        {}));
        QVERIFY(!t.previous(Cue
        {}));
        QVERIFY(!t.cueAt("a",-1));
    }
};
QTEST_APPLESS_MAIN(tst_cue_timeline)
#include "tst_cue_timeline.moc"
