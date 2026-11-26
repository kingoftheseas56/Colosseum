#include "engine/DownloadLocation.h"
#include "engine/DownloadMediaPaths.h"
#include "engine/DownloadMoveController.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace Qt::StringLiterals;

class DownloadLocationTest : public QObject {
    Q_OBJECT

private:
    static bool write(const QString &path, const QByteArray &contents)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

private slots:
    void previewLeavesBothLocationsUntouched()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const QString source = sandbox.filePath(u"legacy/ebook.epub"_s);
        QVERIFY(write(source, "ebook"));
        const QString statePath = sandbox.filePath(u"profile/location.json"_s);
        const DownloadLocation location(statePath);
        const QString root = DownloadMediaPaths::rootForSelection(QUrl::fromLocalFile(sandbox.path()));
        QCOMPARE(DownloadMediaPaths::rootForSelection(QUrl::fromLocalFile(root)), root);
        const auto plan = location.plan(root, {{source, u"Books/Book.epub"_s, {}}});
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QCOMPARE(plan.bytes, 5);
        QCOMPARE(plan.items, 1);
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!QFileInfo::exists(root));
        QVERIFY(!QFileInfo::exists(statePath));
    }

    void copiesExactOwnersAndRecoversAcrossPhases()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const QString root = sandbox.filePath(u"Colosseum"_s);
        const QString statePath = sandbox.filePath(u"profile/location.json"_s);
        const QString chapter = sandbox.filePath(u"legacy/manga/chapter"_s);
        const QString book = sandbox.filePath(u"other-root/books/hashed.epub"_s);
        const QString userFile = root + u"/Books/Book.epub"_s;
        QVERIFY(write(chapter + u"/001.png"_s, "page"));
        QVERIFY(write(chapter + u"/unowned.txt"_s, "personal"));
        QVERIFY(write(book, "ebook"));
        QVERIFY(write(userFile, "unrelated"));

        {
            DownloadLocation location(statePath);
            const auto plan = location.plan(root, {
                {chapter, u"Manga/Series/Chapter 1"_s, {u"001.png"_s}},
                {book, u"Books/Book.epub"_s, {}},
                {book, u"Books/Duplicate ref.epub"_s, {}}
            });
            QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
            QCOMPARE(plan.items, 2);
            QCOMPARE(plan.collisions, 1);
            QCOMPARE(plan.bytes, 9);
            QCOMPARE(location.begin(plan), QString());
            QVERIFY(location.root() != root);
        }
        {
            DownloadLocation location(statePath);
            QCOMPARE(location.copy(), QString());
            QVERIFY(QFileInfo::exists(book));
            QVERIFY(location.root() != root);
            QVERIFY(!location.committed());
        }
        {
            DownloadLocation location(statePath);
            QCOMPARE(location.copy(), QString());
            QCOMPARE(location.commit(), QString());
            QVERIFY(QFileInfo::exists(book));
        }
        {
            DownloadLocation location(statePath);
            QCOMPARE(location.root(), root);
            QVERIFY(location.committed());
            QCOMPARE(location.aliases().value(DownloadMediaPaths::pathKey(book)),
                     root + u"/Books/Book (2).epub"_s);
            QCOMPARE(location.cleanSources(), QString());
            QVERIFY(!location.committed());
        }
        QVERIFY(QFileInfo::exists(chapter + u"/unowned.txt"_s));
        QVERIFY(QFileInfo::exists(root + u"/Manga/Series/Chapter 1/001.png"_s));
        QVERIFY(!QFileInfo::exists(book));
        QVERIFY(!QFileInfo::exists(chapter + u"/001.png"_s));
        QFile unrelated(userFile);
        QVERIFY(unrelated.open(QIODevice::ReadOnly));
        QCOMPARE(unrelated.readAll(), QByteArray("unrelated"));
    }

    void disappearanceAndConflictsKeepTheSource()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const QString source = sandbox.filePath(u"legacy/Movie.mkv"_s);
        const QString root = sandbox.filePath(u"Colosseum"_s);
        const QString relative = u"Movies/Movie.mkv"_s;
        QVERIFY(write(source, "video"));
        DownloadLocation location(sandbox.filePath(u"profile/location.json"_s));
        QCOMPARE(location.begin(location.plan(root, {{source, relative, {}}})), QString());
        const QString finalFile = QDir(root).filePath(relative);
        QVERIFY(write(finalFile, "collision"));
        QVERIFY(!location.copy().isEmpty());
        QVERIFY(QFile::remove(finalFile));
        QCOMPARE(location.copy(), QString());
        const QString marker = DownloadMediaPaths::markerPath(root);
        QVERIFY(QFile::rename(marker, marker + u".away"_s));
        QVERIFY(!location.copy().isEmpty());
        QVERIFY(!location.commit().isEmpty());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(QFile::rename(marker + u".away"_s, marker));
        QCOMPARE(location.commit(), QString());
        QVERIFY(write(finalFile, "other"));
        QVERIFY(!location.cleanSources().isEmpty());
        QVERIFY(QFileInfo::exists(source));
    }

    void namesRemainPortableAndReadable()
    {
        QCOMPARE(DownloadMediaPaths::component(u"  Emília: Volume 2. "_s, u"Untitled"_s),
                 u"Emília- Volume 2"_s);
        QCOMPARE(DownloadMediaPaths::fileName(u"COM1"_s, u"Book"_s, u"epub"_s), u"_COM1.epub"_s);
        QCOMPARE(DownloadMediaPaths::component(u"..."_s, u"Book"_s), u"Book"_s);
    }

    void previewControllerRequiresNewConfirmationAfterChange()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const QString root = sandbox.filePath(u"Colosseum"_s);
        const QString source = sandbox.filePath(u"legacy/Book.epub"_s);
        const QString second = sandbox.filePath(u"legacy/Another.epub"_s);
        const QString state = sandbox.filePath(u"profile/location.json"_s);
        QVERIFY(write(source, "first"));
        QList<DownloadLocation::Item> inventory{{source, u"Books/Book.epub"_s, {}}};
        QString busy;
        bool personal = false;
        const QString personalNotice = u"This move includes a Personal download"_s;
        int commits = 0;
        DownloadMoveController controller(DownloadLocation(state), [&] { return inventory; },
            [&] { return busy; }, [&](const auto &) { ++commits; },
            [&](const DownloadLocation::Plan &plan) {
                return personal && !plan.files.isEmpty() ? personalNotice : QString();
            });
        const QUrl selected = QUrl::fromLocalFile(sandbox.path());
        controller.previewLocation(selected);
        controller.cancelPreview();
        QCoreApplication::processEvents();
        QVERIFY(controller.preview().isEmpty());
        QVERIFY(!QFileInfo::exists(state));

        busy = u"Waiting for an active download"_s;
        controller.previewLocation(selected);
        QTRY_COMPARE(controller.preview().value(u"error"_s).toString(), busy);
        controller.confirmMove();
        QVERIFY(!controller.pending());
        QVERIFY(!QFileInfo::exists(root));
        controller.cancelPreview();
        busy.clear();

        controller.previewLocation(selected);
        QTRY_VERIFY(!controller.preview().isEmpty() && controller.preview().value(u"error"_s).toString().isEmpty());
        personal = true;
        controller.confirmMove();
        QCOMPARE(controller.preview().value(u"error"_s).toString(), personalNotice);
        QVERIFY(!controller.pending());
        QVERIFY(!QFileInfo::exists(state));
        QVERIFY(!QFileInfo::exists(root));
        QVERIFY(QFileInfo::exists(source));
        controller.cancelPreview();
        personal = false;

        controller.previewLocation(selected);
        QTRY_VERIFY(controller.preview().value(u"error"_s).toString().isEmpty());
        QVERIFY(write(second, "second"));
        inventory.append({second, u"Books/Another.epub"_s, {}});
        controller.confirmMove();
        QCOMPARE(controller.preview().value(u"items"_s).toInt(), 2);
        QVERIFY(!QFileInfo::exists(state));

        controller.confirmMove();
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(commits, 1);
        QCOMPARE(controller.resolveLocalPath(source), root + u"/Books/Book.epub"_s);
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(!QFileInfo::exists(second));
        QVERIFY(QFileInfo::exists(root + u"/Books/Another.epub"_s));
    }

    void aRestartKeepsJobPathsAndExistingFiles()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const QString root = sandbox.filePath(u"Colosseum"_s);
        const QString state = sandbox.filePath(u"profile/job-paths.json"_s);
        const QString relative = u"Books/Shared title.epub"_s;
        DownloadMediaPaths::install(root, {}, {});
        DownloadMediaPaths::loadJobPaths(state);
        const QString first = DownloadMediaPaths::allocate(relative, false, {}, u"book:first"_s);
        QCOMPARE(first, root + QLatin1Char('/') + relative);
        QVERIFY(QFileInfo::exists(state));
        QVERIFY(!QFileInfo::exists(root));

        DownloadMediaPaths::install(root, {}, {});
        DownloadMediaPaths::loadJobPaths(state);
        const QString second = DownloadMediaPaths::allocate(relative, false, {}, u"book:second"_s);
        QCOMPARE(second, root + u"/Books/Shared title (2).epub"_s);
        QCOMPARE(DownloadMediaPaths::allocate(relative, false, {}, u"book:first"_s), first);
        QVERIFY(write(first, "personal"));

        DownloadMediaPaths::install(root, {}, {});
        DownloadMediaPaths::loadJobPaths(state);
        QCOMPARE(DownloadMediaPaths::allocate(relative, false, {}, u"book:first"_s),
                 root + u"/Books/Shared title (3).epub"_s);
        QFile personal(first);
        QVERIFY(personal.open(QIODevice::ReadOnly));
        QCOMPARE(personal.readAll(), QByteArray("personal"));
    }

    void committedMoveRefreshesReadersBeforeCleanupOnRestart()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const QString root = sandbox.filePath(u"Colosseum"_s);
        const QString source = sandbox.filePath(u"legacy/Book.epub"_s);
        const QString state = sandbox.filePath(u"profile/location.json"_s);
        QVERIFY(write(source, "ebook"));
        {
            DownloadLocation location(state);
            QCOMPARE(location.begin(location.plan(root, {{source, u"Books/Book.epub"_s, {}}})), QString());
            QCOMPARE(location.copy(), QString());
            QCOMPARE(location.commit(), QString());
        }
        int refreshes = 0;
        DownloadMoveController controller(DownloadLocation(state), [] { return QList<DownloadLocation::Item>{}; },
            [] { return QString(); }, [&](const auto &) { ++refreshes; });
        QCOMPARE(refreshes, 1);
        QVERIFY(controller.pending());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(root + u"/Books/Book.epub"_s));
        controller.retryMove();
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!QFileInfo::exists(source));
    }
};

QTEST_GUILESS_MAIN(DownloadLocationTest)
#include "tst_download_location.moc"
