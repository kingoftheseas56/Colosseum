#include "AndroidWindowModeAdapter.h"

#include <QQuickWindow>

namespace Colosseum::Platform {

AndroidWindowModeAdapter::AndroidWindowModeAdapter(QObject *parent)
    : QObject(parent) {
}

void AndroidWindowModeAdapter::enterPip(QQuickWindow *window) {
    Q_UNUSED(window)
}

void AndroidWindowModeAdapter::exitPip(QQuickWindow *window) {
    Q_UNUSED(window)
}

void AndroidWindowModeAdapter::initializeShell(QQuickWindow *window) {
    if (window)
        window->showFullScreen();
}

void AndroidWindowModeAdapter::toggleShellMode(QQuickWindow *window) {
    if (window && window->visibility() != QWindow::FullScreen)
        window->showFullScreen();
}

void AndroidWindowModeAdapter::toggleMaximized(QQuickWindow *window) {
    Q_UNUSED(window)
}

bool AndroidWindowModeAdapter::startSystemMove(QQuickWindow *window) {
    Q_UNUSED(window)
    return false;
}

bool AndroidWindowModeAdapter::startSystemResize(QQuickWindow *window, int edges) {
    Q_UNUSED(window)
    Q_UNUSED(edges)
    return false;
}

} // namespace Colosseum::Platform
