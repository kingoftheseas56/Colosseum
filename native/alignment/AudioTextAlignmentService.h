#pragma once

// AudioTextAlignmentService — the QML-facing facade + background orchestration for
// the audiobook↔EPUB read-along (Agent 2).
//
// One small thing: given a book that has a paired audiobook, it schedules one
// resumable alignment job per chapter on the APP-OWNED work::BackgroundWorkCoordinator
// and answers the reader's status/queue/pause/retry questions — keyed by the reader's
// `bookId`. It owns NO thread and NO worker of its own. This mirrors, faithfully,
// guided::PanelAnalysisService: all service bookkeeping (per-pair chapter records, the
// focus chapter, the paused flag) lives on the service (GUI) thread; a work item touches
// ONLY a WORKER-LOCAL AlignmentStore it opens itself and the injected pure processor,
// and reports progress back exclusively through a private QUEUED signal
// (chapterProgressed → handleChapterProgress). That split is the whole thread-safety
// story.
//
// Two concurrency invariants make the split correct (violate either and the feature
// silently corrupts):
//   1. Worker WRITES go through a worker-local AlignmentStore(dbPath) — never the
//      injected read-store, and dbPath is a real FILE path (never ":memory:", which is
//      per-connection and would write a different, empty database). Main-thread reads
//      (statusFor/chaptersFor) use the injected read-store; WAL lets the reader see the
//      worker's committed writes.
//   2. BackgroundActivityRegistry::publish() and jobChanged are emitted ONLY on the
//      service thread. The work item reports progress solely via the queued signal; the
//      GUI-thread handler is the one that touches the registry.
//
// The real speech/alignment pipeline (Task 12) supplies the real `processor`; Task 3
// proves the whole scheduling/pause/failure/retry/restart machinery with a fake one.
//
// Consumes Qt Core + the app work spine + the alignment store/types. Not the app's
// main.cpp registration (that is applied separately).

#include "AlignmentStore.h"
#include "AlignmentTypes.h"
#include "work/BackgroundActivityRegistry.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

class AudioPairingStore;

namespace alignment {

// The read-only input handed to a chapter's processor: enough to transcribe/match/align
// one chapter, captured by value into the work item so the worker never reads mutable
// service state. audioStart/EndMs are the chapter's audio bounds (nominal in Task 3;
// real durations arrive in Task 8).
struct ChapterInput {
    QString pairId;         // AlignmentStore key for this book↔audiobook pairing
    QString bookId;         // the reader's stable book identity (facade key)
    QString pairKey;        // the audiobook's title+author pairing identity
    QString bookPath;       // the EPUB's local path (empty until on disk)
    int chapterIndex = 0;
    qint64 audioStartMs = 0;
    qint64 audioEndMs = 0;
    QStringList localFiles; // the chapter's local audio file(s)
};

// What a processor produces for a chapter. On success it fills the cues + confidence and
// leaves `failure` None; on a terminal pipeline failure it sets `failure` and the service
// records it. An empty-but-successful result still runs through the store's Ready gate,
// which fails it closed (CouldntSync/edition_mismatch) — nothing guessed is ever stored.
struct ChapterResult {
    QList<SentenceCue> sentences;
    QList<WordCue> words;
    QList<RegionRecord> regions;
    double confidence = 0.0;
    FailureCode failure = FailureCode::None;
};

// The ChapterProcessor seam. `report(stage, progress)` is the worker's progress channel
// (checkpoint + activity row update); the processor calls it as it advances, and returns
// a work::WorkResult (Completed on a finished attempt, Cancelled if ctx.checkpoint()
// went false). Pure: it reads only its arguments and may call ctx.checkpoint().
using ChapterReport = std::function<void(Stage, double)>;
using ChapterProcessor = std::function<work::WorkResult(
    const ChapterInput&, work::WorkContext&, const ChapterReport&, ChapterResult&)>;

class AudioTextAlignmentService : public QObject {
    Q_OBJECT
public:
    AudioTextAlignmentService(work::BackgroundWorkCoordinator* coordinator,
                              work::BackgroundActivityRegistry* registry,
                              AlignmentStore* readStore,
                              QString dbPath,
                              AudioPairingStore* pairing,
                              std::function<QStringList(QString pairKey)> localFiles,
                              ChapterProcessor processor,
                              QObject* parent = nullptr);
    ~AudioTextAlignmentService() override;

    // Establish (or re-sync) the bookId→pair mapping, enumerate chapters from the
    // injected localFiles(pairKey) — one local audio file = one chapter, in order — and
    // submit one resumable work item per chapter. Idempotent: re-calling upgrades the
    // identity and refreshes bounds without duplicating chapter rows or re-running the
    // chapters already Ready.
    Q_INVOKABLE void ensurePair(const QString& bookId, const QString& bookPath,
                                const QString& pairKey);

    // Pair-level summary: { stage, ready, total, paused }.
    Q_INVOKABLE QVariantMap statusFor(const QString& bookId) const;
    // One map per chapter: { index, stage, failureCode, coverage, confidence }.
    Q_INVOKABLE QVariantList chaptersFor(const QString& bookId) const;

    Q_INVOKABLE void pause(const QString& bookId);
    Q_INVOKABLE void resume(const QString& bookId);
    Q_INVOKABLE void retry(const QString& bookId, int chapterIndex);
    Q_INVOKABLE void restart(const QString& bookId);       // the restart-confirmation seam
    Q_INVOKABLE void prioritize(const QString& bookId, int chapterIndex);

signals:
    void jobChanged(const QString& bookId);
    // Internal marshaling channel: a work item (worker thread) emits this; a queued
    // connection delivers it to handleChapterProgress() on the service thread. Not part
    // of the QML API — do not connect from UI.
    void chapterProgressed(const QString& bookId, int chapterIndex, int stage,
                           double progress, int failureCode);

private:
    struct ChapterRuntime {
        int index = 0;
        qint64 audioStartMs = 0;
        qint64 audioEndMs = 0;
        QStringList localFiles;
    };
    struct PairState {
        QString pairId;
        QString pairKey;
        QString bookPath;
        QString bookLabel;               // display label for the activity row title
        QVector<ChapterRuntime> chapters;
        int focusIndex = 0;
        bool paused = false;
    };

    void handleChapterProgress(const QString& bookId, int chapterIndex, int stage,
                               double progress, int failureCode);
    void onActivityPauseRequested(const QString& rowId);
    void onActivityResumeRequested(const QString& rowId);

    work::WorkFn makeChapterFn(const ChapterInput& input);
    void submitChapter(const QString& bookId, const PairState& state, int chapterIndex);
    void publishRow(const QString& bookId);
    Stage pairStage(const QList<ChapterStatus>& chapters, int ready, int total) const;
    int priorityFor(int chapterIndex, int focus, const QVector<int>& allIndices) const;
    QVector<int> allIndices(const PairState& state) const;
    QString bookIdForRow(const QString& rowId) const;

    static QString makePairId(const QString& bookId, const QString& pairKey);
    static QString rowId(const QString& pairId);
    static QString workId(const QString& pairId, int chapterIndex);

    work::BackgroundWorkCoordinator* m_coordinator = nullptr;  // app-owned; not owned here
    work::BackgroundActivityRegistry* m_registry = nullptr;    // app-owned; not owned here
    AlignmentStore* m_readStore = nullptr;                     // main-thread reads; not owned
    QString m_dbPath;                                          // worker-local write connections
    AudioPairingStore* m_pairing = nullptr;                    // injected; reserved for Task 12
    std::function<QStringList(QString)> m_localFiles;
    ChapterProcessor m_processor;
    QHash<QString, PairState> m_pairs;                         // bookId → state (service thread only)
};

} // namespace alignment
