#include "account/CollectionShareLink.h"
#include <QtTest>
using namespace Colosseum::Collections;
class Checks : public QObject {
    Q_OBJECT
    const QString id = QString(43, QLatin1Char('a'));
    const QUrl web{QStringLiteral("https://getcolosseum.world")};
    const QUrl legacy{QStringLiteral("https://service.test")};
private slots:
    void parsesCanonicalWwwLegacyAndAppLinks_data() {
        QTest::addColumn<QString>("prefix"); QTest::addColumn<QString>("suffix");
        for (const auto &prefix : {"https://getcolosseum.world/c/", "https://www.getcolosseum.world/c/", "https://service.test/collections/s/", "colosseum://collection/"}) {
            QTest::newRow(prefix) << QString(prefix) << QString();
            if (!QString(prefix).startsWith("colosseum:")) QTest::newRow(qPrintable(QString(prefix)+"slash")) << QString(prefix) << QString("/");
        }
    }
    void parsesCanonicalWwwLegacyAndAppLinks() {
        QFETCH(QString,prefix); QFETCH(QString,suffix);
        const QString text=prefix+id+suffix;
        QCOMPARE(shareIdFromUrl(text,web,legacy), std::optional<QString>(id));
        QCOMPARE(shareIdFromUrl(QUrl(text),web,legacy), std::optional<QString>(id));
    }
    void rejectsUnsafeCollectionLinks_data() {
        QTest::addColumn<QString>("text");
        const QString good="https://getcolosseum.world/c/"+id;
        const QStringList bad={"https://user:secret@getcolosseum.world/c/"+id,"https://@getcolosseum.world/c/"+id,
            "https://getcolosseum.world:443/c/"+id,"https://getcolosseum.world:8443/c/"+id,
            "https://getcolosseum.world.evil/c/"+id,"https://getcolosseurn.world/c/"+id,
            good+"?",good+"#",good+"?secret=x",good+"#x",good+"/extra",good+"//",
            "http://getcolosseum.world/c/"+id,"file:///c/"+id,"colosseum://collection/"+id+"/",
            "colosseum://collection:9/"+id,"colosseum://user@collection/"+id,
            "https://getcolosseum.world/c/"+QString(42,'a'),"https://getcolosseum.world/c/"+QString(44,'a'),
            "https://getcolosseum.world/c/"+QString(42,'a')+QChar(0x00e9),
            "https://getcolosseum.world/c/"+id+"%2F", "https://getcolosseum.world/c/%2F"+id,
            good+"\\",good+"\n",good+"\t",good+"\"",good+"'",QString(2049,'x'),
            "https://service.test:443/collections/s/"+id,"https://service.test/c/"+id};
        int row=0; for(const auto &text:bad) QTest::newRow(qPrintable(QString::number(row++))) << text;
    }
    void rejectsUnsafeCollectionLinks() {
        QFETCH(QString,text); QVERIFY(!shareIdFromUrl(text,web,legacy));
        QVERIFY(!shareIdFromUrl(QUrl(text,QUrl::StrictMode),web,legacy));
    }
    void rejectsEscapesBeforeQtNormalization() {
        const QString raw="https://getcolosseum.world/c/%41"+QString(42,'a');
        QVERIFY(!shareIdFromUrl(raw,web,legacy));
        // QUrl has discarded the original %41 spelling. Validate its normalized identity honestly.
        const QString normalized="A"+QString(42,'a');
        QCOMPARE(shareIdFromUrl(QUrl(raw,QUrl::StrictMode),web,legacy),std::optional<QString>(normalized));
        QVERIFY(!shareIdFromUrl("https://getcolosseum.world/c/%2f"+id,web,legacy));
    }
    void buildersValidateIdsAndOrigins() {
        QCOMPARE(publicShareLink(id,web).toString(),"https://getcolosseum.world/c/"+id);
        QCOMPARE(applicationShareLink(id).toString(),"colosseum://collection/"+id);
        for(const auto &bad:QStringList{QString(),QString(42,'a'),QString(44,'a'),QString(42,'a')+"/",QString(42,'a')+"%"}) {
            QVERIFY(publicShareLink(bad,web).isEmpty()); QVERIFY(applicationShareLink(bad).isEmpty());
        }
        const QUrl fixture("http://127.0.0.1:12345");
        QCOMPARE(shareIdFromUrl("http://127.0.0.1:12345/c/"+id,fixture,legacy),std::optional<QString>(id));
        QVERIFY(!shareIdFromUrl("http://127.0.0.1:12346/c/"+id,fixture,legacy));
        QVERIFY(publicShareLink(id,QUrl("https://user@site.test")).isEmpty());
        QVERIFY(publicShareLink(id,QUrl("https://site.test/path")).isEmpty());
        QVERIFY(publicShareLink(id,QUrl("ftp://site.test")).isEmpty());
        const QString mixed="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-abcde";
        QCOMPARE(mixed.size(),43); QVERIFY(!applicationShareLink(mixed).isEmpty());
    }
};
QTEST_GUILESS_MAIN(Checks)
#include "collection_share_link_test.moc"
