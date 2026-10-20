#include "server1/ports/ProcessPort.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace server1::media {

namespace {

bool isExecutableCandidate(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return false;

#ifdef Q_OS_WIN
    // Node's fs.accessSync(path, X_OK) treats X_OK as an existence/access check
    // on Windows, including accessible directories.
    return true;
#else
    return info.isExecutable();
#endif
}

QStringList pathCandidates(const QString &name, const QProcessEnvironment &environment)
{
    QStringList candidates;
    const QString path = environment.value(QStringLiteral("PATH"));
    const auto directories = path.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const auto &directory : directories)
        candidates.push_back(QDir(directory).filePath(name));
    return candidates;
}

} // namespace

ExecutableLocator::ExecutableLocator(QProcessEnvironment environment)
    : environment_(std::move(environment))
{
}

std::optional<QString> ExecutableLocator::locate(const QString &name,
                                                 const QStringList &preferred) const
{
    QStringList candidates = preferred;
    candidates.append(pathCandidates(name, environment_));
    for (const auto &candidate : candidates) {
        if (!candidate.isEmpty() && isExecutableCandidate(candidate))
            return candidate;
    }
    return std::nullopt;
}

ExecutablePaths ExecutableLocator::locateAll(const ExecutableSearchPaths &searchPaths) const
{
    if (const auto path = locate(QStringLiteral("ffmpeg"), searchPaths.ffmpeg))
        paths_.ffmpeg = *path;
    if (const auto path = locate(QStringLiteral("ffprobe"), searchPaths.ffprobe))
        paths_.ffprobe = *path;
    if (const auto path = locate(QStringLiteral("ffsplit"), searchPaths.ffsplit))
        paths_.ffsplit = *path;
    return paths_;
}

} // namespace server1::media
