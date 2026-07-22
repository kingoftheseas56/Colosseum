// native/guided/PanelMapStore.h
//
// PanelMapStore — the SQLite persistence layer for the Panel-Aware Guided Comic
// Reader (Agent 1). It is the durable cache between the model/planner pipeline and
// the two readers (Panel Step, Auto Read): jobs, per-canvas detections, the
// machine-generated GuidedPath, and the user's whole-page/detected-panel overrides.
//
// One store owns one SQLite file over a uniquely-named QSqlDatabase connection.
// Paths are stored as the schema-gated compact JSON blob (serializePath); nothing
// here re-implements GuidedPath comparison — callers diff via serializePath.
#pragma once

#include "guided/GuidedTypes.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <optional>

namespace guided {

// OverrideKind now lives in GuidedTypes.h (shared with the planner).

// A guided-reader job: one comic entry queued for panel detection + planning.
struct JobSpec {
    QString jobId;
    QString entryId;
    QString entryFingerprint;
    ReadingDirection direction = ReadingDirection::Rtl;
    QString modelVersion;
    QString plannerVersion;
    int priorityCanvas = 0;
};

// The cache-lookup key: a canvas fingerprint qualified by the model + planner
// versions and reading direction it was generated for.
struct CacheKey {
    QString fingerprint;
    QString modelVersion;
    QString plannerVersion;
    ReadingDirection direction = ReadingDirection::Rtl;

    CacheKey withPlanner(const QString& planner) const;    // copy with plannerVersion replaced
    CacheKey withDirection(ReadingDirection dir) const;    // copy with direction replaced
};

// The result of a cache lookup: the effective path (override wins), plus the raw
// machine-generated path when one is stored and version-matching.
struct LookupResult {
    bool found = false;                    // true iff a version-matching EFFECTIVE path is available
    GuidedPath path;                       // effective path: whole-page override wins, else the generated path
    std::optional<GuidedPath> generated;   // the machine-generated path IF stored and version-matching
    OverrideKind override = OverrideKind::None;
    CanvasStage stage = CanvasStage::Waiting;
    bool cacheMiss() const { return !found; }
};

// A progress snapshot for a job: how many of its canvases are Ready.
struct JobSummary {
    QString jobId;
    CanvasStage stage = CanvasStage::Waiting;
    int ready = 0;
    int total = 0;
    bool paused = false;
    int priorityCanvas = 0;
};

class PanelMapStore {
public:
    explicit PanelMapStore(const QString& dbPath);
    ~PanelMapStore();

    PanelMapStore(const PanelMapStore&) = delete;
    PanelMapStore& operator=(const PanelMapStore&) = delete;

    bool open();                                    // opens SQLite, creates schema v1 if absent (idempotent)
    bool beginJob(const JobSpec& job);              // upsert a job row by jobId
    bool publishCanvas(const CanvasSpec& canvas,
                       const QVector<Detection>& detections,
                       const GuidedPath& path);     // atomic: detections + path + canvas stage in ONE transaction
    LookupResult lookup(const CacheKey& key) const;
    QVector<Detection> rawDetections(const CacheKey& key) const;  // matched by fingerprint only (direction-independent)
    bool setOverride(const QString& fingerprint, OverrideKind kind);
    OverrideKind overrideFor(const QString& fingerprint) const;   // None if fingerprint unknown
    void retryCanvas(const QString& fingerprint);   // clears generated detections+path, stage->Waiting; KEEPS override
    bool saveCheckpoint(const QString& jobId, int priorityCanvas); // update job progress cursor + updated_at
    JobSummary jobSummary(const QString& jobId) const;

private:
    bool ensureSchema();
    QString resolveJobId(const QString& entryId) const;   // most-recently-created job for an entry, or empty

    QString m_dbPath;
    QString m_conn;
    QSqlDatabase m_db;
};

} // namespace guided
