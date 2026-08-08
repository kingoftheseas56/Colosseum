#include "update/UpdateInstallBridge.h"

#include "update/UpdateCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace Colosseum::Update {
namespace {

constexpr auto kInstallRegistryKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Colosseum";

QString localAppDataRoot()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty())
        root = qEnvironmentVariable("LOCALAPPDATA");
    return root;
}

} // namespace

UpdateInstallBridge::UpdateInstallBridge(QObject* parent)
    : QObject(parent), m_paths(defaultPaths())
{
}

UpdateInstallBridge::UpdateInstallBridge(Paths paths, QObject* parent)
    : QObject(parent), m_paths(std::move(paths))
{
    const Paths defaults = defaultPaths();
    if (m_paths.executablePath.isEmpty())
        m_paths.executablePath = defaults.executablePath;
    if (m_paths.expectedInstallRoot.isEmpty())
        m_paths.expectedInstallRoot = defaults.expectedInstallRoot;
    if (m_paths.registryInstallRoot.isEmpty())
        m_paths.registryInstallRoot = defaults.registryInstallRoot;
    if (m_paths.cacheRoot.isEmpty())
        m_paths.cacheRoot = defaults.cacheRoot;
}

UpdateInstallBridge::Paths UpdateInstallBridge::defaultPaths()
{
    Paths paths;
    paths.executablePath = QCoreApplication::applicationFilePath();
    if (paths.executablePath.isEmpty())
        paths.executablePath = QStringLiteral("colosseum.exe");
    paths.expectedInstallRoot = QDir(localAppDataRoot()).filePath(
        QStringLiteral("Programs/Colosseum"));
    paths.registryInstallRoot = registryInstallRoot();
    paths.cacheRoot = UpdateCache::productionRoot();
    return paths;
}

QString UpdateInstallBridge::cleanAbsolute(const QString& path)
{
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool UpdateInstallBridge::samePath(const QString& left, const QString& right)
{
    const QString a = cleanAbsolute(left);
    const QString b = cleanAbsolute(right);
    if (a.isEmpty() || b.isEmpty())
        return false;
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

bool UpdateInstallBridge::isWithin(const QString& root, const QString& child)
{
    const QString cleanRoot = cleanAbsolute(root);
    const QString cleanChild = cleanAbsolute(child);
    if (cleanRoot.isEmpty() || cleanChild.isEmpty())
        return false;
    if (samePath(cleanRoot, cleanChild))
        return true;
    QString prefix = cleanRoot;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
#ifdef Q_OS_WIN
    return cleanChild.startsWith(prefix, Qt::CaseInsensitive);
#else
    return cleanChild.startsWith(prefix);
#endif
}

bool UpdateInstallBridge::isSourceTreePath(const QString& path)
{
    QFileInfo current(cleanAbsolute(path));
    QDir directory = current.isDir() ? current.absoluteDir() : current.absoluteDir();
    while (directory.exists()) {
        const QFileInfo gitMarker(directory.filePath(QStringLiteral(".git")));
        if (gitMarker.exists())
            return true;
        const QString parent = directory.absolutePath();
        if (!directory.cdUp() || directory.absolutePath() == parent)
            break;
    }
    return false;
}

QString UpdateInstallBridge::registryInstallRoot()
{
    QSettings settings(QString::fromLatin1(kInstallRegistryKey), QSettings::NativeFormat);
    return settings.value(QStringLiteral("InstallLocation")).toString();
}

bool UpdateInstallBridge::installedBuildEligible() const
{
    if (qEnvironmentVariableIsSet("COLOSSEUM_DEV"))
        return false;

    const QString executable = cleanAbsolute(m_paths.executablePath);
    const QString installRoot = cleanAbsolute(m_paths.expectedInstallRoot);
    const QString expectedExecutable = QDir(installRoot).filePath(
        QStringLiteral("native/build-msvc/colosseum.exe"));
    if (executable.isEmpty() || installRoot.isEmpty()
        || !QFileInfo(executable).isFile()
        || !QDir(installRoot).exists()
        || !samePath(executable, expectedExecutable)
        || isSourceTreePath(executable))
        return false;

    QString registeredRoot = m_paths.registryInstallRoot;
    if (registeredRoot.isEmpty())
        registeredRoot = registryInstallRoot();
    return !registeredRoot.isEmpty() && samePath(registeredRoot, installRoot);
}

void UpdateInstallBridge::setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

std::optional<InstallLaunch> UpdateInstallBridge::prepare(const QString& verifiedInstaller,
                                                           const Version& target,
                                                           QString* error) const
{
    if (!installedBuildEligible()) {
        setError(error, QStringLiteral("installed_build_required"));
        return std::nullopt;
    }

    const QString cacheRoot = cleanAbsolute(m_paths.cacheRoot);
    const QString installer = cleanAbsolute(verifiedInstaller);
    const QString versionRoot = QDir(cacheRoot).filePath(target.canonical());
    const QFileInfo info(installer);
    if (cacheRoot.isEmpty() || installer.isEmpty() || !info.isFile() || info.isSymLink()
        || !isWithin(versionRoot, installer)) {
        setError(error, QStringLiteral("verified_installer_outside_update_cache"));
        return std::nullopt;
    }

    InstallLaunch launch;
    launch.program = info.absoluteFilePath();
    launch.workingDirectory = info.absolutePath();
    launch.arguments = {
        QStringLiteral("/UPDATE=1"),
        QStringLiteral("/WAITPID=") + QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("/TARGETVERSION=") + target.canonical(),
        QStringLiteral("/RESTART=1"),
        QStringLiteral("/LOG=") + QDir(cacheRoot).filePath(
            QStringLiteral("update-%1.log").arg(target.canonical())),
    };
    return launch;
}

bool UpdateInstallBridge::launchDetached(const InstallLaunch& launch, QString* error) const
{
    if (launch.program.isEmpty() || launch.workingDirectory.isEmpty()) {
        setError(error, QStringLiteral("invalid_install_launch"));
        return false;
    }
    qint64 childPid = 0;
    if (!QProcess::startDetached(launch.program, launch.arguments,
                                 launch.workingDirectory, &childPid)) {
        setError(error, QStringLiteral("installer_launch_failed"));
        return false;
    }
    return true;
}

QString UpdateInstallBridge::exactSibling(const QString& name) const
{
    const QString installRoot = cleanAbsolute(m_paths.expectedInstallRoot);
    const QFileInfo rootInfo(installRoot);
    return QDir(rootInfo.absoluteDir()).filePath(name);
}

bool UpdateInstallBridge::removeExactSibling(const QString& candidate,
                                             const QString& expectedName) const
{
    const QString expected = exactSibling(expectedName);
    const QFileInfo candidateInfo(cleanAbsolute(candidate));
    if (candidateInfo.isSymLink() || !candidateInfo.isDir()
        || !samePath(candidateInfo.absoluteFilePath(), expected))
        return false;
    return QDir(candidateInfo.absoluteFilePath()).removeRecursively();
}

void UpdateInstallBridge::acknowledgeHealthyBoot(const QStringList& arguments)
{
    QString result;
    QString backup;
    QString target;
    for (const QString& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--update-result=")))
            result = argument.mid(QStringLiteral("--update-result=").size());
        else if (argument.startsWith(QStringLiteral("--update-backup=")))
            backup = argument.mid(QStringLiteral("--update-backup=").size());
        else if (argument.startsWith(QStringLiteral("--update-target=")))
            target = argument.mid(QStringLiteral("--update-target=").size());
    }

    if (result == QLatin1String("success")) {
        if (!backup.isEmpty() && !removeExactSibling(backup, QStringLiteral("Colosseum.__update-old")))
            qWarning("[update] refused unsafe backup cleanup: %s", qUtf8Printable(backup));
    } else if (result == QLatin1String("rollback")) {
        if (!target.isEmpty() && !removeExactSibling(target, QStringLiteral("Colosseum.__update-new")))
            qWarning("[update] refused unsafe rollback cleanup: %s", qUtf8Printable(target));
    }
}

} // namespace Colosseum::Update
