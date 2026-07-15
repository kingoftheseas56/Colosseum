#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSettings>
#include <QTimer>
#include <QWindow>

class QQuickWindow;

// The single native authority for the Colosseum shell window: fullscreen (public
// identity), the secret F11 developer-windowed mode, and the temporary PiP overlay.
// PiP remains an overlaying state ABOVE the persistent base mode — its public API
// (pipMode, enterPip, exitPip, pipEntered/Exited) is preserved intact.
class WindowModeStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool pipMode READ pipMode NOTIFY changed)
    Q_PROPERTY(bool shellWindowed READ shellWindowed NOTIFY changed)
    Q_PROPERTY(QRect savedNormalGeometry READ savedNormalGeometry NOTIFY changed)
    Q_PROPERTY(bool savedMaximized READ savedMaximized NOTIFY changed)

public:
    explicit WindowModeStore(QObject *parent = nullptr);

    bool pipMode() const { return m_pipMode; }
    bool shellWindowed() const { return m_shellWindowed; }
    QRect savedNormalGeometry() const { return m_normalGeometry; }
    bool savedMaximized() const { return m_windowedMaximized; }

    Q_INVOKABLE void enterPip(QQuickWindow *window);
    Q_INVOKABLE void exitPip(QQuickWindow *window);

    // Base shell mode: chosen once at startup, toggled by the secret F11 door.
    Q_INVOKABLE void initializeShell(QQuickWindow *window);
    Q_INVOKABLE void toggleShellMode(QQuickWindow *window);
    Q_INVOKABLE void toggleMaximized(QQuickWindow *window);
    Q_INVOKABLE bool startSystemMove(QQuickWindow *window);
    Q_INVOKABLE bool startSystemResize(QQuickWindow *window, int edges);

signals:
    void changed();
    void pipEntered();
    void pipExited();

private:
    QRect pipGeometryFor(QQuickWindow *window) const;
    void setPipMode(bool enabled);

    void applyBaseMode(QQuickWindow *window);
    void applyFullscreen(QQuickWindow *window);
    void applyWindowed(QQuickWindow *window);
    void scheduleStableCapture();
    void captureStableWindowState();
    void persistStableState();
    QList<QRect> availableScreenGeometries() const;
    QRect primaryAvailableGeometry() const;

    QPointer<QQuickWindow> m_window;
    QSettings m_settings;
    QTimer m_captureDebounce;
    QRect m_normalGeometry;
    bool m_pipMode = false;
    bool m_shellWindowed = false;
    bool m_windowedMaximized = false;
    bool m_transitioning = false;
};
