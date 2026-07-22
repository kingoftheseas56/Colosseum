// tests/panel_map_store_harness.cpp
//
// TDD harness for guided::PanelMapStore — the SQLite persistence layer for the
// Panel-Aware Guided Comic Reader. Fixture db lives in a QTemporaryDir; exit code
// is the verdict. Run from native/build-msvc so the deployed sqldrivers/qsqlite.dll
// (and Qt6Sql.dll) beside the exe are found.
//
// GuidedPath has NO operator== (GuidedTypes.h is frozen); paths are compared by
// serializePath() byte-identity — the serializer sorts keys, so this is stable.
#include "guided/PanelMapStore.h"
#include "guided/GuidedTypes.h"

#include <QCoreApplication>
#include <QSize>
#include <QTemporaryDir>
#include <cstdio>

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

using namespace guided;

// Path equality via the schema-gated serializer (byte-identical, key-sorted).
static bool samePath(const GuidedPath& a, const GuidedPath& b) {
    return serializePath(a) == serializePath(b);
}

static JobSpec makeJob() {
    JobSpec j;
    j.jobId = QStringLiteral("job-1");
    j.entryId = QStringLiteral("entry-onepiece-042");
    j.entryFingerprint = QStringLiteral("sha256:entry-fp");
    j.direction = ReadingDirection::Rtl;
    j.modelVersion = QStringLiteral("panel-yolo26n-535bbe1");
    j.plannerVersion = QStringLiteral("guided-v1");
    j.priorityCanvas = 0;
    return j;
}

static CanvasSpec makeCanvas() {
    CanvasSpec c;
    c.entryId = QStringLiteral("entry-onepiece-042");   // must match the job's entry_identity
    c.canvasIndex = 0;
    c.kind = CanvasKind::Spread;
    c.localFiles = {QStringLiteral("/tmp/p4.png"), QStringLiteral("/tmp/p5.png")};
    c.sourcePageIndices = {4, 5};
    c.fingerprint = QStringLiteral("sha256:canvas-c1");
    c.sourceSize = QSize(2048, 1536);
    c.direction = ReadingDirection::Rtl;
    return c;
}

static QVector<Detection> makeDetections() {
    return {
        {0, DetectionKind::Panel, {0.50, 0.00, 0.50, 0.50}, 0.91},
        {1, DetectionKind::Panel, {0.00, 0.00, 0.50, 0.50}, 0.88},
        {2, DetectionKind::Text,  {0.10, 0.10, 0.20, 0.10}, 0.75},
    };
}

static GuidedPath makePath() {
    GuidedPath p;
    p.canvasFingerprint = QStringLiteral("sha256:canvas-c1");
    p.modelVersion = QStringLiteral("panel-yolo26n-535bbe1");
    p.plannerVersion = QStringLiteral("guided-v1");
    p.outcome = PlanOutcome::Trusted;
    p.reason = FallbackCode::None;
    p.steps = {
        {StepKind::Overview, -1, {0, 0, 1, 1},        0.8, 0.35, 1.0},
        {StepKind::Panel,     0, {0.5, 0, 0.5, 0.5},  2.4, 0.62, 0.91},
        {StepKind::Panel,     1, {0.0, 0, 0.5, 0.5},  2.4, 0.50, 0.88},
        {StepKind::Overview, -1, {0, 0, 1, 1},        0.8, 0.62, 1.0},
    };
    return p;
}

static CacheKey makeKey() {
    CacheKey k;
    k.fingerprint = QStringLiteral("sha256:canvas-c1");
    k.modelVersion = QStringLiteral("panel-yolo26n-535bbe1");
    k.plannerVersion = QStringLiteral("guided-v1");
    k.direction = ReadingDirection::Rtl;
    return k;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir");
    const QString dbPath = dir.filePath(QStringLiteral("panelmap.db"));

    PanelMapStore store(dbPath);

    // (1) open() creates the schema and is idempotent.
    CHECK(store.open(), "open creates schema v1");
    CHECK(store.open(), "second open() on the same file/instance does not error");

    // (2a) beginJob then publishCanvas succeeds.
    CHECK(store.beginJob(makeJob()), "beginJob upserts a job row");
    CHECK(store.publishCanvas(makeCanvas(), makeDetections(), makePath()),
          "publishCanvas succeeds against an existing job");

    // (3) lookup returns the published effective path (byte-equal) + the generated path.
    {
        const LookupResult r = store.lookup(makeKey());
        CHECK(r.found, "lookup found for a version-matching key");
        CHECK(!r.cacheMiss(), "found => not a cache miss");
        CHECK(samePath(r.path, makePath()), "effective path is byte-equal to the published path");
        CHECK(r.generated.has_value(), "generated path is present");
        CHECK(samePath(*r.generated, makePath()), "generated path is byte-equal to the published path");
        CHECK(r.override == OverrideKind::None, "no override yet");
        CHECK(r.stage == CanvasStage::Ready, "published canvas is Ready");
    }

    // (7) rawDetections is direction-INDEPENDENT: matched by fingerprint only.
    {
        const QVector<Detection> a = store.rawDetections(makeKey());
        const QVector<Detection> b = store.rawDetections(makeKey().withDirection(ReadingDirection::Ltr));
        CHECK(a.size() == 3, "rawDetections returns all 3 stored detections");
        CHECK(b.size() == a.size(), "flipping direction returns the same count");
        for (int i = 0; i < a.size(); ++i) {
            CHECK(a[i].id == b[i].id, "same detection id regardless of direction");
            CHECK(a[i].kind == b[i].kind, "same detection kind regardless of direction");
            CHECK(qFuzzyCompare(a[i].box.x, b[i].box.x) && qFuzzyCompare(a[i].box.width, b[i].box.width),
                  "same detection box regardless of direction");
            CHECK(qFuzzyCompare(a[i].confidence, b[i].confidence), "same confidence regardless of direction");
        }
        // contents match what we stored
        const QVector<Detection> stored = makeDetections();
        CHECK(a[2].kind == DetectionKind::Text, "text detection round-trips");
        CHECK(qFuzzyCompare(a[0].confidence, stored[0].confidence), "detection confidence round-trips");
    }

    // (9) jobSummary reports total + ready after a publish.
    {
        const JobSummary s = store.jobSummary(QStringLiteral("job-1"));
        CHECK(s.jobId == QStringLiteral("job-1"), "jobSummary echoes jobId");
        CHECK(s.total == 1, "jobSummary total counts canvases in the job");
        CHECK(s.ready == 1, "jobSummary ready counts Ready canvases");
    }

    // (2b) ATOMICITY: a publish that fails mid-way rolls back entirely — no canvas
    // row, no detections, no path. Force a mid-transaction failure with duplicate
    // detection ids (PRIMARY KEY(canvas_id, detection_id) violation on the 2nd insert).
    {
        CanvasSpec bad = makeCanvas();
        bad.canvasIndex = 99;                                // a fresh canvas row
        bad.fingerprint = QStringLiteral("sha256:bad-canvas");
        QVector<Detection> dupe = {
            {5, DetectionKind::Panel, {0.0, 0, 0.5, 0.5}, 0.9},
            {5, DetectionKind::Panel, {0.5, 0, 0.5, 0.5}, 0.8},  // duplicate id 5 -> insert fails
        };
        CHECK(!store.publishCanvas(bad, dupe, makePath()), "publish with duplicate detection ids fails");
        // Nothing partial survived the rollback:
        CHECK(store.overrideFor(QStringLiteral("sha256:bad-canvas")) == OverrideKind::None,
              "rolled-back publish left NO canvas row (overrideFor => None)");
        CacheKey badKey = makeKey();
        badKey.fingerprint = QStringLiteral("sha256:bad-canvas");
        CHECK(!store.lookup(badKey).found, "rolled-back publish left NO path");
        {
            QVector<Detection> badDets = store.rawDetections(badKey);
            CHECK(badDets.isEmpty(), "rolled-back publish left NO detections");
        }
        // The pre-existing good canvas is untouched by the failed publish.
        CHECK(store.lookup(makeKey()).found, "existing canvas survives a failed publish");
        CHECK(store.jobSummary(QStringLiteral("job-1")).total == 1, "failed publish added no canvas");
    }

    // (2c) publishCanvas against an entry with NO job also fails and writes nothing.
    {
        CanvasSpec ghost = makeCanvas();
        ghost.entryId = QStringLiteral("entry-does-not-exist");
        ghost.canvasIndex = 0;
        ghost.fingerprint = QStringLiteral("sha256:ghost");
        CHECK(!store.publishCanvas(ghost, makeDetections(), makePath()),
              "publish with no matching job fails");
        CHECK(store.overrideFor(QStringLiteral("sha256:ghost")) == OverrideKind::None,
              "no-job publish left no canvas row");
    }

    // (4) whole-page override WINS the effective path (single Overview step) while
    // the generated multi-step path is still available.
    {
        CHECK(store.setOverride(QStringLiteral("sha256:canvas-c1"), OverrideKind::WholePage),
              "setOverride WholePage");
        const LookupResult r = store.lookup(makeKey());
        CHECK(r.found, "lookup still found under a whole-page override");
        CHECK(r.override == OverrideKind::WholePage, "override reported as WholePage");
        CHECK(r.path.steps.size() == 1, "whole-page effective path is a single step");
        CHECK(r.path.steps[0].kind == StepKind::Overview, "the single step is an Overview");
        CHECK(r.path.steps[0].camera.width == 1.0 && r.path.steps[0].camera.height == 1.0,
              "whole-page camera covers {0,0,1,1}");
        CHECK(r.generated.has_value(), "generated path still present under override");
        CHECK(samePath(*r.generated, makePath()), "generated is still the original multi-step machine path");
        CHECK(r.generated->steps.size() == 4, "generated retains all 4 machine steps");
    }

    // (5) retryCanvas clears the generated detections + path (stage -> Waiting) but
    // KEEPS the override.
    {
        store.retryCanvas(QStringLiteral("sha256:canvas-c1"));
        const LookupResult r = store.lookup(makeKey());
        CHECK(!r.generated.has_value(), "retry cleared the generated path");
        CHECK(store.rawDetections(makeKey()).isEmpty(), "retry cleared the generated detections");
        CHECK(store.overrideFor(QStringLiteral("sha256:canvas-c1")) == OverrideKind::WholePage,
              "retry preserved the override");
    }

    // (6) a request for a different planner version is a cache MISS.
    {
        const LookupResult r = store.lookup(makeKey().withPlanner(QStringLiteral("guided-v2")));
        CHECK(!r.found, "planner-version mismatch => not found");
        CHECK(r.cacheMiss(), "planner-version mismatch => cacheMiss()");
    }

    // (8) overrideFor on a never-seen fingerprint is None (new source bytes => new
    // fingerprint => no override carries over).
    {
        CHECK(store.overrideFor(QStringLiteral("sha256:never-published")) == OverrideKind::None,
              "unknown fingerprint => OverrideKind::None");
    }

    // saveCheckpoint updates the job's progress cursor.
    {
        CHECK(store.saveCheckpoint(QStringLiteral("job-1"), 3), "saveCheckpoint updates an existing job");
        CHECK(store.jobSummary(QStringLiteral("job-1")).priorityCanvas == 3,
              "checkpoint cursor is reflected in the summary");
        CHECK(!store.saveCheckpoint(QStringLiteral("no-such-job"), 1),
              "saveCheckpoint on a missing job returns false");
    }

    // (1, again) a SECOND store instance opens the same file without error.
    {
        PanelMapStore second(dbPath);
        CHECK(second.open(), "a second store instance opens the same db file");
        CHECK(second.overrideFor(QStringLiteral("sha256:canvas-c1")) == OverrideKind::WholePage,
              "second instance reads the persisted override");
    }

    std::puts("PANEL_MAP_STORE_OK");
    return 0;
}
