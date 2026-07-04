#include "windowmodestore.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

WindowModeStore::WindowModeStore(QObject *parent)
    : QObject(parent) {}

void WindowModeStore::enterPip(QQuickWindow *window) {
    if (!window)
        return;
    if (!m_pipMode) {
        m_snapshot = snapshotFor(window);
        m_hasSnapshot = true;
    }

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

    window->showNormal();
    if (m_hasSnapshot) {
        window->setFlags(m_snapshot.flags);
        window->setMinimumSize(QSize(960, 600));
        window->setGeometry(m_snapshot.geometry);
        if (m_snapshot.visibility == QWindow::FullScreen)
            window->showFullScreen();
        else if (m_snapshot.visibility == QWindow::Maximized)
            window->showMaximized();
        else
            window->showNormal();
        m_hasSnapshot = false;
    } else {
        window->setFlags(Qt::Window | Qt::FramelessWindowHint);
        window->setMinimumSize(QSize(960, 600));
        window->setGeometry(QRect(80, 80, 1280, 800));
        window->showNormal();
    }

    window->requestActivate();
    setPipMode(false);
    emit pipExited();
}

WindowModeStore::WindowSnapshot WindowModeStore::snapshotFor(QQuickWindow *window) const {
    WindowSnapshot snapshot;
    snapshot.geometry = window->geometry();
    snapshot.flags = window->flags();
    snapshot.visibility = window->visibility();
    return snapshot;
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
