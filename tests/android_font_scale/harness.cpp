// WT-04 host harness: the real Colosseum::Platform::Runtime (Android kind) is exposed as the
// PlatformRuntime context property so QML Theme consumers react to acceptFontScale() exactly
// as they do to the Activity's native Configuration.fontScale notification. A second runtime
// of desktop kind proves the desktop baseline ignores the preference. Adapted from Alpha
// tests/phone_font_scaling_harness.cpp. Built by tests/android_font_scale_test.py.
#include "platform/PlatformRuntime.h"
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class FontScaleSetup : public QObject {
    Q_OBJECT
public:
    Colosseum::Platform::Runtime android{nullptr, Colosseum::Platform::Kind::Android};
    Colosseum::Platform::Runtime desktop{nullptr, Colosseum::Platform::Kind::LinuxDesktop};
    QQmlEngine *engine = nullptr;
    Q_INVOKABLE void setScale(qreal value) { android.acceptFontScale(value); }
    Q_INVOKABLE void setDesktopScale(qreal value) { desktop.acceptFontScale(value); }
    Q_INVOKABLE void useDesktopRuntime(bool desktopHost) {
        engine->rootContext()->setContextProperty(
            "PlatformRuntime", desktopHost ? &desktop : &android);
    }
public slots:
    void qmlEngineAvailable(QQmlEngine *available) {
        engine = available;
        engine->rootContext()->setContextProperty("PlatformRuntime", &android);
        engine->rootContext()->setContextProperty("FontTestFixture", this);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(android_font_scale, FontScaleSetup)
#include "harness.moc"
