#include "platform/PlatformRuntime.h"
#include <QQmlEngine>
#include <QQmlContext>
#include <QtQuickTest/quicktest.h>

class FontScaleSetup : public QObject {
    Q_OBJECT
public:
    Colosseum::Platform::Runtime runtime{nullptr, Colosseum::Platform::Kind::Android};
    Q_INVOKABLE void setScale(qreal value) { runtime.acceptFontScale(value); }
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("PlatformRuntime", &runtime);
        engine->rootContext()->setContextProperty("FontTestFixture", this);
    }
};
QUICK_TEST_MAIN_WITH_SETUP(phone_font_scaling, FontScaleSetup)
#include "phone_font_scaling_harness.moc"
