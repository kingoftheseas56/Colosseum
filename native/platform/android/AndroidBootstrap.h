#pragma once

#include <QString>

class QGuiApplication;
class QQmlApplicationEngine;
class QQuickWindow;
class Reader2Bridge;

namespace Colosseum::Platform {

bool bootstrapAndroidStartup(const QString &instanceAppData,
                             QString *startupManifestPathOverride);

void installAndroidEngineBindings(QQmlApplicationEngine *engine,
                                  Reader2Bridge *reader2Bridge,
                                  QGuiApplication *app);

void attachAndroidRootWindow(QQuickWindow *rootWindow);

} // namespace Colosseum::Platform
