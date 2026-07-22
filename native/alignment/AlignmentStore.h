#pragma once

// AlignmentStore — the durable SQLite memory of the audiobook↔EPUB read-along.
//
// One small thing: a database that remembers, per book↔audiobook pair, which EPUB
// sentences and words map to which audio timestamps, chapter by chapter — and
// answers the two navigation questions in logarithmic time:
//   • cueAtTime(t)      -> which sentence/word is being narrated at audio time t
//   • timeAtLocation(l) -> what audio time a canonical EPUB location narrates
//
// Every write that could publish a chapter is transactional, so a pause, crash, or
// shutdown can never leave half a chapter marked Ready. A chapter flips to Ready
// only through publishReadyChapter(), and only if it clears the coverage/gap gate
// from the approved design; otherwise it records an honest CouldntSync and keeps
// zero cues (nothing guessed is ever stored as trustworthy).
//
// Consumes Qt Core + Qt Sql only. Not a QObject — the service (Task 3) is the
// QML-facing façade; this is plain persistence.

#include "AlignmentTypes.h"

#include <QString>
#include <QList>
#include <QByteArray>
#include <optional>

namespace alignment {

// The Ready gate thresholds from the approved design (Matching and Confidence Rules).
namespace gate {
    constexpr double kMinCoverage = 0.80;        // trusted cues cover >=80% of narrative speech
    constexpr qint64 kMaxUnresolvedRunMs = 30000; // no unresolved internal run may EXCEED 30s
}

class AlignmentStore {
public:
    // Opens (creating the schema if absent) the SQLite database at dbPath. Use an
    // in-memory database by passing ":memory:".
    explicit AlignmentStore(const QString &dbPath);
    ~AlignmentStore();

    AlignmentStore(const AlignmentStore &) = delete;
    AlignmentStore &operator=(const AlignmentStore &) = delete;

    bool isOpen() const { return m_open; }

    // ── Pair lifecycle ───────────────────────────────────────────────────────
    // Upsert the pair identity. If a pair with this id already exists and either
    // asset FINGERPRINT changed, every dependent chapter result is invalidated
    // (only this pair). An engine/model version change alone updates the identity
    // but preserves usable results — a later Improve pass replaces them atomically.
    bool upsertPair(const PairIdentity &pair);
    bool hasPair(const QString &pairId) const;
    PairIdentity pairIdentity(const QString &pairId) const;   // {} when absent
    QString overallState(const QString &pairId) const;         // "" when absent

    // Ensure a chapter job row exists with its audio bounds (idempotent). A brand
    // new job starts Waiting; an existing job keeps its stage while bounds and
    // priority are refreshed from the current audiobook file model.
    bool ensureChapter(const QString &pairId, int chapterIndex,
                       qint64 audioStartMs, qint64 audioEndMs, int priority = 0);

    // ── Resumable progress ───────────────────────────────────────────────────
    // Advance a chapter's stage and persist its resumable checkpoint blob in one
    // transaction. Refuses to set Ready — only publishReadyChapter() may.
    bool saveCheckpoint(const QString &pairId, int chapterIndex,
                        Stage stage, const QByteArray &checkpoint);

    // Atomically publish a chapter's cues. In ONE transaction it replaces any
    // prior cues, inserts the aligned sentences, the explicit gap regions, and the
    // word cues, computes coverage, and enforces the Ready gate:
    //   coverage >= 80% of narrative speech AND no unresolved run > 30s.
    // Returns true and sets Ready when the gate passes. Returns false, sets
    // CouldntSync(edition_mismatch), and persists NO cues when it fails.
    bool publishReadyChapter(const QString &pairId, int chapterIndex,
                             const QList<SentenceCue> &sentences,
                             const QList<WordCue> &words,
                             const QList<RegionRecord> &regions,
                             double confidence);

    // Record a terminal pipeline failure (a non-gate failure, e.g. audio decode).
    // Clears the chapter's cues and marks it CouldntSync with the given code.
    bool failChapter(const QString &pairId, int chapterIndex,
                     FailureCode code, const QString &detail = QString());

    // ── Lookups (logarithmic, never a chapter scan) ──────────────────────────
    ActiveCue cueAtTime(const QString &pairId, qint64 timeMs) const;
    std::optional<qint64> timeAtLocation(const QString &pairId,
                                         const CanonicalLocation &loc) const;

    // ── Status ───────────────────────────────────────────────────────────────
    ChapterStatus chapterStatus(const QString &pairId, int chapterIndex) const;
    QList<ChapterStatus> chapters(const QString &pairId) const;

    // ── Retry / restart ──────────────────────────────────────────────────────
    // Retry ONE chapter: clear its cues, checkpoint, and failure; reset to Waiting.
    // The pair and every other chapter are untouched.
    bool retryChapter(const QString &pairId, int chapterIndex);
    // Restart the whole pair: clear all generated cues and reset every chapter to
    // Waiting. The pair identity and the chapter audio bounds survive.
    bool restartPair(const QString &pairId);

private:
    // Internal helpers live in the .cpp; the header stays a pure contract.
    bool createSchema();
    std::optional<qint64> chapterJobId(const QString &pairId, int chapterIndex) const;
    bool clearChapterCues(qint64 chapterJobId);
    void recomputeOverall(const QString &pairId);

    QString m_conn;   // unique QSqlDatabase connection name
    bool m_open = false;
};

} // namespace alignment
