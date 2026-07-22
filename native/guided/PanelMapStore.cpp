// native/guided/PanelMapStore.cpp
#include "guided/PanelMapStore.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <algorithm>

namespace guided {

// --- CacheKey copy-with helpers ---------------------------------------------

CacheKey CacheKey::withPlanner(const QString& planner) const {
    CacheKey copy = *this;
    copy.plannerVersion = planner;
    return copy;
}

CacheKey CacheKey::withDirection(ReadingDirection dir) const {
    CacheKey copy = *this;
    copy.direction = dir;
    return copy;
}

// --- helpers -----------------------------------------------------------------

namespace {

QString joinIndices(const QVector<int>& indices) {
    QStringList parts;
    parts.reserve(indices.size());
    for (int i : indices) parts.append(QString::number(i));
    return parts.join(QLatin1Char(','));
}

double maxConfidence(const QVector<Detection>& dets) {
    double c = 0.0;
    for (const Detection& d : dets) c = std::max(c, d.confidence);
    return c;
}

} // namespace

// --- lifecycle ---------------------------------------------------------------

PanelMapStore::PanelMapStore(const QString& dbPath) : m_dbPath(dbPath) {
    // A unique connection name per instance so multiple stores/threads never clash.
    static QAtomicInteger<quint64> counter(0);
    m_conn = QStringLiteral("panel_map_store_%1_%2")
                 .arg(reinterpret_cast<quintptr>(this))
                 .arg(counter.fetchAndAddOrdered(1));
}

PanelMapStore::~PanelMapStore() {
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();                       // drop the handle before removing the connection
    QSqlDatabase::removeDatabase(m_conn);
}

// --- schema ------------------------------------------------------------------

bool PanelMapStore::ensureSchema() {
    // Schema v1. IF NOT EXISTS keeps open() idempotent (the contract) without
    // altering the table shapes.
    static const char* const kDdl[] = {
        "CREATE TABLE IF NOT EXISTS guided_jobs("
        "  job_id TEXT PRIMARY KEY, entry_identity TEXT NOT NULL, entry_fingerprint TEXT NOT NULL,"
        "  direction INTEGER NOT NULL, model_version TEXT NOT NULL, planner_version TEXT NOT NULL,"
        "  state INTEGER NOT NULL, paused INTEGER NOT NULL DEFAULT 0,"
        "  priority_canvas INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS guided_canvases("
        "  canvas_id INTEGER PRIMARY KEY, job_id TEXT NOT NULL, canvas_index INTEGER NOT NULL,"
        "  page_indices TEXT NOT NULL, canvas_kind INTEGER NOT NULL, fingerprint TEXT NOT NULL,"
        "  width INTEGER NOT NULL, height INTEGER NOT NULL, stage INTEGER NOT NULL,"
        "  confidence REAL NOT NULL DEFAULT 0, fallback_code TEXT NOT NULL DEFAULT '',"
        "  override_kind INTEGER NOT NULL DEFAULT 0,"
        "  model_version TEXT NOT NULL DEFAULT '', planner_version TEXT NOT NULL DEFAULT '',"
        "  UNIQUE(job_id, canvas_index))",
        "CREATE TABLE IF NOT EXISTS guided_detections("
        "  canvas_id INTEGER NOT NULL, detection_id INTEGER NOT NULL, kind INTEGER NOT NULL,"
        "  x REAL NOT NULL, y REAL NOT NULL, w REAL NOT NULL, h REAL NOT NULL,"
        "  confidence REAL NOT NULL, accepted INTEGER NOT NULL, PRIMARY KEY(canvas_id, detection_id))",
        "CREATE TABLE IF NOT EXISTS guided_paths("
        "  canvas_id INTEGER PRIMARY KEY, serialized_json BLOB NOT NULL, published_at INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS guided_canvas_fingerprint_idx ON guided_canvases(fingerprint)",
    };
    QSqlQuery q(m_db);
    for (const char* ddl : kDdl) {
        if (!q.exec(QString::fromLatin1(ddl))) return false;
    }
    q.exec(QStringLiteral("PRAGMA user_version = 1"));
    return true;
}

bool PanelMapStore::open() {
    if (m_db.isOpen()) return ensureSchema();       // idempotent second call
    if (!m_db.isValid()) {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
        m_db.setDatabaseName(m_dbPath);
    }
    if (!m_db.open()) return false;
    // Enforce PKs/uniqueness so a mid-transaction violation actually fails (atomicity).
    QSqlQuery(m_db).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    return ensureSchema();
}

// --- jobs --------------------------------------------------------------------

bool PanelMapStore::beginJob(const JobSpec& job) {
    if (!m_db.isOpen()) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO guided_jobs"
        " (job_id, entry_identity, entry_fingerprint, direction, model_version, planner_version,"
        "  state, paused, priority_canvas, created_at, updated_at)"
        " VALUES (:id,:ent,:fp,:dir,:mv,:pv,:state,0,:prio,:ca,:ua)"
        " ON CONFLICT(job_id) DO UPDATE SET"
        "  entry_identity=excluded.entry_identity, entry_fingerprint=excluded.entry_fingerprint,"
        "  direction=excluded.direction, model_version=excluded.model_version,"
        "  planner_version=excluded.planner_version, priority_canvas=excluded.priority_canvas,"
        "  updated_at=excluded.updated_at"));
    q.bindValue(QStringLiteral(":id"), job.jobId);
    q.bindValue(QStringLiteral(":ent"), job.entryId);
    q.bindValue(QStringLiteral(":fp"), job.entryFingerprint);
    q.bindValue(QStringLiteral(":dir"), static_cast<int>(job.direction));
    q.bindValue(QStringLiteral(":mv"), job.modelVersion);
    q.bindValue(QStringLiteral(":pv"), job.plannerVersion);
    q.bindValue(QStringLiteral(":state"), static_cast<int>(CanvasStage::Waiting));
    q.bindValue(QStringLiteral(":prio"), job.priorityCanvas);
    q.bindValue(QStringLiteral(":ca"), now);
    q.bindValue(QStringLiteral(":ua"), now);
    return q.exec();
}

QString PanelMapStore::resolveJobId(const QString& entryId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT job_id FROM guided_jobs WHERE entry_identity = :e"
        " ORDER BY created_at DESC, rowid DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":e"), entryId);
    if (!q.exec() || !q.next()) return QString();
    return q.value(0).toString();
}

// --- publish (atomic) --------------------------------------------------------

bool PanelMapStore::publishCanvas(const CanvasSpec& canvas,
                                  const QVector<Detection>& detections,
                                  const GuidedPath& path) {
    if (!m_db.isOpen()) return false;

    // Resolve the owning job first; a canvas with no job is never (even partially)
    // written.
    const QString jobId = resolveJobId(canvas.entryId);
    if (jobId.isEmpty()) return false;

    if (!m_db.transaction()) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto rollback = [this]() -> bool { m_db.rollback(); return false; };

    // 1) Upsert the canvas row (Ready). ON CONFLICT preserves override_kind — it is
    //    deliberately absent from the DO UPDATE set so a user override survives a
    //    re-publish.
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO guided_canvases"
            " (job_id, canvas_index, page_indices, canvas_kind, fingerprint, width, height,"
            "  stage, confidence, fallback_code, model_version, planner_version)"
            " VALUES (:job,:idx,:pages,:kind,:fp,:w,:h,:stage,:conf,:fb,:mv,:pv)"
            " ON CONFLICT(job_id, canvas_index) DO UPDATE SET"
            "  page_indices=excluded.page_indices, canvas_kind=excluded.canvas_kind,"
            "  fingerprint=excluded.fingerprint, width=excluded.width, height=excluded.height,"
            "  stage=excluded.stage, confidence=excluded.confidence,"
            "  fallback_code=excluded.fallback_code, model_version=excluded.model_version,"
            "  planner_version=excluded.planner_version"));
        q.bindValue(QStringLiteral(":job"), jobId);
        q.bindValue(QStringLiteral(":idx"), canvas.canvasIndex);
        q.bindValue(QStringLiteral(":pages"), joinIndices(canvas.sourcePageIndices));
        q.bindValue(QStringLiteral(":kind"), static_cast<int>(canvas.kind));
        q.bindValue(QStringLiteral(":fp"), canvas.fingerprint);
        q.bindValue(QStringLiteral(":w"), canvas.sourceSize.width());
        q.bindValue(QStringLiteral(":h"), canvas.sourceSize.height());
        q.bindValue(QStringLiteral(":stage"), static_cast<int>(CanvasStage::Ready));
        q.bindValue(QStringLiteral(":conf"), maxConfidence(detections));
        q.bindValue(QStringLiteral(":fb"), toCode(path.reason));
        q.bindValue(QStringLiteral(":mv"), path.modelVersion);
        q.bindValue(QStringLiteral(":pv"), path.plannerVersion);
        if (!q.exec()) return rollback();
    }

    // 2) Resolve the canvas_id for the (job, index) we just wrote.
    qint64 canvasId = -1;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT canvas_id FROM guided_canvases WHERE job_id=:job AND canvas_index=:idx"));
        q.bindValue(QStringLiteral(":job"), jobId);
        q.bindValue(QStringLiteral(":idx"), canvas.canvasIndex);
        if (!q.exec() || !q.next()) return rollback();
        canvasId = q.value(0).toLongLong();
    }

    // 3) Replace generated detections for this canvas.
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM guided_detections WHERE canvas_id = :c"));
        q.bindValue(QStringLiteral(":c"), canvasId);
        if (!q.exec()) return rollback();
    }
    for (const Detection& d : detections) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO guided_detections"
            " (canvas_id, detection_id, kind, x, y, w, h, confidence, accepted)"
            " VALUES (:c,:id,:k,:x,:y,:w,:h,:conf,:acc)"));
        q.bindValue(QStringLiteral(":c"), canvasId);
        q.bindValue(QStringLiteral(":id"), d.id);
        q.bindValue(QStringLiteral(":k"), static_cast<int>(d.kind));
        q.bindValue(QStringLiteral(":x"), d.box.x);
        q.bindValue(QStringLiteral(":y"), d.box.y);
        q.bindValue(QStringLiteral(":w"), d.box.width);
        q.bindValue(QStringLiteral(":h"), d.box.height);
        q.bindValue(QStringLiteral(":conf"), d.confidence);
        q.bindValue(QStringLiteral(":acc"), 1);
        if (!q.exec()) return rollback();          // e.g. duplicate detection id -> whole publish rolls back
    }

    // 4) Upsert the serialized path blob.
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO guided_paths (canvas_id, serialized_json, published_at)"
            " VALUES (:c,:j,:t)"
            " ON CONFLICT(canvas_id) DO UPDATE SET"
            "  serialized_json=excluded.serialized_json, published_at=excluded.published_at"));
        q.bindValue(QStringLiteral(":c"), canvasId);
        q.bindValue(QStringLiteral(":j"), serializePath(path));
        q.bindValue(QStringLiteral(":t"), now);
        if (!q.exec()) return rollback();
    }

    if (!m_db.commit()) return rollback();
    return true;
}

// --- lookup ------------------------------------------------------------------

LookupResult PanelMapStore::lookup(const CacheKey& key) const {
    LookupResult res;
    if (!m_db.isOpen()) return res;

    // Match by fingerprint + version (model/planner) + reading direction (from the
    // owning job). Version/direction gating is what makes a mismatched request a
    // clean cache miss.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT c.canvas_id, c.stage, c.override_kind, p.serialized_json"
        " FROM guided_canvases c"
        " JOIN guided_jobs j ON j.job_id = c.job_id"
        " LEFT JOIN guided_paths p ON p.canvas_id = c.canvas_id"
        " WHERE c.fingerprint = :fp AND c.model_version = :mv AND c.planner_version = :pv"
        "   AND j.direction = :dir"
        " ORDER BY c.canvas_id DESC LIMIT 1"));
    q.bindValue(QStringLiteral(":fp"), key.fingerprint);
    q.bindValue(QStringLiteral(":mv"), key.modelVersion);
    q.bindValue(QStringLiteral(":pv"), key.plannerVersion);
    q.bindValue(QStringLiteral(":dir"), static_cast<int>(key.direction));
    if (!q.exec() || !q.next()) return res;         // cache miss

    res.stage = static_cast<CanvasStage>(q.value(1).toInt());
    res.override = static_cast<OverrideKind>(q.value(2).toInt());

    const QByteArray blob = q.value(3).toByteArray();
    if (!blob.isEmpty()) {
        if (auto gen = deserializePath(blob)) res.generated = *gen;
    }

    if (res.override == OverrideKind::WholePage) {
        // The whole-page override is a synthetic single-step Overview covering the
        // full canvas; it wins over the generated path but leaves it intact.
        GuidedPath eff;
        eff.canvasFingerprint = key.fingerprint;
        eff.modelVersion = key.modelVersion;
        eff.plannerVersion = key.plannerVersion;
        eff.outcome = PlanOutcome::Trusted;
        eff.reason = FallbackCode::None;
        PathStep step;
        step.kind = StepKind::Overview;
        step.sourcePanelId = -1;
        step.camera = NormalizedRect{0.0, 0.0, 1.0, 1.0};
        step.holdSecondsAt1x = 0.8;
        step.transitionSecondsAt1x = 0.35;
        step.plannerConfidence = 1.0;
        eff.steps = {step};
        res.path = eff;
        res.found = true;
    } else if (res.generated.has_value()) {
        res.path = *res.generated;
        res.found = true;
    }
    return res;
}

QVector<Detection> PanelMapStore::rawDetections(const CacheKey& key) const {
    QVector<Detection> out;
    if (!m_db.isOpen()) return out;
    // Detections are direction-independent: matched by fingerprint alone.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT d.detection_id, d.kind, d.x, d.y, d.w, d.h, d.confidence"
        " FROM guided_detections d"
        " JOIN guided_canvases c ON c.canvas_id = d.canvas_id"
        " WHERE c.fingerprint = :fp"
        " ORDER BY d.canvas_id, d.detection_id"));
    q.bindValue(QStringLiteral(":fp"), key.fingerprint);
    if (!q.exec()) return out;
    while (q.next()) {
        Detection d;
        d.id = q.value(0).toInt();
        d.kind = static_cast<DetectionKind>(q.value(1).toInt());
        d.box = NormalizedRect{q.value(2).toDouble(), q.value(3).toDouble(),
                               q.value(4).toDouble(), q.value(5).toDouble()};
        d.confidence = q.value(6).toDouble();
        out.append(d);
    }
    return out;
}

// --- overrides ---------------------------------------------------------------

bool PanelMapStore::setOverride(const QString& fingerprint, OverrideKind kind) {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE guided_canvases SET override_kind = :k WHERE fingerprint = :fp"));
    q.bindValue(QStringLiteral(":k"), static_cast<int>(kind));
    q.bindValue(QStringLiteral(":fp"), fingerprint);
    return q.exec();
}

OverrideKind PanelMapStore::overrideFor(const QString& fingerprint) const {
    if (!m_db.isOpen()) return OverrideKind::None;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT override_kind FROM guided_canvases WHERE fingerprint = :fp LIMIT 1"));
    q.bindValue(QStringLiteral(":fp"), fingerprint);
    if (!q.exec() || !q.next()) return OverrideKind::None;
    return static_cast<OverrideKind>(q.value(0).toInt());
}

// --- retry -------------------------------------------------------------------

void PanelMapStore::retryCanvas(const QString& fingerprint) {
    if (!m_db.isOpen()) return;

    QVector<qint64> canvasIds;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT canvas_id FROM guided_canvases WHERE fingerprint = :fp"));
        q.bindValue(QStringLiteral(":fp"), fingerprint);
        if (!q.exec()) return;
        while (q.next()) canvasIds.append(q.value(0).toLongLong());
    }
    for (qint64 cid : canvasIds) {
        QSqlQuery dp(m_db);
        dp.prepare(QStringLiteral("DELETE FROM guided_paths WHERE canvas_id = :c"));
        dp.bindValue(QStringLiteral(":c"), cid);
        dp.exec();
        QSqlQuery dd(m_db);
        dd.prepare(QStringLiteral("DELETE FROM guided_detections WHERE canvas_id = :c"));
        dd.bindValue(QStringLiteral(":c"), cid);
        dd.exec();
    }
    // Reset only the generated state; override_kind (and version columns) are kept.
    QSqlQuery us(m_db);
    us.prepare(QStringLiteral(
        "UPDATE guided_canvases SET stage = :s, confidence = 0, fallback_code = ''"
        " WHERE fingerprint = :fp"));
    us.bindValue(QStringLiteral(":s"), static_cast<int>(CanvasStage::Waiting));
    us.bindValue(QStringLiteral(":fp"), fingerprint);
    us.exec();
}

// --- checkpoint / summary ----------------------------------------------------

bool PanelMapStore::saveCheckpoint(const QString& jobId, int priorityCanvas) {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE guided_jobs SET priority_canvas = :p, updated_at = :t WHERE job_id = :j"));
    q.bindValue(QStringLiteral(":p"), priorityCanvas);
    q.bindValue(QStringLiteral(":t"), QDateTime::currentMSecsSinceEpoch());
    q.bindValue(QStringLiteral(":j"), jobId);
    if (!q.exec()) return false;
    return q.numRowsAffected() != 0;
}

JobSummary PanelMapStore::jobSummary(const QString& jobId) const {
    JobSummary sum;
    sum.jobId = jobId;
    if (!m_db.isOpen()) return sum;

    QSqlQuery j(m_db);
    j.prepare(QStringLiteral(
        "SELECT state, paused, priority_canvas FROM guided_jobs WHERE job_id = :j"));
    j.bindValue(QStringLiteral(":j"), jobId);
    if (j.exec() && j.next()) {
        sum.stage = static_cast<CanvasStage>(j.value(0).toInt());
        sum.paused = j.value(1).toInt() != 0;
        sum.priorityCanvas = j.value(2).toInt();
    }

    QSqlQuery c(m_db);
    c.prepare(QStringLiteral(
        "SELECT COUNT(*),"
        "       COALESCE(SUM(CASE WHEN stage = :ready THEN 1 ELSE 0 END), 0)"
        " FROM guided_canvases WHERE job_id = :j"));
    c.bindValue(QStringLiteral(":ready"), static_cast<int>(CanvasStage::Ready));
    c.bindValue(QStringLiteral(":j"), jobId);
    if (c.exec() && c.next()) {
        sum.total = c.value(0).toInt();
        sum.ready = c.value(1).toInt();
    }
    return sum;
}

} // namespace guided
