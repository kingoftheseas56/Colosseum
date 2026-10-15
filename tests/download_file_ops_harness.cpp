#include "engine/DownloadFileOps.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

static void require(bool value, const char *message)
{
    if (!value) qFatal("%s", message);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");

    const QString filePath = temp.filePath(QStringLiteral("landed.epub"));
    QFile file(filePath);
    require(file.open(QIODevice::WriteOnly), "fixture open");
    file.write("payload");
    file.close();

    auto denied = DownloadFileOps::removeFile(
        filePath, [](const QString &) { return false; });
    require(!denied.success, "injected failure must fail");
    require(QFile::exists(filePath), "failed deletion must preserve payload");
    require(!denied.message.isEmpty(), "failure must carry bounded copy");

    auto removed = DownloadFileOps::removeFile(filePath);
    require(removed.success, "real deletion must succeed");
    require(!QFile::exists(filePath), "successful deletion removes payload");

    auto alreadyMissing = DownloadFileOps::removeFile(filePath);
    require(alreadyMissing.success, "missing payload is already deleted");
    return 0;
}
