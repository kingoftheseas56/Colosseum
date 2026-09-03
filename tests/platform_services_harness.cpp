#include "account/AccountCredentialStoreFactory.h"
#include "account/AccountDeviceIdentity.h"
#include "account/AndroidAccountCredentialStore.h"
#include "account/AndroidSecureStorageBackend.h"
#include "platform/AndroidWindowModeAdapter.h"
#include "platform/BackgroundDownloadPolicy.h"
#include "platform/PlatformKind.h"
#include "platform/PlatformRuntime.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QPlatformSurfaceEvent>
#include <QUuid>
#include <QWindow>

#include <cstdlib>
#include <iostream>

namespace {

class MemoryAndroidSecureStorage final : public AndroidSecureStorageBackend {
public:
    bool isAvailable() const override { return available; }

    std::optional<QByteArray> read(const QString &key) const override {
        const auto it = values.constFind(key);
        return it == values.cend() ? std::nullopt
                                   : std::optional<QByteArray>(*it);
    }

    bool write(const QString &key, const QByteArray &value) override {
        if (!available || key.isEmpty())
            return false;
        values.insert(key, value);
        return true;
    }

    bool remove(const QString &key) override {
        if (!available)
            return false;
        values.remove(key);
        return true;
    }

    QStringList keys(const QString &prefix) const override {
        QStringList result;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (it.key().startsWith(prefix))
                result.append(it.key());
        }
        return result;
    }

    bool available = true;
    QHash<QString, QByteArray> values;
};

bool expect(bool condition, const char *message) {
    if (condition)
        return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

} // namespace

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);
    bool ok = true;

    using namespace Colosseum::Platform;
    ok &= expect(selectKind(true, true, true) == Kind::Android,
                 "Android must win platform selection before Linux");
    ok &= expect(selectKind(false, true, false) == Kind::WindowsDesktop,
                 "Windows selection failed");
    ok &= expect(selectKind(false, false, true) == Kind::LinuxDesktop,
                 "Linux selection failed");
    ok &= expect(selectKind(false, false, false) == Kind::Other,
                 "fallback selection failed");

    const Capabilities androidCaps = capabilitiesFor(Kind::Android);
    ok &= expect(androidCaps.systemBack && androidCaps.safeAreaInsets
                     && androidCaps.softwareKeyboard
                     && androidCaps.storageAccessFramework
                     && androidCaps.backgroundDownloadNotifications
                     && androidCaps.playbackScreenInhibit,
                 "Android capability set is incomplete");
    ok &= expect(!androidCaps.desktopUpdater && !androidCaps.desktopWindowChrome,
                 "desktop capabilities leaked into Android");
    const Capabilities windowsCaps = capabilitiesFor(Kind::WindowsDesktop);
    ok &= expect(windowsCaps.desktopUpdater && windowsCaps.desktopWindowChrome,
                 "Windows desktop capabilities regressed");

    Runtime androidRuntime(nullptr, Kind::Android);
    int backCount = 0;
    int storageCount = 0;
    int permissionCount = 0;
    QObject::connect(&androidRuntime, &Runtime::backRequested,
                     [&backCount] { ++backCount; });
    QObject::connect(&androidRuntime, &Runtime::storageAccessRequested,
                     [&storageCount](const QString &) { ++storageCount; });
    QObject::connect(&androidRuntime, &Runtime::permissionRequested,
                     [&permissionCount](const QString &) { ++permissionCount; });

    androidRuntime.dispatchSystemBack();
    ok &= expect(backCount == 1, "explicit Android Back dispatch was lost");
    ok &= expect(androidRuntime.requestStorageAccess(QStringLiteral("vault")),
                 "Android SAF request was rejected");
    ok &= expect(storageCount == 1, "Android SAF request signal missing");
    ok &= expect(androidRuntime.requestPermission(QStringLiteral("POST_NOTIFICATIONS")),
                 "Android permission request was rejected");
    ok &= expect(permissionCount == 1, "Android permission signal missing");

    QWindow androidWindow;
    androidRuntime.attachWindow(&androidWindow);
    QKeyEvent backEvent(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier);
    QCoreApplication::sendEvent(&androidWindow, &backEvent);
    ok &= expect(backCount == 2 && backEvent.isAccepted(),
                 "Android window Back event did not converge on backRequested");

    QPlatformSurfaceEvent created(QPlatformSurfaceEvent::SurfaceCreated);
    QCoreApplication::sendEvent(&androidWindow, &created);
    ok &= expect(androidRuntime.surfaceAvailable(),
                 "surface recreation did not publish availability");
    QPlatformSurfaceEvent destroyed(QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed);
    QCoreApplication::sendEvent(&androidWindow, &destroyed);
    ok &= expect(!androidRuntime.surfaceAvailable(),
                 "surface destruction did not clear availability");

    AndroidWindowModeAdapter androidWindowMode;
    ok &= expect(!androidWindowMode.pipMode() && !androidWindowMode.shellWindowed()
                     && !androidWindowMode.savedMaximized(),
                 "Android window-mode adapter exposed desktop state");
    ok &= expect(!androidWindowMode.startSystemMove(nullptr)
                     && !androidWindowMode.startSystemResize(nullptr, 0),
                 "Android window-mode adapter accepted desktop mutations");

    Runtime desktopRuntime(nullptr, Kind::WindowsDesktop);
    int desktopBackCount = 0;
    QObject::connect(&desktopRuntime, &Runtime::backRequested,
                     [&desktopBackCount] { ++desktopBackCount; });
    desktopRuntime.dispatchSystemBack();
    ok &= expect(desktopBackCount == 0,
                 "Android Back behavior leaked into desktop runtime");
    ok &= expect(!desktopRuntime.requestStorageAccess(QStringLiteral("vault")),
                 "SAF leaked into desktop runtime");
    ok &= expect(!desktopRuntime.requestPermission(QStringLiteral("CAMERA")),
                 "Android permission seam leaked into desktop runtime");

    MemoryAndroidSecureStorage secureStorage;
    AndroidAccountCredentialStore credentialStore(&secureStorage);
    ok &= expect(credentialStore.isAvailable(),
                 "Android credential store did not adopt secure backend");

    StoredAccountCredential credential;
    credential.accountId = QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
    credential.deviceId = QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
    credential.refreshToken = QByteArrayLiteral("refresh-token-a");
    ok &= expect(credentialStore.saveActive(credential),
                 "Android active credential save failed");
    const auto loaded = credentialStore.loadActive();
    ok &= expect(loaded.has_value()
                     && QUuid(loaded->accountId) == QUuid(credential.accountId)
                     && QUuid(loaded->deviceId) == QUuid(credential.deviceId)
                     && loaded->refreshToken == credential.refreshToken
                     && loaded->accountId == loaded->accountId.toLower()
                     && loaded->deviceId == loaded->deviceId.toLower(),
                 "Android active credential round-trip/normalization failed");

    const QByteArray revokedA("revoked-a");
    const QByteArray revokedB("revoked-b");
    ok &= expect(credentialStore.addPendingRevocation(revokedA)
                     && credentialStore.addPendingRevocation(revokedA)
                     && credentialStore.addPendingRevocation(revokedB),
                 "Android pending-revocation persistence failed");
    const QList<QByteArray> pending = credentialStore.pendingRevocations();
    ok &= expect(pending.size() == 2 && pending.contains(revokedA) && pending.contains(revokedB),
                 "Android pending-revocation set did not deduplicate");
    ok &= expect(credentialStore.removePendingRevocation(revokedA)
                     && !credentialStore.pendingRevocations().contains(revokedA),
                 "Android pending-revocation removal failed");
    ok &= expect(credentialStore.clearActive() && !credentialStore.loadActive().has_value(),
                 "Android active credential clear failed");

    auto unavailableAndroid = createAccountCredentialStore(Kind::Android, nullptr);
    ok &= expect(unavailableAndroid && !unavailableAndroid->isAvailable(),
                 "Android secure-store fallback must fail closed");
    auto unavailableLinux = createAccountCredentialStore(Kind::LinuxDesktop, nullptr);
    ok &= expect(unavailableLinux && !unavailableLinux->isAvailable(),
                 "unsupported secure store must fail closed");

#if defined(Q_OS_WIN)
    AccountDeviceIdentity desktopIdentity;
    ok &= expect(desktopIdentity.label() == QStringLiteral("Windows desktop")
                     && desktopIdentity.platform() == QStringLiteral("Windows"),
                 "Windows account device identity regressed");
#endif

    QVariantMap activeJob{{QStringLiteral("id"), QStringLiteral("job-1")},
                          {QStringLiteral("state"), QStringLiteral("downloading")}};
    QVariantMap pausedJob{{QStringLiteral("id"), QStringLiteral("job-2")},
                          {QStringLiteral("state"), QStringLiteral("paused")}};
    QVariantMap doneJob{{QStringLiteral("id"), QStringLiteral("job-3")},
                        {QStringLiteral("state"), QStringLiteral("done")}};
    ok &= expect(jobRequiresBackgroundHost(activeJob),
                 "active download did not request Android host lifetime");
    ok &= expect(!jobRequiresBackgroundHost(pausedJob)
                     && !jobRequiresBackgroundHost(doneJob),
                 "terminal/paused download incorrectly requested Android host lifetime");

    if (!ok)
        return EXIT_FAILURE;
    std::cout << "PLATFORM_SERVICES_OK\n";
    return EXIT_SUCCESS;
}
