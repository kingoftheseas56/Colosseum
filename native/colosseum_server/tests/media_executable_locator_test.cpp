#include "media/MediaPipeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "media locator temporary directory must be valid");
    const QString tools = QDir(root.path()).filePath(QStringLiteral("tools"));
    require(QDir().mkpath(tools), "media locator tools directory must be created");
    const QString executable = QCoreApplication::applicationFilePath();
    require(QFile::copy(executable, QDir(tools).filePath(QStringLiteral("ffmpeg.exe"))),
            "media locator ffmpeg fixture must be created");
    require(QFile::copy(executable, QDir(tools).filePath(QStringLiteral("ffprobe.exe"))),
            "media locator ffprobe fixture must be created");

    const auto located = ColosseumServer::Media::ExecutableLocator::locateAll(root.path());
    require(QDir::cleanPath(located.ffmpeg)
                == QDir::cleanPath(QDir(tools).filePath(QStringLiteral("ffmpeg.exe"))),
            "ffmpeg must resolve from application tools");
    require(QDir::cleanPath(located.ffprobe)
                == QDir::cleanPath(QDir(tools).filePath(QStringLiteral("ffprobe.exe"))),
            "ffprobe must resolve from application tools");
    std::puts("MEDIA_EXECUTABLE_LOCATOR_OK");
    return 0;
}
