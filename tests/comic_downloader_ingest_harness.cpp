// External-archive ingest contract: two locally produced CBZs enter the existing
// ComicDownloader extraction queue, finish under their original issue IDs, and
// expose reader-shaped localPages through ComicDownloader itself.
#include "engine/ComicDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {
bool makeCbz(const QString& root, const QString& name, QString* archivePath)
{
    const QString pages = root + QLatin1Char('/') + name + QStringLiteral("-pages");
    if (!QDir().mkpath(pages)) return false;
    for (int i = 0; i < 2; ++i) {
        QFile page(pages + QStringLiteral("/page%1.jpg").arg(i));
        if (!page.open(QIODevice::WriteOnly)) return false;
        page.write("not-decoded-by-this-contract-test");
    }

    const QString zip = root + QLatin1Char('/') + name + QStringLiteral(".zip");
    const int exitCode = QProcess::execute(QStringLiteral("C:/Windows/System32/tar.exe"),
        {QStringLiteral("-a"), QStringLiteral("-cf"), zip,
         QStringLiteral("-C"), pages, QStringLiteral(".")});
    if (exitCode != 0) return false;
    *archivePath = root + QLatin1Char('/') + name + QStringLiteral(".cbz");
    return QFile::rename(zip, *archivePath);
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("ComicDownloaderIngestHarness"));

    QTemporaryDir temp;
    QString firstArchive;
    QString secondArchive;
    if (!temp.isValid()
        || !makeCbz(temp.path(), QStringLiteral("first"), &firstArchive)
        || !makeCbz(temp.path(), QStringLiteral("second"), &secondArchive)) {
        std::printf("FAIL: could not create CBZ fixtures\n");
        return 1;
    }

    QNetworkAccessManager nam;
    ComicDownloader comics(&nam);
    comics.deleteIssue(QStringLiteral("torrent-ingest-1"));
    comics.deleteIssue(QStringLiteral("torrent-ingest-2"));

    int finishedCount = 0;
    QObject::connect(&comics, &ComicDownloader::failed, &app,
        [&app](const QString& id, const QString& reason) {
            std::printf("FAIL: %s: %s\n", qPrintable(id), qPrintable(reason));
            app.exit(1);
        });
    QObject::connect(&comics, &ComicDownloader::finished, &app,
        [&](const QString& id) {
            if (comics.localPages(id).size() != 2
                || comics.statusOf(id).value(QStringLiteral("state")).toString() != QStringLiteral("done")) {
                std::printf("FAIL: %s did not surface two reader pages as done\n", qPrintable(id));
                app.exit(1);
                return;
            }
            if (++finishedCount == 2) app.exit(0);
        });

    comics.ingestLocalArchive(QStringLiteral("torrent-ingest-1"), QStringLiteral("gc:test"),
                              QStringLiteral("Test Series"), QStringLiteral("Volume One"), firstArchive);
    comics.ingestLocalArchive(QStringLiteral("torrent-ingest-2"), QStringLiteral("gc:test"),
                              QStringLiteral("Test Series"), QStringLiteral("Volume Two"), secondArchive);

    QTimer::singleShot(30000, &app, [&app]() {
        std::printf("FAIL: extraction timeout\n");
        app.exit(2);
    });
    const int code = app.exec();
    comics.deleteIssue(QStringLiteral("torrent-ingest-1"));
    comics.deleteIssue(QStringLiteral("torrent-ingest-2"));
    return code;
}
