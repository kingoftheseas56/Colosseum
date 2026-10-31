// Arc 74-D: a real ProgressStore file + the real ComicReaderShell QObject.
// Hermetic Qt test; all writes are to QTemporaryDir on D:, never the user's profile.
#include "ProgressStore.h"

#include <QGuiApplication>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QMetaObject>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

namespace {
const QString kSeries = QStringLiteral("mal:1");
const QString kChapter = QStringLiteral("tankoyomi:pt:source:chapter-47");

QVariantMap record(const QString &kind, const QString &chapter) {
    const QVariantMap resume{
        {QStringLiteral("chapterId"), chapter},
        {QStringLiteral("page"), 4},
        {QStringLiteral("maxSeen"), 4},
        {QStringLiteral("pageFraction"), 0.37},
        {QStringLiteral("scrollFrac"), 0.81},
        {QStringLiteral("finished"), false}};
    return {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), kSeries},
        {QStringLiteral("title"), QStringLiteral("Portuguese manga")},
        {QStringLiteral("caption"), QStringLiteral("Portuguese manga")},
        {QStringLiteral("sub"), QStringLiteral("Chapter 47")},
        {QStringLiteral("progress"), 0.8},
        {QStringLiteral("resume"), resume}};
}

class Pages final : public QObject {
    Q_OBJECT
public:
    QString readyChapter = kChapter;
    bool available = true;
    Q_INVOKABLE QVariantList localPages(const QString &chapter) const {
        if (!available || chapter != readyChapter) return {};
        QVariantList result;
        for (int i = 0; i < 5; ++i) {
            result.push_back(QVariantMap{
                {QStringLiteral("index"), i},
                {QStringLiteral("url"),
                 QStringLiteral("file:///nonexistent/arc74d/p%1.png").arg(i)},
                {QStringLiteral("group"), -1}});
        }
        return result;
    }
    Q_INVOKABLE QVariantMap statusOf(const QString &chapter) const {
        return {{QStringLiteral("state"),
                 localPages(chapter).isEmpty()
                 ? QStringLiteral("none") : QStringLiteral("done")}};
    }
signals:
    void finished(const QString &id);
    void failed(const QString &id, const QString &reason);
    void removed(const QString &id);
};

class SeriesRecords final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString all READ all WRITE setAll NOTIFY changed)
public:
    QString all() const { return m_all; }
    void setAll(const QString &str) { m_all=str; emit changed(); }
signals:
    void changed();
private:
    QString m_all = QStringLiteral(
        R"({"mal:1":{"layout":"long_strip"}})");
};

bool run(QQmlEngine &engine, QObject *shell, const QString &expr,
         QString *error = nullptr) {
    QQmlExpression evaluated(QQmlEngine::contextForObject(shell),shell,expr);
    evaluated.evaluate();
    if (evaluated.hasError()) {
        if (error) *error=evaluated.error().toString();
        return false;
    }
    return true;
}
} // namespace

class tst_arc74d_progress_resume : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void persistsExactChapterPageFractionAndNamespace();
    void rebuiltQmlReaderReadsRealPersistedRecord();
    void survivesIndependentProcessRestart();
};

void tst_arc74d_progress_resume::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("Arc74DIsolatedTest"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("test.invalid"));
    QCoreApplication::setApplicationName(QStringLiteral("Arc74DProgress"));
    QStandardPaths::setTestModeEnabled(true);
}

void tst_arc74d_progress_resume::persistsExactChapterPageFractionAndNamespace() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path=temporary.filePath(QStringLiteral("progress.ini"));
    {
        ProgressStore first(path);
        QVERIFY(first.healthy());
        first.record(record(QStringLiteral("manga"),kChapter));
        first.record(record(QStringLiteral("tankoban"),QStringLiteral("tankoban:mal:1:vol-5")));
        first.flush();
    }
    {
        ProgressStore restored(path);
        QVERIFY(restored.healthy());
        const auto chapter=restored.get(QStringLiteral("manga"),kSeries);
        const auto volume=restored.get(QStringLiteral("tankoban"),kSeries);
        QCOMPARE(chapter.value(QStringLiteral("resume")).toMap()
                     .value(QStringLiteral("chapterId")).toString(),kChapter);
        QCOMPARE(chapter.value(QStringLiteral("resume")).toMap()
                     .value(QStringLiteral("page")).toInt(),4);
        QVERIFY(qAbs(chapter.value(QStringLiteral("resume")).toMap()
                     .value(QStringLiteral("pageFraction")).toDouble()-0.37)<1e-7);
        QVERIFY(qAbs(chapter.value(QStringLiteral("resume")).toMap()
                     .value(QStringLiteral("scrollFrac")).toDouble()-0.81)<1e-7);
        QCOMPARE(volume.value(QStringLiteral("resume")).toMap()
                     .value(QStringLiteral("chapterId")).toString(),
                 QStringLiteral("tankoban:mal:1:vol-5"));
        restored.flush();
    }
}

void tst_arc74d_progress_resume::rebuiltQmlReaderReadsRealPersistedRecord() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path=temporary.filePath(QStringLiteral("progress.ini"));
    {
        ProgressStore previous(path);
        previous.record(record(QStringLiteral("manga"),kChapter));
        previous.flush();
    }

    {
        ProgressStore fresh(path);
        QVERIFY(fresh.healthy());
        Pages pages;
        SeriesRecords prefs;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(
            QStringLiteral("Progress"),&fresh);
        engine.rootContext()->setContextProperty(
            QStringLiteral("Downloads"),&pages);
        QQmlComponent component(&engine,QUrl::fromLocalFile(
            QDir(QString::fromUtf8(ARC74D_QML_SOURCE_DIR))
                .filePath(QStringLiteral("comicreader/ComicReaderShell.qml"))));
        QVERIFY2(component.status()==QQmlComponent::Ready,
                 qPrintable(component.errorString()));

        QObject *instance=component.beginCreate(engine.rootContext());
        QVERIFY2(instance,qPrintable(component.errorString()));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("seriesId"),kSeries));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("chapterId"),kChapter));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("chapterLabel"),
                                   QStringLiteral("Chapter 47")));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("entryKind"),
                                   QStringLiteral("manga")));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("pageStore"),
                                   QVariant::fromValue(static_cast<QObject*>(&pages))));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("seriesRecords"),
                                   QVariant::fromValue(static_cast<QObject*>(&prefs))));
        QVERIFY(QQmlProperty::write(instance,QStringLiteral("progress"),
                                   QVariant::fromValue(static_cast<QObject*>(&fresh))));
        component.completeCreate();

        QCOMPARE(instance->property("curChapterId").toString(),kChapter);
        QCOMPARE(instance->property("currentPage").toInt(),4);
        QVERIFY(qAbs(instance->property("_pendingPageFraction").toDouble()-0.37)<1e-7);
        QVERIFY(instance->property("_stripRestorePending").toBool());
        QVERIFY(!instance->property("readerReady").toBool());

        QString error;
        QVERIFY2(run(engine,instance,QStringLiteral("_onPresented(1,0)"),&error),
                 qPrintable(error));
        QVERIFY2(run(engine,instance,QStringLiteral("goMinimize()"),&error),
                 qPrintable(error));
        QCOMPARE(fresh.get(QStringLiteral("manga"),kSeries)
                     .value(QStringLiteral("resume")).toMap()
                     .value(QStringLiteral("page")).toInt(),4);
        delete instance;
        fresh.flush();
    }

    ProgressStore afterClose(path);
    QVERIFY(afterClose.healthy());
    const auto resume=afterClose.get(QStringLiteral("manga"),kSeries)
                          .value(QStringLiteral("resume")).toMap();
    QCOMPARE(resume.value(QStringLiteral("chapterId")).toString(),kChapter);
    QCOMPARE(resume.value(QStringLiteral("page")).toInt(),4);
    QVERIFY(qAbs(resume.value(QStringLiteral("pageFraction")).toDouble()-0.37)<1e-7);
    QCOMPARE(resume.value(QStringLiteral("scrollFrac")).toDouble(),0.81);
}

// Launch this binary twice as independent processes (seed -> reconstruct).
// Keeping helpers in the same binary avoids inventing a second Progress writer.
static int runIsolatedChild(const QString &phase, const QString &path) {
    if (phase == QStringLiteral("--arc74-seed")) {
        ProgressStore seed(path);
        if (!seed.healthy()) return 21;
        seed.record(record(QStringLiteral("manga"), kChapter));
        seed.record(record(QStringLiteral("tankoban"),
                           QStringLiteral("tankoban:mal:1:vol-5")));
        seed.flush();
        return seed.healthy() ? 0 : 22;
    }
    if (phase != QStringLiteral("--arc74-verify")) return 23;
    ProgressStore restored(path);
    if (!restored.healthy()) return 24;
    const QVariantMap manga=restored.get(QStringLiteral("manga"),kSeries);
    const QVariantMap prior=manga.value(QStringLiteral("resume")).toMap();
    if (prior.value(QStringLiteral("chapterId")).toString()!=kChapter
        || prior.value(QStringLiteral("page")).toInt()!=4
        || qAbs(prior.value(QStringLiteral("pageFraction")).toDouble()-0.37)>1e-7)
        return 25;
    const QVariantMap vol=restored.get(QStringLiteral("tankoban"),kSeries)
                              .value(QStringLiteral("resume")).toMap();
    if (vol.value(QStringLiteral("chapterId")).toString()
        != QStringLiteral("tankoban:mal:1:vol-5")) return 26;

    Pages pages;
    SeriesRecords prefs;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Progress"), &restored);
    engine.rootContext()->setContextProperty(QStringLiteral("Downloads"), &pages);
    QQmlComponent component(&engine,QUrl::fromLocalFile(
        QDir(QString::fromUtf8(ARC74D_QML_SOURCE_DIR))
            .filePath(QStringLiteral("comicreader/ComicReaderShell.qml"))));
    if (component.status()!=QQmlComponent::Ready) {
        qCritical().noquote() << component.errorString();
        return 27;
    }
    QObject *reader=component.beginCreate(engine.rootContext());
    if (!reader) return 28;
    const auto write=[reader](const QString &key,const QVariant &value) {
        return QQmlProperty::write(reader,key,value);
    };
    if (!write(QStringLiteral("seriesId"),kSeries)
        || !write(QStringLiteral("chapterId"),kChapter)
        || !write(QStringLiteral("entryKind"),QStringLiteral("manga"))
        || !write(QStringLiteral("pageStore"),
                  QVariant::fromValue(static_cast<QObject*>(&pages)))
        || !write(QStringLiteral("seriesRecords"),
                  QVariant::fromValue(static_cast<QObject*>(&prefs)))
        || !write(QStringLiteral("progress"),
                  QVariant::fromValue(static_cast<QObject*>(&restored)))) {
        delete reader;
        return 29;
    }
    component.completeCreate();
    const bool loaded=reader->property("curChapterId").toString()==kChapter
        && reader->property("currentPage").toInt()==4
        && qAbs(reader->property("_pendingPageFraction").toDouble()-0.37)<1e-7
        && reader->property("_stripRestorePending").toBool();
    QString error;
    const bool safe=run(engine,reader,QStringLiteral("_onPresented(1,0)"),&error)
        && run(engine,reader,QStringLiteral("goMinimize()"),&error);
    delete reader;
    restored.flush();
    const QVariantMap after=restored.get(QStringLiteral("manga"),kSeries)
                                .value(QStringLiteral("resume")).toMap();
    if (!loaded) return 30;
    if (!safe) { qCritical().noquote() << error; return 31; }
    return after.value(QStringLiteral("chapterId")).toString()==kChapter
        && after.value(QStringLiteral("page")).toInt()==4
        && qAbs(after.value(QStringLiteral("pageFraction")).toDouble()-0.37)<1e-7
        ? 0 : 32;
}

void tst_arc74d_progress_resume::survivesIndependentProcessRestart() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path=temp.filePath(QStringLiteral("progress.ini"));
    for (const QString phase : {QStringLiteral("--arc74-seed"),
                                QStringLiteral("--arc74-verify")}) {
        QProcess child;
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment();
        if (!env.contains(QStringLiteral("QT_QPA_PLATFORM")))
            env.insert(QStringLiteral("QT_QPA_PLATFORM"),QStringLiteral("offscreen"));
        child.setProcessEnvironment(env);
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({phase,path});
        child.start();
        QVERIFY2(child.waitForStarted(5000),qPrintable(child.errorString()));
        QVERIFY2(child.waitForFinished(20000),qPrintable(child.errorString()));
        QCOMPARE(child.exitStatus(),QProcess::NormalExit);
        QVERIFY2(child.exitCode()==0,
                 qPrintable(phase + QStringLiteral(" exited ")
                     + QString::number(child.exitCode()) + QStringLiteral(": ")
                     + QString::fromUtf8(child.readAllStandardError())));
    }
    ProgressStore finalReadback(path);
    QVERIFY(finalReadback.healthy());
    const auto resume=finalReadback.get(QStringLiteral("manga"),kSeries)
        .value(QStringLiteral("resume")).toMap();
    QCOMPARE(resume.value(QStringLiteral("chapterId")).toString(),kChapter);
    QCOMPARE(resume.value(QStringLiteral("page")).toInt(),4);
    QVERIFY(qAbs(resume.value(QStringLiteral("pageFraction")).toDouble()-0.37)<1e-7);
}

int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arc74DIsolatedTest"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("test.invalid"));
    QCoreApplication::setApplicationName(QStringLiteral("Arc74DProgress"));
    QStandardPaths::setTestModeEnabled(true);
    const QStringList args=QCoreApplication::arguments();
    if (args.size()==3 && args[1].startsWith(QStringLiteral("--arc74-")))
        return runIsolatedChild(args[1],args[2]);
    tst_arc74d_progress_resume suite;
    return QTest::qExec(&suite,argc,argv);
}
#include "tst_arc74d_progress_resume.moc"
