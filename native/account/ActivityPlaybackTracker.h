#pragma once

// ActivityPlaybackTracker — Slice D3 native port of "Your Colosseum"'s transient
// playback sampler (CPP-PORT-CONTRACT.md §8). Reusable by Theatre Player 1,
// Theatre Player 2, and the Biblio audiobook session: converts live wall-clock
// elapsed time plus media-position advancement into qualified playback_delta
// facts and writes them through an ActivityStore sink.
//
// The tracker measures real wall-clock time but requires media advancement as
// proof that consumption actually occurred — QML/player code never has to say
// "one timer tick equals N watched seconds." It owns:
//   - the sampling law (wallMs/contentDeltaMs/contentEquivalentWallMs,
//     reject-and-reset conditions) — §8 "Sampling algorithm";
//   - the 10-second activation gate (buffer-until-10s, discard-below-gate,
//     persist-original-timestamps-on-crossing) — §8 "10-second activation gate";
//   - event coalescing (contiguous qualified intervals into <=30,000ms blocks,
//     independent of the ~10,000ms flush target) — §8 "Event coalescing";
//   - UTC-offset-change splitting (flush unambiguous time, discard the bridge)
//     — §8 "UTC-offset changes";
//   - guarded 90% completion for movie/episode only, reset by discontinuity(),
//     never used for audiobooks — §8 "Video guarded 90-percent completion".
//
// It never reads or writes ProgressStore (§1) and is deliberately independent
// of either player engine's own state machine — callers translate their own
// state transitions (seek, pause, speed change, buffering, recovery, item
// change) into begin()/sample()/discontinuity()/naturalEof()/endSession()
// calls; see CPP-PORT-CONTRACT.md §9 for the per-lane hook map.
//
// Every persisted fact is validated by ActivityStore::recordPlaybackDelta()/
// recordCompletion() (which in turn delegate to ActivityProjector::
// validateEvent()) exactly like any other producer — this tracker builds nothing
// but plain QVariantMap facts and never re-derives that validation.
//
// Clock injection: a monotonic clock (arbitrary origin, immune to wall-clock
// jumps — used only to measure elapsed real time between samples and to gate
// the >30s/<=0 reject conditions) and a wall-clock/UTC-offset provider (used
// only to timestamp emitted events) are both injectable so tests are fully
// deterministic. Defaults use QElapsedTimer / QDateTime::currentMSecsSinceEpoch()
// / the system's local UTC offset.
//
// Reference: Preflight-Architect arcs/02-profile-account-centre/activity-engine
// CPP-PORT-CONTRACT.md §8 (tracker algorithm), §6 (event schema), §22 (lane
// playback proofs), §25 (fail closed toward undercount, never invented
// consumption).

#include "ActivityStore.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <functional>

// ActivityStore.h is included in full (not just forward-declared): Qt 6's
// moc-generated metatype registration for a `ActivityStore *` Q_PROPERTY
// requires the pointee to be a complete type wherever the generated
// moc_ActivityPlaybackTracker.cpp is compiled (QMetaType::fromType<T>()'s
// checkTypeIsSuitableForMetaType<T>() static_asserts on it) — a forward
// declaration alone fails that compile.

// NOT final (Slice D5): qmlRegisterType<T>() — both the classic API used here and the
// QML_ELEMENT/qmltyperegistrar path — instantiates a QML-creatable type through
// QQmlPrivate::QQmlElement<T>, which itself PUBLICLY SUBCLASSES T (MSVC/cl.exe: "cannot
// inherit from 'ActivityPlaybackTracker' as it has been declared as 'final'", C3246,
// discovered compiling native/main.cpp's new qmlRegisterType<ActivityPlaybackTracker>
// call). This is a hard Qt constraint for any type registered as creatable from QML via
// the standard automatic-constructor path, not specific to this class. No other Colosseum
// QML-creatable type (MpvItem, SeekThumbnailer, Player2VideoItem, Player2Backend) is final
// either, for the same reason.
class ActivityPlaybackTracker : public QObject {
    Q_OBJECT
    Q_PROPERTY(ActivityStore *sink READ sink WRITE setSink NOTIFY sinkChanged)

public:
    using MonotonicClockFn = std::function<qint64()>;
    using WallClockFn = std::function<qint64()>;
    using UtcOffsetFn = std::function<int()>;

    explicit ActivityPlaybackTracker(QObject *parent = nullptr);
    ~ActivityPlaybackTracker() override;

    ActivityStore *sink() const;
    void setSink(ActivityStore *sink);

    // Clock injection for deterministic tests — a null function restores the
    // corresponding system-clock default. Set BEFORE begin() so the first
    // sample() call observes the intended clock.
    void setMonotonicClock(MonotonicClockFn fn);
    void setWallClock(WallClockFn fn);
    void setUtcOffsetProvider(UtcOffsetFn fn);

    // Starts a new logical consumption session. `identity` carries the common
    // event fields (world/kind/titleKey/itemKey/title/itemLabel?/cover?/
    // syncable/source?) captured once and reused for every fact this session
    // emits; `sessionId` is the caller-supplied logical session UUID (normally
    // ActivityStore::newSessionId()).
    //
    // If a prior session is still open (an identity/session change without an
    // explicit endSession()), the prior session is ended first under its own
    // gate/discard rules — CPP-PORT-CONTRACT.md §25 "fail closed", never a
    // silent carry-over of one item's watched time onto another's identity.
    Q_INVOKABLE void begin(const QVariantMap &identity, const QString &sessionId);

    // One sampling tick: `positionMs`/`durationMs` are the current media
    // position/duration, `rateMilli` the current constant-integer play rate,
    // `consuming` whether the media is actively advancing right now (false for
    // paused/buffering/seeking/stalled/error states). See §8 "Sampling
    // algorithm" for the exact reject-and-reset law this implements.
    Q_INVOKABLE void sample(qint64 positionMs, qint64 durationMs, qint64 rateMilli, bool consuming);

    // A known seek/load/recovery: flushes any already-qualified pending time,
    // then resets the sampling baseline to `positionMs` (post-jump) so the
    // bridge between the old and new position is discarded rather than counted
    // as watched. Also resets the guarded-90% completion baseline to the new
    // current fraction (§8: "seeking directly to 95% cannot create the
    // crossing").
    Q_INVOKABLE void discontinuity(qint64 positionMs, qint64 durationMs, qint64 rateMilli);

    // Natural end-of-media: flushes any pending qualified time, then always
    // records a media_completed fact with reason "eof" (regardless of kind —
    // the guarded-90% rule is the movie/episode-only *early* signal; eof is
    // unconditional). Does not itself tear down the session — call
    // endSession() afterward for lifecycle symmetry with begin().
    Q_INVOKABLE void naturalEof();

    // Ends the current session: flushes any pending qualified time, then — if
    // the 10-second activation gate was never crossed this session — discards
    // every buffered-but-unpersisted interval, exactly as if the session never
    // happened (§8 "session ends below 10 seconds -> discard buffered
    // activity"). A no-op when no session is active.
    Q_INVOKABLE void endSession();

signals:
    void sinkChanged();

private:
    struct Baseline {
        qint64 positionMs = 0;
        qint64 durationMs = 0;
        qint64 rateMilli = 0;
        qint64 monotonicMs = 0;
        int utcOffsetMinutes = 0;
    };

    // A pending, not-yet-persisted coalesced qualified interval. Concrete
    // startAtMs/endAtMs/activeMs are derived at close time from qualifiedMs and
    // lastWallMs so activeMs == endAtMs - startAtMs holds exactly by
    // construction (ActivityProjector::validateEvent()'s "activeMs mismatch"
    // rule), independent of any partial-stall sub-intervals folded into it.
    struct OpenInterval {
        bool valid = false;
        qint64 qualifiedMs = 0;
        qint64 lastWallMs = 0;
        qint64 rateMilli = 0;
        int utcOffsetMinutes = 0;
    };

    qint64 monotonicNow() const;
    qint64 wallNow() const;
    int utcOffsetNow() const;

    void resetSamplingBaseline(qint64 positionMs, qint64 durationMs, qint64 rateMilli,
                                qint64 monotonicMs, int utcOffsetMinutes);
    void initCompletionBaseline(qint64 positionMs, qint64 durationMs);
    void updateCompletionCrossing(qint64 positionMs, qint64 durationMs, qint64 nowWallMs,
                                   int nowOffsetMinutes);

    void extendOpenInterval(qint64 qualifiedMs, qint64 rateMilli, int utcOffsetMinutes,
                             qint64 nowWallMs);
    void closeOpenInterval();
    void submitChunk(const QVariantMap &fact, qint64 qualifiedMs);
    void emitCompletion(const QString &reason, qint64 atMs, int utcOffsetMinutes);
    void endSessionInternal();

    ActivityStore *m_sink = nullptr;

    MonotonicClockFn m_monotonicClockFn;
    WallClockFn m_wallClockFn;
    UtcOffsetFn m_utcOffsetFn;

    bool m_active = false;
    QVariantMap m_identity; // normalized copy captured at begin()
    QString m_kind;
    QString m_sessionId;

    bool m_haveBaseline = false;
    Baseline m_baseline;

    OpenInterval m_openInterval;

    // 10-second activation gate.
    bool m_gateCrossed = false;
    qint64 m_gateAccumulatedMs = 0;
    QVector<QVariantMap> m_pendingBuffer;

    // Guarded 90% completion (movie/episode only).
    bool m_completionBaselineValid = false;
    double m_completionBaseline = 0.0;
};
