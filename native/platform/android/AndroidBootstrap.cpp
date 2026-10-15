#include "platform/android/AndroidBootstrap.h"

#include "bootstrap/StartupLayout.h"
#include "platform/AndroidGraphicsWorkarounds.h"
#include "platform/AndroidWindowModeAdapter.h"
#include "player/androidmedia3item.h"
#include "player/androidseekthumbnailer.h"
#include "reader2/AndroidEbookRenderer.h"

#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <qqml.h>

namespace Colosseum::Platform {

bool bootstrapAndroidStartup(const QString &instanceAppData,
                             QString *startupManifestPathOverride) {
    // Android packages the filtered source-shaped runtime under assets:/. Materialize
    // it once per content hash into app-private storage before any owner resolves
    // checkout-relative qml/assets/resources paths.
    QString runtimeBundleError;
    const QString runtimeCacheRoot = QDir(instanceAppData).filePath(QStringLiteral("runtime"));
    const auto runtimeRoot = materializeRuntimeBundle(
        QStringLiteral("assets:/colosseum-runtime"), runtimeCacheRoot, &runtimeBundleError);
    if (!runtimeRoot) {
        qCritical("[boot] Android runtime bundle rejected: %s",
                  qUtf8Printable(runtimeBundleError));
        return false;
    }
    if (!QDir::setCurrent(*runtimeRoot)) {
        qCritical("[boot] Android runtime root unavailable: %s", qUtf8Printable(*runtimeRoot));
        return false;
    }

    // QML asks for "Segoe UI" in Theme.ui and several direct use sites. Android does not ship
    // that family, so bind the requested UI family to Android's real sans-serif family before
    // installing the symbol fallback. Otherwise the Script_Common fallback wins for ordinary
    // body text as well as missing symbols.
    QFont::insertSubstitution(QStringLiteral("Segoe UI"), QStringLiteral("Roboto"));
    const QFont requestedUiFont(QStringLiteral("Segoe UI"));
    const QFontInfo resolvedUiFontInfo(requestedUiFont);
    qInfo("[boot] Android UI font substitution: requested=Segoe UI substitute=Roboto resolved=%s",
          qUtf8Printable(resolvedUiFontInfo.family()));

    // Android's system fonts miss several symbols used by existing QML controls. Keep every
    // use site platform-neutral and register one broad symbol fallback after the packaged
    // runtime has been materialized, but before any QML text is created.
    const QString symbolFontPath =
        QDir(*runtimeRoot).filePath(QStringLiteral("assets/fonts/Unifont-17.0.05.otf"));
    const int symbolFontId = QFontDatabase::addApplicationFont(symbolFontPath);
    if (symbolFontId < 0) {
        qWarning("[boot] Android symbol fallback font failed to load: source=%s",
                 qUtf8Printable(symbolFontPath));
    } else {
        const QStringList symbolFontFamilies =
            QFontDatabase::applicationFontFamilies(symbolFontId);
        if (symbolFontFamilies.isEmpty()) {
            qWarning("[boot] Android symbol fallback font loaded without a reported family: source=%s",
                     qUtf8Printable(symbolFontPath));
        } else {
            const QString symbolFontFamily = symbolFontFamilies.constFirst();
            QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Common,
                                                             symbolFontFamily);
            qInfo("[boot] Android symbol fallback font registered: family=%s source=%s",
                  qUtf8Printable(symbolFontFamily), qUtf8Printable(symbolFontPath));
        }
    }
    if (startupManifestPathOverride) {
        *startupManifestPathOverride =
            QDir(*runtimeRoot).filePath(QStringLiteral("qml-build.manifest"));
    }

    qmlRegisterType<AndroidMedia3Item>("Colosseum.Player", 1, 0, "PlayerItem");
    qmlRegisterType<AndroidSeekThumbnailer>("Colosseum.Player", 1, 0, "SeekThumbnailer");
    return true;
}

void installAndroidEngineBindings(QQmlApplicationEngine *engine,
                                  Reader2Bridge *reader2Bridge,
                                  QGuiApplication *app) {
    if (!engine)
        return;
    engine->rootContext()->setContextProperty(
        QStringLiteral("AndroidEbookRenderer"),
        new AndroidEbookRenderer(reader2Bridge, app));
    auto *windowMode = new AndroidWindowModeAdapter(app);
    engine->rootContext()->setContextProperty(QStringLiteral("WindowMode"), windowMode);
}

void attachAndroidRootWindow(QQuickWindow *rootWindow) {
    if (!rootWindow)
        return;
#ifdef Q_OS_ANDROID
    installAndroidGraphicsWorkarounds(rootWindow);
#endif
}

} // namespace Colosseum::Platform
