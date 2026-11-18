#include "engine/VaultConfig.h"
#include "engine/VaultIndex.h"
#include "engine/VaultLibrary.h"
#include "engine/VaultIdentifier.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

namespace {
struct Fixture {
    QTemporaryDir temporary;
    VaultIndex index{temporary.filePath("index.sqlite")};
    VaultConfig config{temporary.path()};
    VaultLibrary library{&index, nullptr, &config, nullptr, temporary.path()};
    QString root = QDir::cleanPath(temporary.filePath("root")).toLower();

    Fixture() {
        QDir().mkpath(root);
        QSignalBlocker blocker(&config);
        config.addRoot(root);
        config.confirmRoot(root);
        for (auto* timer : library.findChildren<QTimer*>()) timer->stop();
    }
    QList<VaultIndex::FileRow> rows(int count) const {
        QList<VaultIndex::FileRow> out;
        for (int i = 0; i < count; ++i) {
            VaultIndex::FileRow row;
            row.id = QStringLiteral("vault:fixture-%1").arg(i);
            row.rootPath = root;
            row.subtreePath = row.groupKey = root + QStringLiteral("/film-%1").arg(i);
            row.groupTitle = row.identityTitle = QStringLiteral("Film %1").arg(i);
            row.kind = QStringLiteral("video");
            row.path = row.subtreePath + QStringLiteral("/film.mkv");
            row.realName = QStringLiteral("film.mkv");
            row.identityId = QStringLiteral("imdb:tt%1").arg(1000000 + i);
            row.identityType = QStringLiteral("movie");
            row.identitySource = QStringLiteral("IMDB");
            row.identityYear = 2020;
            row.identitySynopsis = QStringLiteral("A retained synopsis");
            row.identityRating = 8.2;
            row.identityGenres = QStringLiteral("Drama");
            row.coverRef = QStringLiteral("file:///fixture-inert.jpg");
            row.durationSec = 600;
            row.admissionVerdict = QStringLiteral("Admitted");
            row.size = 10;
            row.mtimeMs = 1000;
            out.append(row);
        }
        return out;
    }
};

QByteArray adapterSource() {
    return QByteArrayLiteral("import QtQuick\nimport \"")
        + QUrl::fromLocalFile(QStringLiteral(COLOSSEUM_QML_SOURCE_DIR)).toString().toUtf8()
        + QByteArrayLiteral("\"\nVaultLibraryAdapter { progressStore: null; homeServers: null }");
}

struct KeyReceiver : QObject {
    bool received = false;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::KeyPress) { received = true; return true; }
        return QObject::event(event);
    }
};

class RevisionRaceSource : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    VaultLibrary& library;
    VaultIndex& index;
    VaultIndex::FileRow revised;
    QString triggerKey;
    bool armed = true;
    RevisionRaceSource(VaultLibrary& library, VaultIndex& index, VaultIndex::FileRow revised)
        : library(library), index(index), revised(std::move(revised)), triggerKey(this->revised.groupKey) {
        connect(&library, &VaultLibrary::changed, this, &RevisionRaceSource::changed);
    }
    int revision() const { return library.revision(); }
    Q_INVOKABLE QVariantList rootsDetail() const { return library.rootsDetail(); }
    Q_INVOKABLE QVariantList series(const QString& kind) const { return library.series(kind); }
    Q_INVOKABLE QVariantList hiddenSeries() const { return library.hiddenSeries(); }
    Q_INVOKABLE QVariantMap admissionById() const { return library.admissionById(); }
    Q_INVOKABLE QVariantList items(const QString& kind, const QString& key) const { return library.items(kind, key); }
    Q_INVOKABLE QVariantMap seasonFactsForShow(const QString& key) const { return library.seasonFactsForShow(key); }
    Q_INVOKABLE QVariantMap presentationDetail(const QString& key) {
        if (armed && key == triggerKey) {
            armed = false;
            if (!index.upsert(revised)) qFatal("Could not publish revision-race fixture");
        }
        return library.presentationDetail(key);
    }
signals:
    void changed();
};

class PublicationObserver : public QObject {
    Q_OBJECT
public:
    QQmlEngine* engine = nullptr;
    QString key;
    bool mixedRevision = false;
    int publications = 0;
public slots:
    void observe() {
        ++publications;
        const auto rows = engine->evaluate("projection.allItems").toVariant().toList();
        for (const auto& value : rows) {
            const auto row = value.toMap();
            if (row.value("seriesKey").toString() == key
                && row.value("runtimeText").toString() == QStringLiteral("20m")
                && row.value("title").toString() != QStringLiteral("Revised final batch"))
                mixedRevision = true;
        }
    }
};
}

class tst_vault_projection : public QObject {
    Q_OBJECT
private slots:
    void qualifiedComicBrowseKeepsFilesystemPathsPhysical() {
        Fixture fixture;
        auto comic = fixture.rows(1).first();
        comic.kind = QStringLiteral("comic"); comic.identitySource = QStringLiteral("COMICS");
        comic.identityId = QStringLiteral("comics:1"); comic.identityType.clear();
        comic.subtreePath = fixture.root + QStringLiteral("/comic-only");
        comic.groupKey = QStringLiteral("comic::") + comic.subtreePath;
        comic.path = comic.subtreePath + QStringLiteral("/issue.cbz");
        QVERIFY(QDir().mkpath(comic.subtreePath));
        QFile file(comic.path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        QVERIFY(fixture.index.publish({comic}));
        const auto browse = fixture.library.browseAt(fixture.root);
        QCOMPARE(browse.size(), 1);
        QCOMPARE(browse.first().toMap().value("key").toString(), comic.groupKey);
        QCOMPARE(browse.first().toMap().value("kind").toString(), QStringLiteral("comic"));
        QCOMPARE(browse.first().toMap().value("path").toString(), comic.path);
        QCOMPARE(fixture.library.recentArrivals(5).first().toMap().value("path").toString(), comic.path);
        const auto detail = fixture.library.browseDetail(comic.groupKey);
        QCOMPARE(detail.value("copies").toList().first().toMap().value("path").toString(), comic.path);

        const QString synthetic = fixture.root + QStringLiteral("/downloads");
        { QSignalBlocker blocker(&fixture.config); fixture.config.addSyntheticRoot(synthetic); }
        comic.rootPath = synthetic;
        comic.subtreePath = synthetic + QStringLiteral("/comics");
        comic.groupKey = QStringLiteral("comic::") + comic.subtreePath;
        comic.path = comic.subtreePath + QStringLiteral("/issue.cbz");
        QVERIFY(QDir().mkpath(comic.subtreePath));
        QFile syntheticFile(comic.path);
        QVERIFY(syntheticFile.open(QIODevice::WriteOnly)); syntheticFile.write("fixture"); syntheticFile.close();
        QVERIFY(fixture.index.publish({comic}));
        const auto syntheticBrowse = fixture.library.browseAt(synthetic);
        QCOMPARE(syntheticBrowse.size(), 1);
        QCOMPARE(syntheticBrowse.first().toMap().value("key").toString(), comic.groupKey);
        QCOMPARE(syntheticBrowse.first().toMap().value("path").toString(), comic.path);
    }

    void reshelvingDeclaredComicUsesSelectorForActualFiles() {
        Fixture fixture;
        VaultIdentifier identifier(&fixture.index, nullptr, nullptr, nullptr);
        fixture.library.setIdentifier(&identifier);
        auto row = fixture.rows(1).first();
        row.kind = QStringLiteral("comic"); row.identityType.clear();
        row.subtreePath = fixture.root + QStringLiteral("/declared-comic");
        row.path = row.subtreePath + QStringLiteral("/book.pdf");
        row.groupKey = QStringLiteral("comic::") + row.subtreePath;
        QVERIFY(fixture.index.publish({row}));
        QVERIFY(fixture.library.reshelveGroup(row.groupKey, QStringLiteral("book")));
        QCOMPARE(fixture.config.kindFor(row.subtreePath), QStringLiteral("book"));
        QVERIFY(fixture.config.kindFor(row.groupKey).isEmpty());

        row.id = QStringLiteral("vault:actual-comic");
        row.subtreePath = fixture.root + QStringLiteral("/actual-comic");
        row.path = row.subtreePath + QStringLiteral("/issue.cbz");
        row.groupKey = QStringLiteral("comic::") + row.subtreePath;
        QVERIFY(fixture.index.publish({row}));
        QVERIFY(fixture.library.reshelveGroup(row.groupKey, QStringLiteral("book")));
        QCOMPARE(fixture.config.kindFor(row.groupKey), QStringLiteral("book"));
        QVERIFY(fixture.config.kindFor(row.subtreePath).isEmpty());
    }

    void groupItemsKeepActualKindsAndExplicitLegacyFallback() {
        Fixture fixture;
        auto film = fixture.rows(1).first();
        film.groupKey = film.subtreePath = fixture.root + QStringLiteral("/mixed");
        film.path = film.subtreePath + QStringLiteral("/film.mkv");
        auto comic = film;
        comic.id = QStringLiteral("vault:comic"); comic.kind = QStringLiteral("comic");
        comic.groupKey = QStringLiteral("comic::") + comic.subtreePath;
        comic.path = comic.subtreePath + QStringLiteral("/issue.cbz");
        comic.identityId = QStringLiteral("comics:1"); comic.identitySource = QStringLiteral("COMICS");
        comic.identityTitle = QStringLiteral("A comic"); comic.identityType.clear();
        auto book = film;
        book.id = QStringLiteral("vault:book"); book.kind = QStringLiteral("book");
        book.subtreePath = fixture.root + QStringLiteral("/legacy");
        book.path = book.groupKey = book.subtreePath + QStringLiteral("/book.epub");
        book.identityId = QStringLiteral("book:1"); book.identitySource = QStringLiteral("MANUAL");
        book.identityTitle = QStringLiteral("A book"); book.identityType.clear();
        auto legacyComic = comic;
        legacyComic.id = QStringLiteral("vault:legacy-comic");
        legacyComic.subtreePath = book.subtreePath;
        legacyComic.groupKey = QStringLiteral("comic::") + book.subtreePath;
        legacyComic.path = book.subtreePath + QStringLiteral("/another.cbz");
        QVERIFY(fixture.index.publish({film, comic, book, legacyComic}));
        const auto films = fixture.library.items(QStringLiteral("video"), film.groupKey);
        QCOMPARE(films.size(), 1);
        QCOMPARE(films.first().toMap().value("kind").toString(), QStringLiteral("video"));
        QCOMPARE(fixture.library.items(QStringLiteral("comic"), comic.groupKey).size(), 1);
        QVERIFY(fixture.library.items(QStringLiteral("comic"), film.groupKey).isEmpty());
        QVERIFY(fixture.library.items(QStringLiteral("book"), comic.groupKey).isEmpty());
        const auto legacyBooks = fixture.library.items(QStringLiteral("book"), book.subtreePath);
        QCOMPARE(legacyBooks.size(), 1);
        QCOMPARE(legacyBooks.first().toMap().value("id").toString(), book.id);
        QCOMPARE(fixture.library.items(QStringLiteral("comic"), book.subtreePath).size(), 1);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty("VaultLibrary", &fixture.library);
        QQmlComponent component(&engine);
        component.setData(adapterSource(), QUrl(QStringLiteral("file:///vault-mixed-kind-test.qml")));
        std::unique_ptr<QObject> adapter(component.create());
        QVERIFY2(adapter, qPrintable(component.errorString()));
        engine.globalObject().setProperty("projection", engine.newQObject(adapter.get()));
        QVERIFY(engine.evaluate("projection.allItems.every(row => row.items.length > 0 && row.items.every(file => file.kind === row.nativeKind))").toBool());
    }

    void supplementalCountsFollowAttachmentAndIndexRevisions() {
        Fixture fixture;
        auto rows = fixture.rows(2);
        rows[0].identityType = QStringLiteral("series");
        rows[1].identityId.clear(); rows[1].identityTitle.clear(); rows[1].identitySource.clear();
        QVERIFY(fixture.index.publish(rows));
        auto count = [&] {
            for (const auto& value : fixture.library.series(QStringLiteral("video"))) {
                const auto group = value.toMap();
                if (group.value("key").toString() == rows.first().groupKey)
                    return group.value("supplementalCount").toInt();
            }
            return -1;
        };
        QCOMPARE(count(), 0);
        QVERIFY(fixture.library.attachExtrasToShow(rows[1].groupKey, rows[0].groupKey));
        QCOMPARE(count(), 1);
        QCOMPARE(count(), fixture.library.extrasForShow(rows[0].groupKey).size());
        QVERIFY(fixture.library.detachExtras(rows[1].id));
        QCOMPARE(count(), 0);
        QVERIFY(fixture.library.attachExtrasToShow(rows[1].groupKey, rows[0].groupKey));
        QVERIFY(fixture.library.hideGroup(rows[1].groupKey));
        QCOMPARE(count(), 0);
        QVERIFY(fixture.library.restoreGroup(rows[1].groupKey));
        QCOMPARE(count(), 1);
        QVERIFY(fixture.index.publish({rows[0]}));
        QCOMPARE(count(), 0);
        QVERIFY(fixture.index.publish(rows));
        QCOMPARE(count(), 1);
    }

    void realRevisionDuringFinalBatchNeverPublishesMixedFacts() {
        Fixture fixture;
        const auto rows = fixture.rows(16);
        QVERIFY(fixture.index.publish(rows));
        const auto groups = fixture.library.series(QStringLiteral("video"));
        const auto lastKey = groups.last().toMap().value("key").toString();
        auto revised = fixture.index.rowsForGroup(lastKey).first();
        revised.identityTitle = QStringLiteral("Revised final batch");
        revised.durationSec = 1200;
        RevisionRaceSource source(fixture.library, fixture.index, revised);
        QQuickWindow animationClock;
        animationClock.resize(1, 1); animationClock.show();
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("VaultLibrary", &source);
        QQmlComponent component(&engine);
        component.setData(adapterSource(), QUrl(QStringLiteral("file:///vault-revision-test.qml")));
        std::unique_ptr<QObject> adapter(component.create());
        QVERIFY2(adapter, qPrintable(component.errorString()));
        engine.globalObject().setProperty("projection", engine.newQObject(adapter.get()));
        engine.globalObject().setProperty("raceKey", lastKey);
        PublicationObserver observer;
        observer.engine = &engine; observer.key = lastKey;
        QVERIFY(connect(adapter.get(), SIGNAL(allItemsChanged()), &observer, SLOT(observe())));
        QTRY_VERIFY(!source.armed);
        QTRY_COMPARE(engine.evaluate("projection.allItems.find(row => row.seriesKey === raceKey).title").toString(),
                     QStringLiteral("Revised final batch"));
        QVERIFY(observer.publications > 0);
        QVERIFY2(!observer.mixedRevision, "An old group snapshot was published with new-revision detail facts");
    }
    void rootsAndPresentationUseIndexWithoutPhysicalFolders() {
        Fixture fixture;
        const auto rows = fixture.rows(2);
        QVERIFY(fixture.index.publish(rows));
        const auto roots = fixture.library.rootsDetail();
        QCOMPARE(roots.size(), 1);
        QCOMPARE(roots.first().toMap().value("fileCount").toInt(), 2);
        QCOMPARE(roots.first().toMap().value("itemCount").toInt(), 2);
        QVariantMap presentation;
        QVERIFY(QMetaObject::invokeMethod(&fixture.library, "presentationDetail",
            Q_RETURN_ARG(QVariantMap, presentation), Q_ARG(QString, rows.first().groupKey)));
        QCOMPARE(presentation.value("runtimeText").toString(), QStringLiteral("10m"));
        QCOMPARE(presentation.value("ratingText").toString(), QStringLiteral("IMDb 8.2"));
        QCOMPARE(presentation.value("genresLine").toString(), QStringLiteral("Drama"));
        QCOMPARE(presentation.value("copies").toList().size(), 1);
        QVERIFY(presentation.value("companions").toList().isEmpty());
        QVERIFY(QDir().mkpath(rows.first().subtreePath + QStringLiteral("/Extras")));
        for (const auto& path : {rows.first().subtreePath + QStringLiteral("/film.srt"),
                                rows.first().subtreePath + QStringLiteral("/Extras/featurette.mkv")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("fixture");
        }
        const auto physical = fixture.library.browseDetail(rows.first().groupKey);
        QCOMPARE(physical.value("copies"), presentation.value("copies"));
        QVERIFY(!physical.value("companions").toList().isEmpty());
        QVERIFY(!physical.value("extras").toList().isEmpty());
    }

    void largeProjectionYieldsAndArtworkKeepsMetadata() {
        Fixture fixture;
        const auto rows = fixture.rows(800);
        QVERIFY(fixture.index.publish(rows));
        QQuickWindow animationClock;
        animationClock.resize(1, 1);
        animationClock.show();
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("VaultLibrary", &fixture.library);
        QQmlComponent component(&engine);
        component.setData(adapterSource(), QUrl(QStringLiteral("file:///vault-projection-test.qml")));
        QElapsedTimer initialDuration;
        initialDuration.start();
        std::unique_ptr<QObject> adapter(component.create());
        const qint64 initialCreateMs = initialDuration.elapsed();
        QVERIFY2(adapter, qPrintable(component.errorString()));
        QVERIFY(adapter->property("busy").toBool());
        engine.rootContext()->setContextProperty("projection", adapter.get());
        engine.globalObject().setProperty("projection", engine.newQObject(adapter.get()));
        QVERIFY(!engine.evaluate("projection.allItems.length").isError());
        QVERIFY(QMetaObject::invokeMethod(&fixture.library, "browseArtworkReady",
            Q_ARG(QString, rows.first().groupKey), Q_ARG(QString, QStringLiteral("file:///obsolete.jpg"))));
        auto revised = rows.first();
        revised.identityTitle = QStringLiteral("Updated during projection");
        revised.coverRef = QStringLiteral("file:///revised-local.jpg");
        QVERIFY(fixture.index.upsert(revised));
        QTimer initialHeartbeat;
        initialHeartbeat.setInterval(0);
        KeyReceiver initialInput;
        int beatsBeforeReady = 0;
        bool inputBeforeReady = false;
        connect(&initialHeartbeat, &QTimer::timeout, this, [&] {
            if (engine.evaluate("projection.allItems.length").toInt() == 0) {
                ++beatsBeforeReady;
                inputBeforeReady = inputBeforeReady || initialInput.received;
            }
        });
        initialHeartbeat.start();
        QCoreApplication::postEvent(&initialInput, new QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
        QTRY_COMPARE(engine.evaluate("projection.allItems.length").toInt(), 800);
        QVERIFY(!adapter->property("busy").toBool());
        initialHeartbeat.stop();
        QVERIFY(beatsBeforeReady > 0);
        QVERIFY(inputBeforeReady);
        QCOMPARE(engine.evaluate("projection.movies[0].title").toString(), QStringLiteral("Updated during projection"));
        QCOMPARE(engine.evaluate("projection.movies[0].cover").toString(), QStringLiteral("file:///revised-local.jpg"));
        qInfo() << "800-group initial create/ready ms" << initialCreateMs << initialDuration.elapsed()
                << "heartbeat callbacks before ready" << beatsBeforeReady;
        QCOMPARE(engine.evaluate("projection.movies[0].runtimeText").toString(), QStringLiteral("10m"));
        QCOMPARE(engine.evaluate("projection.movies[0].imdbRating").toNumber(), 8.2);

        KeyReceiver key;
        QTimer producer, heartbeat;
        producer.setInterval(0);
        heartbeat.setInterval(0);
        int emitted = 0, heartbeatDuringWork = 0;
        bool inputDuringWork = false;
        QElapsedTimer duration;
        duration.start();
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            if (emitted > 0 && emitted < 80) ++heartbeatDuringWork;
        });
        connect(&producer, &QTimer::timeout, this, [&] {
            if (emitted == 0)
                QCoreApplication::postEvent(&key, new QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier));
            if (key.received && emitted < 80) inputDuringWork = true;
            const auto& row = rows.at(emitted);
            const QString cover = QStringLiteral("file:///resolved-%1.jpg").arg(emitted);
            QVERIFY(QMetaObject::invokeMethod(&fixture.library, "browseArtworkReady",
                Q_ARG(QString, row.groupKey), Q_ARG(QString, cover)));
            ++emitted;
            if (emitted == 80) producer.stop();
        });
        heartbeat.start();
        producer.start();
        QTRY_COMPARE(emitted, 80);
        QTRY_COMPARE(engine.evaluate("projection.movies[0].cover").toString(), QStringLiteral("file:///resolved-0.jpg"));
        heartbeat.stop();
        QVERIFY(inputDuringWork);
        QVERIFY(heartbeatDuringWork > 0);
        QCOMPARE(engine.evaluate("projection.movies[0].imdbRating").toNumber(), 8.2);
        QCOMPARE(engine.evaluate("projection.movies[0].genres").toString(), QStringLiteral("Drama"));
        qInfo() << "800 groups; 80 queued art updates; elapsed ms" << duration.elapsed()
                << "heartbeat callbacks during work" << heartbeatDuringWork;
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    tst_vault_projection test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_vault_projection.moc"
