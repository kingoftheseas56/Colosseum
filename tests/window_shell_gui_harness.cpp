// GUI harness (offscreen QPA) for the parts of WindowModeStore that need a real
// QQuickWindow: PiP round-trips onto the base shell mode, F11-in-PiP ordering, the
// live-reload re-attach lifecycle, and restore-from-minimize. Pure geometry math lives
// in the QCoreApplication window_state_policy_harness; this drives real transitions.
//
// RUNNING from build-msvc: the windeployqt'd platforms/ dir beside the exe carries ONLY
// qwindows.dll and overrides the Qt install's plugins, so "offscreen" fails with a
// silent 0xC0000409 fail-fast (output needs QT_FORCE_STDERR_LOGGING=1 to even appear).
// Set QT_QPA_PLATFORM=offscreen AND
//     QT_QPA_PLATFORM_PLUGIN_PATH=C:/Qt/6.11.1/msvc2022_64/plugins/platforms
#include "player/windowmodestore.h"
#include "player/windowstatepolicy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QRect>
#include <QSettings>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        qFatal("window_shell_gui_harness: %s", message);
}

void settleEvents() {
    for (int i = 0; i < 20; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

#ifdef Q_OS_WIN
QSize nativeWindowSize(QQuickWindow &window) {
    RECT rect{};
    require(GetWindowRect(reinterpret_cast<HWND>(window.winId()), &rect),
            "GetWindowRect must succeed for the shell HWND");
    return QSize(rect.right - rect.left, rect.bottom - rect.top);
}
#endif
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
        require(win.screen(), "fullscreen shell must resolve an active screen");
        require(win.visibility() == QWindow::Windowed,
                "borderless fullscreen must remain a visible Windowed native surface");
        require(win.geometry() == win.screen()->geometry(),
                "borderless fullscreen must cover the active monitor geometry");
        const WId originalId = win.winId();
        settleEvents();
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            const qreal dpr = win.devicePixelRatio();
            const QSize expectedNativeSize(
                qRound(win.screen()->geometry().width() * dpr),
                qRound(win.screen()->geometry().height() * dpr));
            require(nativeWindowSize(win) == expectedNativeSize,
                    "native borderless HWND must cover the complete monitor, not the work area");
        }
#endif
        store.toggleShellMode(&win);
        store.toggleShellMode(&win);
        require(win.winId() == originalId,
                "fullscreen round-trip must retain the same native window identity");
        require(win.visibility() == QWindow::Windowed,
                "fullscreen round-trip must never enter native FullScreen visibility");
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

    // --- Test E: restore-from-minimize must reassert borderless fullscreen geometry ---
    // On Windows, restoring a minimized frameless-fullscreen shell from the taskbar lands
    // it in Windowed visibility at a default tiny "normal placement" rect (the window was
    // born fullscreen, so it has none) — the centered-blob bug, 2026-07-16. The old
    // Main.qml one-liner guard was removed by the F11 mode; the authority must do it now.
    {
        clearSettings();
        WindowModeStore store;
        QQuickWindow win;
        win.setVisible(false);
        store.initializeShell(&win);
        require(win.screen(), "fullscreen restore test needs an active screen");
        require(win.visibility() == QWindow::Windowed,
                "borderless fullscreen must use Windowed visibility at init");
        require(win.geometry() == win.screen()->geometry(),
                "fullscreen base must start at full-monitor geometry");
        const WId fullscreenId = win.winId();
        win.showMinimized();
        settleEvents();
        win.showNormal();   // what the taskbar restore does to the shell
        settleEvents();
        require(win.isVisible() && win.visibility() != QWindow::Minimized,
                "restore from minimize must make the fullscreen shell visible");
        require(win.winId() == fullscreenId,
                "restore from minimize must retain the same native window identity");
        require(win.geometry() == win.screen()->geometry(),
                "restore from minimize must reassert full-monitor geometry");
    }
    qInfo("window_shell_gui_harness: fullscreen restore-from-minimize OK");

    // --- Test F: the reassert must NOT fight legitimate windowed or PiP states ---
    {
        seedWindowed(QRect(120, 140, 1280, 720));
        WindowModeStore store;
        QQuickWindow win;
        win.setVisible(false);
        store.initializeShell(&win);
        require(store.shellWindowed(), "windowed base loads");
        win.showMinimized();
        settleEvents();
        win.showNormal();
        settleEvents();
        require(win.visibility() == QWindow::Windowed,
                "windowed-base restore must stay windowed (never forced fullscreen)");
    }
    {
        clearSettings();
        WindowModeStore store;
        QQuickWindow win;
        win.setVisible(false);
        store.initializeShell(&win);
        store.enterPip(&win);
        settleEvents();
        require(store.pipMode(), "in PiP");
        require(win.visibility() == QWindow::Windowed && store.pipMode(),
                "PiP sits in Windowed visibility and must be left alone by the reassert");
    }
    qInfo("window_shell_gui_harness: windowed/PiP restore untouched OK");

    qInfo("window_shell_gui_harness: PASS");
    return 0;
}
