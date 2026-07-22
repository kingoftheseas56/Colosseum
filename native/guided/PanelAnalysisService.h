// native/guided/PanelAnalysisService.h
//
// PanelAnalysisService — the QML-facing facade + background orchestration for the
// Panel-Aware Guided Comic Reader (Agent 1). It owns NO threads and NO worker: it
// submits one resumable work item per canvas to the APP-OWNED
// work::BackgroundWorkCoordinator, and marshals results back onto its own (GUI)
// thread. All service bookkeeping (per-entry canvas records, visible canvas, paused
// flag) lives on the service thread; a work item touches ONLY the injected
// detector/planner/store and emits a queued signal to report progress. That split
// is the whole thread-safety story — the store is per-thread-connection safe, the
// planner/detector are pure, and no mutable service state is read on the worker.
#pragma once

#include "guided/GuidedTypes.h"
#include "guided/PanelDetector.h"
#include "guided/PanelMapStore.h"
#include "guided/PanelPlanner.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace guided {

class PanelAnalysisService : public QObject {
    Q_OBJECT
public:
    PanelAnalysisService(work::BackgroundWorkCoordinator* work, IPanelDetector* detector,
                         PanelPlanner* planner, PanelMapStore* store, QObject* parent = nullptr);
    ~PanelAnalysisService() override;

    Q_INVOKABLE void openEntry(const QString& entryId, const QVariantList& canvasModel, bool rtl);
    Q_INVOKABLE void closeEntry(const QString& entryId);          // drops reader interest; analysis continues
    Q_INVOKABLE void setVisibleCanvas(const QString& entryId, int canvasIndex);
    Q_INVOKABLE QVariantMap pathForCanvas(const QString& entryId, int canvasIndex) const;
    Q_INVOKABLE QVariantMap jobSummary(const QString& entryId) const;   // {stage,ready,total,paused,currentCanvas}
    Q_INVOKABLE QVariantList activeJobs() const;                  // one row per open entry
    Q_INVOKABLE QVariantList canvasDetails(const QString& entryId) const;
    Q_INVOKABLE void pauseJob(const QString& entryId);
    Q_INVOKABLE void resumeJob(const QString& entryId);
    Q_INVOKABLE void retryCanvas(const QString& entryId, int canvasIndex);
    Q_INVOKABLE void useWholePage(const QString& entryId, int canvasIndex);
    Q_INVOKABLE void useDetectedPanels(const QString& entryId, int canvasIndex);
    Q_INVOKABLE void reverseOrder(const QString& entryId, int canvasIndex);

signals:
    void jobChanged(const QString& entryId);
    void canvasChanged(const QString& entryId, int canvasIndex);
    // Internal marshaling channel: a work item (worker thread) emits this; a queued
    // connection delivers it to handleStageUpdate() on the service thread. Not part
    // of the QML API — do not connect from UI.
    void stageUpdated(const QString& entryId, int canvasIndex, int stage, int failureCode);

private:
    struct CanvasRuntime {
        CanvasSpec spec;                                 // fingerprint filled at openEntry
        bool valid = true;                               // all localFiles are existing file: URLs
        CanvasStage stage = CanvasStage::Waiting;
        FallbackCode failureCode = FallbackCode::None;
    };
    struct EntryState {
        QString entryId;
        ReadingDirection direction = ReadingDirection::Rtl;
        QVector<CanvasRuntime> canvases;
        QVector<int> order;                              // canvasIndex values, submission order
        int visibleCanvas = 0;
        bool paused = false;
        bool closed = false;
    };

    void handleStageUpdate(const QString& entryId, int canvasIndex, int stage, int failureCode);
    work::WorkFn makeAnalyzeFn(const CanvasSpec& spec, bool valid,
                               const QString& entryId, int canvasIndex);
    int priorityFor(int canvasIndex, int visible, const QVector<int>& allIndices) const;
    int recordIndexFor(const EntryState& entry, int canvasIndex) const;
    static QString workId(const QString& entryId, int canvasIndex);

    work::BackgroundWorkCoordinator* m_work = nullptr;   // APP-owned; not owned here
    IPanelDetector* m_detector = nullptr;
    PanelPlanner* m_planner = nullptr;
    PanelMapStore* m_store = nullptr;
    QHash<QString, EntryState> m_entries;                // service-thread only
};

} // namespace guided
