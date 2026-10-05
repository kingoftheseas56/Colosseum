// Proves real archive loading, Unicode filenames, normalized resource access and named failures.
// Negative control performed and reverted: bypass href resolution in the loader; canonicalPathsAndSamples goes red.
#include "reader2/mediaoverlay/EpubOverlayLoader.h"
#include "third_party/miniz/miniz.h"
#include <QtTest>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QFile>
using namespace MediaOverlay;
static QString fixtures()
{
    return QString::fromUtf8(MEDIAOVERLAY_FIXTURES_DIR);
}
static QString epub()
{
    return fixtures()+"/tiny-narrated.epub";
}
static QByteArray bytes(const QString &path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) return
    {};
    return f.readAll();
}
static bool variant(const QString &path, const QStringList &omit=
{}, const QMap<QString,QByteArray> &replace=
{})
{
    mz_zip_archive zip
    {};
    const auto utf8=QDir::toNativeSeparators(path).toUtf8();
    if(!mz_zip_writer_init_file(&zip,utf8.constData(),0)) return false;
    const QString root=fixtures()+"/tiny-narrated-src";
    QDirIterator it(root,QDir::Files,QDirIterator::Subdirectories);
    bool ok=true;
    while(it.hasNext() && ok)
    {
        const QString source=it.next(),name=QDir(root).relativeFilePath(source);
        if(omit.contains(name)) continue;
        const auto data=replace.contains(name)?replace.value(name):bytes(source);
        const auto key=name.toUtf8();
        ok=mz_zip_writer_add_mem(&zip,key.constData(),data.constData(),size_t(data.size()),MZ_DEFAULT_COMPRESSION);
    }
    if(ok) ok=mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return ok;
}
class tst_epub_overlay_loader : public QObject
{
    Q_OBJECT
private slots:
    void fixtureOrder()
    {
        auto r=EpubOverlayLoader::load(epub());
        QVERIFY2(r.ok,qPrintable(r.message));
        QVERIFY(r.hasOverlays);
        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(r.timeline.cues().size(),4);
        const auto &c=r.timeline.cues();
        QCOMPARE(c[0].fragmentId,QString("s1"));
        QCOMPARE(c[1].fragmentId,QString("s2"));
        QCOMPARE(c[2].fragmentId,QString("s1"));
        QCOMPARE(c[3].fragmentId,QString("s2"));
        QVERIFY(!r.timeline.firstCueOf("OPS/text/ch2.xhtml"));
        auto next=r.timeline.next(c[1]);
        QVERIFY(next);
        QCOMPARE(next->textPath,QString("OPS/text/ch3.xhtml"));
        QCOMPARE(c[0].structurePath,(QStringList
        {
            "chapter1","section1"
        }));
        QVERIFY(c[0].semanticTypes.contains("chapter"));
        QCOMPARE(c[0].sourceTag,QString("epub-media-overlay"));
        QCOMPARE(r.durationMs.value_or(-1),qint64(4000));
        QCOMPARE(r.overlayDurations.value("mo1"),qint64(2000));
        QCOMPARE(r.activeClass,QString("spoken"));
        QCOMPARE(r.playbackActiveClass,QString("narrating"));
    }
    void canonicalPathsAndSamples()
    {
        auto r=EpubOverlayLoader::load(epub());
        QVERIFY(r.ok);
        QCOMPARE(r.timeline.cues().size(),4);
        for(const auto &c:r.timeline.cues())
        {
            QVERIFY2(c.textPath.startsWith("OPS/text/"),qPrintable(c.textPath));
            QVERIFY2(c.audioPath.startsWith("OPS/audio/"),qPrintable(c.audioPath));
            QVERIFY(EpubOverlayLoader::readResource(epub(),c.textPath).ok);
            QVERIFY(EpubOverlayLoader::readResource(epub(),c.audioPath).ok);
        }
        for(int i:
        {
            0,1,3
        })
        {
            const auto &c=r.timeline.cues()[i];
            auto a=r.timeline.cueAt(c.audioPath,c.beginMs+500),b=r.timeline.cueFor(c.textPath,c.fragmentId);
            QVERIFY(a);
            QVERIFY(b);
            QCOMPARE(a->identity,b->identity);
        }
    }
    void resources()
    {
        auto r=EpubOverlayLoader::load(epub());
        QVERIFY(r.ok);
        QCOMPARE(r.audioResources.size(),2);
        for(const auto &a:r.audioResources) QCOMPARE(a.size,quint64(bytes(fixtures()+"/tiny-narrated-src/"+a.path).size()));
        const auto resource=EpubOverlayLoader::readResource(epub(),"OPS/text/ch1.xhtml");
        QVERIFY(resource.ok);
        QCOMPARE(resource.bytes,bytes(fixtures()+"/tiny-narrated-src/OPS/text/ch1.xhtml"));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto output=dir.filePath(QString::fromUtf8("\xC3\xA9-chapter.xhtml"));
        auto extracted=EpubOverlayLoader::extractResource(epub(),"OPS/text/ch1.xhtml",output);
        QVERIFY(extracted.ok);
        QCOMPARE(bytes(output),resource.bytes);
        QVERIFY(!EpubOverlayLoader::extractResource(epub(),"OPS/text/ch1.xhtml",dir.filePath("missing/child")).ok);
    }
    void plainBook()
    {
        const auto path=QDir(fixtures()).filePath("../vault/real/tiny-book.epub");
        auto r=EpubOverlayLoader::load(path);
        QVERIFY2(r.ok,qPrintable(r.message));
        QVERIFY(!r.hasOverlays);
        QVERIFY(r.timeline.cues().isEmpty());
    }
    void unicodeName()
    {
        auto r=EpubOverlayLoader::load(fixtures()+QString::fromUtf8("/tiny-narrated-\xC3\xA9.epub"));
        QVERIFY2(r.ok,qPrintable(r.message));
        QCOMPARE(r.timeline.cues().size(),4);
    }
    void errors_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("file-missing")<<0;
        QTest::newRow("not-zip")<<1;
        QTest::newRow("container-missing")<<2;
        QTest::newRow("package-missing")<<3;
        QTest::newRow("package-malformed")<<4;
        QTest::newRow("container-malformed")<<5;
    }
    void errors()
    {
        QFETCH(int,kind);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto path=dir.filePath("case.epub");
        Error expected=Error::None;
        if(kind==0) expected=Error::FileMissing;
        if(kind==1)
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("not zip");
            f.close();
            expected=Error::NotZip;
        }
        if(kind==2)
        {
            QVERIFY(variant(path,
            {
                "META-INF/container.xml"
            }));
            expected=Error::ContainerMissing;
        }
        if(kind==3)
        {
            QVERIFY(variant(path,
            {
                "OPS/book.opf"
            }));
            expected=Error::PackageMissing;
        }
        if(kind==4)
        {
            QVERIFY(variant(path,
            {},
            {
                {
                    "OPS/book.opf","<package"
                }
            }));
            expected=Error::PackageMalformed;
        }
        if(kind==5)
        {
            QVERIFY(variant(path,
            {},
            {
                {
                    "META-INF/container.xml","<container"
                }
            }));
            expected=Error::ContainerMalformed;
        }
        auto r=EpubOverlayLoader::load(path);
        QVERIFY(!r.ok);
        QCOMPARE(int(r.error),int(expected));
        QVERIFY(!errorName(r.error).isEmpty());
        QVERIFY(!r.message.isEmpty());
        QVERIFY(r.timeline.cues().isEmpty());
    }
    void partialOverlay_data()
    {
        QTest::addColumn<bool>("malformed");
        QTest::newRow("missing")<<false;
        QTest::newRow("malformed")<<true;
    }
    void partialOverlay()
    {
        QFETCH(bool,malformed);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto path=dir.filePath("partial.epub");
        QVERIFY(variant(path,malformed?QStringList
        {}:QStringList
        {
            "OPS/overlays/ch1.smil"
        },malformed?QMap<QString,QByteArray>
        {
            {
                "OPS/overlays/ch1.smil","<smil"
            }
        }:QMap<QString,QByteArray>
        {}));
        auto r=EpubOverlayLoader::load(path);
        QVERIFY(r.ok);
        QVERIFY(r.hasOverlays);
        QCOMPARE(r.timeline.cues().size(),2);
        QCOMPARE(r.timeline.cues()[0].textPath,QString("OPS/text/ch3.xhtml"));
        QVERIFY(!r.warnings.isEmpty());
        QCOMPARE(r.warnings[0].source,QString("OPS/overlays/ch1.smil"));
        QCOMPARE(r.warnings[0].code,malformed?QString("overlay-malformed"):QString("overlay-missing"));
    }
    void warningSource()
    {
        QTemporaryDir dir;
        auto path=dir.filePath("bad-clip.epub");
        auto xml=bytes(fixtures()+"/tiny-narrated-src/OPS/overlays/ch1.smil");
        xml.replace("clipBegin=\"0s\"","clipBegin=\"9s\"");
        QVERIFY(variant(path,
        {},
        {
            {
                "OPS/overlays/ch1.smil",xml
            }
        }));
        auto r=EpubOverlayLoader::load(path);
        QVERIFY(r.ok);
        QCOMPARE(r.timeline.cues().size(),3);
        QVERIFY(!r.warnings.isEmpty());
        QCOMPARE(r.warnings[0].code,QString("invalid-clip"));
        QCOMPARE(r.warnings[0].source,QString("OPS/overlays/ch1.smil"));
        QVERIFY(r.warnings[0].position>0);
    }
    void sharedOverlay()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto path=dir.filePath("shared.epub");
        auto opf=bytes(fixtures()+"/tiny-narrated-src/OPS/book.opf");
        opf.replace("media-overlay=\"mo3\"","media-overlay=\"mo1\"");
        auto xml=bytes(fixtures()+"/tiny-narrated-src/OPS/overlays/ch1.smil");
        xml.replace("</body>","<par><text src='../text/ch3.xhtml#s1'/><audio src='../audio/three.mp3' clipBegin='0' clipEnd='1'/></par></body>");
        QVERIFY(variant(path,
        {},
        {
            {
                "OPS/book.opf",opf
            },
            {
                "OPS/overlays/ch1.smil",xml
            }
        }));
        auto r=EpubOverlayLoader::load(path);
        QVERIFY(r.ok);
        QCOMPARE(r.timeline.cues().size(),3);
        QCOMPARE(r.timeline.cues()[0].textPath,QString("OPS/text/ch1.xhtml"));
        QCOMPARE(r.timeline.cues()[2].textPath,QString("OPS/text/ch3.xhtml"));
    }
    void packageWarnings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto path=dir.filePath("dangling.epub");
        auto opf=bytes(fixtures()+"/tiny-narrated-src/OPS/book.opf");
        opf.replace("media-overlay=\"mo1\"","media-overlay=\"missing\"");
        QVERIFY(variant(path,
        {},
        {
            {
                "OPS/book.opf",opf
            }
        }));
        auto r=EpubOverlayLoader::load(path);
        QVERIFY(r.ok);
        QCOMPARE(r.timeline.cues().size(),2);
        QVERIFY(!r.warnings.isEmpty());
        QCOMPARE(r.warnings[0].code,QString("missing-overlay"));
        QCOMPARE(r.warnings[0].source,QString("OPS/book.opf"));
    }
    void refusesSourceOverwrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto path=dir.filePath("source.epub");
        QVERIFY(QFile::copy(epub(),path));
        auto original=bytes(path);
        auto r=EpubOverlayLoader::extractResource(path,"OPS/text/ch1.xhtml",path);
        QVERIFY(!r.ok);
        QCOMPARE(int(r.error),int(Error::WriteFailed));
        QCOMPARE(bytes(path),original);
    }
    void unsafePaths_data()
    {
        QTest::addColumn<QString>("path");
        for(const auto &p:QStringList
        {
            "../outside","OPS/../../outside","/etc/passwd","C:/book","OPS\\text\\ch1.xhtml","absent"
        }) QTest::newRow(qPrintable(p))<<p;
    }
    void unsafePaths()
    {
        QFETCH(QString,path);
        QTemporaryDir dir;
        const auto output=dir.filePath("untouched");
        QFile f(output);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("keep");
        f.close();
        auto r=EpubOverlayLoader::readResource(epub(),path);
        QVERIFY(!r.ok);
        QVERIFY(!EpubOverlayLoader::extractResource(epub(),path,output).ok);
        QCOMPARE(bytes(output),QByteArray("keep"));
    }
};
QTEST_APPLESS_MAIN(tst_epub_overlay_loader)
#include "tst_epub_overlay_loader.moc"
