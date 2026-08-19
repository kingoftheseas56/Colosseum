#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <stdexcept>

// ActivityProjector — Slice D1 native port of "Your Colosseum"'s activity oracle.
//
// Reference: Preflight-Architect arcs/02-profile-account-centre/activity-engine
// (SEMANTICS.md is the product contract, activity-reference.js is the executable
// behavioral truth this header ports literally, CPP-PORT-CONTRACT.md sections 4/6/
// 10-13/21/24-25 are the native-port instructions).
//
// projectMonth() is a pure function over the full normalized activity-event
// ledger: no filesystem, QML, network, or machine-clock dependency. The caller
// (a future ActivityStore/ProfileActivity) supplies both the ledger and the
// explicit "YYYY-MM" month key; this function never reads the system clock.
//
// The contract is field-for-field behavioral parity with activity-reference.js,
// not merely a similarly-shaped implementation — see
// tests/auto/activity/tst_activity_projector.cpp for the golden fixture
// comparisons this must keep passing.
namespace ActivityProjector {

// Thrown for any malformed event, schema violation, or invalid month key.
// Never silently repairs or drops bad input — mirrors activity-reference.js's
// fail(message). what() carries the same message text the JS oracle raises
// (e.g. "invalid eventId", "eventId conflict: <id>", "invalid month key",
// "reflowable pageKeys must be empty") so callers/tests can match on it.
class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const QString &message);
};

// Projects `events` (the full ledger, not pre-filtered to the month — required
// for lifetime eventId/completion dedupe and same-session page dedupe) onto
// `monthKey`. Performs its own schema validation and eventId dedupe/conflict
// rejection first, exactly like the JS oracle; throws ValidationError on the
// first invalid event or an invalid monthKey.
//
// Output shape: month, watchSeconds, listenSeconds, pagesRead, completedCount,
// activeDays, highlights[], recentActivity[] — see CPP-PORT-CONTRACT.md §13.
QJsonObject projectMonth(const QJsonArray &events, const QString &monthKey);

// --- Slice D2 shared seam -----------------------------------------------
// ActivityStore (the durable SQLite writer) must validate a fact BEFORE it
// ever reaches the database, using the exact same schema rules projectMonth()
// already applies per-event — CPP-PORT-CONTRACT.md §4 "Malformed input" is
// explicit that this must be reused, not re-derived. These three functions
// are thin public wrappers over projectMonth()'s existing internal per-event
// validation/canonicalization/local-calendar logic; none of that internal
// logic is changed, so tst_activity_projector's 31 cases stay authoritative.

// Validates a single event exactly like projectMonth()'s per-event schema/
// world-kind/field rules (no ledger-level eventId dedupe — that is
// ActivityStore's own job against its durable rows). Throws ValidationError
// on the first violation, with the same message text projectMonth() raises.
void validateEvent(const QJsonObject &event);

// The canonical (sorted-key, compact) JSON text projectMonth()'s eventId
// dedupe compares — used by ActivityStore to tell an exact-duplicate insert
// (idempotent success) apart from a conflicting payload for the same
// eventId (§4 "Local insert idempotency").
QString canonicalEventJson(const QJsonObject &event);

// Local calendar month ("YYYY-MM") for a UTC millisecond instant under a
// captured signed UTC offset, via the same civil-calendar math projectMonth()
// uses for point events (§12). Used by ActivityStore::earliestActivityMonth().
QString localMonthKey(qint64 utcMs, qint64 utcOffsetMinutes);

} // namespace ActivityProjector
