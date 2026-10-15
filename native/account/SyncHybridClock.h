#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SyncProtocol.h"

#include <QString>

class SyncHybridClock {
public:
    explicit SyncHybridClock(
        const QString &deviceId = QString());

    void setDeviceId(
        const QString &deviceId);

    void restore(
        qint64 physicalMs,
        quint64 counter,
        qint64 serverOffsetMs);

    SyncWireHlc next(
        qint64 localNowMs);

    SyncWireHlc nextFromLocalOrder(
        qint64 localOrderMs,
        qint64 fallbackLocalNowMs);

    void observe(
        const SyncWireHlc &remote,
        qint64 localNowMs);

    void observeServiceTime(
        qint64 serverTimeMs,
        qint64 requestSentLocalMs,
        qint64 responseReceivedLocalMs);

    // Only for mutations explicitly rejected by the service as future-clock
    // values. Rejected mutations are reissued with new ids, so their old HLCs
    // are not part of accepted distributed history.
    void rebaseRejectedFuture(
        qint64 localNowMs);

    qint64 physicalMs() const;
    quint64 counter() const;
    qint64 serverOffsetMs() const;
    QString deviceId() const;

private:
    qint64 adjustedNow(
        qint64 localNowMs) const;

    QString m_deviceId;
    qint64 m_physicalMs = 0;
    quint64 m_counter = 0;
    qint64 m_serverOffsetMs = 0;
};
