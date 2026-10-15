#include "update/UpdateInstallBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>

using Colosseum::Update::InstallLaunch;
using Colosseum::Update::UpdateInstallBridge;
using Colosseum::Update::Version;

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        qCritical("UPDATE_INSTALL_BRIDGE_FAIL: %s", message);
        std::exit(1);
    }
}

Version version(const QString &value)
{
    const auto parsed = Version::parseCanonical(value);
    require(parsed.has_value(), "fixture version parses");
    return *parsed;
}

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "fixture file opens");
    file.write("fixture");
}

QString argValue(const QStringList &args, const QString &prefix)
{
    for (const QString &arg : args) {
        if (arg.startsWith(prefix))
            return arg.mid(prefix.size());
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary layout created");

    const QString root = temp.path();
    const QString installRoot = QDir(root).filePath("Programs/Colosseum");
    const QString executable = QDir(installRoot).filePath("native/build-msvc/colosseum.exe");
    const QString cacheRoot = QDir(root).filePath("update-cache");
    QDir().mkpath(QFileInfo(executable).absolutePath());
    touch(executable);

    UpdateInstallBridge::Paths paths;
    paths.executablePath = executable;
    paths.expectedInstallRoot = installRoot;
    paths.registryInstallRoot = installRoot;
    paths.cacheRoot = cacheRoot;
    UpdateInstallBridge bridge(paths);

#ifndef Q_OS_WIN
    require(!bridge.installedBuildEligible(),
            "non-Windows builds are never eligible for automatic installer launch");
    const Version linuxTarget = version("1.1.1");
    const QString linuxInstaller = QDir(cacheRoot).filePath(
        "1.1.1/Colosseum-1.1.1-setup.exe");
    touch(linuxInstaller);
    QString linuxError;
    require(!bridge.prepare(linuxInstaller, linuxTarget, &linuxError).has_value(),
            "non-Windows bridge refuses a verified Windows installer");
    require(linuxError == QStringLiteral("automatic_install_unsupported_platform"),
            "non-Windows refusal is an explicit platform policy");
    InstallLaunch craftedLaunch;
    craftedLaunch.program = QStringLiteral("/bin/true");
    craftedLaunch.workingDirectory = root;
    linuxError.clear();
    require(!bridge.launchDetached(craftedLaunch, &linuxError),
            "non-Windows bridge refuses a crafted direct launch bypass");
    require(linuxError == QStringLiteral("automatic_install_unsupported_platform"),
            "direct launch bypass fails with the same explicit platform policy");
    const QString linuxBackup = QDir(root).filePath("Programs/Colosseum.__update-old");
    QDir().mkpath(linuxBackup);
    bridge.acknowledgeHealthyBoot({QStringLiteral("--update-result=success"),
                                   QStringLiteral("--update-backup=") + linuxBackup});
    require(QFileInfo::exists(linuxBackup),
            "non-Windows bridge ignores Windows updater cleanup arguments");
    qInfo("UPDATE_INSTALL_BRIDGE_OK");
    return 0;
#else
    require(bridge.installedBuildEligible(), "installed layout is eligible");

    QDir().mkpath(QDir(root).filePath(".git"));
    UpdateInstallBridge sourceTree(paths);
    require(!sourceTree.installedBuildEligible(), "source-tree executable is suppressed");
    QDir(root).rmdir(".git");

    qputenv("COLOSSEUM_DEV", "1");
    UpdateInstallBridge devBuild(paths);
    require(!devBuild.installedBuildEligible(), "COLOSSEUM_DEV suppresses update eligibility");
    qunsetenv("COLOSSEUM_DEV");

    UpdateInstallBridge::Paths wrongRegistry = paths;
    wrongRegistry.registryInstallRoot = QDir(root).filePath("Programs/Other");
    UpdateInstallBridge registryMismatch(wrongRegistry);
    require(!registryMismatch.installedBuildEligible(), "registry root must match install root");

    const Version target = version("1.1.1");
    const QString installer = QDir(cacheRoot).filePath("1.1.1/Colosseum-1.1.1-setup.exe");
    touch(installer);
    QString error;
    const auto launch = bridge.prepare(installer, target, &error);
    require(launch.has_value() && error.isEmpty(), "verified installer prepares a launch");
    require(launch->program == QFileInfo(installer).absoluteFilePath(), "installer is the program");
    require(launch->workingDirectory == QFileInfo(installer).absolutePath(), "working directory is sibling");
    require(launch->arguments.contains(QStringLiteral("/UPDATE=1")), "update mode argument present");
    require(launch->arguments.contains(QStringLiteral("/RESTART=1")), "restart argument present");
    require(launch->arguments.contains(QStringLiteral("/TARGETVERSION=1.1.1")), "target version present");
    require(launch->arguments.contains(QStringLiteral("/WAITPID=") +
                    QString::number(QCoreApplication::applicationPid())), "wait pid is exact");
    require(argValue(launch->arguments, QStringLiteral("/LOG=")).startsWith(cacheRoot),
            "installer log remains outside the payload");

    const QString outsideInstaller = QDir(root).filePath("outside/installer.exe");
    touch(outsideInstaller);
    error.clear();
    require(!bridge.prepare(outsideInstaller, target, &error).has_value(),
            "installer outside update cache is rejected");
    require(!error.isEmpty(), "outside-cache rejection explains itself");

    const QString updateParent = QFileInfo(installRoot).absolutePath();
    const QString oldRoot = QDir(updateParent).filePath("Colosseum.__update-old");
    touch(QDir(oldRoot).filePath("sentinel.txt"));
    bridge.acknowledgeHealthyBoot({
        QStringLiteral("colosseum.exe"),
        QStringLiteral("--update-result=success"),
        QStringLiteral("--update-from=") + installRoot,
        QStringLiteral("--update-backup=") + oldRoot,
    });
    require(!QDir(oldRoot).exists(), "exact sibling backup is cleaned after healthy boot");

    const QString unsafe = QDir(root).filePath("unsafe-backup");
    touch(QDir(unsafe).filePath("sentinel.txt"));
    bridge.acknowledgeHealthyBoot({
        QStringLiteral("colosseum.exe"),
        QStringLiteral("--update-result=success"),
        QStringLiteral("--update-backup=") + unsafe,
    });
    require(QDir(unsafe).exists(), "backup outside exact sibling is never cleaned");

    const QString failedBackup = QDir(updateParent).filePath("Colosseum.__update-old");
    touch(QDir(failedBackup).filePath("sentinel.txt"));
    bridge.acknowledgeHealthyBoot({
        QStringLiteral("colosseum.exe"),
        QStringLiteral("--update-result=failure"),
        QStringLiteral("--update-backup=") + failedBackup,
    });
    require(QDir(failedBackup).exists(), "failed update leaves recovery backup untouched");
    QDir(failedBackup).removeRecursively();

    const QString newRoot = QDir(updateParent).filePath("Colosseum.__update-new");
    touch(QDir(newRoot).filePath("sentinel.txt"));
    bridge.acknowledgeHealthyBoot({
        QStringLiteral("colosseum.exe"),
        QStringLiteral("--update-result=rollback"),
        QStringLiteral("--update-target=") + newRoot,
    });
    require(!QDir(newRoot).exists(), "exact sibling new payload is cleaned after rollback");

    qInfo("UPDATE_INSTALL_BRIDGE_OK");
    return 0;
#endif
}
