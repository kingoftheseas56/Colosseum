// native/alignment/AudioTextAlignmentService.cpp
#include "AudioTextAlignmentService.h"

#include "../AudioPairingStore.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <climits>

namespace alignment {

namespace {

// Nominal per-chapter audio span until real durations arrive (Task 8). Chapter i owns
// [i·kNominalChapterMs, (i+1)·kNominalChapterMs). The Ready-gate denominator is this
// span; the fake/real processor's aligned cues must cover ≥80% of it.
constexpr qint64 kNominalChapterMs = 600000;

int stageRank(Stage s) {
    switch (s) {
    case Stage::Waiting:      return 0;
    case Stage::Preparing:    return 1;
    case Stage::Transcribing: return 2;
    case Stage::Matching:     return 3;
    case Stage::Aligning:     return 4;
    case Stage::Ready:        return 5;
    case Stage::CouldntSync:  return 6;
    }
    return 0;
}

QString labelFor(const QString& pairKey, const QString& bookPath) {
    // pairKey is "title|author"; the label is the human title. Fall back to the book
    // path's file name, then to the raw key.
    const int bar = pairKey.indexOf(QChar('|'));
    if (bar > 0) return pairKey.left(bar);
    if (!pairKey.isEmpty()) return pairKey;
    const int slash = bookPath.lastIndexOf(QChar('/'));
    return slash >= 0 ? bookPath.mid(slash + 1) : bookPath;
}

} // namespace

AudioTextAlignmentService::AudioTextAlignmentService(
    work::BackgroundWorkCoordinator* coordinator, work::BackgroundActivityRegistry* registry,
    AlignmentStore* readStore, QString dbPath, AudioPairingStore* pairing,
    std::function<QStringList(QString)> localFiles, ChapterProcessor processor, QObject* parent)
    : QObject(parent), m_coordinator(coordinator), m_registry(registry), m_readStore(readStore),
      m_dbPath(std::move(dbPath)), m_pairing(pairing), m_localFiles(std::move(localFiles)),
      m_processor(std::move(processor)) {
    // Marshal worker-thread progress back onto the service thread. Queued delivery is
    // what keeps all m_pairs mutation and every registry->publish() single-threaded.
    connect(this, &AudioTextAlignmentService::chapterProgressed,
            this, &AudioTextAlignmentService::handleChapterProgress, Qt::QueuedConnection);

    // The global activity row and the Reader2 controls must drive the SAME job: a pause
    // requested on our row routes into this service's pause(bookId).
    if (m_registry) {
        connect(m_registry, &work::BackgroundActivityRegistry::pauseRequested,
                this, &AudioTextAlignmentService::onActivityPauseRequested);
        connect(m_registry, &work::BackgroundActivityRegistry::resumeRequested,
                this, &AudioTextAlignmentService::onActivityResumeRequested);
    }
}

AudioTextAlignmentService::~AudioTextAlignmentService() {
    // Cancel every outstanding work item so a still-alive coordinator never runs a fn
    // against this destroyed service. Cancel is a cheap no-op on already-terminal items.
    if (m_coordinator) {
        for (auto it = m_pairs.constBegin(); it != m_pairs.constEnd(); ++it)
            for (const ChapterRuntime& c : it.value().chapters)
                m_coordinator->cancel(workId(it.value().pairId, c.index));
    }
}

QString AudioTextAlignmentService::makePairId(const QString& bookId, const QString& pairKey) {
    // The reader's bookId is the stable per-pairing key; it is what statusFor/chaptersFor
    // and every façade call are keyed by, so the store pairId maps 1:1 to it.
    return bookId.isEmpty() ? pairKey : bookId;
}

QString AudioTextAlignmentService::rowId(const QString& pairId) {
    return QStringLiteral("audio-align:") + pairId;
}

QString AudioTextAlignmentService::workId(const QString& pairId, int chapterIndex) {
    return QStringLiteral("audio-align:") + pairId + QStringLiteral(":") + QString::number(chapterIndex);
}

QVector<int> AudioTextAlignmentService::allIndices(const PairState& state) const {
    QVector<int> idx;
    idx.reserve(state.chapters.size());
    for (const ChapterRuntime& c : state.chapters)
        idx.append(c.index);
    return idx;
}

// Deterministic, UNIQUE priorities so one worker visits chapters in exact reading order
// around the focus chapter — the same scheme as PanelAnalysisService::priorityFor, over
// chapter index:
//   focus F                 -> 100
//   F+1..F+4 (next four)     -> 90,89,88,87 (ascending distance)
//   F-1 (previous)           -> 80
//   everything else          -> 10,9,8,... in ascending chapter-index order
int AudioTextAlignmentService::priorityFor(int chapterIndex, int focus,
                                           const QVector<int>& all) const {
    if (chapterIndex == focus) return 100;
    const int dist = chapterIndex - focus;
    if (dist >= 1 && dist <= 4) return 90 - (dist - 1);   // 90,89,88,87
    if (chapterIndex == focus - 1) return 80;

    QVector<int> remainder;
    remainder.reserve(all.size());
    for (int idx : all) {
        if (idx == focus) continue;
        if (idx == focus - 1) continue;
        const int d = idx - focus;
        if (d >= 1 && d <= 4) continue;
        remainder.append(idx);
    }
    std::sort(remainder.begin(), remainder.end());
    const int pos = static_cast<int>(remainder.indexOf(chapterIndex));
    return 10 - pos;                                       // unique descending; pos>=0 for a remainder
}

void AudioTextAlignmentService::ensurePair(const QString& bookId, const QString& bookPath,
                                           const QString& pairKey) {
    if (bookId.isEmpty()) return;
    if (!m_readStore) return;

    const QStringList files = m_localFiles ? m_localFiles(pairKey) : QStringList();
    const QString pairId = makePairId(bookId, pairKey);

    // Upsert the pair identity. Task-3 fingerprints are placeholders derived from the
    // assets; the real EPUB/audio fingerprints arrive with the real pipeline. A stable
    // derivation keeps re-ensure idempotent (unchanged assets preserve chapter results).
    PairIdentity pid;
    pid.pairId = pairId;
    pid.epubFingerprint = QStringLiteral("book:") + (bookPath.isEmpty() ? bookId : bookPath);
    pid.audioFingerprint = QStringLiteral("audio:") + pairKey;
    pid.language = QStringLiteral("en");
    pid.engineVersion = QStringLiteral("align-dev-0");
    pid.coarseModelId = QStringLiteral("coarse-dev-0");
    pid.alignmentModelId = QStringLiteral("ctc-dev-0");
    m_readStore->upsertPair(pid);

    PairState& st = m_pairs[bookId];
    st.pairId = pairId;
    st.pairKey = pairKey;
    st.bookPath = bookPath;
    st.bookLabel = labelFor(pairKey, bookPath);

    // Rebuild the chapter list (indices stay 0..N-1). focusIndex/paused are preserved.
    st.chapters.clear();
    st.chapters.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        ChapterRuntime c;
        c.index = i;
        c.audioStartMs = static_cast<qint64>(i) * kNominalChapterMs;
        c.audioEndMs = static_cast<qint64>(i + 1) * kNominalChapterMs;
        c.localFiles = QStringList{files.at(i)};
        st.chapters.append(c);
    }

    const QVector<int> idx = allIndices(st);
    for (const ChapterRuntime& c : st.chapters)
        m_readStore->ensureChapter(pairId, c.index, c.audioStartMs, c.audioEndMs,
                                   priorityFor(c.index, st.focusIndex, idx));

    // Submit one work item per chapter that is not already Ready. Re-ensure re-syncs
    // (upgrades identity, refreshes bounds) but does NOT re-run a completed chapter, and
    // the ensureChapter UNIQUE gate means no chapter row is ever duplicated. Cancel any
    // prior job for the id before resubmitting so a stale queued job can never double-run.
    for (const ChapterRuntime& c : st.chapters) {
        const ChapterStatus cs = m_readStore->chapterStatus(pairId, c.index);
        if (cs.stage == Stage::Ready) continue;
        m_coordinator->cancel(workId(pairId, c.index));
        submitChapter(bookId, st, c.index);
    }

    publishRow(bookId);
    emit jobChanged(bookId);
}

void AudioTextAlignmentService::submitChapter(const QString& bookId, const PairState& state,
                                              int chapterIndex) {
    if (!m_coordinator) return;
    if (chapterIndex < 0 || chapterIndex >= state.chapters.size()) return;
    const ChapterRuntime& c = state.chapters[chapterIndex];

    ChapterInput input;
    input.pairId = state.pairId;
    input.bookId = bookId;
    input.pairKey = state.pairKey;
    input.bookPath = state.bookPath;
    input.chapterIndex = chapterIndex;
    input.audioStartMs = c.audioStartMs;
    input.audioEndMs = c.audioEndMs;
    input.localFiles = c.localFiles;

    work::WorkSpec ws;
    ws.id = workId(state.pairId, chapterIndex);
    ws.priority = priorityFor(chapterIndex, state.focusIndex, allIndices(state));
    m_coordinator->submit(ws, makeChapterFn(input));
}

work::WorkFn AudioTextAlignmentService::makeChapterFn(const ChapterInput& input) {
    // Everything the worker needs is captured BY VALUE (input) or is `this` used only to
    // emit the queued signal and read the immutable db path / processor. The worker's
    // writes go through a WORKER-LOCAL store; it never touches m_readStore or m_pairs.
    return [this, input](work::WorkContext& ctx) -> work::WorkResult {
        AlignmentStore workerStore(m_dbPath);

        // The progress channel: persist a resumable checkpoint on the worker-local store
        // AND emit the queued signal so the GUI thread updates the activity row. Never
        // called with Ready (the store refuses it; Ready is set atomically by publish).
        const ChapterReport report = [this, &workerStore, &input](Stage stage, double progress) {
            QByteArray blob;
            {
                QDataStream ds(&blob, QIODevice::WriteOnly);
                ds << static_cast<int>(stage) << progress;
            }
            workerStore.saveCheckpoint(input.pairId, input.chapterIndex, stage, blob);
            emit chapterProgressed(input.bookId, input.chapterIndex, static_cast<int>(stage),
                                   progress, static_cast<int>(FailureCode::None));
        };

        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;

        ChapterResult out;
        const work::WorkResult pr = m_processor(input, ctx, report, out);
        if (pr == work::WorkResult::Cancelled || pr == work::WorkResult::Paused)
            return pr;   // leave the chapter's stage/checkpoint as-is → it resumes, never restarts

        if (out.failure != FailureCode::None) {
            workerStore.failChapter(input.pairId, input.chapterIndex, out.failure);
            emit chapterProgressed(input.bookId, input.chapterIndex,
                                   static_cast<int>(Stage::CouldntSync), 1.0,
                                   static_cast<int>(out.failure));
            return work::WorkResult::Failed;
        }

        // Publish through the store's Ready gate. If the cues fall short the chapter is
        // now CouldntSync(edition_mismatch) with zero cues — a correct, visible failure.
        const bool ready = workerStore.publishReadyChapter(
            input.pairId, input.chapterIndex, out.sentences, out.words, out.regions, out.confidence);
        if (ready) {
            emit chapterProgressed(input.bookId, input.chapterIndex,
                                   static_cast<int>(Stage::Ready), 1.0,
                                   static_cast<int>(FailureCode::None));
            return work::WorkResult::Completed;
        }
        emit chapterProgressed(input.bookId, input.chapterIndex,
                               static_cast<int>(Stage::CouldntSync), 1.0,
                               static_cast<int>(FailureCode::EditionMismatch));
        return work::WorkResult::Failed;
    };
}

void AudioTextAlignmentService::handleChapterProgress(const QString& bookId, int /*chapterIndex*/,
                                                      int /*stage*/, double /*progress*/,
                                                      int /*failureCode*/) {
    // The store is the source of truth for stage/coverage/confidence; a progress event
    // just re-derives the activity row and notifies QML. (m_pairs is only touched here on
    // the service thread — the whole point of the queued hop.)
    if (!m_pairs.contains(bookId)) return;
    publishRow(bookId);
    emit jobChanged(bookId);
}

Stage AudioTextAlignmentService::pairStage(const QList<ChapterStatus>& chapters,
                                           int ready, int total) const {
    if (total <= 0) return Stage::Waiting;
    bool anyNonTerminal = false;
    int minRank = INT_MAX;
    Stage minStage = Stage::Ready;
    for (const ChapterStatus& cs : chapters) {
        if (stageIsTerminal(cs.stage)) continue;
        anyNonTerminal = true;
        const int r = stageRank(cs.stage);
        if (r < minRank) { minRank = r; minStage = cs.stage; }
    }
    if (anyNonTerminal) return minStage;
    return ready >= total ? Stage::Ready : Stage::CouldntSync; // all terminal: Ready iff all Ready
}

void AudioTextAlignmentService::publishRow(const QString& bookId) {
    if (!m_registry || !m_readStore) return;
    const auto it = m_pairs.constFind(bookId);
    if (it == m_pairs.constEnd()) return;
    const PairState& st = *it;

    const QList<ChapterStatus> chapters = m_readStore->chapters(st.pairId);
    const int total = st.chapters.size();
    int ready = 0;
    for (const ChapterStatus& cs : chapters)
        if (cs.stage == Stage::Ready) ++ready;
    const Stage stage = pairStage(chapters, ready, total);

    QVariantMap row;
    row.insert(QStringLiteral("title"), QStringLiteral("Text Sync — ") + st.bookLabel);
    row.insert(QStringLiteral("stage"), stageWireCode(stage));
    row.insert(QStringLiteral("progress"), total > 0 ? static_cast<double>(ready) / total : 0.0);
    row.insert(QStringLiteral("paused"), st.paused);
    row.insert(QStringLiteral("canPause"), true);
    row.insert(QStringLiteral("kind"), QStringLiteral("audio_text_alignment"));
    m_registry->publish(rowId(st.pairId), row);
}

QVariantMap AudioTextAlignmentService::statusFor(const QString& bookId) const {
    QVariantMap out;
    const auto it = m_pairs.constFind(bookId);
    if (it == m_pairs.constEnd() || !m_readStore) return out;
    const PairState& st = *it;

    const QList<ChapterStatus> chapters = m_readStore->chapters(st.pairId);
    const int total = st.chapters.size();
    int ready = 0;
    for (const ChapterStatus& cs : chapters)
        if (cs.stage == Stage::Ready) ++ready;
    const Stage stage = pairStage(chapters, ready, total);

    out.insert(QStringLiteral("stage"), stageWireCode(stage));
    out.insert(QStringLiteral("ready"), ready);
    out.insert(QStringLiteral("total"), total);
    out.insert(QStringLiteral("paused"), st.paused);
    return out;
}

QVariantList AudioTextAlignmentService::chaptersFor(const QString& bookId) const {
    QVariantList out;
    const auto it = m_pairs.constFind(bookId);
    if (it == m_pairs.constEnd() || !m_readStore) return out;
    const PairState& st = *it;

    for (const ChapterRuntime& c : st.chapters) {
        const ChapterStatus cs = m_readStore->chapterStatus(st.pairId, c.index);
        QVariantMap m;
        m.insert(QStringLiteral("index"), c.index);
        m.insert(QStringLiteral("stage"), stageWireCode(cs.stage));
        m.insert(QStringLiteral("failureCode"), failureWireCode(cs.failureCode)); // "" when None
        m.insert(QStringLiteral("coverage"), cs.coverage);
        m.insert(QStringLiteral("confidence"), cs.confidence);
        out.append(m);
    }
    return out;
}

void AudioTextAlignmentService::pause(const QString& bookId) {
    auto it = m_pairs.find(bookId);
    if (it == m_pairs.end()) return;
    if (m_coordinator)
        for (const ChapterRuntime& c : it->chapters)
            m_coordinator->pause(workId(it->pairId, c.index));
    it->paused = true;
    publishRow(bookId);
    emit jobChanged(bookId);
}

void AudioTextAlignmentService::resume(const QString& bookId) {
    auto it = m_pairs.find(bookId);
    if (it == m_pairs.end()) return;
    if (m_coordinator)
        for (const ChapterRuntime& c : it->chapters)
            m_coordinator->resume(workId(it->pairId, c.index));
    it->paused = false;
    publishRow(bookId);
    emit jobChanged(bookId);
}

void AudioTextAlignmentService::retry(const QString& bookId, int chapterIndex) {
    auto it = m_pairs.find(bookId);
    if (it == m_pairs.end()) return;
    if (chapterIndex < 0 || chapterIndex >= it->chapters.size()) return;
    if (m_readStore)
        m_readStore->retryChapter(it->pairId, chapterIndex);   // clears cues/failure → Waiting
    if (m_coordinator)
        m_coordinator->cancel(workId(it->pairId, chapterIndex));
    submitChapter(bookId, *it, chapterIndex);
    publishRow(bookId);
    emit jobChanged(bookId);
}

void AudioTextAlignmentService::restart(const QString& bookId) {
    auto it = m_pairs.find(bookId);
    if (it == m_pairs.end()) return;
    if (m_readStore)
        m_readStore->restartPair(it->pairId);                  // reset every chapter → Waiting
    for (const ChapterRuntime& c : it->chapters) {
        if (m_coordinator)
            m_coordinator->cancel(workId(it->pairId, c.index));
        submitChapter(bookId, *it, c.index);
    }
    publishRow(bookId);
    emit jobChanged(bookId);
}

void AudioTextAlignmentService::prioritize(const QString& bookId, int chapterIndex) {
    auto it = m_pairs.find(bookId);
    if (it == m_pairs.end()) return;
    it->focusIndex = chapterIndex;
    if (m_coordinator) {
        const QVector<int> idx = allIndices(*it);
        for (const ChapterRuntime& c : it->chapters)
            m_coordinator->reprioritize(workId(it->pairId, c.index),
                                        priorityFor(c.index, chapterIndex, idx));
    }
    publishRow(bookId);
    emit jobChanged(bookId);
}

QString AudioTextAlignmentService::bookIdForRow(const QString& row) const {
    for (auto it = m_pairs.constBegin(); it != m_pairs.constEnd(); ++it)
        if (rowId(it.value().pairId) == row) return it.key();
    return QString();
}

void AudioTextAlignmentService::onActivityPauseRequested(const QString& row) {
    const QString bookId = bookIdForRow(row);
    if (!bookId.isEmpty()) pause(bookId);
}

void AudioTextAlignmentService::onActivityResumeRequested(const QString& row) {
    const QString bookId = bookIdForRow(row);
    if (!bookId.isEmpty()) resume(bookId);
}

} // namespace alignment
