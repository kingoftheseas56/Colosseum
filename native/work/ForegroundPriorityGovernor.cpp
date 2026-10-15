// native/work/ForegroundPriorityGovernor.cpp
#include "work/ForegroundPriorityGovernor.h"

#include <QEvent>
#include <QDebug>
#include <QMouseEvent>
#include <QTimer>

namespace work {

ForegroundPriorityGovernor::ForegroundPriorityGovernor(int interactionIdleMs,
                                                       QObject *parent)
    : QObject(parent), m_interactionIdleMs(qMax(1, interactionIdleMs))
{
    m_interactionTimer = new QTimer(this);
    m_interactionTimer->setSingleShot(true);
    m_interactionTimer->setInterval(m_interactionIdleMs);
    connect(m_interactionTimer, &QTimer::timeout, this, [this] {
        if (!m_interactionActive)
            return;
        m_interactionActive = false;
        recomputePressure();
    });
}

void ForegroundPriorityGovernor::noteUserInteraction()
{
    if (!m_interactionActive) {
        m_interactionActive = true;
        recomputePressure();
    }
    m_interactionTimer->start(m_interactionIdleMs);
}

void ForegroundPriorityGovernor::setImmersiveSurfaceOpen(bool open)
{
    if (m_immersiveSurfaceOpen == open)
        return;
    m_immersiveSurfaceOpen = open;
    emit immersiveSurfaceOpenChanged();
    recomputePressure();
}

void ForegroundPriorityGovernor::recomputePressure()
{
    const Pressure next = m_immersiveSurfaceOpen
        ? Suspended
        : (m_interactionActive ? LatencySensitive : Normal);
    if (m_pressure == next)
        return;
    m_pressure = next;
    if (qEnvironmentVariableIsSet("COLOSSEUM_PRIORITY_GOVERNOR_TRACE"))
        qInfo().noquote() << "FOREGROUND_PRIORITY pressure=" << static_cast<int>(m_pressure);
    emit pressureChanged(static_cast<int>(m_pressure));
}

bool ForegroundPriorityGovernor::isUserInputEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::NativeGesture:
        return true;
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        return mouse->buttons() != Qt::NoButton;
    }
    default:
        return false;
    }
}

bool ForegroundPriorityGovernor::eventFilter(QObject *watched, QEvent *event)
{
    if (event && isUserInputEvent(event))
        noteUserInteraction();
    return QObject::eventFilter(watched, event);
}

} // namespace work
