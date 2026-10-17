#pragma once

#include <QObject>
#include <QRect>

class QQuickWindow;

namespace Colosseum::Platform {

// Android compatibility surface for QML that still references the desktop
// WindowMode contract. Android has no developer-windowed mode or PiP in this
// foundation, so all desktop-only mutations fail closed.
class AndroidWindowModeAdapter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool pipMode READ pipMode NOTIFY changed)
    Q_PROPERTY(bool shellWindowed READ shellWindowed NOTIFY changed)
    Q_PROPERTY(QRect savedNormalGeometry READ savedNormalGeometry NOTIFY changed)
    Q_PROPERTY(bool savedMaximized READ savedMaximized NOTIFY changed)

public:
    explicit AndroidWindowModeAdapter(QObject *parent = nullptr);

    bool pipMode() const { return false; }
    bool shellWindowed() const { return false; }
    QRect savedNormalGeometry() const { return {}; }
    bool savedMaximized() const { return false; }

    Q_INVOKABLE void enterPip(QQuickWindow *window);
    Q_INVOKABLE void exitPip(QQuickWindow *window);
    Q_INVOKABLE void initializeShell(QQuickWindow *window);
    Q_INVOKABLE void toggleShellMode(QQuickWindow *window);
    Q_INVOKABLE void toggleMaximized(QQuickWindow *window);
    Q_INVOKABLE bool startSystemMove(QQuickWindow *window);
    Q_INVOKABLE bool startSystemResize(QQuickWindow *window, int edges);

signals:
    void changed();
    void pipEntered();
    void pipExited();
};

} // namespace Colosseum::Platform
