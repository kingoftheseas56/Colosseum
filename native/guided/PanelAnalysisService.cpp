// native/guided/PanelAnalysisService.cpp
#include "guided/PanelAnalysisService.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QUrl>
#include <QtEndian>

#include <algorithm>

namespace guided {

namespace {

// The service's fixed versions. The real detector version is stamped by the
// concrete detector later; an EMPTY-but-non-null string here is a valid, matching
// cache dimension. (A null QString binds as SQL NULL — which both violates the
// store's NOT NULL columns AND never compares equal, even to another NULL — so the
// empty string must be non-null.)
QString modelVersion() { return QStringLiteral(""); }
QString plannerVersion() { return QStringLiteral("guided-v1"); }

QString stageToString(CanvasStage s) {
    switch (s) {
    case CanvasStage::Waiting:   return QStringLiteral("waiting");
    case CanvasStage::Decoding:  return QStringLiteral("decoding");
    case CanvasStage::Detecting: return QStringLiteral("detecting");
    case CanvasStage::Planning:  return QStringLiteral("planning");
    case CanvasStage::Ready:     return QStringLiteral("ready");
    case CanvasStage::Fallback:  return QStringLiteral("fallback");
    case CanvasStage::Failed:    return QStringLiteral("failed");
    }
    return QStringLiteral("waiting");
}

// SHA-256 over: one schema byte, one canvas-kind byte, then per source file in
// PHYSICAL order — its page index (LE i32), its byte count (LE i64), its bytes.
// Deterministic; a pure function of the canvas's files, so it can be computed on
// the service thread up front and reused verbatim by the worker.
QString computeFingerprint(const CanvasSpec& spec) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray(1, char(0x01)));                         // schema byte
    hash.addData(QByteArray(1, char(static_cast<int>(spec.kind)))); // canvas kind byte
    for (int i = 0; i < spec.localFiles.size(); ++i) {
        const qint32 page = (i < spec.sourcePageIndices.size()) ? spec.sourcePageIndices[i] : i;
        const qint32 pageLE = qToLittleEndian<qint32>(page);
        hash.addData(QByteArray(reinterpret_cast<const char*>(&pageLE), sizeof(pageLE)));

        QFile f(spec.localFiles[i]);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const QByteArray bytes = f.readAll();
        const qint64 countLE = qToLittleEndian<qint64>(static_cast<qint64>(bytes.size()));
        hash.addData(QByteArray(reinterpret_cast<const char*>(&countLE), sizeof(countLE)));
        hash.addData(bytes);
    }
    return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}

// Decode the combined canvas image from local file paths. Single: the one image.
// Spread: lay the pages side by side (physical order) into one wide image. Uses
// only its arguments — safe to call on the worker thread. Null on any decode fail.
QImage decodeCanvas(const CanvasSpec& spec) {
    if (spec.localFiles.isEmpty()) return QImage();
    if (spec.kind == CanvasKind::SinglePage || spec.localFiles.size() == 1) {
        QImage img;
        if (!img.load(spec.localFiles.first())) return QImage();
        return img;
    }
    QVector<QImage> imgs;
    int totalW = 0;
    int maxH = 0;
    for (const QString& p : spec.localFiles) {
        QImage img;
        if (!img.load(p)) return QImage();
        totalW += img.width();
        maxH = std::max(maxH, img.height());
        imgs.append(img);
    }
    if (imgs.isEmpty() || totalW <= 0 || maxH <= 0) return QImage();
    QImage combined(totalW, maxH, QImage::Format_RGB32);
    combined.fill(Qt::black);
    QPainter painter(&combined);
    int x = 0;
    for (const QImage& img : imgs) {
        painter.drawImage(x, 0, img);
        x += img.width();
    }
    painter.end();
    return combined;
}

} // namespace

PanelAnalysisService::PanelAnalysisService(work::BackgroundWorkCoordinator* work, IPanelDetector* detector,
                                           PanelPlanner* planner, PanelMapStore* store, QObject* parent)
    : QObject(parent), m_work(work), m_detector(detector), m_planner(planner), m_store(store) {
    // Marshal worker-thread results back onto the service thread. Queued delivery is
    // what keeps all m_entries mutation single-threaded.
    connect(this, &PanelAnalysisService::stageUpdated,
            this, &PanelAnalysisService::handleStageUpdate, Qt::QueuedConnection);
}

PanelAnalysisService::~PanelAnalysisService() {
    // Cancel every outstanding work item so a still-alive coordinator never runs a
    // fn against this destroyed service. (Owners drain the coordinator before this
    // runs; cancel is a cheap no-op on already-terminal items.)
    if (m_work) {
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
            for (int idx : it.value().order)
                m_work->cancel(workId(it.key(), idx));
    }
}

QString PanelAnalysisService::workId(const QString& entryId, int canvasIndex) {
    return entryId + QStringLiteral("#") + QString::number(canvasIndex);
}

int PanelAnalysisService::recordIndexFor(const EntryState& entry, int canvasIndex) const {
    for (int i = 0; i < entry.canvases.size(); ++i)
        if (entry.canvases[i].spec.canvasIndex == canvasIndex) return i;
    return -1;
}

// Deterministic, UNIQUE priorities so one worker visits in exact reading order.
//   visible V              -> 100
//   V+1..V+4 (next four)    -> 90,89,88,87 (ascending distance)
//   V-1 (previous)          -> 80
//   everything else         -> 10,9,8,... in ascending canvas-index order
int PanelAnalysisService::priorityFor(int canvasIndex, int visible,
                                      const QVector<int>& allIndices) const {
    if (canvasIndex == visible) return 100;
    const int dist = canvasIndex - visible;
    if (dist >= 1 && dist <= 4) return 90 - (dist - 1);   // 90,89,88,87
    if (canvasIndex == visible - 1) return 80;

    QVector<int> remainder;
    remainder.reserve(allIndices.size());
    for (int idx : allIndices) {
        if (idx == visible) continue;
        if (idx == visible - 1) continue;
        const int d = idx - visible;
        if (d >= 1 && d <= 4) continue;
        remainder.append(idx);
    }
    std::sort(remainder.begin(), remainder.end());
    const int pos = static_cast<int>(remainder.indexOf(canvasIndex));
    return 10 - pos;                                       // unique descending; pos>=0 for a remainder
}

void PanelAnalysisService::openEntry(const QString& entryId, const QVariantList& canvasModel, bool rtl) {
    EntryState e;
    e.entryId = entryId;
    e.direction = rtl ? ReadingDirection::Rtl : ReadingDirection::Ltr;
    e.visibleCanvas = 0;

    JobSpec job;
    job.jobId = entryId;
    job.entryId = entryId;
    job.entryFingerprint = QStringLiteral("");        // non-null: entry_fingerprint is NOT NULL
    job.direction = e.direction;
    job.modelVersion = modelVersion();
    job.plannerVersion = plannerVersion();
    m_store->beginJob(job);

    for (const QVariant& cv : canvasModel) {
        const QVariantMap m = cv.toMap();
        CanvasRuntime rt;
        CanvasSpec& spec = rt.spec;
        spec.entryId = entryId;
        spec.canvasIndex = m.value(QStringLiteral("canvasIndex")).toInt();
        spec.kind = (m.value(QStringLiteral("kind")).toString() == QLatin1String("spread"))
                        ? CanvasKind::Spread : CanvasKind::SinglePage;
        spec.direction = e.direction;
        spec.sourceSize = QSize(m.value(QStringLiteral("width")).toInt(),
                                m.value(QStringLiteral("height")).toInt());
        for (const QVariant& pv : m.value(QStringLiteral("pageIndices")).toList())
            spec.sourcePageIndices.append(pv.toInt());

        // Convert localFiles to local FS paths; reject non-file / nonexistent so we
        // never analyze remote or undownloaded content.
        const QVariantList files = m.value(QStringLiteral("localFiles")).toList();
        rt.valid = !files.isEmpty();
        for (const QVariant& fv : files) {
            const QUrl u(fv.toString());
            if (!u.isLocalFile()) { rt.valid = false; continue; }
            const QString path = u.toLocalFile();
            const QFileInfo fi(path);
            if (!fi.exists() || !fi.isFile()) rt.valid = false;
            spec.localFiles.append(path);
        }
        if (rt.valid) {
            spec.fingerprint = computeFingerprint(spec);
            if (spec.fingerprint.isEmpty()) rt.valid = false;   // unreadable bytes
        }

        e.order.append(spec.canvasIndex);
        e.canvases.append(rt);
    }

    m_entries.insert(entryId, e);

    // Submit one work item per canvas at the initial (visible=0) priorities. Under
    // suspended pressure the caller can reprioritize before anything runs.
    const EntryState& stored = m_entries[entryId];
    for (const CanvasRuntime& rt : stored.canvases) {
        work::WorkSpec ws;
        ws.id = workId(entryId, rt.spec.canvasIndex);
        ws.priority = priorityFor(rt.spec.canvasIndex, stored.visibleCanvas, stored.order);
        m_work->submit(ws, makeAnalyzeFn(rt.spec, rt.valid, entryId, rt.spec.canvasIndex));
    }
    emit jobChanged(entryId);
}

void PanelAnalysisService::closeEntry(const QString& entryId) {
    auto it = m_entries.find(entryId);
    if (it == m_entries.end()) return;
    it->closed = true;                 // drop reader interest; do NOT cancel — analysis continues
    emit jobChanged(entryId);
}

void PanelAnalysisService::setVisibleCanvas(const QString& entryId, int canvasIndex) {
    auto it = m_entries.find(entryId);
    if (it == m_entries.end()) return;
    it->visibleCanvas = canvasIndex;
    for (int idx : it->order)
        m_work->reprioritize(workId(entryId, idx), priorityFor(idx, canvasIndex, it->order));
    m_store->saveCheckpoint(entryId, canvasIndex);
    emit jobChanged(entryId);
}

work::WorkFn PanelAnalysisService::makeAnalyzeFn(const CanvasSpec& spec, bool valid,
                                                 const QString& entryId, int canvasIndex) {
    // Everything the worker needs is captured BY VALUE (spec, flags) or is an
    // injected pointer read-only (detector/planner/store). It never reads mutable
    // service state; it reports progress ONLY through the queued stageUpdated signal.
    return [this, spec, valid, entryId, canvasIndex](work::WorkContext& ctx) -> work::WorkResult {
        auto fail = [&](FallbackCode code) {
            emit stageUpdated(entryId, canvasIndex,
                              static_cast<int>(CanvasStage::Failed), static_cast<int>(code));
            return work::WorkResult::Failed;
        };

        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        emit stageUpdated(entryId, canvasIndex,
                          static_cast<int>(CanvasStage::Decoding), static_cast<int>(FallbackCode::None));

        if (!valid) return fail(FallbackCode::ImageDecodeFailed);

        const QImage image = decodeCanvas(spec);
        if (image.isNull()) return fail(FallbackCode::ImageDecodeFailed);

        emit stageUpdated(entryId, canvasIndex,
                          static_cast<int>(CanvasStage::Detecting), static_cast<int>(FallbackCode::None));
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;

        DetectorResult raw = m_detector->detect(image, ctx);
        if (raw.error != FallbackCode::None) return fail(raw.error);

        emit stageUpdated(entryId, canvasIndex,
                          static_cast<int>(CanvasStage::Planning), static_cast<int>(FallbackCode::None));

        GuidedPath path = m_planner->plan(spec, raw.detections);
        path.modelVersion = modelVersion();                 // service-known model version (empty here)

        if (!m_store->publishCanvas(spec, raw.detections, path))
            return fail(FallbackCode::StoreFailed);

        // Both Trusted and planner-Fallback paths persist as a usable Ready row; the
        // fallback reason rides in failureCode so the UI can surface it.
        emit stageUpdated(entryId, canvasIndex,
                          static_cast<int>(CanvasStage::Ready), static_cast<int>(path.reason));
        return work::WorkResult::Completed;
    };
}

void PanelAnalysisService::handleStageUpdate(const QString& entryId, int canvasIndex,
                                             int stage, int failureCode) {
    auto it = m_entries.find(entryId);
    if (it != m_entries.end()) {
        const int ri = recordIndexFor(*it, canvasIndex);
        if (ri >= 0) {
            it->canvases[ri].stage = static_cast<CanvasStage>(stage);
            it->canvases[ri].failureCode = static_cast<FallbackCode>(failureCode);
        }
    }
    emit canvasChanged(entryId, canvasIndex);
}

QVariantMap PanelAnalysisService::pathForCanvas(const QString& entryId, int canvasIndex) const {
    const auto it = m_entries.constFind(entryId);
    if (it == m_entries.constEnd()) return {};
    const int ri = recordIndexFor(*it, canvasIndex);
    if (ri < 0) return {};
    const CanvasRuntime& rt = it->canvases[ri];
    if (rt.spec.fingerprint.isEmpty()) return {};           // not yet analyzable

    CacheKey key;
    key.fingerprint = rt.spec.fingerprint;
    key.modelVersion = modelVersion();
    key.plannerVersion = plannerVersion();
    key.direction = it->direction;
    const LookupResult r = m_store->lookup(key);
    if (!r.found) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(serializePath(r.path));
    return doc.object().toVariantMap();
}

QVariantMap PanelAnalysisService::jobSummary(const QString& entryId) const {
    const auto it = m_entries.constFind(entryId);
    if (it == m_entries.constEnd()) return {};
    const EntryState& e = *it;

    int ready = 0;
    for (const CanvasRuntime& rt : e.canvases)
        if (rt.stage == CanvasStage::Ready) ++ready;
    const int total = e.canvases.size();

    CanvasStage stage = CanvasStage::Waiting;
    if (total > 0 && ready == total) {
        stage = CanvasStage::Ready;
    } else {
        const int ri = recordIndexFor(e, e.visibleCanvas);
        if (ri >= 0) stage = e.canvases[ri].stage;
    }

    QVariantMap m;
    m.insert(QStringLiteral("stage"), stageToString(stage));
    m.insert(QStringLiteral("ready"), ready);
    m.insert(QStringLiteral("total"), total);
    m.insert(QStringLiteral("paused"), e.paused);
    m.insert(QStringLiteral("currentCanvas"), e.visibleCanvas);
    return m;
}

QVariantList PanelAnalysisService::activeJobs() const {
    QVariantList out;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const EntryState& e = it.value();
        if (e.closed) continue;

        int ready = 0;
        for (const CanvasRuntime& rt : e.canvases)
            if (rt.stage == CanvasStage::Ready) ++ready;
        const int total = e.canvases.size();

        FallbackCode fc = FallbackCode::None;
        const int ri = recordIndexFor(e, e.visibleCanvas);
        if (ri >= 0) fc = e.canvases[ri].failureCode;

        const bool allReady = total > 0 && ready == total;
        QVariantMap row;
        row.insert(QStringLiteral("id"), e.entryId);
        row.insert(QStringLiteral("title"), e.entryId);
        row.insert(QStringLiteral("state"),
                   e.paused ? QStringLiteral("paused")
                            : (allReady ? QStringLiteral("ready") : QStringLiteral("analyzing")));
        row.insert(QStringLiteral("paused"), e.paused);
        row.insert(QStringLiteral("ready"), ready);
        row.insert(QStringLiteral("total"), total);
        row.insert(QStringLiteral("currentCanvas"), e.visibleCanvas);
        row.insert(QStringLiteral("failureCode"), toCode(fc));
        out.append(row);
    }
    return out;
}

QVariantList PanelAnalysisService::canvasDetails(const QString& entryId) const {
    QVariantList out;
    const auto it = m_entries.constFind(entryId);
    if (it == m_entries.constEnd()) return out;
    for (const CanvasRuntime& rt : it->canvases) {
        QVariantMap m;
        m.insert(QStringLiteral("canvasIndex"), rt.spec.canvasIndex);
        m.insert(QStringLiteral("kind"),
                 rt.spec.kind == CanvasKind::Spread ? QStringLiteral("spread") : QStringLiteral("single"));
        m.insert(QStringLiteral("stage"), stageToString(rt.stage));
        m.insert(QStringLiteral("fingerprint"), rt.spec.fingerprint);
        m.insert(QStringLiteral("failureCode"), toCode(rt.failureCode));
        m.insert(QStringLiteral("valid"), rt.valid);
        out.append(m);
    }
    return out;
}

void PanelAnalysisService::pauseJob(const QString& entryId) {
    auto it = m_entries.find(entryId);
    if (it == m_entries.end()) return;
    for (int idx : it->order)
        m_work->pause(workId(entryId, idx));
    it->paused = true;
    emit jobChanged(entryId);
}

void PanelAnalysisService::resumeJob(const QString& entryId) {
    auto it = m_entries.find(entryId);
    if (it == m_entries.end()) return;
    for (int idx : it->order)
        m_work->resume(workId(entryId, idx));
    it->paused = false;
    emit jobChanged(entryId);
}

void PanelAnalysisService::retryCanvas(const QString& entryId, int canvasIndex) {
    auto it = m_entries.find(entryId);
    if (it == m_entries.end()) return;
    const int ri = recordIndexFor(*it, canvasIndex);
    if (ri < 0) return;
    CanvasRuntime& rt = it->canvases[ri];

    if (!rt.spec.fingerprint.isEmpty())
        m_store->retryCanvas(rt.spec.fingerprint);
    rt.stage = CanvasStage::Waiting;
    rt.failureCode = FallbackCode::None;

    work::WorkSpec ws;
    ws.id = workId(entryId, canvasIndex);
    ws.priority = priorityFor(canvasIndex, it->visibleCanvas, it->order);
    m_work->submit(ws, makeAnalyzeFn(rt.spec, rt.valid, entryId, canvasIndex));
    emit canvasChanged(entryId, canvasIndex);
}

void PanelAnalysisService::useWholePage(const QString& entryId, int canvasIndex) {
    const auto it = m_entries.constFind(entryId);
    if (it == m_entries.constEnd()) return;
    const int ri = recordIndexFor(*it, canvasIndex);
    if (ri < 0 || it->canvases[ri].spec.fingerprint.isEmpty()) return;
    m_store->setOverride(it->canvases[ri].spec.fingerprint, OverrideKind::WholePage);
    emit canvasChanged(entryId, canvasIndex);
}

void PanelAnalysisService::useDetectedPanels(const QString& entryId, int canvasIndex) {
    const auto it = m_entries.constFind(entryId);
    if (it == m_entries.constEnd()) return;
    const int ri = recordIndexFor(*it, canvasIndex);
    if (ri < 0 || it->canvases[ri].spec.fingerprint.isEmpty()) return;
    m_store->setOverride(it->canvases[ri].spec.fingerprint, OverrideKind::DetectedPanels);
    emit canvasChanged(entryId, canvasIndex);
}

void PanelAnalysisService::reverseOrder(const QString& entryId, int canvasIndex) {
    auto it = m_entries.find(entryId);
    if (it == m_entries.end()) return;
    const int ri = recordIndexFor(*it, canvasIndex);
    if (ri < 0) return;
    CanvasRuntime& rt = it->canvases[ri];
    if (rt.spec.fingerprint.isEmpty()) return;

    // Flip only THIS canvas's planning direction and republish. The lookup dimension
    // stays the entry/job direction (publishCanvas keys the row to the job), so the
    // re-ordered path remains resolvable via pathForCanvas.
    rt.spec.direction = (rt.spec.direction == ReadingDirection::Rtl)
                            ? ReadingDirection::Ltr : ReadingDirection::Rtl;

    CacheKey key;
    key.fingerprint = rt.spec.fingerprint;
    key.modelVersion = modelVersion();
    key.plannerVersion = plannerVersion();
    key.direction = it->direction;
    const QVector<Detection> dets = m_store->rawDetections(key);

    GuidedPath path = m_planner->plan(rt.spec, dets);
    path.modelVersion = modelVersion();
    m_store->publishCanvas(rt.spec, dets, path);
    emit canvasChanged(entryId, canvasIndex);
}

} // namespace guided
