#include "player/windowstatepolicy.h"
#include "player/windowmodestore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        qFatal("window_state_policy_harness: %s", message);
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    require(WindowStatePolicy::defaultSize() == QSize(1280, 720),
            "default size must be 1280x720");
    require(WindowStatePolicy::minimumSize() == QSize(1024, 640),
            "minimum size must be 1024x640");

    const QRect primary(0, 0, 1920, 1040);
    require(WindowStatePolicy::centeredDefault(primary)
                == QRect(320, 160, 1280, 720),
            "default rectangle must be centered in available geometry");

    const QList<QRect> twoScreens{
        QRect(-1920, 0, 1920, 1040),
        primary
    };
    require(WindowStatePolicy::validatedNormalGeometry(
                QRect(-1800, 80, 1280, 720), twoScreens, primary)
                == QRect(-1800, 80, 1280, 720),
            "visible geometry on a secondary screen must be preserved");

    require(WindowStatePolicy::validatedNormalGeometry(
                QRect(5000, 5000, 1280, 720), twoScreens, primary)
                == QRect(320, 160, 1280, 720),
            "fully off-screen geometry must recenter");

    require(WindowStatePolicy::validatedNormalGeometry(
                QRect(80, 80, 800, 500), twoScreens, primary)
                == QRect(320, 160, 1280, 720),
            "undersized geometry must use the safe default");

    require(WindowStatePolicy::validatedNormalGeometry(
                QRect(1500, 700, 1280, 720), {primary}, primary)
                == QRect(640, 320, 1280, 720),
            "partly off-screen geometry must clamp inside the chosen screen");

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory must exist");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowModeHarness"));

    {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("window/baseMode"),
                          QStringLiteral("windowed"));
        settings.setValue(QStringLiteral("window/normalGeometry"),
                          QRect(100, 120, 1280, 720));
        settings.setValue(QStringLiteral("window/maximized"), true);
        settings.sync();
    }

    WindowModeStore restored;
    require(restored.shellWindowed(), "saved windowed base mode must load");
    require(restored.savedNormalGeometry() == QRect(100, 120, 1280, 720),
            "saved normal geometry must load independently");
    require(restored.savedMaximized(), "saved maximized state must load");

    qInfo("window_state_policy_harness: PASS");
    return 0;
}
