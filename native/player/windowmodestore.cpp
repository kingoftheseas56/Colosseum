#include "windowmodestore.h"

#include "windowstatepolicy.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

WindowModeStore::WindowModeStore(QObject *parent)
    : QObject(parent) {
    // Load only recognized, untrusted persisted values. Anything corrupt or missing
    // is normalized: unknown mode -> fullscreen; the rectangle is validated lazily
    // against the real screens in initializeShell()/applyWindowed().
    const QString mode = m_settings.value(
        QStringLiteral("window/baseMode"),
        QStringLiteral("fullscreen")).toString();
    m_shellWindowed = mode == QStringLiteral("windowed");
    m_normalGeometry = m_settings.value(
        QStringLiteral("window/normalGeometry")).toRect();
    m_windowedMaximized = m_settings.value(
        QStringLiteral("window/maximized"), false).toBool();

    // Persist normal geometry only after movement/resize settles, never on every
    // raw pointer event.
    m_captureDebounce.setSingleShot(true);
    m_captureDebounce.setInterval(250);
    connect(&m_captureDebounce, &QTimer::timeout,
            this, &WindowModeStore::captureStableWindowState);
}

// ---- PiP (Harbor parity) — now restores onto the persistent base shell mode ----

void WindowModeStore::enterPip(QQuickWindow *window) {
    if (!window)
        return;
    // Snapshot the stable normal rectangle first when the base shell is normal
    // windowed, so exiting PiP restores it exactly.
    if (!m_pipMode && m_shellWindowed
        && window->visibility() == QWindow::Windowed)
        m_normalGeometry = window->geometry();

    window->showNormal();
    window->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    window->setMinimumSize(QSize(360, 240));
    window->setGeometry(pipGeometryFor(window));
    window->show();
    window->raise();
    window->requestActivate();
    setPipMode(true);
    emit pipEntered();
}

void WindowModeStore::exitPip(QQuickWindow *window) {
    if (!window)
        return;
    // Clear PiP and restore the underlying base shell mode (fullscreen or windowed)
    // rather than a stale independent snapshot. applyBaseMode() resets the flags and
    // minimum size, so the PiP 360x240 minimum and WindowStaysOnTopHint can never
    // leak into developer-windowed mode.
    setPipMode(false);
    applyBaseMode(window);
    emit pipExited();
}

QRect WindowModeStore::pipGeometryFor(QQuickWindow *window) const {
    const int pipWidth = 480;
    const int pipHeight = 320;
    QScreen *screen = window->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const int x = available.x() + qMax(0, available.width() - pipWidth - 24);
    const int y = available.y() + qMax(0, available.height() - pipHeight - 56);
    return QRect(x, y, pipWidth, pipHeight);
}

void WindowModeStore::setPipMode(bool enabled) {
    if (m_pipMode == enabled)
        return;
    m_pipMode = enabled;
    emit changed();
}

// ---- Persistent shell-mode authority (secret windowed developer mode, 2026-07-15) ----

void WindowModeStore::initializeShell(QQuickWindow *window) {
    if (!window || m_window)
        return;
    m_window = window;

    // Validate any restored windowed rectangle against the screens present now, so a
    // saved rectangle from a removed monitor / changed resolution never strands the
    // window off-screen.
    m_normalGeometry = WindowStatePolicy::validatedNormalGeometry(
        m_normalGeometry, availableScreenGeometries(), primaryAvailableGeometry());

    connect(window, &QWindow::xChanged, this, &WindowModeStore::scheduleStableCapture);
    connect(window, &QWindow::yChanged, this, &WindowModeStore::scheduleStableCapture);
    connect(window, &QWindow::widthChanged, this, &WindowModeStore::scheduleStableCapture);
    connect(window, &QWindow::heightChanged, this, &WindowModeStore::scheduleStableCapture);
    connect(window, &QWindow::visibilityChanged, this, &WindowModeStore::scheduleStableCapture);
    // A shutdown mid-transition must still leave the last stable state on disk.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        captureStableWindowState();
        persistStableState();
    });

    applyBaseMode(window);
    emit changed();
}

void WindowModeStore::toggleShellMode(QQuickWindow *window) {
    if (!window || m_transitioning)
        return;
    m_transitioning = true;
    // F11 stays authoritative for the WHOLE shell: exit PiP first, then flip the
    // underlying base mode.
    if (m_pipMode)
        exitPip(window);
    if (m_shellWindowed)
        captureStableWindowState();
    m_shellWindowed = !m_shellWindowed;
    persistStableState();
    applyBaseMode(window);
    m_transitioning = false;
    emit changed();
}

void WindowModeStore::applyBaseMode(QQuickWindow *window) {
    if (!window)
        return;
    if (m_shellWindowed)
        applyWindowed(window);
    else
        applyFullscreen(window);
}

void WindowModeStore::applyFullscreen(QQuickWindow *window) {
    window->setFlags(Qt::Window | Qt::FramelessWindowHint);
    window->setMinimumSize(QSize());
    window->showFullScreen();
    window->requestActivate();
}

void WindowModeStore::applyWindowed(QQuickWindow *window) {
    const QRect restored = WindowStatePolicy::validatedNormalGeometry(
        m_normalGeometry, availableScreenGeometries(),
        primaryAvailableGeometry());
    m_normalGeometry = restored;
    window->showNormal();
    window->setFlags(Qt::Window | Qt::FramelessWindowHint);
    window->setMinimumSize(WindowStatePolicy::minimumSize());
    window->setGeometry(restored);
    if (m_windowedMaximized)
        window->showMaximized();
    else
        window->showNormal();
    window->requestActivate();
}

void WindowModeStore::toggleMaximized(QQuickWindow *window) {
    // Only meaningful inside normal windowed mode; PiP and fullscreen ignore it.
    if (!window || !m_shellWindowed || m_pipMode)
        return;
    if (window->visibility() == QWindow::Maximized) {
        m_windowedMaximized = false;
        window->showNormal();
        window->setGeometry(m_normalGeometry);
    } else {
        if (window->visibility() == QWindow::Windowed)
            m_normalGeometry = window->geometry();
        m_windowedMaximized = true;
        window->showMaximized();
    }
    persistStableState();
    emit changed();
}

bool WindowModeStore::startSystemMove(QQuickWindow *window) {
    if (!window)
        return false;
    return window->startSystemMove();
}

bool WindowModeStore::startSystemResize(QQuickWindow *window, int edges) {
    // Reject zero/invalid masks and only resize in normal, non-PiP windowed mode.
    if (!window || edges == 0)
        return false;
    if (!m_shellWindowed || m_pipMode)
        return false;
    if (window->visibility() != QWindow::Windowed)
        return false;
    return window->startSystemResize(static_cast<Qt::Edges>(edges));
}

void WindowModeStore::scheduleStableCapture() {
    // Transition-induced geometry churn is handled explicitly by toggleShellMode /
    // toggleMaximized; the debounce is only for genuine user move/resize.
    if (m_transitioning)
        return;
    m_captureDebounce.start();
}

void WindowModeStore::captureStableWindowState() {
    QQuickWindow *window = m_window;
    if (!window || m_pipMode || !m_shellWindowed)
        return;
    const QWindow::Visibility vis = window->visibility();
    if (vis == QWindow::Maximized) {
        // A maximized windowed shell records the maximized flag but never overwrites
        // the normal rectangle.
        if (!m_windowedMaximized) {
            m_windowedMaximized = true;
            persistStableState();
            emit changed();
        }
        return;
    }
    if (vis != QWindow::Windowed)
        return;  // minimized / fullscreen / hidden must never touch normal geometry
    bool dirty = false;
    if (m_windowedMaximized) {
        m_windowedMaximized = false;
        dirty = true;
    }
    const QRect geom = window->geometry();
    if (geom.isValid() && geom != m_normalGeometry) {
        m_normalGeometry = geom;
        dirty = true;
    }
    if (dirty) {
        persistStableState();
        emit changed();
    }
}

void WindowModeStore::persistStableState() {
    m_settings.setValue(QStringLiteral("window/baseMode"),
        m_shellWindowed ? QStringLiteral("windowed")
                        : QStringLiteral("fullscreen"));
    if (m_normalGeometry.isValid())
        m_settings.setValue(QStringLiteral("window/normalGeometry"),
                            m_normalGeometry);
    m_settings.setValue(QStringLiteral("window/maximized"),
                        m_windowedMaximized);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        qWarning("[window] failed to persist stable shell state");
}

QList<QRect> WindowModeStore::availableScreenGeometries() const {
    QList<QRect> geoms;
    const QList<QScreen *> screens = QGuiApplication::screens();
    geoms.reserve(screens.size());
    for (QScreen *screen : screens)
        if (screen)
            geoms.append(screen->availableGeometry());
    return geoms;
}

QRect WindowModeStore::primaryAvailableGeometry() const {
    QScreen *primary = QGuiApplication::primaryScreen();
    return primary ? primary->availableGeometry() : QRect(0, 0, 1920, 1040);
}
