#include "TankobanChapterMigration.h"
#include "../ProgressStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

QString TankobanChapterMigration::markerPath(const QString &appDataRoot)
{
    return QDir::cleanPath(appDataRoot) + QStringLiteral("/tankoban-chapter-migration.v1.done");
}

bool TankobanChapterMigration::deleteChapterTree(const QString &appDataRoot, Result &out)
{
    const QString mangaDir = QDir::cleanPath(appDataRoot) + QStringLiteral("/manga");
    QDir dir(mangaDir);
    if (!dir.exists()) {
        out.mangaDirExisted = false;
        return true;   // nothing to delete — trivially successful
    }
    out.mangaDirExisted = true;
    out.chapterDirsDeleted = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
    out.indexDeleted = QFile::exists(mangaDir + QStringLiteral("/index.json"));
    const bool removed = dir.removeRecursively();
    out.mangaDirDeleted = removed;
    return removed;
}

int TankobanChapterMigration::purgeMangaProgress(ProgressStore *progress)
{
    if (!progress)
        return 0;
    return progress->purgeKind(QStringLiteral("manga"));
}

TankobanChapterMigration::Result TankobanChapterMigration::run(const QString &appDataRoot,
                                                                 ProgressStore *progress)
{
    Result out;
    if (appDataRoot.isEmpty())
        return out;

    const QString marker = markerPath(appDataRoot);
    if (QFile::exists(marker)) {
        // Idempotent: a prior successful run already stands (tree gone or never existed,
        // progress already purged). No disk or progress work performed.
        return out;
    }

    out.ran = true;
    const bool diskOk = deleteChapterTree(appDataRoot, out);
    out.progressRecordsPurged = purgeMangaProgress(progress);

    if (!diskOk) {
        qWarning("[tankoban-migration] chapter tree removal FAILED under %s — marker withheld, "
                 "will retry next boot",
                 qUtf8Printable(appDataRoot));
        return out;
    }

    QDir().mkpath(QFileInfo(marker).absolutePath());
    QFile markerFile(marker);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Text))
        markerFile.write(QByteArrayLiteral("1"));
    else
        qWarning("[tankoban-migration] could not write marker at %s — will retry next boot",
                 qUtf8Printable(marker));

    qInfo("[tankoban-migration] chapter store purge complete: manga/ existed=%s deleted=%s, "
          "%d series dir(s), index.json=%s, %d manga-kind progress record(s) purged",
          out.mangaDirExisted ? "yes" : "no",
          out.mangaDirDeleted ? "yes" : "n/a",
          out.chapterDirsDeleted,
          out.indexDeleted ? "removed" : "absent",
          out.progressRecordsPurged);
    return out;
}
