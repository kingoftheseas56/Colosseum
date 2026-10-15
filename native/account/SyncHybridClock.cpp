// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncHybridClock.h"

#include <QtGlobal>

SyncHybridClock::SyncHybridClock(
    const QString &deviceId)
    : m_deviceId(
          deviceId.trimmed().toLower()) {}

void SyncHybridClock::setDeviceId(
    const QString &deviceId) {
    m_deviceId =
        deviceId.trimmed().toLower();
}

void SyncHybridClock::restore(
    qint64 physicalMs,
    quint64 counter,
    qint64 serverOffsetMs) {
    m_physicalMs =
        qMax<qint64>(
            0,
            physicalMs);
    m_counter =
        counter;
    m_serverOffsetMs =
        serverOffsetMs;
}

SyncWireHlc SyncHybridClock::next(
    qint64 localNowMs) {
    const qint64 now =
        adjustedNow(localNowMs);

    if (now > m_physicalMs) {
        m_physicalMs = now;
        m_counter = 0;
    } else {
        ++m_counter;
    }

    return SyncWireHlc{
        m_physicalMs,
        m_counter,
        m_deviceId};
}

SyncWireHlc SyncHybridClock::nextFromLocalOrder(
    qint64 localOrderMs,
    qint64 fallbackLocalNowMs) {
    if (localOrderMs <= 0)
        return next(fallbackLocalNowMs);

    const qint64 candidate =
        qMax<qint64>(
            0,
            localOrderMs
                + m_serverOffsetMs);

    if (candidate > m_physicalMs) {
        m_physicalMs = candidate;
        m_counter = 0;
    } else {
        ++m_counter;
    }

    return SyncWireHlc{
        m_physicalMs,
        m_counter,
        m_deviceId};
}

void SyncHybridClock::observe(
    const SyncWireHlc &remote,
    qint64 localNowMs) {
    const qint64 now =
        adjustedNow(localNowMs);
    const qint64 nextPhysical =
        qMax(
            now,
            qMax(
                m_physicalMs,
                remote.physicalMs));

    if (nextPhysical == m_physicalMs
        && nextPhysical
            == remote.physicalMs) {
        m_counter =
            qMax(
                m_counter,
                remote.counter)
            + 1;
    } else if (nextPhysical
               == m_physicalMs) {
        ++m_counter;
    } else if (nextPhysical
               == remote.physicalMs) {
        m_counter =
            remote.counter + 1;
    } else {
        m_counter = 0;
    }

    m_physicalMs =
        nextPhysical;
}

void SyncHybridClock::observeServiceTime(
    qint64 serverTimeMs,
    qint64 requestSentLocalMs,
    qint64 responseReceivedLocalMs) {
    if (serverTimeMs <= 0
        || requestSentLocalMs <= 0
        || responseReceivedLocalMs
            < requestSentLocalMs) {
        return;
    }

    const qint64 midpoint =
        requestSentLocalMs
        + ((responseReceivedLocalMs
            - requestSentLocalMs)
           / 2);

    m_serverOffsetMs =
        serverTimeMs - midpoint;
}

void SyncHybridClock::rebaseRejectedFuture(
    qint64 localNowMs) {
    m_physicalMs =
        adjustedNow(localNowMs);
    m_counter = 0;
}

qint64 SyncHybridClock::physicalMs() const {
    return m_physicalMs;
}

quint64 SyncHybridClock::counter() const {
    return m_counter;
}

qint64 SyncHybridClock::serverOffsetMs() const {
    return m_serverOffsetMs;
}

QString SyncHybridClock::deviceId() const {
    return m_deviceId;
}

qint64 SyncHybridClock::adjustedNow(
    qint64 localNowMs) const {
    if (localNowMs < 0)
        return 0;

    return qMax<qint64>(
        0,
        localNowMs
            + m_serverOffsetMs);
}
