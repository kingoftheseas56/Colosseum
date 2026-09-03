#include "colosseum_server/media/MediaPipeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace ColosseumServer::Media {
namespace {

bool executableExists(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable();
}

QStringList pathCandidates(const QString &name)
{
    QStringList result;
    const QString path = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    for (const QString &dir : path.split(QDir::listSeparator(), Qt::SkipEmptyParts))
        result.append(QDir(dir).filePath(name));
    return result;
}

} // namespace
ProcessResult MediaProcess::run(const QString &program,
                                const QStringList &arguments,
                                int timeoutMs)
{
    ProcessResult result;
    if (!executableExists(program)) {
        result.error = QStringLiteral("executable not found: %1").arg(program);
        return result;
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(qMin(timeoutMs, 10000))) {
        result.error = process.errorString();
        return result;
    }

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
#ifdef Q_OS_WIN
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/PID"), QString::number(process.processId()),
                      QStringLiteral("/T"), QStringLiteral("/F")});
        killer.waitForFinished(5000);
#endif
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(5000);
    }
    result.stdOut = process.readAllStandardOutput();
    result.stdErr = process.readAllStandardError();
    result.exitCode = process.exitCode();
    result.crashed = process.exitStatus() == QProcess::CrashExit;
    result.error = QStringLiteral("process timed out after %1 ms").arg(timeoutMs);
    return result;
}

result.stdOut = process.readAllStandardOutput();
result.stdErr = process.readAllStandardError();
result.exitCode = process.exitCode();
result.crashed = process.exitStatus() == QProcess::CrashExit;
if (result.crashed)
    result.error = QStringLiteral("process crashed");
return result;
}

QString ExecutableLocator::locate(const QString &name, const QStringList &searchIn)
{
    for (const QString &candidate : searchIn)
        if (executableExists(candidate)) return QFileInfo(candidate).absoluteFilePath();
    for (const QString &candidate : pathCandidates(name))
        if (executableExists(candidate)) return QFileInfo(candidate).absoluteFilePath();
    return {};
}Executables ExecutableLocator::locateAll(const QString &applicationDir,
                                         const QString &ffmpegOverride,
                                         const QString &ffprobeOverride,
                                         const QString &ffsplitOverride)
{
    const QString dir = applicationDir.isEmpty()
        ? QCoreApplication::applicationDirPath() : applicationDir;
#ifdef Q_OS_WIN
    const QString ffmpegName = QStringLiteral("ffmpeg.exe");
    const QString ffprobeName = QStringLiteral("ffprobe.exe");
    const QString ffsplitName = QStringLiteral("ffsplit.exe");
#else
    const QString ffmpegName = QStringLiteral("ffmpeg");
    const QString ffprobeName = QStringLiteral("ffprobe");
    const QString ffsplitName = QStringLiteral("ffsplit");
#endif
    const auto env = QProcessEnvironment::systemEnvironment();
    const QStringList ffmpegSearch{ffmpegOverride, env.value(QStringLiteral("FFMPEG_BIN")),
        QDir(dir).filePath(ffmpegName), QDir(dir).filePath(QStringLiteral("bin/") + ffmpegName),
        QStringLiteral("/usr/lib/jellyfin-ffmpeg/ffmpeg"), QStringLiteral("/usr/bin/ffmpeg"),
        QStringLiteral("/usr/local/bin/ffmpeg")};
    const QStringList ffprobeSearch{ffprobeOverride, env.value(QStringLiteral("FFPROBE_BIN")),
        QDir(dir).filePath(ffprobeName), QDir(dir).filePath(QStringLiteral("bin/") + ffprobeName),
        QStringLiteral("/usr/lib/jellyfin-ffmpeg/ffprobe"), QStringLiteral("/usr/bin/ffprobe"),
        QStringLiteral("/usr/local/bin/ffprobe")};    const QStringList ffsplitSearch{ffsplitOverride,
        QDir(dir).filePath(QStringLiteral("bin/") + ffsplitName)};
    Executables result;
    result.ffmpeg = locate(ffmpegName, ffmpegSearch);
    result.ffprobe = locate(ffprobeName, ffprobeSearch);
    result.ffsplit = locate(ffsplitName, ffsplitSearch);
    return result;
}

} // namespace ColosseumServer::Media
