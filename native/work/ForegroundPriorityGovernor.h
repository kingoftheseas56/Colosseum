// native/work/ForegroundPriorityGovernor.h
#pragma once

#include <QObject>

class QEvent;
class QTimer;

namespace work {

class ForegroundPriorityGovernor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int pressure READ pressure NOTIFY pressureChanged)
    Q_PROPERTY(bool immersiveSurfaceOpen READ immersiveSurfaceOpen
               WRITE setImmersiveSurfaceOpen NOTIFY immersiveSurfaceOpenChanged)
public:
    enum Pressure {
        Normal = 0,
        LatencySensitive = 1,
        Suspended = 2,
    };
    Q_ENUM(Pressure)

    explicit ForegroundPriorityGovernor(int interactionIdleMs = 350,
                                        QObject *parent = nullptr);

    int pressure() const { return static_cast<int>(m_pressure); }
    bool immersiveSurfaceOpen() const { return m_immersiveSurfaceOpen; }

    Q_INVOKABLE void noteUserInteraction();
    Q_INVOKABLE void setImmersiveSurfaceOpen(bool open);

signals:
    void pressureChanged(int pressure);
    void immersiveSurfaceOpenChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void recomputePressure();
    static bool isUserInputEvent(QEvent *event);

    QTimer *m_interactionTimer = nullptr;
    int m_interactionIdleMs = 350;
    Pressure m_pressure = Normal;
    bool m_interactionActive = false;
    bool m_immersiveSurfaceOpen = false;
};

} // namespace work
