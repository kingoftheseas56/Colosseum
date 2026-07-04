#pragma once

#include <QObject>
#include <QRect>
#include <QWindow>

class QQuickWindow;

class WindowModeStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool pipMode READ pipMode NOTIFY changed)

public:
    explicit WindowModeStore(QObject *parent = nullptr);

    bool pipMode() const { return m_pipMode; }

    Q_INVOKABLE void enterPip(QQuickWindow *window);
    Q_INVOKABLE void exitPip(QQuickWindow *window);

signals:
    void changed();
    void pipEntered();
    void pipExited();

private:
    struct WindowSnapshot {
        QRect geometry;
        Qt::WindowFlags flags;
        QWindow::Visibility visibility = QWindow::Windowed;
    };

    WindowSnapshot snapshotFor(QQuickWindow *window) const;
    QRect pipGeometryFor(QQuickWindow *window) const;
    void setPipMode(bool enabled);

    bool m_pipMode = false;
    bool m_hasSnapshot = false;
    WindowSnapshot m_snapshot;
};
