// GUI harness (offscreen QPA) for the parts of WindowModeStore that need a real
// QQuickWindow: PiP round-trips onto the base shell mode, F11-in-PiP ordering, and
// the live-reload re-attach lifecycle. Pure geometry math lives in the QCoreApplication
// window_state_policy_harness; this one drives the actual window transitions.
#include "player/windowmodestore.h"
#include "player/windowstatepolicy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QRect>
#include <QSettings>
#include <QTemporaryDir>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        qFatal("window_shell_gui_harness: %s", message);
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory must exist");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("BrotherhoodTest"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowShellGuiHarness"));

    auto clearSettings = []() {
        QSettings s;
        s.clear();
        s.sync();
    };
    auto seedWindowed = [](const QRect &geom) {
        QSettings s;
        s.clear();
        s.setValue(QStringLiteral("window/baseMode"), QStringLiteral("windowed"));
        s.setValue(QStringLiteral("window/normalGeometry"), geom);
        s.setValue(QStringLiteral("window/maximized"), false);
        s.sync();
    };

    // --- Test B: fullscreen -> PiP -> fullscreen (Finding 3) ---
    {
        clearSettings();
        WindowModeStore store;
        QQuickWindow win;
        win.setVisible(false);
        store.initializeShell(&win);
        require(!store.shellWindowed(), "clean settings -> fullscreen base");
        require(!store.pipMode(), "not in PiP initially");
        store.enterPip(&win);
        require(store.pipMode(), "enterPip sets pipMode");
        require((win.flags() & Qt::WindowStaysOnTopHint) != 0,
                "PiP window must be always-on-top");
        store.exitPip(&win);
        require(!store.pipMode(), "exitPip clears pipMode");
        require(!store.shellWindowed(), "exitPip restores the fullscreen base mode");
        require((win.flags() & Qt::WindowStaysOnTopHint) == 0,
                "exitPip must drop always-on-top when restoring fullscreen");
    }
    qInfo("window_shell_gui_harness: fullscreen PiP round-trip OK");

    // --- Test C: windowed -> PiP -> windowed, normal geometry preserved (Finding 3) ---
    {
        seedWindowed(QRect(120, 140, 1280, 720));
        WindowModeStore store;
        QQuickWindow win;
        win.setVisible(false);
        store.initializeShell(&win);
        require(store.shellWindowed(), "seeded windowed base loads");
        const QRect normal = store.savedNormalGeometry();
        store.enterPip(&win);
        require(store.pipMode(), "enterPip sets pipMode from windowed base");
        store.exitPip(&win);
        require(!store.pipMode(), "exitPip clears pipMode");
        require(store.shellWindowed(), "exitPip restores the windowed base mode");
        require(store.savedNormalGeometry() == normal,
                "PiP round-trip must not corrupt the persisted normal geometry");
        require(win.minimumSize() == WindowStatePolicy::minimumSize(),
                "exitPip must restore the windowed minimum, not the PiP 360x240");
        require((win.flags() & Qt::WindowStaysOnTopHint) == 0,
                "exitPip must drop always-on-top when restoring windowed");
    }
    qInfo("window_shell_gui_harness: windowed PiP round-trip OK");

    // --- Test D: F11 while in PiP exits PiP first, then flips the base mode (Finding 3) ---
    {
        clearSettings();
        WindowModeStore store;
        QQuickWindow win;
        win.setVisible(false);
        store.initializeShell(&win);
        require(!store.shellWindowed(), "fullscreen base");
        store.enterPip(&win);
        require(store.pipMode(), "in PiP");
        store.toggleShellMode(&win);
        require(!store.pipMode(), "F11 in PiP exits PiP first");
        require(store.shellWindowed(), "F11 in PiP then flips the base to windowed");
    }
    qInfo("window_shell_gui_harness: F11-in-PiP ordering OK");

    // --- Test A: live-reload re-attach (Finding 1) ---
    // Colosseum's dev reloader loads the replacement root BEFORE deleting the old one,
    // so initializeShell() must re-attach to the new window instead of bailing while the
    // old root is still registered — otherwise the replacement stays visible:false.
    {
        clearSettings();
        WindowModeStore store;
        QQuickWindow first;
        first.setVisible(false);
        store.initializeShell(&first);
        require(first.isVisible(), "initial root must be shown by initializeShell");

        QQuickWindow replacement;
        replacement.setVisible(false);
        store.initializeShell(&replacement);
        require(replacement.isVisible(),
                "live-reload replacement root must be applied and shown");

        // Idempotent: re-initializing the same window must not fail or hide it.
        store.initializeShell(&replacement);
        require(replacement.isVisible(), "re-init of the same window stays shown");
    }
    qInfo("window_shell_gui_harness: live-reload re-attach OK");

    qInfo("window_shell_gui_harness: PASS");
    return 0;
}
