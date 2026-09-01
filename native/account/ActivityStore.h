#pragma once

// ActivityStore — Slice D2 native port of "Your Colosseum"'s durable activity
// ledger (CPP-PORT-CONTRACT.md §4/§5). The only durable writer for normalized
// activity facts: persists immutable semantic events to a profile-owned
// SQLite database and exposes the profile/QML read+append seam.
//
// ActivityStore never re-derives ActivityProjector's schema/world-kind/field
// validation or its local-calendar month math — every fact is validated via
// ActivityProjector::validateEvent() before it ever reaches SQLite, and
// projectMonth()/earliestActivityMonth() delegate to
// ActivityProjector::projectMonth()/localMonthKey() over the full persisted
// ledger. See ActivityProjector.h's "Slice D2 shared seam" section.
//
// Reference: Preflight-Architect arcs/02-profile-account-centre/activity-engine
// CPP-PORT-CONTRACT.md §4 (ActivityStore contract), §5 (SQLite persistence),
// §10/§11 (page/completion dedupe — delegated to the projector), §25 (failure
// behavior: fail closed toward undercount, never fabricate zeros).

#include <QJsonObject>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class ActivityStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)

public:
    // Parentless/default: opens a private in-memory SQLite database (no
    // durable file). Useful for a sealed/ephemeral profile or a caller that
    // wants a scratch store without touching disk.
    explicit ActivityStore(QObject *parent = nullptr);

    // Opens (creating if needed) the SQLite database at `databasePath` —
    // normally ProfilePaths::activityDbPath(). Parent directories are
    // created as needed. Construction never throws; on failure healthy()
    // reports false and every operation fails closed.
    explicit ActivityStore(const QString &databasePath, QObject *parent = nullptr);

    ~ActivityStore() override;

    // In-process mutation counter: increments on every accepted mutation
    // (a non-idempotent insert, or clearAll()). Not persisted — a fresh
    // instance over the same file starts back at 0. QML bindings should
    // read this property (not call projectMonth() unconditionally) to know
    // when to re-project (CPP-PORT-CONTRACT §24).
    quint64 revision() const;

    // False when the database failed to open or its schema could not be
    // established/verified (unwritable path, corrupt file, newer-schema
    // file). Every Q_INVOKABLE below fails closed (returns false/empty,
    // never fabricates zeros) while this is false.
    bool healthy(QString *error = nullptr) const;

    // Lowercase, brace-less UUID — the same shape ActivityStore generates
    // internally for a locally-created fact's eventId when the caller
    // supplies none.
    Q_INVOKABLE QString newSessionId() const;

    // `fact` carries the common event fields (sessionId, world, kind,
    // titleKey, itemKey, title, itemLabel?, cover?, utcOffsetMinutes,
    // syncable, source?) plus the type's own fields (playback:
    // startAtMs/endAtMs/activeMs/rateMilli; reading: atMs/readingForm/
    // pageKeys/progressMicros; completion: atMs/reason). `v` and `type` are
    // supplied by the store. `eventId` is optional — generated when absent.
    //
    // Validated via ActivityProjector::validateEvent() BEFORE any database
    // mutation. An exact-duplicate eventId (identical canonical payload)
    // succeeds idempotently with no second row and no revision bump; a
    // conflicting payload for an existing eventId is rejected. Either
    // rejection emits integrityError() and returns false without mutating
    // the database.
    Q_INVOKABLE bool recordPlaybackDelta(const QVariantMap &fact);
    Q_INVOKABLE bool recordReadingDelta(const QVariantMap &fact);
    Q_INVOKABLE bool recordCompletion(const QVariantMap &fact);

    void setRetentionEnabled(bool enabled) { m_retentionEnabled = enabled; }
    bool retentionEnabled() const { return m_retentionEnabled; }

    // Projects the FULL persisted ledger onto `monthKey` via
    // ActivityProjector::projectMonth(). Returns an empty QVariantMap and
    // emits integrityError() when the database is unhealthy or the month
    // key is invalid — never a fabricated zero-valued projection.
    Q_INVOKABLE QVariantMap projectMonth(const QString &monthKey) const;

    // Earliest local month ("YYYY-MM") touched by any persisted event, using
    // each event's own captured utcOffsetMinutes. Empty string when there is
    // no activity yet, or the database is unhealthy.
    Q_INVOKABLE QString earliestActivityMonth() const;

    // True iff every key in `requiredPageKeys` has at least one persisted
    // fixed reading_delta fact for kind+itemKey, across all retained
    // history (not scoped to any one session or month) — the Lane C fixed-
    // page completion seam (CPP-PORT-CONTRACT §9 Lane C, §25).
    Q_INVOKABLE bool hasFixedCoverage(const QString &kind, const QString &itemKey,
                                       const QVariantList &requiredPageKeys) const;

    // Deletes every persisted event in one transaction. Bumps revision and
    // emits changed() only on success.
    Q_INVOKABLE bool clearAll();

    QList<QVariantMap> historyProjectionFacts() const;

    // Portable immutable Activity facts used by account sync. Only durable
    // syncable events are exported. Machine-local presentation (notably
    // filesystem/resource cover values) is sanitized without changing the
    // richer local ledger row. Ordering is deterministic by lowercase eventId.
    QList<QVariantMap> portableSyncFacts(QString *error = nullptr) const;

    // Applies one remote portable fact through the same ActivityProjector
    // validation and ActivityStore insertion authority as local facts. Existing
    // eventIds compare portable projections: equal is idempotent success; a
    // semantic mismatch fails with activity_event_conflict and no mutation.
    bool applySyncedPortableFact(const QVariantMap &fact, QString *error = nullptr);

    // Merges the WAL file back into the main database file (PRAGMA
    // wal_checkpoint(TRUNCATE)) without closing the connection. First-account
    // adoption (CPP-PORT-CONTRACT §17) calls this on a still-open legacy
    // ActivityStore immediately before copying its .sqlite file bytes, so a
    // plain file copy of the single main file captures every committed
    // event without also needing to carry a "-wal" sidecar. Best-effort: a
    // failure here does not corrupt the database, it just means a copy taken
    // immediately afterward may miss very recent WAL-only commits, so callers
    // treat a false return as "proceed anyway, digest whatever is on disk"
    // rather than aborting account creation over an observational store.
    bool checkpointForSafeCopy(QString *error = nullptr);

    // SHA-256 hex digest of a file's raw bytes, read directly (no SQLite
    // connection involved) — the adoption digest CPP-PORT-CONTRACT §17
    // recommends ("simple SHA-256 of the sqlite file bytes after a clean
    // close + wal checkpoint"). Returns an empty string when `path` does not
    // exist — the deliberate "no legacy activity ledger to migrate" sentinel
    // adoption code compares against, not a fabricated digest of zero bytes.
    static QString fileDigestSha256(const QString &path);

    // Stable semantic digest over event_id + canonical_hash, independent of
    // SQLite page layout/WAL state. Empty string is the valid no-file/no-event
    // sentinel when `databasePath` does not exist.
    static QString semanticEventDigest(
        const QString &databasePath,
        QString *error = nullptr);

    // Union portable local activity into an account-owned target ledger.
    // Existing event ids must have the same canonical hash; a conflicting id
    // fails closed. Events carrying filesystem/resource paths are local-only
    // and are deliberately not copied into the account profile.
    static bool mergePortableEvents(
        const QString &sourceDatabasePath,
        const QString &targetDatabasePath,
        QString *error = nullptr);

signals:
    void changed();
    void integrityError(const QString &code, const QString &detail);
    void factCommitted(const QVariantMap &event);

private:
    bool ensureSchema();
    bool insertFact(const QString &type, const QVariantMap &fact);
    bool insertEventRow(const QJsonObject &event, const QString &canonicalJson,
                         const QByteArray &canonicalHash);

    QString m_conn;
    QSqlDatabase m_db;
    QString m_openError;
    quint64 m_revision = 0;
    bool m_retentionEnabled = true;
};
