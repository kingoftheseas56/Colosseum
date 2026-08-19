#include "ActivityPlaybackTracker.h"

#include "ActivityStore.h"

#include <QDateTime>
#include <QElapsedTimer>

#include <algorithm>

namespace {

// §8: reject a sampling interval whose measured wall gap exceeds this, and
// cap a single coalesced block at the same bound —
// ActivityProjector::validateEvent() rejects activeMs > 30000 outright.
constexpr qint64 kMaxGapMs = 30000;
constexpr qint64 kFlushCapMs = 30000;
// §8 "Event coalescing": a reasonable default flush target; correctness must
// not depend on this value (only the 30s cap above is load-bearing).
constexpr qint64 kFlushTargetMs = 10000;
// §8 "10-second activation gate".
constexpr qint64 kActivationGateMs = 10000;
// §8 "Video guarded 90-percent completion".
constexpr double kCompletionThreshold = 0.9;

bool usesGuarded90(const QString &kind) {
    return kind == QLatin1String("movie") || kind == QLatin1String("episode");
}

qint64 defaultMonotonicMs() {
    // Arbitrary origin, monotonic (immune to wall-clock adjustments/DST) —
    // exactly the property §8's wallMs gap/reject law needs. Lazily started on
    // first use; shared across all default-clocked tracker instances in this
    // process, which is fine since only relative deltas are ever read.
    static QElapsedTimer timer;
    static bool started = false;
    if (!started) {
        timer.start();
        started = true;
    }
    return timer.elapsed();
}

qint64 defaultWallClockMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

int defaultUtcOffsetMinutes() {
    return static_cast<int>(QDateTime::currentDateTime().offsetFromUtc() / 60);
}

// Fills in the common identity fields ActivityStore/ActivityProjector expect
// to at least be present (even if empty) so a caller's partial identity map
// never turns into an "invalid <field>" rejection surprise at record time —
// §4 "Malformed input" is ActivityStore/ActivityProjector's job to enforce;
// this only avoids a missing-key surprise, it changes no validation rule.
QVariantMap normalizeIdentity(const QVariantMap &identity) {
    QVariantMap out = identity;
    static const QStringList stringKeys{
        QStringLiteral("world"), QStringLiteral("kind"), QStringLiteral("titleKey"),
        QStringLiteral("itemKey"), QStringLiteral("title"), QStringLiteral("itemLabel"),
        QStringLiteral("cover"), QStringLiteral("source")};
    for (const QString &key : stringKeys) {
        if (!out.contains(key))
            out.insert(key, QString());
    }
    if (!out.contains(QStringLiteral("syncable")))
        out.insert(QStringLiteral("syncable"), false);
    return out;
}

} // namespace

ActivityPlaybackTracker::ActivityPlaybackTracker(QObject *parent)
    : QObject(parent), m_monotonicClockFn(defaultMonotonicMs), m_wallClockFn(defaultWallClockMs),
      m_utcOffsetFn(defaultUtcOffsetMinutes) {}

ActivityPlaybackTracker::~ActivityPlaybackTracker() = default;

ActivityStore *ActivityPlaybackTracker::sink() const {
    return m_sink;
}

void ActivityPlaybackTracker::setSink(ActivityStore *sink) {
    if (m_sink == sink)
        return;
    m_sink = sink;
    emit sinkChanged();
}

void ActivityPlaybackTracker::setMonotonicClock(MonotonicClockFn fn) {
    m_monotonicClockFn = fn ? std::move(fn) : MonotonicClockFn(defaultMonotonicMs);
}

void ActivityPlaybackTracker::setWallClock(WallClockFn fn) {
    m_wallClockFn = fn ? std::move(fn) : WallClockFn(defaultWallClockMs);
}

void ActivityPlaybackTracker::setUtcOffsetProvider(UtcOffsetFn fn) {
    m_utcOffsetFn = fn ? std::move(fn) : UtcOffsetFn(defaultUtcOffsetMinutes);
}

qint64 ActivityPlaybackTracker::monotonicNow() const {
    return m_monotonicClockFn ? m_monotonicClockFn() : defaultMonotonicMs();
}

qint64 ActivityPlaybackTracker::wallNow() const {
    return m_wallClockFn ? m_wallClockFn() : defaultWallClockMs();
}

int ActivityPlaybackTracker::utcOffsetNow() const {
    return m_utcOffsetFn ? m_utcOffsetFn() : defaultUtcOffsetMinutes();
}

void ActivityPlaybackTracker::begin(const QVariantMap &identity, const QString &sessionId) {
    if (m_active)
        endSessionInternal(); // identity/session change mid-stream — end the old one first (§25 fail closed)

    m_identity = normalizeIdentity(identity);
    m_kind = m_identity.value(QStringLiteral("kind")).toString();
    m_sessionId = sessionId;
    m_active = true;
    m_haveBaseline = false;
    m_openInterval = OpenInterval();
    m_gateCrossed = false;
    m_gateAccumulatedMs = 0;
    m_pendingBuffer.clear();
    m_completionBaselineValid = false;
}

void ActivityPlaybackTracker::resetSamplingBaseline(qint64 positionMs, qint64 durationMs,
                                                      qint64 rateMilli, qint64 monotonicMs,
                                                      int utcOffsetMinutes) {
    m_baseline.positionMs = positionMs;
    m_baseline.durationMs = durationMs;
    m_baseline.rateMilli = rateMilli;
    m_baseline.monotonicMs = monotonicMs;
    m_baseline.utcOffsetMinutes = utcOffsetMinutes;
    m_haveBaseline = true;
}

void ActivityPlaybackTracker::initCompletionBaseline(qint64 positionMs, qint64 durationMs) {
    if (!usesGuarded90(m_kind) || durationMs <= 0) {
        m_completionBaselineValid = false;
        return;
    }
    m_completionBaseline = double(positionMs) / double(durationMs);
    m_completionBaselineValid = true;
}

void ActivityPlaybackTracker::updateCompletionCrossing(qint64 positionMs, qint64 durationMs,
                                                          qint64 nowWallMs, int nowOffsetMinutes) {
    if (!usesGuarded90(m_kind) || durationMs <= 0)
        return;

    const double fraction = double(positionMs) / double(durationMs);
    if (!m_completionBaselineValid) {
        m_completionBaseline = fraction;
        m_completionBaselineValid = true;
        return;
    }
    if (m_completionBaseline < kCompletionThreshold && fraction >= kCompletionThreshold)
        emitCompletion(QStringLiteral("guarded_90_percent"), nowWallMs, nowOffsetMinutes);
    m_completionBaseline = fraction;
}

void ActivityPlaybackTracker::sample(qint64 positionMs, qint64 durationMs, qint64 rateMilli,
                                      bool consuming) {
    if (!m_active)
        return;

    const qint64 nowMono = monotonicNow();
    const qint64 nowWall = wallNow();
    const int nowOffset = utcOffsetNow();

    if (!m_haveBaseline) {
        // First sample of the session (or since the last reset that cleared
        // the baseline entirely) — nothing to diff against yet, just anchor.
        resetSamplingBaseline(positionMs, durationMs, rateMilli, nowMono, nowOffset);
        initCompletionBaseline(positionMs, durationMs);
        return;
    }

    // §8 "Rate changes": a speed change flushes pending time, then resets the
    // baseline — this transition itself never contributes qualified time.
    if (rateMilli != m_baseline.rateMilli) {
        closeOpenInterval();
        resetSamplingBaseline(positionMs, durationMs, rateMilli, nowMono, nowOffset);
        return;
    }

    // §8 "UTC-offset changes": flush the already-unambiguous pending time,
    // discard the ambiguous bridge to this sample, start a fresh baseline.
    if (nowOffset != m_baseline.utcOffsetMinutes) {
        closeOpenInterval();
        resetSamplingBaseline(positionMs, durationMs, rateMilli, nowMono, nowOffset);
        return;
    }

    const qint64 wallMs = nowMono - m_baseline.monotonicMs;
    const qint64 contentDeltaMs = positionMs - m_baseline.positionMs;

    const bool reject = rateMilli <= 0 || wallMs <= 0 || wallMs > kMaxGapMs || !consuming
        || contentDeltaMs <= 0; // "media position does not advance" — also rejects rewind

    if (reject) {
        closeOpenInterval();
        resetSamplingBaseline(positionMs, durationMs, rateMilli, nowMono, nowOffset);
        return;
    }

    const qint64 contentEquivalentWallMs = (contentDeltaMs * 1000) / rateMilli;
    const qint64 qualifiedMs = std::min(wallMs, std::max<qint64>(0, contentEquivalentWallMs));

    extendOpenInterval(qualifiedMs, rateMilli, nowOffset, nowWall);
    updateCompletionCrossing(positionMs, durationMs, nowWall, nowOffset);

    // Advance the sampling baseline to this point for the next delta — the
    // rate/offset are already known unchanged (handled above).
    m_baseline.positionMs = positionMs;
    m_baseline.durationMs = durationMs;
    m_baseline.monotonicMs = nowMono;

    if (m_openInterval.valid && m_openInterval.qualifiedMs >= kFlushTargetMs)
        closeOpenInterval();
}

void ActivityPlaybackTracker::extendOpenInterval(qint64 qualifiedMs, qint64 rateMilli,
                                                   int utcOffsetMinutes, qint64 nowWallMs) {
    if (qualifiedMs <= 0)
        return;

    if (m_openInterval.valid && m_openInterval.qualifiedMs + qualifiedMs > kFlushCapMs)
        closeOpenInterval(); // hard 30s cap — never let one persisted event exceed it

    if (!m_openInterval.valid) {
        m_openInterval.valid = true;
        m_openInterval.qualifiedMs = 0;
        m_openInterval.rateMilli = rateMilli;
        m_openInterval.utcOffsetMinutes = utcOffsetMinutes;
    }
    m_openInterval.qualifiedMs += qualifiedMs;
    m_openInterval.lastWallMs = nowWallMs;
}

void ActivityPlaybackTracker::closeOpenInterval() {
    if (!m_openInterval.valid || m_openInterval.qualifiedMs <= 0) {
        m_openInterval = OpenInterval();
        return;
    }

    // endAtMs is the most recent wall sample folded into this block;
    // startAtMs is derived from it so activeMs == endAtMs - startAtMs holds
    // exactly, regardless of any partial-stall sub-intervals coalesced in.
    const qint64 endAtMs = m_openInterval.lastWallMs;
    const qint64 startAtMs = endAtMs - m_openInterval.qualifiedMs;

    QVariantMap fact = m_identity;
    fact.insert(QStringLiteral("sessionId"), m_sessionId);
    fact.insert(QStringLiteral("utcOffsetMinutes"), qint64(m_openInterval.utcOffsetMinutes));
    fact.insert(QStringLiteral("startAtMs"), startAtMs);
    fact.insert(QStringLiteral("endAtMs"), endAtMs);
    fact.insert(QStringLiteral("activeMs"), m_openInterval.qualifiedMs);
    fact.insert(QStringLiteral("rateMilli"), m_openInterval.rateMilli);

    submitChunk(fact, m_openInterval.qualifiedMs);
    m_openInterval = OpenInterval();
}

void ActivityPlaybackTracker::submitChunk(const QVariantMap &fact, qint64 qualifiedMs) {
    if (!m_gateCrossed) {
        // §8 "10-second activation gate": buffer until the gate crosses;
        // pause/seek/buffering never erase this progress, only item/session end.
        m_pendingBuffer.append(fact);
        m_gateAccumulatedMs += qualifiedMs;
        if (m_gateAccumulatedMs >= kActivationGateMs) {
            m_gateCrossed = true;
            const QVector<QVariantMap> toFlush = m_pendingBuffer;
            m_pendingBuffer.clear();
            for (const QVariantMap &buffered : toFlush) {
                if (m_sink)
                    m_sink->recordPlaybackDelta(buffered);
            }
        }
        return;
    }

    if (m_sink)
        m_sink->recordPlaybackDelta(fact);
}

void ActivityPlaybackTracker::emitCompletion(const QString &reason, qint64 atMs,
                                               int utcOffsetMinutes) {
    if (!m_sink)
        return;

    QVariantMap fact = m_identity;
    fact.insert(QStringLiteral("sessionId"), m_sessionId);
    fact.insert(QStringLiteral("utcOffsetMinutes"), qint64(utcOffsetMinutes));
    fact.insert(QStringLiteral("atMs"), atMs);
    fact.insert(QStringLiteral("reason"), reason);
    m_sink->recordCompletion(fact);
}

void ActivityPlaybackTracker::discontinuity(qint64 positionMs, qint64 durationMs, qint64 rateMilli) {
    if (!m_active)
        return;

    closeOpenInterval();
    const qint64 nowMono = monotonicNow();
    const int nowOffset = utcOffsetNow();
    resetSamplingBaseline(positionMs, durationMs, rateMilli, nowMono, nowOffset);

    // §8: "A call to discontinuity() resets the completion baseline to the new
    // current fraction. Therefore seeking directly to 95% cannot create the
    // crossing."
    if (usesGuarded90(m_kind) && durationMs > 0) {
        m_completionBaseline = double(positionMs) / double(durationMs);
        m_completionBaselineValid = true;
    } else {
        m_completionBaselineValid = false;
    }
}

void ActivityPlaybackTracker::naturalEof() {
    if (!m_active)
        return;

    closeOpenInterval();
    emitCompletion(QStringLiteral("eof"), wallNow(), utcOffsetNow());
}

void ActivityPlaybackTracker::endSessionInternal() {
    closeOpenInterval();
    if (!m_gateCrossed) {
        // §8: "session ends below 10 seconds -> discard buffered activity."
        m_pendingBuffer.clear();
        m_gateAccumulatedMs = 0;
    }
    m_active = false;
    m_haveBaseline = false;
    m_gateCrossed = false;
    m_gateAccumulatedMs = 0;
    m_pendingBuffer.clear();
    m_completionBaselineValid = false;
    m_openInterval = OpenInterval();
}

void ActivityPlaybackTracker::endSession() {
    if (!m_active)
        return;
    endSessionInternal();
}
