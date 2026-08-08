#pragma once

#include "update/UpdateVersion.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace Colosseum::Update {

struct InstallLaunch final {
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

class UpdateInstallBridge final : public QObject {
    Q_OBJECT
public:
    // The path bundle is injectable so the safety rules can be tested against a
    // disposable layout without touching a user's real install or registry.
    struct Paths final {
        QString executablePath;
        QString expectedInstallRoot;
        QString registryInstallRoot;
        QString cacheRoot;
    };

    explicit UpdateInstallBridge(QObject* parent = nullptr);
    explicit UpdateInstallBridge(Paths paths, QObject* parent = nullptr);

    bool installedBuildEligible() const;
    std::optional<InstallLaunch> prepare(const QString& verifiedInstaller,
                                         const Version& target, QString* error) const;
    bool launchDetached(const InstallLaunch& launch, QString* error) const;
    void acknowledgeHealthyBoot(const QStringList& arguments);

private:
    static Paths defaultPaths();
    static QString cleanAbsolute(const QString& path);
    static bool samePath(const QString& left, const QString& right);
    static bool isWithin(const QString& root, const QString& child);
    static bool isSourceTreePath(const QString& path);
    static QString registryInstallRoot();

    QString exactSibling(const QString& name) const;
    bool removeExactSibling(const QString& candidate, const QString& expectedName) const;
    static void setError(QString* error, const QString& message);

    Paths m_paths;
};

} // namespace Colosseum::Update
