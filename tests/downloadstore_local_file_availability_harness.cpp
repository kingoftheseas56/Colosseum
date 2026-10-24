#include "player/downloadstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>

namespace {
int fail(const char *message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
}

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ColosseumAudit"));
    QCoreApplication::setApplicationName(
        QStringLiteral("DownloadStoreLocalFileAvailability-%1")
            .arg(QCoreApplication::applicationPid()));

    QTemporaryDir temp;
    if (!temp.isValid())
        return fail("temporary directory unavailable");
    const QString indexedMissingPath = temp.filePath(QStringLiteral("stale-index-entry.mkv"));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString videosDir = QDir(appData).filePath(QStringLiteral("videos"));
    if (!QDir().mkpath(videosDir))
        return fail("could not create isolated download index directory");
    QFile indexFile(QDir(videosDir).filePath(QStringLiteral("index.json")));
    if (!indexFile.open(QIODevice::WriteOnly))
        return fail("could not seed isolated download index");
    const QJsonObject staleEntry{
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("path"), indexedMissingPath}
    };
    const QJsonObject index{{QStringLiteral("stale-index-entry"), staleEntry}};
    const QByteArray indexBytes = QJsonDocument(index).toJson(QJsonDocument::Compact);
    if (indexFile.write(indexBytes) != indexBytes.size())
        return fail("could not write isolated download index");
    indexFile.close();

    DownloadStore store;
    if (!store.hasVideo(QStringLiteral("stale-index-entry")))
        return fail("stale index entry was not loaded");
    bool reportsMissing = false;
    for (const QVariant &row : store.downloadedVideos()) {
        const QVariantMap video = row.toMap();
        if (video.value(QStringLiteral("id")).toString() == QStringLiteral("stale-index-entry"))
            reportsMissing = video.value(QStringLiteral("missing")).toBool();
    }
    if (!reportsMissing)
        return fail("downloadedVideos did not identify the stale path as missing");

#ifdef Q_OS_WIN
    // '?' cannot appear in a Windows filename; '#%' still exercises URI encoding.
    const QString complexFilePath = temp.filePath(
        QStringLiteral("real fixture λ #% movie.mkv"));
#else
    const QString complexFilePath = temp.filePath(
        QStringLiteral("real fixture λ #%? movie.mkv"));
#endif
    const QByteArray payload("availability-helper-must-not-modify-this-file");
    QFile file(complexFilePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size())
        return fail("could not create Unicode/special-character local fixture");
    file.close();

    const QString fileUri = QUrl::fromLocalFile(complexFilePath).toString(QUrl::FullyEncoded);
    if (!store.localFileAvailable(complexFilePath))
        return fail("existing raw local path was not available");
    if (!store.localFileAvailable(fileUri))
        return fail("existing encoded file URI was not available");
    if (store.localFileAvailable(indexedMissingPath))
        return fail("stale index membership incorrectly made a missing path available");
    if (store.localFileAvailable(temp.path()))
        return fail("directory was incorrectly accepted as a local file");
    if (store.localFileAvailable(QString()))
        return fail("empty path was incorrectly accepted");
    if (store.localFileAvailable(QStringLiteral("http://example.test/video.mkv")))
        return fail("HTTP URL was incorrectly accepted as a local file");
    if (store.localFileAvailable(QStringLiteral("content://media/external/video/42")))
        return fail("content URI was incorrectly accepted as a local file");

#ifdef Q_OS_WIN
    if (!QDir::isAbsolutePath(complexFilePath)
        || complexFilePath.size() < 3 || complexFilePath.at(1) != QLatin1Char(':'))
        return fail("Windows fixture did not exercise an absolute drive-letter path");
#endif

    QFile verifyFile(complexFilePath);
    if (!verifyFile.open(QIODevice::ReadOnly) || verifyFile.readAll() != payload)
        return fail("availability checks modified the local fixture");
    QFile verifyIndex(QDir(videosDir).filePath(QStringLiteral("index.json")));
    if (!verifyIndex.open(QIODevice::ReadOnly) || verifyIndex.readAll() != indexBytes)
        return fail("availability checks modified the download index");
    if (QFileInfo::exists(indexedMissingPath)
        || !store.hasVideo(QStringLiteral("stale-index-entry")) || !reportsMissing)
        return fail("availability checks changed indexed metadata");

    std::printf("PASS: DownloadStore availability accepts local paths/file URIs and rejects missing, non-file, and remote inputs.\n");
    return 0;
}
