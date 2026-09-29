#pragma once

// ScrollProbe — what does the mouse wheel actually send, and how much of it moves the page?
//
// WHY (2026-09-29). Hemanth: covering distance with the mouse wheel takes far longer than in the
// mock. Frame-drop metrics never measured motion. This logs the ground truth: every wheel event
// Qt delivers to a Quick window (time, angleDelta, pixelDelta, device, phase). ScrollGlide logs
// what it received, every frame it drained, and every backlog it threw away (with the reason), so
// input rolled vs distance travelled can be compared line by line.
//
// OFF unless COLOSSEUM_SCROLL_PROBE=1. When off, no filter is installed and QML sees false.
// Lines go to the app log as "SCROLL_PROBE os ..." (here) and "SCROLL_PROBE glide ..." (QML).

#include <QDateTime>
#include <QEvent>
#include <QInputDevice>
#include <QObject>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtGlobal>

class ScrollProbe : public QObject {
public:
    static bool enabledFromEnv()
    {
        return qEnvironmentVariable("COLOSSEUM_SCROLL_PROBE") == QLatin1String("1");
    }

    using QObject::QObject;

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        if (event->type() == QEvent::Wheel && qobject_cast<QQuickWindow*>(receiver)) {
            const auto* wheel = static_cast<QWheelEvent*>(event);
            const QPointingDevice* device = wheel->pointingDevice();
            qInfo().noquote() << QStringLiteral(
                "SCROLL_PROBE os t=%1 ad=%2,%3 pd=%4,%5 dev=%6 name=\"%7\" phase=%8 inverted=%9 y=%10")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(wheel->angleDelta().x()).arg(wheel->angleDelta().y())
                .arg(wheel->pixelDelta().x()).arg(wheel->pixelDelta().y())
                .arg(device ? int(device->type()) : -1)
                .arg(device ? device->name() : QString())
                .arg(int(wheel->phase()))
                .arg(wheel->inverted() ? 1 : 0)
                .arg(wheel->position().y(), 0, 'f', 0);
        }
        return false;
    }
};
